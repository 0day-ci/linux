// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. */
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/hid.h>
#include <uapi/linux/bpf_hid.h>
#include <bpf/bpf_helpers.h>

#include "hid_usi.h"

#define MS_TO_JIFFIES(t) ((t) * HZ / 1000)

static const char param_names[USI_NUM_PARAMS][6] = {
	"id",
	"range",
	"touch",
	"color",
	"width",
	"style",
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, USI_NUM_PARAMS);
} cache SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, USI_NUM_PARAMS);
} wr_cache SEC(".maps");

struct rep_data {
	int offset;
	int size;
	int idx;
};

static struct rep_data inputs[USI_NUM_PARAMS];
static struct rep_data features[USI_NUM_PARAMS];
static bool work_pending;
static u64 last_pen_event;

SEC("hid/work")
int hid_work(struct hid_bpf_ctx *ctx)
{
	int i, tmp;
	int *wrc, *c;
	u8 *buf;
	bool have_work = false;

	for (i = USI_PEN_COLOR; i < USI_NUM_PARAMS; i++) {
		tmp = i;

		wrc = bpf_map_lookup_elem(&wr_cache, &tmp);
		c = bpf_map_lookup_elem(&cache, &tmp);

		if (!wrc || !c)
			continue;

		if (*wrc == -1)
			continue;

		if (*wrc != *c) {
			bpf_printk("updating %s to %x", param_names[i],
				   *wrc);
			buf = bpf_ringbuf_reserve(&ringbuf, 16, 0);
			if (!buf)
				continue;

			buf[0] = features[i].idx;
			buf[1] = 1;
			if (i == USI_PEN_LINE_STYLE && *wrc == 6)
				buf[2] = 255;
			else
				buf[2] = *wrc;

			bpf_hid_raw_request(ctx, buf, 3,
					    HID_FEATURE_REPORT,
					    HID_REQ_SET_REPORT);

			bpf_ringbuf_discard(buf, 0);

			*c = *wrc;

			*wrc = -1;

			have_work = true;
			break;
		}
	}

	if (have_work)
		bpf_hid_schedule_work(ctx, MS_TO_JIFFIES(100));
	else
		work_pending = false;

	return 0;
}

SEC("hid/raw_event")
int hid_raw_event(struct hid_bpf_ctx *ctx)
{
	u32 i;
	u32 tmp;
	int val, new_val;
	int *wrc, *c;
	u8 *buf;
	u32 flags = 0;
	int offset;
	int size;
	int in_range;
	int touching;
	u64 jiffies;

	if (ctx->event.data[0] != inputs[USI_PEN_IN_RANGE].idx)
		return 0;

	in_range = bpf_hid_get_data(ctx, inputs[USI_PEN_IN_RANGE].offset,
				    inputs[USI_PEN_IN_RANGE].size);
	touching = bpf_hid_get_data(ctx, inputs[USI_PEN_TOUCHING].offset,
				    inputs[USI_PEN_TOUCHING].size);

	if (!touching) {
		last_pen_event = 0;
		return 0;
	}

	jiffies = bpf_jiffies64();

	if (!last_pen_event)
		last_pen_event = jiffies;

	for (i = USI_PEN_COLOR; i < USI_NUM_PARAMS; i++) {
		offset = inputs[i].offset;
		size = inputs[i].size;
		val = bpf_hid_get_data(ctx, offset, size);

		new_val = val;
		if (i == USI_PEN_LINE_STYLE && new_val == 255)
			new_val = 6;

		/*
		 * Make a local copy of 'i' which we can refer via a
		 * pointer to satisfy BPF verifier.
		 */
		tmp = i;

		wrc = bpf_map_lookup_elem(&wr_cache, &tmp);
		c = bpf_map_lookup_elem(&cache, &tmp);
		if (!wrc || !c)
			continue;

		if (*wrc != -1 && *wrc != *c && !work_pending) {
			bpf_hid_schedule_work(ctx, MS_TO_JIFFIES(200));
			work_pending = true;
		}

		if (jiffies < last_pen_event + MS_TO_JIFFIES(200))
			*c = new_val;

		if (new_val != *c)
			new_val = *c;

		if (new_val != val)
			bpf_hid_set_data(ctx, offset, size, new_val);
	}

	return 0;
}

static void process_tag(struct hid_bpf_ctx *ctx, struct rep_data *data,
			struct hid_bpf_parser *parser, u64 *idx)
{
	u32 i;
	int id;
	u32 offset;

	for (i = 0; i < parser->local.usage_index && i < 16; i++) {
		offset = parser->report.current_offset +
			i * parser->global.report_size;

		switch (parser->local.usage[i]) {
		case HID_DG_PEN_COLOR:
			id = USI_PEN_COLOR;
			break;
		case HID_DG_PEN_LINE_WIDTH:
			id = USI_PEN_LINE_WIDTH;
			break;
		case HID_DG_PEN_LINE_STYLE_INK:
			id = USI_PEN_LINE_STYLE;
			break;
		case HID_DG_INRANGE:
			if (parser->local.usage_index == 1)
				continue;

			id = USI_PEN_IN_RANGE;
			break;
		case HID_DG_TIPSWITCH:
			if (parser->local.usage_index == 1)
				continue;

			id = USI_PEN_TOUCHING;
			break;
		default:
			continue;
		}

		data[id].offset = offset + 8;
		data[id].size = parser->global.report_size;
		data[id].idx = parser->report.id;

		bpf_printk("parsed id=%d, offset=%d, idx=%d",
			   id, data[id].offset, data[id].idx);
	}
}

static u64 process_hid_rdesc_item(struct hid_bpf_ctx *ctx,
				  struct hid_bpf_parser *parser, u64 *idx,
				  void *data)
{
	struct hid_bpf_item *item = &parser->item;

	switch (item->type) {
	case HID_ITEM_TYPE_MAIN:
		if (item->tag == HID_MAIN_ITEM_TAG_INPUT)
			process_tag(ctx, inputs, parser, idx);
		if (item->tag == HID_MAIN_ITEM_TAG_FEATURE)
			process_tag(ctx, features, parser, idx);
	}

	return 0;
}

SEC("hid/rdesc_fixup")
int hid_rdesc_fixup(struct hid_bpf_ctx *ctx)
{
	int ret;
	int *wrc;
	int i, tmp;

	if (ctx->type != HID_BPF_RDESC_FIXUP)
		return 0;

	ret = bpf_hid_foreach_rdesc_item(ctx, process_hid_rdesc_item, (void *)0, 0);
	for (i = USI_PEN_COLOR; i < USI_NUM_PARAMS; i++) {
		tmp = i;
		wrc = bpf_map_lookup_elem(&wr_cache, &tmp);
		if (!wrc)
			continue;
		*wrc = -1;
	}

	bpf_printk("ret: %d", ret);
	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
