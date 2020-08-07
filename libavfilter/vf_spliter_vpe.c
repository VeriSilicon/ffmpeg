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

#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/frame.h"
#include "libavutil/buffer.h"
#include "libavutil/internal.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"

typedef struct SpliterVpeContext {
    const AVClass *class;
    int nb_outputs;
    struct {
        int enabled;
        int out_index;
        int flag;
        int width;
        int height;
        struct {
            int enabled;
            int x;
            int y;
            int w;
            int h;
        } crop;
        struct {
            int enabled;
            int w;
            int h;
        } scale;
    } pic_info[PIC_INDEX_MAX_NUMBER];
} SpliterVpeContext;

static int spliter_vpe_out_config_props(AVFilterLink *outlink);

static av_cold int spliter_vpe_init(AVFilterContext *ctx)
{
    SpliterVpeContext *s = ctx->priv;
    int i, ret;

    for (i = 0; i < s->nb_outputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "output%d", i);
        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_strdup(name);
        if (!pad.name) {
            return AVERROR(ENOMEM);
        }
        pad.config_props = spliter_vpe_out_config_props;

        if ((ret = ff_insert_outpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    for (i = 0; i < PIC_INDEX_MAX_NUMBER; i++) {
        s->pic_info[i].out_index = -1;
    }

    return 0;
}

static av_cold void spliter_vpe_uninit(AVFilterContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_outputs; i++) {
        av_freep(&ctx->output_pads[i].name);
    }
}

static int spliter_vpe_config_props(AVFilterLink *inlink)
{
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiFrame *frame_hwctx;
    AVFilterContext *dst = inlink->dst;
    SpliterVpeContext *s = dst->priv;
    int i;

    hwframe_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx = vpeframe_ctx->frame;

    for (i = 0; i < PIC_INDEX_MAX_NUMBER; i++) {
        s->pic_info[i].enabled = frame_hwctx->pic_info[i].enabled;
        s->pic_info[i].flag    = frame_hwctx->pic_info[i].flag;
        s->pic_info[i].width   = frame_hwctx->pic_info[i].width;
        s->pic_info[i].height  = frame_hwctx->pic_info[i].height;
    }
    s->pic_info[0].crop.enabled = frame_hwctx->pic_info[0].crop.enabled;
    s->pic_info[0].crop.x       = frame_hwctx->pic_info[0].crop.x;
    s->pic_info[0].crop.y       = frame_hwctx->pic_info[0].crop.y;
    s->pic_info[0].crop.w       = frame_hwctx->pic_info[0].crop.w;
    s->pic_info[0].crop.h       = frame_hwctx->pic_info[0].crop.h;

    return 0;
}

static int spliter_vpe_out_config_props(AVFilterLink *outlink)
{
    AVFilterContext *src = outlink->src;
    SpliterVpeContext *s = src->priv;
    int out_index, pp_index, j;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiFrame *frame_hwctx;

    if (!src->inputs[0]->hw_frames_ctx) {
        // for ffplay
        return 0;
    }

    outlink->hw_frames_ctx  = av_buffer_ref(src->inputs[0]->hw_frames_ctx);
    hwframe_ctx             = (AVHWFramesContext *)outlink->hw_frames_ctx->data;
    vpeframe_ctx            = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx             = vpeframe_ctx->frame;
    frame_hwctx->nb_outputs = s->nb_outputs;

    for (out_index = 0; out_index < src->nb_outputs; out_index++) {
        if (outlink == src->outputs[out_index]) {
            break;
        }
    }
    if (out_index == src->nb_outputs) {
        av_log(src, AV_LOG_ERROR, "can't find output\n");
        return AVERROR_INVALIDDATA;
    }

    for (pp_index = PIC_INDEX_MAX_NUMBER - 1; pp_index >= 0; pp_index--) {
        if (s->pic_info[pp_index].enabled && !s->pic_info[pp_index].flag &&
            s->pic_info[pp_index].out_index == -1) {
            break;
        }
    }

    for (j = 0; j < PIC_INDEX_MAX_NUMBER; j++) {
        if (j == pp_index) {
            continue;
        }
        if (frame_hwctx->pic_info[j].flag) {
            continue;
        }
        frame_hwctx->pic_info[j].enabled = 0;
    }

    outlink->w                      = s->pic_info[pp_index].width;
    outlink->h                      = s->pic_info[pp_index].height;
    s->pic_info[pp_index].out_index = out_index;

    return 0;
}

static int spliter_vpe_query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list;
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_VPE,
                                                   AV_PIX_FMT_NONE };

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list) {
        return AVERROR(ENOMEM);
    }

    return ff_set_common_formats(ctx, fmts_list);
}

static int spliter_vpe_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVFilterContext *ctx   = inlink->dst;
    SpliterVpeContext *s   = ctx->priv;
    int i, j, pp_index, ret = AVERROR_UNKNOWN;
    VpiPicInfo *pic_info;
    VpiFrame *vpi_frame;

    hwframe_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;

    pp_index = 0;
    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFrame *buf_out;

        if (ff_outlink_get_status(ctx->outputs[i])) {
            continue;
        }

        if (ctx->inputs[0]->hw_frames_ctx) {
            for (pp_index = 0; pp_index < PIC_INDEX_MAX_NUMBER; pp_index++) {
                if (i == s->pic_info[pp_index].out_index) {
                    break;
                }
            }
            if (pp_index == PIC_INDEX_MAX_NUMBER) {
                av_log(ctx, AV_LOG_ERROR, "can't find pp_index\n");
                ret = AVERROR_UNKNOWN;
                goto err_exit;
            }
        }

        if (i > 0) {
            buf_out = av_frame_alloc();
            if (!buf_out) {
                ret = AVERROR(ENOMEM);
                goto err_exit;
            }
            ret = av_frame_ref(buf_out, frame);
            if (ret < 0) {
                goto err_exit;
            }

            for (j = 1; j < PIC_INDEX_MAX_NUMBER; j++) {
                if (buf_out->buf[j]) {
                    av_buffer_unref(&buf_out->buf[j]);
                }
            }
            for (j = 1; j < PIC_INDEX_MAX_NUMBER; j++) {
                buf_out->buf[j] =
                    av_buffer_alloc(sizeof(vpeframe_ctx->pic_info_size));
                if (buf_out->buf[j] == NULL) {
                    goto err_exit;
                }
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
            goto err_exit;

        vpi_frame->nb_outputs = s->nb_outputs;
        ret = ff_filter_frame(ctx->outputs[i], buf_out);
        if (ret < 0) {
            goto err_exit;
        }
    }

err_exit:
    return ret;
}

#define OFFSET(x) offsetof(SpliterVpeContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption spliter_vpe_options[] = { { "outputs",
                                                  "set number of outputs",
                                                  OFFSET(nb_outputs),
                                                  AV_OPT_TYPE_INT,
                                                  { .i64 = 1 },
                                                  1,
                                                  4,
                                                  FLAGS },
                                                { NULL } };

AVFILTER_DEFINE_CLASS(spliter_vpe);

static const AVFilterPad spliter_vpe_inputs[] =
    { {
          .name         = "default",
          .type         = AVMEDIA_TYPE_VIDEO,
          .config_props = spliter_vpe_config_props,
          .filter_frame = spliter_vpe_filter_frame,
      },
      { NULL } };

AVFilter ff_vf_spliter_vpe = {
    .name        = "spliter_vpe",
    .description = NULL_IF_CONFIG_SMALL("Filter to split pictures generated by "
                                        "vpe"),
    .priv_size   = sizeof(SpliterVpeContext),
    .priv_class  = &spliter_vpe_class,
    .init        = spliter_vpe_init,
    .uninit      = spliter_vpe_uninit,
    .query_formats  = spliter_vpe_query_formats,
    .inputs         = spliter_vpe_inputs,
    .outputs        = NULL,
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
