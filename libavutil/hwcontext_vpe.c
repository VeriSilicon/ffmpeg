/*
 * Verisilicon hardware context
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd. <>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "buffer.h"
#include "pixfmt.h"
#include "pixdesc.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_vpe.h"
#include "libavutil/opt.h"

typedef struct VpeDevicePriv {
    VpiSysInfo *sys_info;
} VpeDevicePriv;

typedef struct VpeDeviceContext {
    VpiMediaProc *media_proc;
} VpeDeviceContext;

static const enum AVPixelFormat supported_sw_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010LE,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_NV21,
    AV_PIX_FMT_YUV420P10LE,
    AV_PIX_FMT_YUV420P10BE,
    AV_PIX_FMT_YUV422P10LE,
    AV_PIX_FMT_YUV422P10BE,
    AV_PIX_FMT_P010BE,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_BGRA,
};

static const enum AVPixelFormat supported_hw_formats[] = {
    AV_PIX_FMT_VPE,
};

static int vpe_frames_get_constraints(AVHWDeviceContext *ctx,
                                      const void *hwconfig,
                                      AVHWFramesConstraints *constraints)
{
    int i;

    constraints->valid_sw_formats =
        av_malloc_array(FF_ARRAY_ELEMS(supported_sw_formats) + 1,
                        sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_sw_formats); i++)
        constraints->valid_sw_formats[i] = supported_sw_formats[i];
    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_sw_formats)] =
        AV_PIX_FMT_NONE;

    constraints->valid_hw_formats =
        av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats) {
        return AVERROR(ENOMEM);
    }

    constraints->valid_hw_formats[0] = AV_PIX_FMT_VPE;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    constraints->min_width  = 144;
    constraints->min_height = 144;
    constraints->max_width  = 4096;
    constraints->max_height = 4096;

    return 0;
}

static int vpe_transfer_get_formats(AVHWFramesContext *ctx,
                                    enum AVHWFrameTransferDirection dir,
                                    enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;

    return 0;
}

static int vpe_transfer_data_from(AVHWFramesContext *ctx, AVFrame *dst,
                                  const AVFrame *src)
{
    VpeDeviceContext *priv = ctx->device_ctx->internal->priv;
    VpiFrame *in_frame;
    VpiFrame out_frame;
    VpiCtrlCmdParam cmd_param;

    VpiPicInfo *pic_info;
    int pp_index;
    int i, ret;

    in_frame = (VpiFrame *)src->data[0];
    for (i = 1; i < PIC_INDEX_MAX_NUMBER; i++) {
        pic_info = (VpiPicInfo *)src->buf[i]->data;
        if (pic_info->enabled == 1) {
            pp_index = i;
            break;
        }
    }
    if (i == PIC_INDEX_MAX_NUMBER) {
        return AVERROR_EXTERNAL;
    }

    cmd_param.cmd  = VPI_CMD_HWDW_SET_INDEX;
    cmd_param.data = (void *)&pp_index;
    priv->media_proc->vpi->control(priv->media_proc->ctx,
                                  (void *)&cmd_param, NULL);

    out_frame.data[0]     = dst->data[0];
    out_frame.data[1]     = dst->data[1];
    out_frame.data[2]     = dst->data[2];
    out_frame.linesize[0] = dst->linesize[0];
    out_frame.linesize[1] = dst->linesize[1];
    out_frame.linesize[2] = dst->linesize[2];
    ret = priv->media_proc->vpi->process(priv->media_proc->ctx,
                                         in_frame, &out_frame);

    return ret;
}

static void vpe_buffer_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext *ctx           = opaque;
    AVVpeDeviceContext *device_hwctx = ctx->device_ctx->hwctx;
    VpiCtrlCmdParam cmd_param;

    cmd_param.cmd  = VPI_CMD_FREE_FRAME_BUFFER;
    cmd_param.data = (void *)data;
    device_hwctx->func->control(&device_hwctx->device, &cmd_param, NULL);
}

static AVBufferRef *vpe_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext *ctx           = opaque;
    AVVpeDeviceContext *device_hwctx = ctx->device_ctx->hwctx;
    VpiFrame *v_frame                = NULL;
    VpiCtrlCmdParam cmd_param;
    AVBufferRef *ret;

    cmd_param.cmd = VPI_CMD_GET_FRAME_BUFFER;
    device_hwctx->func->control(&device_hwctx->device, &cmd_param, (void *)&v_frame);
    if (!v_frame) {
        return NULL;
    }
    ret = av_buffer_create((uint8_t*)v_frame, size,
                            vpe_buffer_free, ctx, 0);

    return ret;
}

static int vpe_frames_init(AVHWFramesContext *hwfc)
{
    AVVpeDeviceContext *device_hwctx = hwfc->device_ctx->hwctx;
    AVVpeFramesContext *frame_hwctx  = hwfc->hwctx;
    VpiCtrlCmdParam cmd_param;
    int size         = 0;
    int picinfo_size = 0;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_sw_formats); i++) {
        if (hwfc->sw_format == supported_sw_formats[i]) {
            break;
        }
    }
    if (i == FF_ARRAY_ELEMS(supported_sw_formats)) {
        av_log(hwfc, AV_LOG_ERROR, "Pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(hwfc->sw_format));
        return AVERROR(ENOSYS);
    }

    if (!hwfc->pool) {
        cmd_param.cmd = VPI_CMD_GET_VPEFRAME_SIZE;
        device_hwctx->func->control(&device_hwctx->device, &cmd_param, (void *)&size);
        if (size == 0) {
            return AVERROR_EXTERNAL;
        }
        frame_hwctx->frame = (VpiFrame *)av_mallocz(size);
        if (!frame_hwctx->frame) {
            return AVERROR(ENOMEM);
        }
        frame_hwctx->frame_size = size;

        cmd_param.cmd = VPI_CMD_GET_PICINFO_SIZE;
        device_hwctx->func->control(&device_hwctx->device, &cmd_param, (void *)&picinfo_size);
        if (picinfo_size == 0) {
            return AVERROR_EXTERNAL;
        }
        frame_hwctx->pic_info_size = picinfo_size;

        hwfc->internal->pool_internal =
            av_buffer_pool_init2(size, hwfc, vpe_pool_alloc, NULL);
        if (!hwfc->internal->pool_internal)
            return AVERROR(ENOMEM);
    }
    return 0;
}

static int vpe_get_buffer(AVHWFramesContext *hwfc, AVFrame *frame)
{
    AVVpeFramesContext *vpeframe_ctx = hwfc->hwctx;
    VpiPicInfo *pic_info;
    int i;

    frame->buf[0] = av_buffer_pool_get(hwfc->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[0] = frame->buf[0]->data;
    memset(frame->data[0], 0, vpeframe_ctx->frame_size);

    /* suppor max 4 channel low res buffer */
    for (i = 1; i < PIC_INDEX_MAX_NUMBER; i++) {
        frame->buf[i] = av_buffer_alloc(vpeframe_ctx->pic_info_size);
        if (frame->buf[i] == 0) {
            return AVERROR(ENOMEM);
        }
    }
    /* always enable the first channel */
    pic_info = (VpiPicInfo *)frame->buf[1]->data;
    pic_info->enabled = 1;

    frame->format  = AV_PIX_FMT_VPE;
    frame->width   = hwfc->width;
    frame->height  = hwfc->height;
    return 0;
}

static void vpe_device_uninit(AVHWDeviceContext *device_ctx)
{
    AVVpeDeviceContext *hwctx = device_ctx->hwctx;
    VpeDeviceContext *priv    = device_ctx->internal->priv;

    if (priv->media_proc) {
        if (priv->media_proc->ctx) {
            priv->media_proc->vpi->close(priv->media_proc->ctx);
            vpi_destroy(priv->media_proc->ctx, hwctx->device);
            priv->media_proc->ctx = NULL;
        }
        vpi_freep(&priv->media_proc);
    }
}

static int vpe_device_init(AVHWDeviceContext *device_ctx)
{
    AVVpeDeviceContext *hwctx = device_ctx->hwctx;
    VpeDeviceContext *priv    = device_ctx->internal->priv;
    int ret = 0;

    ret = vpi_get_media_proc_struct(&priv->media_proc);
    if (ret || !priv->media_proc) {
        return AVERROR(ENOMEM);
    }

    ret = vpi_create(&priv->media_proc->ctx, &priv->media_proc->vpi,
                      hwctx->device, HWDOWNLOAD_VPE);
    if (ret != 0)
        return AVERROR_EXTERNAL;

    ret = priv->media_proc->vpi->init(priv->media_proc->ctx, NULL);
    if (ret != 0)
       return AVERROR_EXTERNAL;

    return 0;
}

static void vpe_device_free(AVHWDeviceContext *device_ctx)
{
    AVVpeDeviceContext *hwctx = device_ctx->hwctx;
    VpeDevicePriv *priv = (VpeDevicePriv *)device_ctx->user_opaque;

    vpi_destroy(priv->sys_info, hwctx->device);

    vpi_freep(&priv->sys_info);
    av_freep(&priv);

    if (hwctx->device >= 0) {
        vpi_close_hwdevice(hwctx->device);
    }
}

static int vpe_device_create(AVHWDeviceContext *device_ctx, const char *device,
                             AVDictionary *opts, int flags)
{
    AVVpeDeviceContext *hwctx = device_ctx->hwctx;
    AVDictionaryEntry *opt;
    VpeDevicePriv *priv;
    int ret;

    priv = av_mallocz(sizeof(*priv));
    if (!priv)
        return AVERROR(ENOMEM);
    ret = vpi_get_sys_info_struct(&priv->sys_info);
    if (ret || !priv->sys_info) {
        av_freep(&priv);
        return AVERROR(ENOMEM);
    }

    device_ctx->user_opaque = priv;
    device_ctx->free = vpe_device_free;

    if (!device) {
        av_log(device_ctx, AV_LOG_ERROR, "No valid device path\n");
        return AVERROR_INVALIDDATA;
    }
    hwctx->device = vpi_open_hwdevice(device);
    if (hwctx->device == -1) {
        av_log(device_ctx, AV_LOG_ERROR, "failed to open hw device\n");
        return AVERROR_EXTERNAL;
    }

    priv->sys_info->device        = hwctx->device;
    priv->sys_info->priority      = VPE_TASK_VOD;
    priv->sys_info->sys_log_level = 0;

    if (opts) {
        opt = av_dict_get(opts, "priority", NULL, 0);
        if (opt) {
            if (!strcmp(opt->value, "live")) {
                priv->sys_info->priority = VPE_TASK_LIVE;
            } else if (!strcmp(opt->value, "vod")) {
                priv->sys_info->priority = VPE_TASK_VOD;
            } else {
                av_log(device_ctx, AV_LOG_ERROR, "Unknow priority : %s\n",
                       opt->value);
                return AVERROR_INVALIDDATA;
            }
        }

        opt = av_dict_get(opts, "vpeloglevel", NULL, 0);
        if (opt) {
            priv->sys_info->sys_log_level = atoi(opt->value);
        }
    }

    if (vpi_create(&priv->sys_info, &hwctx->func, hwctx->device,
                    HWCONTEXT_VPE) != 0) {
        return AVERROR_EXTERNAL;
    }

    return 0;
}

const HWContextType ff_hwcontext_type_vpe = {
    .type = AV_HWDEVICE_TYPE_VPE,
    .name = "VPE",

    .device_hwctx_size = sizeof(AVVpeDeviceContext),
    .device_priv_size  = sizeof(VpeDeviceContext),
    .frames_hwctx_size = sizeof(AVVpeFramesContext),

    .device_create          = vpe_device_create,
    .device_init            = vpe_device_init,
    .device_uninit          = vpe_device_uninit,
    .frames_get_constraints = vpe_frames_get_constraints,
    .frames_init            = vpe_frames_init,
    .frames_get_buffer      = vpe_get_buffer,
    .transfer_get_formats   = vpe_transfer_get_formats,
    .transfer_data_from     = vpe_transfer_data_from,

    .pix_fmts = (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_NONE },
};
