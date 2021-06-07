#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/asound.h>
#include <linux/firmware.h>
#include <sound/soc-topology.h>
#include <sound/soc-dpcm.h>
#include <uapi/sound/snd_ar_tokens.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include "q6apm.h"
#include "audioreach.h"

struct snd_ar_control {
	u32 sgid; /* Sub Graph ID */
	struct snd_soc_component *scomp;
};

static struct audioreach_graph_info *audioreach_tplg_alloc_graph_info(
					struct q6apm *apm, uint32_t graph_id,
					bool *found)
{
	struct audioreach_graph_info *info;

	spin_lock(&apm->lock);
	info = idr_find(&apm->graph_info_idr, graph_id);
	spin_unlock(&apm->lock);

	if (info) {
		*found = true;
		return info;
	}

	*found = false;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);


	INIT_LIST_HEAD(&info->sg_list);
	spin_lock_init(&info->sg_list_lock);

	spin_lock(&apm->lock);
	idr_alloc(&apm->graph_info_idr, info, graph_id,
		  graph_id + 1, GFP_ATOMIC);
	spin_unlock(&apm->lock);

	info->id = graph_id;

	return info;
}

static void audioreach_tplg_add_sub_graph(struct audioreach_sub_graph *sg,
					  struct audioreach_graph_info *info)
{
	list_add_tail(&sg->node, &info->sg_list);
	info->num_sub_graphs++;
}


static struct audioreach_sub_graph *audioreach_tplg_alloc_sub_graph(
						struct q6apm *apm,
						uint32_t sub_graph_id,
						bool *found)
{
	struct audioreach_sub_graph *sg = NULL;

	/* Find if there is already a matching sub-graph */
	spin_lock(&apm->lock);
	sg = idr_find(&apm->sub_graphs_idr, sub_graph_id);
	spin_unlock(&apm->lock);


	if (sg) {
		*found = true;
		return sg;
	}

	*found = false;
	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&sg->container_list);

	spin_lock(&apm->lock);
	idr_alloc(&apm->sub_graphs_idr, sg, sub_graph_id,
		  sub_graph_id + 1, GFP_ATOMIC);
	spin_unlock(&apm->lock);

	return sg;
}

static struct audioreach_container *audioreach_tplg_alloc_container(struct q6apm *apm,
						struct audioreach_sub_graph *sg,
						uint32_t container_id,
						bool *found)
{
	struct audioreach_container *cont = NULL;

	spin_lock(&apm->lock);
	cont = idr_find(&apm->containers_idr, container_id);
	spin_unlock(&apm->lock);

	if (cont) {
		*found = true;
		return cont;
	}
	*found = false;

	cont = kzalloc(sizeof(*cont), GFP_KERNEL);
	if (!cont)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&cont->modules_list);

	spin_lock(&apm->lock);
	idr_alloc(&apm->containers_idr, cont, container_id,
		  container_id + 1, GFP_ATOMIC);
	spin_unlock(&apm->lock);

	/* add to container list */
	list_add_tail(&cont->node, &sg->container_list);
	sg->num_containers++;

	return cont;
}

static struct audioreach_module *audioreach_tplg_alloc_module(struct q6apm *apm,
					struct audioreach_container *cont,
					struct snd_soc_dapm_widget *w,
					uint32_t module_id,
					bool *found)
{
	struct audioreach_module *mod = NULL;

	spin_lock(&apm->lock);
	mod = idr_find(&apm->modules_idr, module_id);
	spin_unlock(&apm->lock);

	if (mod) {
		*found = true;
		return mod;
	}
	*found = false;
	mod = kzalloc(sizeof(*mod), GFP_KERNEL);
	if (!mod)
		return ERR_PTR(-ENOMEM);

	spin_lock(&apm->lock);
	idr_alloc(&apm->modules_idr, mod, module_id,
		  module_id + 1, GFP_ATOMIC);
	spin_unlock(&apm->lock);

	/* add to module list */
	list_add_tail(&mod->node, &cont->modules_list);
	mod->container = cont;
	mod->widget = w;
	cont->num_modules++;

	return mod;
}

static struct audioreach_sub_graph *audioreach_parse_sg_tokens(struct q6apm *apm,
						struct snd_soc_tplg_vendor_array *sg_array)
{
	struct audioreach_graph_info *info = NULL;
	struct snd_soc_tplg_vendor_value_elem *sg_elem = sg_array->value;
	struct audioreach_sub_graph *sg;
	int graph_id, sub_graph_id, tkn_count = 0;
	bool found;

	while (tkn_count <= (le32_to_cpu(sg_array->num_elems) - 1)) {
		switch (le32_to_cpu(sg_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			sub_graph_id = le32_to_cpu(sg_elem->value);
			sg = audioreach_tplg_alloc_sub_graph(apm, sub_graph_id, &found);
			if (IS_ERR(sg)) {
				return sg;
			} else if (found) {
				/* Already parsed data for this sub-graph */
				return sg;
			}
			dev_err(apm->dev, "%s: New subgraph id : 0x%08x\n", __func__,
				sub_graph_id);
			sg->sub_graph_id = sub_graph_id;
			break;
		case AR_TKN_DAI_INDEX:
			/* Sub graph is associated with predefined graph */
			graph_id = le32_to_cpu(sg_elem->value);
			info = audioreach_tplg_alloc_graph_info(apm, graph_id, &found);
			if (IS_ERR(info))
				return ERR_CAST(info);
			break;
		case AR_TKN_U32_SUB_GRAPH_PERF_MODE:
			sg->perf_mode = le32_to_cpu(sg_elem->value);
			break;
		case AR_TKN_U32_SUB_GRAPH_DIRECTION:
			sg->direction = le32_to_cpu(sg_elem->value);
			break;
		case AR_TKN_U32_SUB_GRAPH_SCENARIO_ID:
			sg->scenario_id = le32_to_cpu(sg_elem->value);
			break;
		default:
			dev_err(apm->dev, "Not a valid token %d for graph\n",
				sg_elem->token);
		break;

		}
		tkn_count++;
		sg_elem++;
	}

	/* Sub graph is associated with predefined graph */
	if (info) {
		dev_err(apm->dev, "%s: adding subgraph id : 0x%08x -> %d\n", __func__,
		sub_graph_id, graph_id);

		audioreach_tplg_add_sub_graph(sg, info);
	}

	return sg;
}


static struct audioreach_container *audioreach_parse_cont_tokens(struct q6apm *apm,
						struct audioreach_sub_graph *sg,
						struct snd_soc_tplg_vendor_array *cont_array)
{
	struct snd_soc_tplg_vendor_value_elem *cont_elem;
	struct audioreach_container *cont;
	int container_id, tkn_count = 0;
	bool found = false;

	cont_elem = cont_array->value;
	while (tkn_count <= (le32_to_cpu(cont_array->num_elems) - 1)) {
		switch (le32_to_cpu(cont_elem->token)) {
		case AR_TKN_U32_CONAINER_INSTANCE_ID:
			container_id = le32_to_cpu(cont_elem->value);
			cont = audioreach_tplg_alloc_container(apm, sg, container_id, &found);
			if (IS_ERR(cont))
				return ERR_PTR(-ENOMEM);
			else if (found) /* Already parsed container data */
				return cont;

			cont->container_id = container_id;
			break;
		case AR_TKN_U32_CONAINER_CAPABILITY_ID:
			cont->capability_id = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONAINER_STACK_SIZE:
			cont->stack_size = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONAINER_GRAPH_POS:
			cont->graph_pos = le32_to_cpu(cont_elem->value);
			break;
		case AR_TKN_U32_CONAINER_PROC_DOMAIN:
			cont->proc_domain = le32_to_cpu(cont_elem->value);
			break;
		default:
			dev_err(apm->dev, "Not a valid token %d for graph\n",
				cont_elem->token);
		break;

		}
		tkn_count++;
		cont_elem++;
	}

	return cont;
}

static struct audioreach_module *audioreach_parse_common_tokens(struct q6apm *apm,
							struct audioreach_container *cont,
							struct snd_soc_tplg_vendor_array *mod_array,
							struct snd_soc_dapm_widget *w)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	struct audioreach_module *mod;
	int module_iid, tkn_count = 0;
	bool found;

	mod_elem = mod_array->value;
	module_iid = le32_to_cpu(mod_elem->value);

	mod = audioreach_tplg_alloc_module(apm, cont, w, module_iid, &found);
	if (IS_ERR(mod)) {
		return ERR_PTR(-ENOMEM);
	} else if (found) {
		dev_err(apm->dev, "Duplicate Module Instance ID 0x%08x found\n", module_iid);
		return ERR_PTR(-EINVAL);
	}

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		/* common module info */
		case AR_TKN_U32_MODULE_ID:
			mod->module_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_INSTANCE_ID:
			mod->instance_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_MAX_IP_PORTS:
			mod->max_ip_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_MAX_OP_PORTS:
			mod->max_op_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_IN_PORTS:
			mod->in_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_OUT_PORTS:
			mod->out_port = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_SRC_OP_PORT_ID:
			mod->src_mod_op_port_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_DST_IN_PORT_ID:
			mod->dst_mod_ip_port_id = le32_to_cpu(mod_elem->value);

		default:
			break;

		}
		tkn_count++;
		mod_elem++;
	}

	return mod;
}

static int audioreach_widget_load_module_common(struct snd_soc_component *component,
				      int index, struct snd_soc_dapm_widget *w,
				      struct snd_soc_tplg_dapm_widget *tplg_w,
				      size_t *sz)
{
	struct q6apm *apm = dev_get_drvdata(component->dev);
	struct audioreach_sub_graph *sg;
	struct audioreach_container *cont;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	void *data = tplg_w->priv.data;
	struct snd_soc_tplg_vendor_array *array = &tplg_w->priv.array[0];

	sg = audioreach_parse_sg_tokens(apm, &tplg_w->priv.array[0]);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	*sz = le32_to_cpu(array->size);
	array = data + *sz;
	cont = audioreach_parse_cont_tokens(apm, sg, array);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	*sz = *sz + le32_to_cpu(array->size);
	array = data + *sz;
	mod = audioreach_parse_common_tokens(apm, cont, array, w);
	if (IS_ERR(mod))
		return PTR_ERR(mod);

	dobj = &w->dobj;
	dobj->private = mod;

	return 0;
}

static int audioreach_widget_load_pcm(struct snd_soc_component *component,
				      int index, struct snd_soc_dapm_widget *w,
				      struct snd_soc_tplg_dapm_widget *tplg_w)
{
	size_t sz;

	return audioreach_widget_load_module_common(component, index, w,
						    tplg_w, &sz);
}

static int audioreach_widget_load_enc_dec_cnv(struct snd_soc_component *component,
					  int index, struct snd_soc_dapm_widget *w,
					  struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct	snd_soc_tplg_vendor_array *mod_array;
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	size_t sz;
	int tkn_count = 0;
	int ret;

	ret = audioreach_widget_load_module_common(component, index, w, tplg_w,
						   &sz);
	if (ret)
		return ret;

	dobj = &w->dobj;
	mod = dobj->private;
	mod_array = (struct snd_soc_tplg_vendor_array *)((u8 *)tplg_w->priv.data + sz);
	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		case AR_TKN_U32_MODULE_FMT_INTERLEAVE:
			mod->interleave_type = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_FREQ:
			mod->rate = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_BIT_DEPTH:
			mod->bit_depth = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_log_module_load(struct audioreach_module *mod,
					     struct snd_soc_tplg_vendor_array *mod_array)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	int tkn_count = 0;

	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {

		case AR_TKN_U32_MODULE_LOG_CODE:
			mod->log_code = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_LOG_TAP_POINT_ID:
			mod->log_tap_point_id = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_LOG_MODE:
			mod->mode = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_dma_module_load(struct audioreach_module *mod,
					     struct snd_soc_tplg_vendor_array *mod_array)
{
	struct snd_soc_tplg_vendor_value_elem *mod_elem;
	int tkn_count = 0;

	mod_elem = mod_array->value;

	while (tkn_count <= (le32_to_cpu(mod_array->num_elems) - 1)) {
		switch (le32_to_cpu(mod_elem->token)) {
		case AR_TKN_U32_MODULE_HW_IF_IDX:
			mod->hw_interface_idx = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_FMT_DATA:
			mod->data_format = le32_to_cpu(mod_elem->value);
			break;
		case AR_TKN_U32_MODULE_HW_IF_TYPE:
			mod->hw_interface_type = le32_to_cpu(mod_elem->value);
			break;
		default:
			break;
		}
		tkn_count++;
		mod_elem++;
	}

	return 0;
}

static int audioreach_widget_load_buffer(struct snd_soc_component *component,
					 int index, struct snd_soc_dapm_widget *w,
					 struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct snd_soc_tplg_vendor_array *mod_array;
	struct audioreach_module *mod;
	struct snd_soc_dobj *dobj;
	size_t sz;
	int ret;

	ret = audioreach_widget_load_module_common(component, index, w, tplg_w,
						   &sz);
	if (ret)
		return ret;

	dobj = &w->dobj;
	mod = dobj->private;

	mod_array = (struct snd_soc_tplg_vendor_array *)((u8 *)tplg_w->priv.data + sz);

	switch (mod->module_id) {
	case MODULE_ID_CODEC_DMA_SINK:
	case MODULE_ID_CODEC_DMA_SOURCE:
		audioreach_widget_dma_module_load(mod, mod_array);
		break;
	case MODULE_ID_DATA_LOGGING:
		audioreach_widget_log_module_load(mod, mod_array);
		break;
	}

	return 0;
}

static int audioreach_widget_load_mixer(struct snd_soc_component *component,
					int index, struct snd_soc_dapm_widget *w,
					struct snd_soc_tplg_dapm_widget *tplg_w)
{
	struct snd_soc_tplg_vendor_value_elem *w_elem;
	struct snd_soc_tplg_vendor_array *w_array;
	struct snd_ar_control *scontrol;
	struct snd_soc_dobj *dobj;
	int tkn_count = 0;

	dev_err(component->dev, "DEBUG:: %s \n", __func__);
	w_array = &tplg_w->priv.array[0];

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->scomp = component;
	dobj = &w->dobj;
	dobj->private = scontrol;

	w_elem = w_array->value;
	while (tkn_count <= (le32_to_cpu(w_array->num_elems) - 1)) {
		switch (le32_to_cpu(w_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			scontrol->sgid = le32_to_cpu(w_elem->value);
			break;
		default: /* ignore other tokens */
			break;
		}
		tkn_count++;
		w_elem++;
	}

	return 0;
}

static int audioreach_widget_ready(struct snd_soc_component *component,
				   int index, struct snd_soc_dapm_widget *w,
				   struct snd_soc_tplg_dapm_widget *tplg_w)
{
	switch (w->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
		audioreach_widget_load_pcm(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_decoder:
	case snd_soc_dapm_encoder:
	case snd_soc_dapm_src:
		audioreach_widget_load_enc_dec_cnv(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_buffer:
		audioreach_widget_load_buffer(component, index, w, tplg_w);
		break;
	case snd_soc_dapm_mixer:
		return audioreach_widget_load_mixer(component, index, w, tplg_w);
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_scheduler:
	case snd_soc_dapm_out_drv:
	default:
		dev_err(component->dev, "Widget type (0x%x) not yet supported\n",
			w->id);
		break;
	}

	return 0;
}


static int audioreach_widget_unload(struct snd_soc_component *scomp,
				    struct snd_soc_dobj *dobj)
{
	struct q6apm *apm = dev_get_drvdata(scomp->dev);
	struct audioreach_container *cont;
	struct audioreach_module *mod;

	mod = dobj->private;
	cont = mod->container;
	/* find a way to remove the container and sub-graph */

	spin_lock(&apm->lock);
	idr_remove(&apm->modules_idr, mod->module_id);
	cont->num_modules--;

	/* delete list */
	list_del(&mod->node);
	/* free memory */
	kfree(mod);

	if (list_empty(&cont->modules_list)) { /* remove container */
		struct audioreach_sub_graph *sg = cont->sub_graph;

		idr_remove(&apm->containers_idr, cont->container_id);
		list_del(&cont->node);
		sg->num_containers--;
		kfree(cont);
		if (list_empty(&sg->container_list)) { /* remove sg */
			struct audioreach_graph_info *info = sg->info;

			idr_remove(&apm->sub_graphs_idr, sg->sub_graph_id);
			list_del(&sg->node);
			info->num_sub_graphs--;
			kfree(sg);
			if (list_empty(&info->sg_list)) { /* remove graph info */
				idr_remove(&apm->graph_info_idr, info->id);
				kfree(info);
			}
		}
	}

	spin_unlock(&apm->lock);

	return 0;
}

static struct audioreach_module *audioreach_find_widget(struct snd_soc_component *comp,
							const char *name)
{
	struct q6apm *apm = dev_get_drvdata(comp->dev);
        struct audioreach_module *module;
        int id;

        idr_for_each_entry(&apm->modules_idr, module, id) {
		if (!strcmp(name, module->widget->name))
			return module;
	}

	return NULL;
}

static int audioreach_route_load(struct snd_soc_component *scomp, int index,
			  struct snd_soc_dapm_route *route)
{
	struct audioreach_module *src, *sink;

	src = audioreach_find_widget(scomp, route->source);
	sink = audioreach_find_widget(scomp, route->sink);

	if (src && sink) {
		src->dst_mod_inst_id = sink->instance_id;
		sink->src_mod_inst_id = src->instance_id;
	}

	return 0;
}

static int audioreach_route_unload(struct snd_soc_component *scomp,
			    struct snd_soc_dobj *dobj)
{

	return 0;
}

static void audioreach_tplg_complete(struct snd_soc_component *component)
{
	/* TBD */
}

/* DAI link - used for any driver specific init */
static int audioreach_link_load(struct snd_soc_component *component, int index,
				struct snd_soc_dai_link *link,
				struct snd_soc_tplg_link_config *cfg)
{
	link->nonatomic = true;
	link->dynamic = true;
	link->platforms->name = NULL;
	link->platforms->of_node = of_get_compatible_child(component->dev->of_node,
				"qcom,q6apm-dais");
	link->trigger[0] = SND_SOC_DPCM_TRIGGER_POST;
	link->trigger[1] = SND_SOC_DPCM_TRIGGER_POST;

	return 0;
}

static int audioreach_get_audio_mixer(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_ar_control *scontrol = mc->dobj.private;
	struct snd_ar_control *dapm_scontrol = dw->dobj.private;
	struct q6apm *data = dev_get_drvdata(c->dev);
	bool connected;

	connected = q6apm_is_sub_graphs_connected(data, scontrol->sgid,
						  dapm_scontrol->sgid);
	if (connected)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int audioreach_put_audio_mixer(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *dw = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_ar_control *scontrol = mc->dobj.private;
	struct snd_ar_control *dapm_scontrol = dw->dobj.private;
	struct q6apm *data = dev_get_drvdata(c->dev);
	struct snd_soc_dapm_update *update = NULL;

	if (ucontrol->value.integer.value[0]) {
		q6apm_connect_sub_graphs(data, scontrol->sgid,
					 dapm_scontrol->sgid, true);
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 1, update);
	} else {
		q6apm_connect_sub_graphs(data, scontrol->sgid,
					 dapm_scontrol->sgid, false);
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 0, update);
	}
	return 0;
}

static int audioreach_control_load_volume(struct snd_soc_component *scomp,
					  struct snd_ar_control *scontrol,
					  struct snd_kcontrol_new *kc,
					  struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_soc_tplg_mixer_control *mc =	container_of(hdr, struct snd_soc_tplg_mixer_control, hdr);
	struct snd_soc_tplg_vendor_array *c_array =(struct snd_soc_tplg_vendor_array *)mc->priv.data;
	int tkn_count = 0;
	struct snd_soc_tplg_vendor_value_elem *c_elem;

	c_elem = c_array->value;

	while (tkn_count <= (le32_to_cpu(c_array->num_elems) - 1)) {
		switch (le32_to_cpu(c_elem->token)) {
		case AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:
			scontrol->sgid = le32_to_cpu(c_elem->value);
			break;
		default:
			/* Ignore other tokens */
		break;

		}
		c_elem++;
		tkn_count++;
	}

	return 0;
}

static int audioreach_control_load(struct snd_soc_component *scomp, int index,
				   struct snd_kcontrol_new *kc,
				   struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_ar_control *scontrol;
	struct snd_soc_dobj *dobj;
	struct soc_mixer_control *sm;
	int ret;

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->scomp = scomp;

	switch (le32_to_cpu(hdr->ops.info)) {
	case SND_SOC_TPLG_CTL_VOLSW:
		sm = (struct soc_mixer_control *)kc->private_value;
		dobj = &sm->dobj;
		ret = audioreach_control_load_volume(scomp, scontrol, kc, hdr);
		break;
	default:
		dev_warn(scomp->dev, "control type not supported %d:%d:%d\n",
			 hdr->ops.get, hdr->ops.put, hdr->ops.info);
		kfree(scontrol);
		break;
	}

	dobj->private = scontrol;
	return ret;
}

static int audioreach_control_unload(struct snd_soc_component *scomp,
				     struct snd_soc_dobj *dobj)
{
	struct snd_ar_control *scontrol = dobj->private;

	kfree(scontrol);

	return 0;
}

static const struct snd_soc_tplg_kcontrol_ops audioreach_io_ops[] = {
	{SND_SOC_AR_TPLG_FE_BE_GRAPH_CTL_MIX, audioreach_get_audio_mixer,
		audioreach_put_audio_mixer, snd_soc_info_volsw},
};

static struct snd_soc_tplg_ops audioreach_tplg_ops  = {
	.io_ops = audioreach_io_ops,
	.io_ops_count = ARRAY_SIZE(audioreach_io_ops),

	.control_load	= audioreach_control_load,
	.control_unload	= audioreach_control_unload,

	.widget_ready = audioreach_widget_ready,
	.widget_unload = audioreach_widget_unload,

	.complete = audioreach_tplg_complete,
        .link_load = audioreach_link_load,

	.dapm_route_load	= audioreach_route_load,
	.dapm_route_unload	= audioreach_route_unload,
};

int audioreach_tplg_init(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, "audioreach.bin", dev);
	if (ret < 0) {
		dev_err(dev, "tplg fw audioreach.bin load failed with %d\n", ret);
		return ret;
	}

	ret = snd_soc_tplg_component_load(component, &audioreach_tplg_ops, fw);
	if (ret < 0) {
		dev_err(dev, "tplg component load failed%d\n", ret);
		release_firmware(fw);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(audioreach_tplg_init);
