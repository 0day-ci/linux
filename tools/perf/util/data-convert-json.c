// SPDX-License-Identifier: GPL-2.0-only
/*
 * JSON export.
 *
 * Copyright (C) 2021, CodeWeavers Inc. <nfraser@codeweavers.com>
 */

#include "data-convert.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux/compiler.h"
#include "linux/err.h"
#include "util/auxtrace.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/event.h"
#include "util/evsel.h"
#include "util/header.h"
#include "util/map.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/tool.h"

struct convert_json {
	struct perf_tool tool;
	FILE *out;
	bool first;
};

static void output_json_string(FILE *out, const char *s)
{
	fputc('"', out);
	while (*s) {
		switch (*s) {

		// required escapes with special forms as per RFC 8259
		case '"':  fputs("\\\"", out); break;
		case '\\': fputs("\\\\", out); break;
		case '\b': fputs("\\b", out);  break;
		case '\f': fputs("\\f", out);  break;
		case '\n': fputs("\\n", out);  break;
		case '\r': fputs("\\r", out);  break;
		case '\t': fputs("\\t", out);  break;

		default:
			// all other control characters must be escaped by hex code
			if (*s <= 0x1f)
				fprintf(out, "\\u%04x", *s);
			else
				fputc(*s, out);
			break;
		}

		++s;
	}
	fputc('"', out);
}

static void output_sample_callchain_entry(struct perf_tool *tool,
		u64 ip, struct addr_location *al)
{
	struct convert_json *c = container_of(tool, struct convert_json, tool);
	FILE *out = c->out;

	fprintf(out, "\n\t\t\t\t{");
	fprintf(out, "\n\t\t\t\t\t\"ip\": \"0x%" PRIx64 "\"", ip);

	if (al && al->sym && al->sym->name && strlen(al->sym->name) > 0) {
		fprintf(out, ",\n\t\t\t\t\t\"symbol\": ");
		output_json_string(out, al->sym->name);

		if (al->map && al->map->dso) {
			const char *dso = al->map->dso->short_name;

			if (dso && strlen(dso) > 0) {
				fprintf(out, ",\n\t\t\t\t\t\"dso\": ");
				output_json_string(out, dso);
			}
		}
	}

	fprintf(out, "\n\t\t\t\t}");
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct evsel *evsel __maybe_unused,
				struct machine *machine)
{
	struct convert_json *c = container_of(tool, struct convert_json, tool);
	FILE *out = c->out;
	struct addr_location al, tal;
	u8 cpumode = PERF_RECORD_MISC_USER;

	if (machine__resolve(machine, &al, sample) < 0) {
		pr_err("Sample resolution failed!\n");
		return -1;
	}

	if (c->first)
		c->first = false;
	else
		fputc(',', out);
	fprintf(out, "\n\t\t{");

	fprintf(out, "\n\t\t\t\"timestamp\": %" PRIi64, sample->time);
	fprintf(out, ",\n\t\t\t\"pid\": %i", al.thread->pid_);
	fprintf(out, ",\n\t\t\t\"tid\": %i", al.thread->tid);

	if (al.thread->cpu >= 0)
		fprintf(out, ",\n\t\t\t\"cpu\": %i", al.thread->cpu);

	fprintf(out, ",\n\t\t\t\"comm\": ");
	output_json_string(out, thread__comm_str(al.thread));

	fprintf(out, ",\n\t\t\t\"callchain\": [");
	if (sample->callchain) {
		unsigned int i;
		bool ok;
		bool first_callchain = true;

		for (i = 0; i < sample->callchain->nr; ++i) {
			u64 ip = sample->callchain->ips[i];

			if (ip >= PERF_CONTEXT_MAX) {
				switch (ip) {
				case PERF_CONTEXT_HV:
					cpumode = PERF_RECORD_MISC_HYPERVISOR;
					break;
				case PERF_CONTEXT_KERNEL:
					cpumode = PERF_RECORD_MISC_KERNEL;
					break;
				case PERF_CONTEXT_USER:
					cpumode = PERF_RECORD_MISC_USER;
					break;
				default:
					pr_debug("invalid callchain context: %"
							PRId64 "\n", (s64) ip);
					break;
				}
				continue;
			}

			if (first_callchain)
				first_callchain = false;
			else
				fputc(',', out);

			ok = thread__find_symbol(al.thread, cpumode, ip, &tal);
			output_sample_callchain_entry(tool, ip, ok ? &tal : NULL);
		}
	} else {
		output_sample_callchain_entry(tool, sample->ip, &al);
	}
	fprintf(out, "\n\t\t\t]");

	fprintf(out, "\n\t\t}");
	return 0;
}

static void output_headers(struct perf_session *session, struct convert_json *c)
{
	struct stat st;
	struct perf_header *header = &session->header;
	int ret;
	int fd = perf_data__fd(session->data);
	int i;
	bool first;

	fprintf(c->out, "\n\t\t\t\"header-version\": %u", header->version);

	ret = fstat(fd, &st);
	if (ret >= 0) {
		time_t stctime = st.st_mtime;
		char buf[256];

		strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&stctime));
		fprintf(c->out, ",\n\t\t\t\"captured-on\": \"%s\"", buf);
	} else {
		pr_debug("Failed to get mtime of source file, not writing \"captured-on\"");
	}

	fprintf(c->out, ",\n\t\t\t\"data-offset\": %" PRIu64, header->data_offset);
	fprintf(c->out, ",\n\t\t\t\"data-size\": %" PRIu64, header->data_size);
	fprintf(c->out, ",\n\t\t\t\"feat-offset\": %" PRIu64, header->feat_offset);

	fputs(",\n\t\t\t\"hostname\": ", c->out);
	output_json_string(c->out, header->env.hostname);
	fputs(",\n\t\t\t\"os-release\": ", c->out);
	output_json_string(c->out, header->env.os_release);
	fputs(",\n\t\t\t\"arch\": ", c->out);
	output_json_string(c->out, header->env.arch);

	fputs(",\n\t\t\t\"cpu-desc\": ", c->out);
	output_json_string(c->out, header->env.cpu_desc);
	fputs(",\n\t\t\t\"cpuid\": ", c->out);
	output_json_string(c->out, header->env.cpuid);
	fprintf(c->out, ",\n\t\t\t\"nrcpus-online\": %u", header->env.nr_cpus_online);
	fprintf(c->out, ",\n\t\t\t\"nrcpus-avail\": %u", header->env.nr_cpus_avail);

	fputs(",\n\t\t\t\"perf-version\": ", c->out);
	output_json_string(c->out, header->env.version);

	fputs(",\n\t\t\t\"cmdline\": [", c->out);
	first = true;
	for (i = 0; i < header->env.nr_cmdline; i++) {
		if (first)
			first = false;
		else
			fputc(',', c->out);
		fputs("\n\t\t\t\t", c->out);
		output_json_string(c->out, header->env.cmdline_argv[i]);
	}
	fputs("\n\t\t\t]", c->out);
}

int bt_convert__perf2json(const char *input_name, const char *output_name,
			 struct perf_data_convert_opts *opts __maybe_unused)
{
	struct perf_session *session;
	int fd;

	struct convert_json c = {
		.tool = {
			.sample         = process_sample_event,
			.mmap           = perf_event__process_mmap,
			.mmap2          = perf_event__process_mmap2,
			.comm           = perf_event__process_comm,
			.namespaces     = perf_event__process_namespaces,
			.cgroup         = perf_event__process_cgroup,
			.exit           = perf_event__process_exit,
			.fork           = perf_event__process_fork,
			.lost           = perf_event__process_lost,
			.tracing_data   = perf_event__process_tracing_data,
			.build_id       = perf_event__process_build_id,
			.id_index       = perf_event__process_id_index,
			.auxtrace_info  = perf_event__process_auxtrace_info,
			.auxtrace       = perf_event__process_auxtrace,
			.event_update   = perf_event__process_event_update,
			.ordered_events = true,
			.ordering_requires_timestamps = true,
		},
		.first = true,
	};

	struct perf_data data = {
		.mode = PERF_DATA_MODE_READ,
		.path = input_name,
		.force = opts->force,
	};

	if (opts->all) {
		pr_err("--all is currently unsupported for JSON output.\n");
		return -1;
	}
	if (opts->tod) {
		pr_err("--tod is currently unsupported for JSON output.\n");
		return -1;
	}

	fd = open(output_name, O_CREAT | O_WRONLY | (opts->force ? 0 : O_EXCL), 0666);
	if (fd == -1) {
		if (errno == EEXIST)
			pr_err("Output file exists. Use --force to overwrite it.\n");
		else
			pr_err("Error opening output file!\n");
		return -1;
	}

	c.out = fdopen(fd, "w");
	if (!c.out) {
		fprintf(stderr, "Error opening output file!\n");
		return -1;
	}

	session = perf_session__new(&data, false, &c.tool);
	if (IS_ERR(session)) {
		fprintf(stderr, "Error creating perf session!\n");
		return -1;
	}

	if (symbol__init(&session->header.env) < 0) {
		fprintf(stderr, "Symbol init error!\n");
		return -1;
	}

	// Version number for future-proofing. Most additions should be able to be
	// done in a backwards-compatible way so this should only need to be bumped
	// if some major breaking change must be made.
	fprintf(c.out, "{\n\t\"linux-perf-json-version\": 1,");

	// Output headers
	fprintf(c.out, "\n\t\"headers\": {");
	output_headers(session, &c);
	fprintf(c.out, "\n\t},");

	// Output samples
	fprintf(c.out, "\n\t\"samples\": [");
	perf_session__process_events(session);
	fprintf(c.out, "\n\t]\n}\n");

	perf_session__delete(session);
	return 0;
}
