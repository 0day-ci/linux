// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#ifndef DP_HDCP_H_
#define DP_HDCP_H_

#define DP_HDCP_KEY_LEN				7
#define DP_HDCP_NUM_KEYS			40

struct dp_hdcp;
struct dp_parser;
struct drm_atomic_state;
struct drm_dp_aux;

struct dp_hdcp *dp_hdcp_get(struct dp_parser *parser, struct drm_dp_aux *aux);
void dp_hdcp_put(struct dp_hdcp *hdcp);

int dp_hdcp_attach(struct dp_hdcp *hdcp, struct drm_connector *connector);
int dp_hdcp_ingest_key(struct dp_hdcp *hdcp, const u8 *raw_key, int raw_len);
void dp_hdcp_commit(struct dp_hdcp *hdcp, struct drm_atomic_state *state);

#endif
