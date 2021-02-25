/*
 * Verisilicon VPE Spliter Filter
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vpe/vpi_types.h>

#include "filters.h"
#include "libavutil/opt.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"

typedef struct VpeSpliterContext {
    const AVClass *class;
    /*setting output channel number*/
    int nb_outputs;
    /*valid output channel number*/
    int valid_outputs;
    struct {
        int enabled;
        int out_index;
        int flag;
        int width;
        int height;
    } pic_info[PIC_INDEX_MAX_NUMBER];
} VpeSpliterContext;

static int vpe_spliter_out_config_props(AVFilterLink *outlink);

static av_cold int vpe_spliter_init(AVFilterContext *ctx)
{
    VpeSpliterContext *s = ctx->priv;
    int i, ret;

    for (i = 0; i < s->nb_outputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "output%d", i);
        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_strdup(name);
        if (!pad.name)
            return AVERROR(ENOMEM);
        pad.config_props = vpe_spliter_out_config_props;

        if ((ret = ff_insert_outpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    for (i = 0; i < PIC_INDEX_MAX_NUMBER; i++) {
        s->pic_info[i].out_index = -1;
    }
    s->valid_outputs = 0;

    return 0;
}

static av_cold void vpe_spliter_uninit(AVFilterContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_outputs; i++) {
        av_freep(&ctx->output_pads[i].name);
    }
}

static int vpe_spliter_config_props(AVFilterLink *inlink)
{
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiFrame *frame_hwctx;
    AVFilterContext *dst = inlink->dst;
    VpeSpliterContext *s = dst->priv;
    AVFilterLink *outlink;
    AVFilterContext *dst_output;
    int i;

    hwframe_ctx  = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx  = vpeframe_ctx->frame;

    for (i = 0; i < PIC_INDEX_MAX_NUMBER; i++) {
        s->pic_info[i].enabled = frame_hwctx->pic_info[i].enabled;
        s->pic_info[i].flag    = frame_hwctx->pic_info[i].flag;
        s->pic_info[i].width   = frame_hwctx->pic_info[i].width;
        s->pic_info[i].height  = frame_hwctx->pic_info[i].height;
    }

    // check the valid output except the null device
    for (i = 0; i < s->nb_outputs; i++) {
        outlink = dst->outputs[i];
        if (outlink) {
            dst_output = outlink->dst;
            av_log(NULL, AV_LOG_INFO, "dst name %s\n", dst_output->name);
            if (strstr(dst_output->name, "format") ||
                strstr(dst_output->name, "hwdownload")) {
                s->valid_outputs++;
            }
        }
    }

    return 0;
}

static int vpe_spliter_out_config_props(AVFilterLink *outlink)
{
    AVFilterContext *src = outlink->src;
    VpeSpliterContext *s = src->priv;
    int out_index, pp_index, j;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiFrame *frame_hwctx;

    if (!src->inputs[0]->hw_frames_ctx)
        return 0;

    outlink->hw_frames_ctx  = av_buffer_ref(src->inputs[0]->hw_frames_ctx);
    hwframe_ctx             = (AVHWFramesContext *)outlink->hw_frames_ctx->data;
    vpeframe_ctx            = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx             = vpeframe_ctx->frame;
    frame_hwctx->nb_outputs = s->nb_outputs;

    for (out_index = 0; out_index < src->nb_outputs; out_index++) {
        if (outlink == src->outputs[out_index])
            break;
    }
    if (out_index == src->nb_outputs) {
        av_log(src, AV_LOG_ERROR, "can't find output\n");
        return AVERROR_INVALIDDATA;
    }

    for (pp_index = PIC_INDEX_MAX_NUMBER - 1; pp_index >= 0; pp_index--) {
        if (s->pic_info[pp_index].enabled && !s->pic_info[pp_index].flag &&
            s->pic_info[pp_index].out_index == -1)
            break;
    }

    for (j = 0; j < PIC_INDEX_MAX_NUMBER; j++) {
        if (j == pp_index)
            continue;
        if (frame_hwctx->pic_info[j].flag)
            continue;
        frame_hwctx->pic_info[j].enabled = 0;
    }

    outlink->w                      = s->pic_info[pp_index].width;
    outlink->h                      = s->pic_info[pp_index].height;
    s->pic_info[pp_index].out_index = out_index;

    return 0;
}

static int vpe_spliter_query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list;
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_VPE,
                                                   AV_PIX_FMT_NONE };

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, fmts_list);
}

static int vpe_spliter_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVFilterContext *ctx = inlink->dst;
    VpeSpliterContext *s   = ctx->priv;
    int i, j, pp_index, ret = AVERROR_UNKNOWN;
    VpiPicInfo *pic_info;
    VpiFrame *vpi_frame;

    hwframe_ctx  = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;

    pp_index = 0;
    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFrame *buf_out;

        if (ff_outlink_get_status(ctx->outputs[i]))
            continue;

        if (ctx->inputs[0]->hw_frames_ctx) {
            for (pp_index = 0; pp_index < PIC_INDEX_MAX_NUMBER; pp_index++) {
                if (i == s->pic_info[pp_index].out_index)
                    break;
            }
            if (pp_index == PIC_INDEX_MAX_NUMBER) {
                av_log(ctx, AV_LOG_ERROR, "can't find pp_index\n");
                return AVERROR_UNKNOWN;
            }
        }

        if (i > 0) {
            buf_out = av_frame_alloc();
            if (!buf_out)
                return AVERROR(ENOMEM);
            ret = av_frame_ref(buf_out, frame);
            if (ret < 0)
                return ret;

            for (j = 1; j < PIC_INDEX_MAX_NUMBER; j++) {
                if (buf_out->buf[j])
                    av_buffer_unref(&buf_out->buf[j]);
            }
            for (j = 1; j < PIC_INDEX_MAX_NUMBER; j++) {
                buf_out->buf[j] =
                    av_buffer_alloc(sizeof(vpeframe_ctx->pic_info_size));
                if (!buf_out->buf[j])
                    return AVERROR(ENOMEM);
            }

        } else {
            buf_out = frame;
        }

        for (j = 1; j < PIC_INDEX_MAX_NUMBER; j++) {
            if (buf_out->buf[j] == NULL || buf_out->buf[j]->data == NULL)
                continue;
            pic_info = (VpiPicInfo *)buf_out->buf[j]->data;
            if (j == pp_index) {
                pic_info->enabled = 1;
            } else {
                pic_info->enabled = 0;
            }
        }

        vpi_frame = (VpiFrame *)buf_out->data[0];
        if (!vpi_frame)
            return AVERROR(ENOMEM);

        vpi_frame->nb_outputs = s->valid_outputs;
        ret = ff_filter_frame(ctx->outputs[i], buf_out);
        if (ret < 0)
            return ret;
    }

    return ret;
}

#define OFFSET(x) offsetof(VpeSpliterContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption vpe_spliter_options[] = { { "outputs",
                                                  "set number of outputs",
                                                  OFFSET(nb_outputs),
                                                  AV_OPT_TYPE_INT,
                                                  { .i64 = 1 },
                                                  1,
                                                  4,
                                                  FLAGS },
                                                { NULL } };

AVFILTER_DEFINE_CLASS(vpe_spliter);

static const AVFilterPad vpe_spliter_inputs[] =
    { {
          .name         = "default",
          .type         = AVMEDIA_TYPE_VIDEO,
          .config_props = vpe_spliter_config_props,
          .filter_frame = vpe_spliter_filter_frame,
      },
      { NULL } };

AVFilter ff_vf_spliter_vpe = {
    .name        = "spliter_vpe",
    .description = NULL_IF_CONFIG_SMALL("Filter to split pictures generated by "
                                        "vpe"),
    .priv_size   = sizeof(VpeSpliterContext),
    .priv_class  = &vpe_spliter_class,
    .init        = vpe_spliter_init,
    .uninit      = vpe_spliter_uninit,
    .query_formats  = vpe_spliter_query_formats,
    .inputs         = vpe_spliter_inputs,
    .outputs        = NULL,
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
