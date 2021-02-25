/*
 * Verisilicon VPE Post Processing Filter
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
 * MERC`ABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include <vpe/vpi_types.h>
#include <vpe/vpi_api.h>

#include "filters.h"
#include "libavutil/opt.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"

typedef struct VpePPFrame {
    AVFrame *av_frame;
    // frame structure used for external hw device
    VpiFrame *vpi_frame;
    // vpi_frame has been used
    int used;
    // the next linked list item
    struct VpePPFrame *next;
} VpePPFrame;

typedef struct VpePPFilter {
    const AVClass *av_class;
    AVBufferRef *hw_device;
    AVBufferRef *hw_frame;

    /*VPI context*/
    VpiCtx ctx;
    /*The pointer of the VPE API*/
    VpiApi *vpi;

    /*output channel number*/
    int nb_outputs;
    /*force 10bit output*/
    int force_10bit;
    /*low_res parameter*/
    char *low_res;
    /*pp option*/
    VpiPPOption *option;
    /*VPE frame linked list*/
    VpePPFrame *frame_list;
} VpePPFilter;

static const enum AVPixelFormat input_pix_fmts[] = {
    AV_PIX_FMT_NV12,        AV_PIX_FMT_P010LE,      AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,     AV_PIX_FMT_NV21,        AV_PIX_FMT_YUV420P10LE,
    AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV422P10BE,
    AV_PIX_FMT_P010BE,      AV_PIX_FMT_YUV444P,     AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_ARGB,        AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,        AV_PIX_FMT_BGRA,        AV_PIX_FMT_VPE,
    AV_PIX_FMT_NONE,
};

typedef struct PixelMapTable {
    enum AVPixelFormat src;
    VpiPixsFmt des;
} PixelMapTable;

static PixelMapTable ptable[] = {
    { AV_PIX_FMT_YUV420P, VPI_FMT_YUV420P },
    { AV_PIX_FMT_YUV422P, VPI_FMT_YUV422P },
    { AV_PIX_FMT_NV12, VPI_FMT_NV12 },
    { AV_PIX_FMT_NV21, VPI_FMT_NV21 },
    { AV_PIX_FMT_YUV420P10LE, VPI_FMT_YUV420P10LE },
    { AV_PIX_FMT_YUV420P10BE, VPI_FMT_YUV420P10BE },
    { AV_PIX_FMT_YUV422P10LE, VPI_FMT_YUV422P10LE },
    { AV_PIX_FMT_YUV422P10BE, VPI_FMT_YUV422P10BE },
    { AV_PIX_FMT_P010LE, VPI_FMT_P010LE },
    { AV_PIX_FMT_P010BE, VPI_FMT_P010BE },
    { AV_PIX_FMT_YUV444P, VPI_FMT_YUV444P },
    { AV_PIX_FMT_RGB24, VPI_FMT_RGB24 },
    { AV_PIX_FMT_BGR24, VPI_FMT_BGR24 },
    { AV_PIX_FMT_ARGB, VPI_FMT_ARGB },
    { AV_PIX_FMT_RGBA, VPI_FMT_RGBA },
    { AV_PIX_FMT_ABGR, VPI_FMT_ABGR },
    { AV_PIX_FMT_BGRA, VPI_FMT_BGRA },
    { AV_PIX_FMT_VPE, VPI_FMT_VPE },
};

static const enum AVPixelFormat output_pix_fmts[] = {
    AV_PIX_FMT_VPE,
    AV_PIX_FMT_NONE,
};

static av_cold int vpe_pp_init(AVFilterContext *avf_ctx)
{
    int ret         = 0;
    AVFilterPad pad = { 0 };

    pad.type = AVMEDIA_TYPE_VIDEO;
    pad.name = "output0";
    if ((ret = ff_insert_outpad(avf_ctx, 0, &pad)) < 0)
        return ret;

    return 0;
}

static av_cold void vpe_pp_uninit(AVFilterContext *avf_ctx)
{
    AVHWFramesContext *hwframe_ctx;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;
    VpePPFilter *ctx = avf_ctx->priv;
    VpePPFrame *cur_frame;

    if (ctx->hw_device) {
        if (!avf_ctx->hw_device_ctx) {
            hwframe_ctx = (AVHWFramesContext *)ctx->hw_frame->data;
            vpedev_ctx  = hwframe_ctx->device_ctx->hwctx;
        } else {
            hwdevice_ctx = (AVHWDeviceContext *)avf_ctx->hw_device_ctx->data;
            vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;
        }

        ctx->vpi->close(ctx->ctx);

        cur_frame = ctx->frame_list;
        while (cur_frame) {
            ctx->frame_list = cur_frame->next;
            av_frame_free(&cur_frame->av_frame);
            av_freep(&cur_frame);
            cur_frame = ctx->frame_list;
        }

        av_buffer_unref(&ctx->hw_frame);
        av_buffer_unref(&ctx->hw_device);
        vpi_destroy(ctx->ctx, vpedev_ctx->device);
        avf_ctx->priv = NULL;
    }
}

static void vpe_pp_output_vpeframe(AVFrame *input, VpiFrame *output)
{
    output->width       = input->width;
    output->height      = input->height;
    output->linesize[0] = input->linesize[0];
    output->linesize[1] = input->linesize[1];
    output->linesize[2] = input->linesize[2];
    output->key_frame   = input->key_frame;
    output->pts         = input->pts;
    output->pkt_dts     = input->pkt_dts;
    output->data[0]     = input->data[0];
    output->data[1]     = input->data[1];
    output->data[2]     = input->data[2];
}

/**
 * alloc frame from hwcontext for pp filter
 */
static int vpe_pp_alloc_frame(AVFilterLink *outlink, VpePPFrame *pp_frame)
{
    AVFrame *frame;

    frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!frame) {
        av_log(outlink, AV_LOG_ERROR, "Failed to allocate frame to pp_vpe to.\n");
        return AVERROR(ENOMEM);
    }

    pp_frame->av_frame  = frame;
    pp_frame->vpi_frame = (VpiFrame*)frame->data[0];
    pp_frame->used      = 1;

    return 0;
}

/**
 * clear the unsed frames
 */
static void vpe_pp_clear_unused_frames(VpePPFilter *ctx)
{
    VpePPFrame *cur_frame = ctx->frame_list;

    while (cur_frame) {
        if (cur_frame->used
            && !cur_frame->vpi_frame->locked) {
            cur_frame->used = 0;
            av_frame_unref(cur_frame->av_frame);
        }
        cur_frame = cur_frame->next;
    }
}

/**
 * get frame from the linked list
 */
static int vpe_pp_get_frame(AVFilterLink *inlink, AVFrame **frame)
{
    AVFilterContext *avf_ctx = inlink->dst;
    AVFilterLink *outlink    = avf_ctx->outputs[0];
    VpePPFilter *ctx         = avf_ctx->priv;
    VpePPFrame *pp_frame, **last;
    int ret;

    vpe_pp_clear_unused_frames(ctx);

    pp_frame = ctx->frame_list;
    last     = &ctx->frame_list;
    while (pp_frame) {
        if (!pp_frame->used) {
            ret = vpe_pp_alloc_frame(outlink, pp_frame);
            if (ret < 0)
                return ret;
            *frame = pp_frame->av_frame;
            return 0;
        }

        last     = &pp_frame->next;
        pp_frame = pp_frame->next;
    }

    pp_frame = av_mallocz(sizeof(*pp_frame));
    if (!pp_frame)
        return AVERROR(ENOMEM);

    *last = pp_frame;

    ret = vpe_pp_alloc_frame(outlink, pp_frame);
    if (ret < 0)
        return ret;

    *frame = pp_frame->av_frame;

    return 0;
}

static int vpe_pp_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *avf_ctx = inlink->dst;
    AVFilterLink *outlink    = avf_ctx->outputs[0];
    AVFrame *pp_frame, *out = NULL;
    VpePPFilter *ctx = avf_ctx->priv;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiFrame *in_picture, *out_picture;
    int ret = 0;

    hwframe_ctx  = (AVHWFramesContext *)ctx->hw_frame->data;
    vpeframe_ctx = hwframe_ctx->hwctx;

    if (inlink->format != AV_PIX_FMT_VPE) {
        in_picture = (VpiFrame *)av_mallocz(vpeframe_ctx->frame_size);
        if (!in_picture) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        vpe_pp_output_vpeframe(frame, in_picture);
    } else {
        in_picture = (VpiFrame *)frame->data[0];
    }

    ret = vpe_pp_get_frame(inlink, &pp_frame);
    if (ret)
        goto fail;

    ret = ctx->vpi->process(ctx->ctx, in_picture, pp_frame->data[0]);
    if (ret) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ret = av_frame_copy_props(pp_frame, frame);
    if (ret)
        goto fail;

    out_picture = (VpiFrame *)pp_frame->data[0];

    pp_frame->width       = out_picture->width;
    pp_frame->height      = out_picture->height;
    pp_frame->linesize[0] = out_picture->linesize[0];
    pp_frame->linesize[1] = out_picture->linesize[1];
    pp_frame->linesize[2] = out_picture->linesize[2];
    pp_frame->key_frame   = out_picture->key_frame;
    pp_frame->format      = AV_PIX_FMT_VPE;

    if (inlink->format != AV_PIX_FMT_VPE)
        av_freep(&in_picture);

    ret = ff_outlink_get_status(outlink);
    if (ret < 0)
        goto fail;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_frame_free(&frame);
    av_frame_ref(out, pp_frame);
    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&frame);
    av_frame_free(&out);
    return ret;
}

static int vpe_pp_init_hwctx(AVFilterContext *ctx, AVFilterLink *inlink)
{
    AVHWFramesContext *hwframe_ctx;
    int ret             = 0;
    VpePPFilter *filter = ctx->priv;

    if (!inlink->hw_frames_ctx) {
        if (ctx->hw_device_ctx) {
            inlink->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
            if (!inlink->hw_frames_ctx) {
                av_log(ctx, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
                return AVERROR(ENOMEM);
            }
        } else {
            av_log(ctx, AV_LOG_ERROR, "No hw frame/device available\n");
            return AVERROR(EINVAL);
        }
        hwframe_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;

        hwframe_ctx->format    = AV_PIX_FMT_VPE;
        hwframe_ctx->sw_format = inlink->format;
        hwframe_ctx->width     = inlink->w;
        hwframe_ctx->height    = inlink->h;

        if ((ret = av_hwframe_ctx_init(inlink->hw_frames_ctx)) < 0)
            return ret;
    }

    filter->hw_device = av_buffer_ref(ctx->hw_device_ctx);
    if (!filter->hw_device)
        return AVERROR(ENOMEM);
    filter->hw_frame  = av_buffer_ref(inlink->hw_frames_ctx);
    if (!filter->hw_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int vpe_get_format(enum AVPixelFormat format)
{
    int i = 0;

    for (i = 0; i < sizeof(ptable) / sizeof(PixelMapTable); i++) {
        if (format == ptable[i].src)
            return ptable[i].des;
    }
    return AVERROR(EINVAL);
}

static int vpe_pp_config_props(AVFilterLink *inlink)
{
    AVFilterContext *avf_ctx = inlink->dst;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;
    VpePPFilter *ctx = avf_ctx->priv;
    VpiPPOption *option;
    VpiCtrlCmdParam cmd;
    int ret = 0;

    ret = vpe_pp_init_hwctx(avf_ctx, inlink);
    if (ret < 0)
        return ret;

    hwframe_ctx  = (AVHWFramesContext *)ctx->hw_frame->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    if (!avf_ctx->hw_device_ctx) {
        vpedev_ctx = hwframe_ctx->device_ctx->hwctx;
    } else {
        hwdevice_ctx = (AVHWDeviceContext *)avf_ctx->hw_device_ctx->data;
        vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;
    }

    ret = vpi_create(&ctx->ctx, &ctx->vpi, vpedev_ctx->device, PP_VPE);
    if (ret)
        return AVERROR_EXTERNAL;

    // get the pp option struct
    cmd.cmd = VPI_CMD_PP_INIT_OPTION;
    ret = ctx->vpi->control(ctx->ctx, (void *)&cmd, (void *)&ctx->option);
    if (ret)
        return AVERROR(ENOMEM);
    option = (VpiPPOption *)ctx->option;
    /*Get config*/
    option->w           = inlink->w;
    option->h           = inlink->h;
    option->format      = vpe_get_format(inlink->format);
    option->nb_outputs  = ctx->nb_outputs;
    option->force_10bit = ctx->force_10bit;
    option->low_res     = ctx->low_res;
    option->frame       = vpeframe_ctx->frame;

    if (inlink->format == AV_PIX_FMT_VPE) {
        // the previous filter is hwupload
        option->b_disable_tcache = 1;
    } else {
        option->b_disable_tcache = 0;
    }

    ret = ctx->vpi->init(ctx->ctx, option);
    if (ret)
        return AVERROR_EXTERNAL;

    return 0;
}

static int vpe_pp_query_formats(AVFilterContext *avf_ctx)
{
    int ret;
    AVFilterFormats *in_fmts;
    AVFilterFormats *out_fmts;

    in_fmts = ff_make_format_list(input_pix_fmts);
    ret = ff_formats_ref(in_fmts, &avf_ctx->inputs[0]->outcfg.formats);
    if (ret < 0){
        av_log(NULL, AV_LOG_ERROR, "input ff_formats_ref error=%d\n", ret);
        return ret;
    }

    out_fmts = ff_make_format_list(output_pix_fmts);
    ret      = ff_formats_ref(out_fmts, &avf_ctx->outputs[0]->incfg.formats);
    if (ret < 0){
        av_log(NULL, AV_LOG_ERROR, "output ff_formats_ref error=%d\n", ret);
        return ret;
    }

    return ret;
}

#define OFFSET(x) offsetof(VpePPFilter, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

static const AVOption vpe_pp_options[] = {
    { "outputs",
      "set number of outputs",
      OFFSET(nb_outputs),
      AV_OPT_TYPE_INT,
      { .i64 = 1 },
      1,
      4,
      FLAGS },
    { "low_res",
      "specific resize configuration.",
      OFFSET(low_res),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { "force10bit",
      "upsampling 8bit to 10bit",
      OFFSET(force_10bit),
      AV_OPT_TYPE_INT,
      { .i64 = 0 },
      0,
      1,
      FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(vpe_pp);

static const AVFilterPad vpe_pp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = vpe_pp_filter_frame,
        .config_props = vpe_pp_config_props,
    },
    { NULL }
};

AVFilter ff_vf_pp_vpe = {
    .name          = "vpe_pp",
    .description   = NULL_IF_CONFIG_SMALL("Filter using vpe post processing."),
    .priv_size     = sizeof(VpePPFilter),
    .priv_class    = &vpe_pp_class,
    .init          = vpe_pp_init,
    .uninit        = vpe_pp_uninit,
    .query_formats = vpe_pp_query_formats,
    .inputs        = vpe_pp_inputs,
    .outputs       = NULL,
    .flags         = 0,
};
