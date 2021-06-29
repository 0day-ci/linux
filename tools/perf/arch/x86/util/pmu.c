// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>

#include "../../../util/intel-pt.h"
#include "../../../util/intel-bts.h"
#include "../../../util/pmu.h"
#include "../../../util/fncache.h"

#define TEMPLATE_UNCORE_ALIAS	"%s/bus/event_source/devices/%s/alias"

struct perf_uncore_pmu_name {
	char *name;
	char *alias;
	struct list_head list;
};

static LIST_HEAD(uncore_pmu_list);

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME))
		return intel_pt_pmu_default_config(pmu);
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME))
		pmu->selectable = true;
#endif
	return NULL;
}

static void setup_uncore_pmu_list(void)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();
	struct perf_uncore_pmu_name *pmu;
	char buf[MAX_PMU_NAME_LEN];
	FILE *file;
	int size;

	if (!sysfs)
		return;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, "..") ||
		    strncmp(dent->d_name, "uncore_", 7))
			continue;

		snprintf(path, PATH_MAX,
			 TEMPLATE_UNCORE_ALIAS, sysfs, dent->d_name);

		if (!file_available(path))
			continue;

		file = fopen(path, "r");
		if (!file)
			continue;

		memset(buf, 0, sizeof(buf));
		if (!fread(buf, 1, sizeof(buf), file))
			continue;

		pmu = zalloc(sizeof(*pmu));
		if (!pmu)
			continue;

		size = strlen(buf) - 1;
		pmu->alias = zalloc(size);
		if (!pmu->alias) {
			free(pmu);
			continue;
		}
		strncpy(pmu->alias, buf, size);
		pmu->name = strdup(dent->d_name);
		list_add_tail(&pmu->list, &uncore_pmu_list);

		fclose(file);
	}

	closedir(dir);

}

static char *__pmu_find_real_name(const char *name)
{
	struct perf_uncore_pmu_name *pmu;

	/*
	 * The template of the uncore alias is uncore_type_*
	 * Only find the real name for the uncore alias.
	 */
	if (strncmp(name, "uncore_type_", 12))
		return strdup(name);

	list_for_each_entry(pmu, &uncore_pmu_list, list) {
		if (!strcmp(name, pmu->alias))
			return strdup(pmu->name);
	}

	return strdup(name);
}

char *pmu_find_real_name(const char *name)
{
	static bool cached_list;

	if (strncmp(name, "uncore_", 7))
		return strdup(name);

	if (cached_list)
		return __pmu_find_real_name(name);

	setup_uncore_pmu_list();
	cached_list = true;

	return __pmu_find_real_name(name);
}

char *pmu_find_alias_name(const char *name)
{
	struct perf_uncore_pmu_name *pmu;

	if (strncmp(name, "uncore_", 7))
		return NULL;

	list_for_each_entry(pmu, &uncore_pmu_list, list) {
		if (!strcmp(name, pmu->name))
			return strdup(pmu->alias);
	}
	return NULL;
}
