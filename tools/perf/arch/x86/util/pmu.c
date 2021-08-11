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
#include <errno.h>

#include "../../../util/intel-pt.h"
#include "../../../util/intel-bts.h"
#include "../../../util/pmu.h"
#include "../../../util/fncache.h"

#define TEMPLATE_ALIAS	"%s/bus/event_source/devices/%s/alias"

struct perf_pmu_alias_name {
	char *name;
	char *alias;
	struct list_head list;
};

static LIST_HEAD(pmu_alias_name_list);

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

static int setup_pmu_alias_list(void)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();
	struct perf_pmu_alias_name *pmu;
	char buf[MAX_PMU_NAME_LEN];
	FILE *file;
	int ret = 0;

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return -1;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		snprintf(path, PATH_MAX,
			 TEMPLATE_ALIAS, sysfs, dent->d_name);

		if (!file_available(path))
			continue;

		file = fopen(path, "r");
		if (!file)
			continue;

		if (!fgets(buf, sizeof(buf), file)) {
			fclose(file);
			continue;
		}

		fclose(file);

		pmu = zalloc(sizeof(*pmu));
		if (!pmu) {
			ret = -ENOMEM;
			break;
		}

		/* Remove the last '\n' */
		buf[strlen(buf) - 1] = 0;

		pmu->alias = strdup(buf);
		if (!pmu->alias)
			goto mem_err;

		pmu->name = strdup(dent->d_name);
		if (!pmu->name)
			goto mem_err;

		list_add_tail(&pmu->list, &pmu_alias_name_list);
		continue;
mem_err:
		ret = -ENOMEM;
		free(pmu->alias);
		free(pmu->name);
		free(pmu);
		break;
	}

	closedir(dir);
	return ret;
}

static char *__pmu_find_real_name(const char *name)
{
	struct perf_pmu_alias_name *pmu;

	list_for_each_entry(pmu, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu->alias))
			return strdup(pmu->name);
	}

	return strdup(name);
}

char *pmu_find_real_name(const char *name)
{
	static bool cached_list;

	if (cached_list)
		return __pmu_find_real_name(name);

	setup_pmu_alias_list();
	cached_list = true;

	return __pmu_find_real_name(name);
}

char *pmu_find_alias_name(const char *name)
{
	struct perf_pmu_alias_name *pmu;

	list_for_each_entry(pmu, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu->name))
			return strdup(pmu->alias);
	}
	return NULL;
}
