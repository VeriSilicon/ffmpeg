/*
 * Verisilicon VPE HW Uploader Filter
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
#include "libavutil/buffer.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"

typedef struct VpeUploadContext {
    const AVClass *class;

    AVBufferRef *hwframe;
    AVBufferRef *hwdevice;

    // VPI codec/filter context
    VpiCtx ctx;
    // VPI codec/filter API
    VpiApi *vpi;
    VpiHWUploadCfg *cfg;
} VpeUploadContext;

static av_cold void vpe_upload_uninit(AVFilterContext *ctx)
{
    VpeUploadContext *s = ctx->priv;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;

    if (s->ctx) {
        s->vpi->close(s->ctx);
    }

    av_buffer_unref(&s->hwframe);
    av_buffer_unref(&s->hwdevice);

    if (s->ctx) {
        hwdevice_ctx = (AVHWDeviceContext *)ctx->hw_device_ctx->data;
        vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;
        vpi_destroy(s->ctx, vpedev_ctx->device);
    }
}

static int vpe_upload_config_input(AVFilterLink *inlink)
{
    AVFilterContext * ctx = inlink->dst;
    VpeUploadContext * s = ctx->priv;

    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;

    int ret = 0;
    VpiCtrlCmdParam cmd_param;
    VpiFrame *vpi_frame;

    if (!inlink->hw_frames_ctx) {
        /* hw device & frame init */
        if(ctx->hw_device_ctx){
            inlink->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
            if (!inlink->hw_frames_ctx) {
                av_log(ctx, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
                return AVERROR(ENOMEM);
            }
        }else{
            av_log(ctx, AV_LOG_ERROR, "No hw frame/device available\n");
            return AVERROR(EINVAL);
        }

        hwframe_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
        hwframe_ctx->format    = AV_PIX_FMT_VPE;
        hwframe_ctx->sw_format = inlink->format;
        hwframe_ctx->width     = inlink->w;
        hwframe_ctx->height    = inlink->h;
        if ((ret = av_hwframe_ctx_init(inlink->hw_frames_ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "av_hwframe_ctx_init failed\n");
            return ret;
        }
    } else {
        hwframe_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    }

    s->hwframe = av_buffer_ref(inlink->hw_frames_ctx);
    if (s->hwframe == NULL)
        return AVERROR(ENOMEM);
    s->hwdevice = av_buffer_ref(ctx->hw_device_ctx);
    if(!s->hwdevice){
        return AVERROR(ENOMEM);
    }

    hwdevice_ctx = (AVHWDeviceContext *)ctx->hw_device_ctx->data;
    vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;

    // Create the VPE context
    ret = vpi_create(&s->ctx, &s->vpi, vpedev_ctx->device, HWUPLOAD_VPE);
    if (ret) {
        av_log(ctx, AV_LOG_ERROR, "vpi create failure ret %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    // get the decoder init option struct
    cmd_param.cmd = VPI_CMD_HWDL_INIT_OPTION;
    ret = s->vpi->control(s->ctx, (void *)&cmd_param, (void *)&s->cfg);
    if (ret != 0) {
        return AVERROR(ENOMEM);
    }

    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    vpi_frame    = (VpiFrame *)vpeframe_ctx->frame;

    vpi_frame->src_width  = inlink->w;
    vpi_frame->src_height = inlink->h;
    s->cfg->frame         = vpi_frame;
    if(inlink->format == AV_PIX_FMT_NV12)
        s->cfg->format = VPI_FMT_NV12;
    else if(inlink->format == AV_PIX_FMT_YUV420P)
        s->cfg->format = VPI_FMT_YUV420P;
    else if (inlink->format == AV_PIX_FMT_P010LE)
        s->cfg->format = VPI_FMT_P010LE;
    else
        s->cfg->format = VPI_FMT_OTHERS;

    ret = s->vpi->init(s->ctx, s->cfg);
    if (ret) {
        av_log(ctx, AV_LOG_ERROR, "vpi hwupload init failure\n");
        return AVERROR_EXTERNAL;
    }

   return 0;
}

static int vpe_upload_query_formats(AVFilterContext *ctx)
{
    int ret;

    static const enum AVPixelFormat input_pix_fmts[] = {
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010LE,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat output_pix_fmts[] = {
        AV_PIX_FMT_VPE,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *in_fmts;
    AVFilterFormats *out_fmts;

    in_fmts  = ff_make_format_list(input_pix_fmts);

    ret = ff_formats_ref(in_fmts, &ctx->inputs[0]->out_formats);
    if (ret < 0)
        return ret;

    out_fmts = ff_make_format_list(output_pix_fmts);

    ret = ff_formats_ref(out_fmts, &ctx->outputs[0]->in_formats);
    if (ret < 0)
        return ret;

    return 0;
}

static void upload_pic_consume(void *opaque, uint8_t *data)
{
    VpeUploadContext *ctx = (VpeUploadContext *)opaque;
    VpiCtrlCmdParam cmd_param;

    // make decoder release DPB
    cmd_param.cmd  = VPI_CMD_HWUL_FREE_BUF;
    cmd_param.data = data;
    ctx->vpi->control(ctx->ctx, (void *)&cmd_param, NULL);

    av_freep(&data);
}

static int vpe_upload_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterLink  *outlink = link->dst->outputs[0];
    VpeUploadContext *ctx = outlink->src->priv;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVFrame *out = NULL;
    int ret = 0;
    VpiFrame *in_frame, *out_frame;

    hwframe_ctx = (AVHWFramesContext *)ctx->hwframe->data;
    vpeframe_ctx = hwframe_ctx->hwctx;

    in_frame = (VpiFrame *)av_mallocz(vpeframe_ctx->frame_size);
    if (!in_frame) {
        return AVERROR(ENOMEM);
    }
    out_frame = (VpiFrame *)av_mallocz(vpeframe_ctx->frame_size);
    if (!out_frame) {
        return AVERROR(ENOMEM);
    }
    in_frame->linesize[0] = in->linesize[0];
    in_frame->linesize[1] = in->linesize[1];
    in_frame->linesize[2] = in->linesize[2];
    in_frame->src_width   = in->width;
    in_frame->src_height  = in->height;
    in_frame->data[0]     = in->data[0];
    in_frame->data[1]     = in->data[1];
    in_frame->data[2]     = in->data[2];
    in_frame->width       = in->width;
    in_frame->height      = in->height;

    in_frame->key_frame   = in->key_frame;
    in_frame->pts         = in->pts;
    in_frame->pkt_dts     = in->pkt_dts;

    ret = ctx->vpi->process(ctx->ctx, in_frame, out_frame);
    if (ret) {
        av_log(ctx, AV_LOG_ERROR,"hwupload_vpe filter frame failed\n");
        return AVERROR_EXTERNAL;
    }

    out = av_frame_alloc();
    if(!out){
        av_log(ctx, AV_LOG_ERROR,"can not alloc \n");
        return AVERROR(ENOMEM);
    }

    ret = av_frame_copy_props(out, in);
    if(ret){
        av_log(ctx, AV_LOG_ERROR,"copy props failed \n");
        goto fail;
    }

    out->width  = in->width;
    out->height = in->height;
    out->format = AV_PIX_FMT_VPE;
    out->linesize[0] = in->width;
    out->linesize[1] = in->width/2;

    out->data[0] = (void *)out_frame;
    out->buf[0] = av_buffer_create((uint8_t *)out_frame,
                                    sizeof(vpeframe_ctx->frame_size),
                                    upload_pic_consume, ctx,
                                    AV_BUFFER_FLAG_READONLY);
    if (out->buf[0] == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    out->hw_frames_ctx = av_buffer_ref(ctx->hwframe);
    if (out->hw_frames_ctx == NULL) {
      goto fail;
    }

    av_freep(&in_frame);
    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(VpeUploadContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption vpe_upload_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(vpe_upload);

static const AVFilterPad vpe_upload_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = vpe_upload_config_input,
        .filter_frame = vpe_upload_filter_frame,
    },
    { NULL }
};

static const AVFilterPad vpe_upload_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_hwupload_vpe = {
    .name        = "hwupload_vpe",
    .description = NULL_IF_CONFIG_SMALL("Upload a system memory frame to a vpe device."),
    .uninit      = vpe_upload_uninit,
    .query_formats = vpe_upload_query_formats,

    .priv_size  = sizeof(VpeUploadContext),
    .priv_class = &vpe_upload_class,

    .inputs    = vpe_upload_inputs,
    .outputs   = vpe_upload_outputs,
};
