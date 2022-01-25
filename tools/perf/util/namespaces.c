// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2017 Hari Bathini, IBM Corporation
 */

#include "namespaces.h"
#include "event.h"
#include "get_current_dir_name.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <asm/bug.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

static const char *perf_ns__names[] = {
	[NET_NS_INDEX]		= "net",
	[UTS_NS_INDEX]		= "uts",
	[IPC_NS_INDEX]		= "ipc",
	[PID_NS_INDEX]		= "pid",
	[USER_NS_INDEX]		= "user",
	[MNT_NS_INDEX]		= "mnt",
	[CGROUP_NS_INDEX]	= "cgroup",
};

#ifndef REFCNT_CHECKING
#define UNWRAP_NSI(x) x
#else
#define UNWRAP_NSI(x) x->orig
#endif

const char *perf_ns__name(unsigned int id)
{
	if (id >= ARRAY_SIZE(perf_ns__names))
		return "UNKNOWN";
	return perf_ns__names[id];
}

struct namespaces *namespaces__new(struct perf_record_namespaces *event)
{
	struct namespaces *namespaces;
	u64 link_info_size = ((event ? event->nr_namespaces : NR_NAMESPACES) *
			      sizeof(struct perf_ns_link_info));

	namespaces = zalloc(sizeof(struct namespaces) + link_info_size);
	if (!namespaces)
		return NULL;

	namespaces->end_time = -1;

	if (event)
		memcpy(namespaces->link_info, event->link_info, link_info_size);

	return namespaces;
}

void namespaces__free(struct namespaces *namespaces)
{
	free(namespaces);
}

static int nsinfo__get_nspid(pid_t *tgid, pid_t *nstgid, bool *in_pidns, const char *path)
{
	FILE *f = NULL;
	char *statln = NULL;
	size_t linesz = 0;
	char *nspid;

	f = fopen(path, "r");
	if (f == NULL)
		return -1;

	while (getline(&statln, &linesz, f) != -1) {
		/* Use tgid if CONFIG_PID_NS is not defined. */
		if (strstr(statln, "Tgid:") != NULL) {
			*tgid = (pid_t)strtol(strrchr(statln, '\t'), NULL, 10);
			*nstgid = *tgid;
		}

		if (strstr(statln, "NStgid:") != NULL) {
			nspid = strrchr(statln, '\t');
			*nstgid = (pid_t)strtol(nspid, NULL, 10);
			/*
			 * If innermost tgid is not the first, process is in a different
			 * PID namespace.
			 */
			*in_pidns = (statln + sizeof("NStgid:") - 1) != nspid;
			break;
		}
	}

	fclose(f);
	free(statln);
	return 0;
}

int nsinfo__init(struct nsinfo *nsi)
{
	char oldns[PATH_MAX];
	char spath[PATH_MAX];
	char *newns = NULL;
	struct stat old_stat;
	struct stat new_stat;
	int rv = -1;

	if (snprintf(oldns, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return rv;

	if (asprintf(&newns, "/proc/%d/ns/mnt", UNWRAP_NSI(nsi)->pid) == -1)
		return rv;

	if (stat(oldns, &old_stat) < 0)
		goto out;

	if (stat(newns, &new_stat) < 0)
		goto out;

	/* Check if the mount namespaces differ, if so then indicate that we
	 * want to switch as part of looking up dso/map data.
	 */
	if (old_stat.st_ino != new_stat.st_ino) {
		UNWRAP_NSI(nsi)->need_setns = true;
		UNWRAP_NSI(nsi)->mntns_path = newns;
		newns = NULL;
	}

	/* If we're dealing with a process that is in a different PID namespace,
	 * attempt to work out the innermost tgid for the process.
	 */
	if (snprintf(spath, PATH_MAX, "/proc/%d/status", UNWRAP_NSI(nsi)->pid) >= PATH_MAX)
		goto out;

	rv = nsinfo__get_nspid(&UNWRAP_NSI(nsi)->tgid, &UNWRAP_NSI(nsi)->nstgid,
			       &UNWRAP_NSI(nsi)->in_pidns, spath);

out:
	free(newns);
	return rv;
}

static struct nsinfo *nsinfo__alloc(void)
{
	struct nsinfo *res;
#ifdef REFCNT_CHECKING
	res = malloc(sizeof(*res));
	if (!res)
		return NULL;

	res->orig = calloc(1, sizeof(struct original_nsinfo));
	if (!res->orig) {
		free(res);
		res = NULL;
	} else
		refcount_set(&res->orig->refcnt, 1);
#else
	res = calloc(1, sizeof(*res));
	if (res)
		refcount_set(&res->refcnt, 1);
#endif
	return res;
}

struct nsinfo *nsinfo__new(pid_t pid)
{
	struct nsinfo *nsi;

	if (pid == 0)
		return NULL;

	nsi = nsinfo__alloc();
	if (!nsi)
		return NULL;

	UNWRAP_NSI(nsi)->pid = pid;
	UNWRAP_NSI(nsi)->tgid = pid;
	UNWRAP_NSI(nsi)->nstgid = pid;
	UNWRAP_NSI(nsi)->need_setns = false;
	UNWRAP_NSI(nsi)->in_pidns = false;
	/* Init may fail if the process exits while we're trying to look at its
	 * proc information. In that case, save the pid but don't try to enter
	 * the namespace.
	 */
	if (nsinfo__init(nsi) == -1)
		UNWRAP_NSI(nsi)->need_setns = false;

	return nsi;
}

struct nsinfo *nsinfo__copy(const struct nsinfo *nsi)
{
	struct nsinfo *nnsi;

	if (nsi == NULL)
		return NULL;

	nnsi = nsinfo__alloc();
	if (!nnsi)
		return NULL;

	UNWRAP_NSI(nnsi)->pid = UNWRAP_NSI(nsi)->pid;
	UNWRAP_NSI(nnsi)->tgid = UNWRAP_NSI(nsi)->tgid;
	UNWRAP_NSI(nnsi)->nstgid = UNWRAP_NSI(nsi)->nstgid;
	UNWRAP_NSI(nnsi)->need_setns = UNWRAP_NSI(nsi)->need_setns;
	UNWRAP_NSI(nnsi)->in_pidns = UNWRAP_NSI(nsi)->in_pidns;
	if (UNWRAP_NSI(nsi)->mntns_path) {
		UNWRAP_NSI(nnsi)->mntns_path = strdup(UNWRAP_NSI(nsi)->mntns_path);
		if (!UNWRAP_NSI(nnsi)->mntns_path) {
			nsinfo__put(nnsi);
			return NULL;
		}
	}

	return nnsi;
}

static void nsinfo__delete(struct nsinfo *nsi)
{
	if (nsi) {
		WARN_ONCE(refcount_read(&UNWRAP_NSI(nsi)->refcnt) != 0,
			"nsinfo refcnt unbalanced\n");
		zfree(&UNWRAP_NSI(nsi)->mntns_path);
#ifdef REFCNT_CHECKING
		zfree(&nsi->orig);
#endif
		free(nsi);
	}
}

struct nsinfo *nsinfo__get(struct nsinfo *nsi)
{
	if (nsi) {
#ifdef REFCNT_CHECKING
		struct nsinfo *new_wrapper;
#endif
		refcount_inc(&UNWRAP_NSI(nsi)->refcnt);
#ifdef REFCNT_CHECKING
		new_wrapper = malloc(sizeof(*new_wrapper));
		new_wrapper->orig = nsi->orig;
		nsi = new_wrapper;
#endif
	}
	return nsi;
}

void nsinfo__put(struct nsinfo *nsi)
{
	if (nsi && refcount_dec_and_test(&UNWRAP_NSI(nsi)->refcnt))
		nsinfo__delete(nsi);
#ifdef REFCNT_CHECKING
	else if (nsi) {
		nsi->orig = NULL;
		free(nsi);
	}
#endif
}

bool nsinfo__need_setns(const struct nsinfo *nsi)
{
	return UNWRAP_NSI(nsi)->need_setns;
}

void nsinfo__clear_need_setns(struct nsinfo *nsi)
{
	UNWRAP_NSI(nsi)->need_setns = false;
}

pid_t nsinfo__tgid(const struct nsinfo  *nsi)
{
	return UNWRAP_NSI(nsi)->tgid;
}

pid_t nsinfo__nstgid(const struct nsinfo  *nsi)
{
	return UNWRAP_NSI(nsi)->nstgid;
}

pid_t nsinfo__pid(const struct nsinfo  *nsi)
{
	return UNWRAP_NSI(nsi)->pid;
}

pid_t nsinfo__in_pidns(const struct nsinfo  *nsi)
{
	return UNWRAP_NSI(nsi)->in_pidns;
}

void nsinfo__mountns_enter(struct nsinfo *nsi,
				  struct nscookie *nc)
{
	char curpath[PATH_MAX];
	int oldns = -1;
	int newns = -1;
	char *oldcwd = NULL;

	if (nc == NULL)
		return;

	nc->oldns = -1;
	nc->newns = -1;

	if (!nsi || !UNWRAP_NSI(nsi)->need_setns)
		return;

	if (snprintf(curpath, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return;

	oldcwd = get_current_dir_name();
	if (!oldcwd)
		return;

	oldns = open(curpath, O_RDONLY);
	if (oldns < 0)
		goto errout;

	newns = open(UNWRAP_NSI(nsi)->mntns_path, O_RDONLY);
	if (newns < 0)
		goto errout;

	if (setns(newns, CLONE_NEWNS) < 0)
		goto errout;

	nc->oldcwd = oldcwd;
	nc->oldns = oldns;
	nc->newns = newns;
	return;

errout:
	free(oldcwd);
	if (oldns > -1)
		close(oldns);
	if (newns > -1)
		close(newns);
}

void nsinfo__mountns_exit(struct nscookie *nc)
{
	if (nc == NULL || nc->oldns == -1 || nc->newns == -1 || !nc->oldcwd)
		return;

	setns(nc->oldns, CLONE_NEWNS);

	if (nc->oldcwd) {
		WARN_ON_ONCE(chdir(nc->oldcwd));
		zfree(&nc->oldcwd);
	}

	if (nc->oldns > -1) {
		close(nc->oldns);
		nc->oldns = -1;
	}

	if (nc->newns > -1) {
		close(nc->newns);
		nc->newns = -1;
	}
}

char *nsinfo__realpath(const char *path, struct nsinfo *nsi)
{
	char *rpath;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	rpath = realpath(path, NULL);
	nsinfo__mountns_exit(&nsc);

	return rpath;
}

int nsinfo__stat(const char *filename, struct stat *st, struct nsinfo *nsi)
{
	int ret;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	ret = stat(filename, st);
	nsinfo__mountns_exit(&nsc);

	return ret;
}

bool nsinfo__is_in_root_namespace(void)
{
	pid_t tgid = 0, nstgid = 0;
	bool in_pidns = false;

	nsinfo__get_nspid(&tgid, &nstgid, &in_pidns, "/proc/self/status");
	return !in_pidns;
}
