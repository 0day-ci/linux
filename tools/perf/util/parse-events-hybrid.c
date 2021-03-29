// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "parse-events-hybrid.h"
#include "debug.h"
#include "pmu.h"
#include "pmu-hybrid.h"
#include "perf.h"

static void config_hybrid_attr(struct perf_event_attr *attr,
			       int type, int pmu_type)
{
	/*
	 * attr.config layout:
	 * PERF_TYPE_HARDWARE_PMU:     0xDD000000AA
	 *                             AA: hardware event ID
	 *                             DD: PMU type ID
	 * PERF_TYPE_HW_CACHE_PMU:     0xDD00CCBBAA
	 *                             AA: hardware cache ID
	 *                             BB: hardware cache op ID
	 *                             CC: hardware cache op result ID
	 *                             DD: PMU type ID
	 */
	attr->type = type;
	attr->config = attr->config | ((__u64)pmu_type << PERF_PMU_TYPE_SHIFT);
}

static int create_event_hybrid(__u32 config_type, int *idx,
			       struct list_head *list,
			       struct perf_event_attr *attr, char *name,
			       struct list_head *config_terms,
			       struct perf_pmu *pmu)
{
	struct evsel *evsel;
	__u32 type = attr->type;
	__u64 config = attr->config;

	config_hybrid_attr(attr, config_type, pmu->type);
	evsel = parse_events__add_event_hybrid(list, idx, attr, name,
					       pmu, config_terms);
	if (evsel)
		evsel->pmu_name = strdup(pmu->name);
	else
		return -ENOMEM;

	attr->type = type;
	attr->config = config;
	return 0;
}

static int add_hw_hybrid(struct parse_events_state *parse_state,
			 struct list_head *list, struct perf_event_attr *attr,
			 char *name, struct list_head *config_terms)
{
	struct perf_pmu *pmu;
	int ret;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		ret = create_event_hybrid(PERF_TYPE_HARDWARE_PMU,
					  &parse_state->idx, list, attr, name,
					  config_terms, pmu);
		if (ret)
			return ret;
	}

	return 0;
}

static int create_raw_event_hybrid(int *idx, struct list_head *list,
				   struct perf_event_attr *attr, char *name,
				   struct list_head *config_terms,
				   struct perf_pmu *pmu)
{
	struct evsel *evsel;

	attr->type = pmu->type;
	evsel = parse_events__add_event_hybrid(list, idx, attr, name,
					       pmu, config_terms);
	if (evsel)
		evsel->pmu_name = strdup(pmu->name);
	else
		return -ENOMEM;

	return 0;
}

static int add_raw_hybrid(struct parse_events_state *parse_state,
			  struct list_head *list, struct perf_event_attr *attr,
			  char *name, struct list_head *config_terms)
{
	struct perf_pmu *pmu;
	int ret;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		ret = create_raw_event_hybrid(&parse_state->idx, list, attr,
					      name, config_terms, pmu);
		if (ret)
			return ret;
	}

	return 0;
}

int parse_events__add_numeric_hybrid(struct parse_events_state *parse_state,
				     struct list_head *list,
				     struct perf_event_attr *attr,
				     char *name, struct list_head *config_terms,
				     bool *hybrid)
{
	*hybrid = false;
	if (attr->type == PERF_TYPE_SOFTWARE)
		return 0;

	if (!perf_pmu__has_hybrid())
		return 0;

	*hybrid = true;
	if (attr->type != PERF_TYPE_RAW) {
		return add_hw_hybrid(parse_state, list, attr, name,
				     config_terms);
	} else {
		return add_raw_hybrid(parse_state, list, attr, name,
				      config_terms);
	}

	return -1;
}

int parse_events__add_cache_hybrid(struct list_head *list, int *idx,
				   struct perf_event_attr *attr, char *name,
				   struct list_head *config_terms,
				   bool *hybrid)
{
	struct perf_pmu *pmu;
	int ret;

	*hybrid = false;
	if (!perf_pmu__has_hybrid())
		return 0;

	*hybrid = true;
	perf_pmu__for_each_hybrid_pmu(pmu) {
		ret = create_event_hybrid(PERF_TYPE_HW_CACHE_PMU, idx, list,
					  attr, name, config_terms, pmu);
		if (ret)
			return ret;
	}

	return 0;
}
