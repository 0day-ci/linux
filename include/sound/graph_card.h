/* SPDX-License-Identifier: GPL-2.0
 *
 * ASoC audio graph card support
 *
 */

#ifndef __GRAPH_CARD_H
#define __GRAPH_CARD_H

#include <sound/simple_card_utils.h>

typedef int (*GRAPH_CUSTOM)(struct asoc_simple_priv *priv,
			    struct device_node *lnk,
			    struct link_info *li);

struct graph_custom_hooks {
	int (*hook_pre)(struct asoc_simple_priv *priv);
	int (*hook_post)(struct asoc_simple_priv *priv);
	GRAPH_CUSTOM custom_normal;
	GRAPH_CUSTOM custom_dpcm;
	GRAPH_CUSTOM custom_c2c;
};

int audio_graph_parse_of(struct asoc_simple_priv *priv, struct device *dev);
int rich_graph_parse_of(struct asoc_simple_priv *priv, struct device *dev,
			struct graph_custom_hooks *hooks);

int rich_graph_link_normal(struct asoc_simple_priv *priv,
			   struct device_node *lnk, struct link_info *li);
int rich_graph_link_dpcm(struct asoc_simple_priv *priv,
			 struct device_node *lnk, struct link_info *li);
int rich_graph_link_c2c(struct asoc_simple_priv *priv,
			struct device_node *lnk, struct link_info *li);

#endif /* __GRAPH_CARD_H */
