/*
 * Verisilicon VPE H264/HEVC Encoder
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

#include "internal.h"
#include "hwconfig.h"
#include "libavutil/opt.h"
#include "vpe_enc_common.h"

#define OFFSETOPT(x) offsetof(VpeEncCtx, x)
#define FLAGS \
    (AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_EXPORT)

static const AVOption vpe_h26x_encode_options[] = {
    { "crf",
      "VCE Constant rate factor mode. Works with lookahead turned on.",
      OFFSETOPT(crf),
      AV_OPT_TYPE_INT,
      { .i64 = -1 },
      -1,
      51,
      FLAGS },
    { "preset",
      "Set the encoding preset",
      OFFSETOPT(preset),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { "profile",
      "Set encode profile. HEVC:0-2; H264:9-12",
      OFFSETOPT(profile),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { "level",
      "Set encode level",
      OFFSETOPT(level),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { "force_idr",
      "If forcing keyframes, force them as IDR frames.",
      OFFSETOPT(force_idr),
      AV_OPT_TYPE_INT,
      { .i64 = 0 },
      0,
      1,
      .flags = FLAGS },

    /* Detail parameters is described in
       https://github.com/VeriSilicon/ffmpeg#H264-HEVC-Encoder*/
    { "enc_params",
      "Override the VPE h264/hevc configuration using a :-separated list of "
      "key=value parameters.",
      OFFSETOPT(enc_params),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { NULL },
};

static av_cold int vpe_h26x_encode_init(AVCodecContext *avctx)
{
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;
    VpiCtrlCmdParam cmd;
    VpiH26xEncCfg *h26x_enc_cfg;
    int extradata_size = 0;
    int ret            = 0;

    ret = ff_vpe_encode_init(avctx, H26XENC_VPE);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_vpe_encode_init H26x failed\n");
        return ret;
    }

    // get the encoder cfg struct
    cmd.cmd = VPI_CMD_ENC_INIT_OPTION;
    ret = enc_ctx->vpi->control(enc_ctx->ctx, (void *)&cmd,
                                (void *)&enc_ctx->enc_cfg);
    if (ret != 0) {
        return AVERROR(ENOMEM);
    }
    h26x_enc_cfg = (VpiH26xEncCfg *)enc_ctx->enc_cfg;

    if (avctx->codec->id == AV_CODEC_ID_HEVC) {
        strcpy(h26x_enc_cfg->module_name, "HEVCENC");
    } else if (avctx->codec->id == AV_CODEC_ID_H264) {
        strcpy(h26x_enc_cfg->module_name, "H264ENC");
    }

    /*Initialize the VPE h26x encoder configuration*/
    h26x_enc_cfg->crf    = enc_ctx->crf;
    h26x_enc_cfg->preset = enc_ctx->preset;
    if (avctx->codec->id == AV_CODEC_ID_HEVC) {
        h26x_enc_cfg->codec_id = CODEC_ID_HEVC;
    } else if (avctx->codec->id == AV_CODEC_ID_H264) {
        h26x_enc_cfg->codec_id = CODEC_ID_H264;
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "%s, avctx->codec->id isn't HEVC or H264 \n", __FUNCTION__);
        return AVERROR(EINVAL);
    }
    h26x_enc_cfg->codec_name = avctx->codec->name;
    h26x_enc_cfg->profile    = enc_ctx->profile;
    h26x_enc_cfg->level      = enc_ctx->level;
    h26x_enc_cfg->force_idr  = enc_ctx->force_idr;

    h26x_enc_cfg->bit_per_second = avctx->bit_rate;
    /* Input frame rate numerator*/
    h26x_enc_cfg->input_rate_numer = avctx->framerate.num;
    /* Input frame rate denominator*/
    h26x_enc_cfg->input_rate_denom = avctx->framerate.den;
    h26x_enc_cfg->lum_width_src    = avctx->width;
    h26x_enc_cfg->lum_height_src   = avctx->height;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        h26x_enc_cfg->input_format = VPI_YUV420_PLANAR;
        break;
    case AV_PIX_FMT_NV12:
        h26x_enc_cfg->input_format = VPI_YUV420_SEMIPLANAR;
        break;
    case AV_PIX_FMT_NV21:
        h26x_enc_cfg->input_format = VPI_YUV420_SEMIPLANAR_VU;
        break;
    case AV_PIX_FMT_YUV420P10LE:
        h26x_enc_cfg->input_format = VPI_YUV420_PLANAR_10BIT_P010;
        break;
    default:
        h26x_enc_cfg->input_format = VPI_YUV420_PLANAR;
        break;
    }
    h26x_enc_cfg->frame_ctx  = enc_ctx->vpi_frame;
    h26x_enc_cfg->param_list = enc_ctx->param_list;

    h26x_enc_cfg->colour_primaries         = avctx->color_primaries;
    h26x_enc_cfg->transfer_characteristics = avctx->color_trc;
    h26x_enc_cfg->matrix_coeffs            = avctx->colorspace;

    h26x_enc_cfg->color_range       = avctx->color_range;
    h26x_enc_cfg->aspect_ratio_num  = avctx->sample_aspect_ratio.num;
    h26x_enc_cfg->aspect_ration_den = avctx->sample_aspect_ratio.den;

    /*Call the VPE h26x encoder initialization function*/
    ret = enc_ctx->vpi->init(enc_ctx->ctx, h26x_enc_cfg);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "vpe_h26x_encode_init failed, error=%s(%d)\n",
               vpi_error_str(ret), ret);
        return AVERROR_EXTERNAL;
    }

    cmd.cmd = VPI_CMD_ENC_GET_EXTRADATA_SIZE;
    ret = enc_ctx->vpi->control(enc_ctx->ctx, &cmd, (void *)&extradata_size);
    if (ret != 0) {
        return AVERROR_EXTERNAL;
    }
    if (extradata_size != 0) {
        avctx->extradata = av_malloc(extradata_size);
        if (avctx->extradata == NULL) {
            return AVERROR(ENOMEM);
        }
        cmd.cmd  = VPI_CMD_ENC_GET_EXTRADATA;
        cmd.data = (void *)avctx->extradata;
        ret = enc_ctx->vpi->control(enc_ctx->ctx, &cmd, NULL);
        if (ret != 0) {
            return AVERROR_EXTERNAL;
        }
        avctx->extradata_size = extradata_size;
    }

    return 0;
}

static const AVClass vpe_encode_h264_class = {
    .class_name = "h264e_vpe",
    .item_name  = av_default_item_name,
    .option     = vpe_h26x_encode_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass vpe_encode_hevc_class = {
    .class_name = "hevce_vpe",
    .item_name  = av_default_item_name,
    .option     = vpe_h26x_encode_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecHWConfigInternal *vpe_h26x_hw_configs[] =
    { &(const AVCodecHWConfigInternal){
          .public =
              {
                  .pix_fmt = AV_PIX_FMT_VPE,
                  .methods = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX |
                             AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX,
                  .device_type = AV_HWDEVICE_TYPE_VPE,
              },
          .hwaccel = NULL,
      },
      NULL };

AVCodec ff_h264_vpe_encoder = {
    .name           = "h264enc_vpe",
    .long_name      = NULL_IF_CONFIG_SMALL("H264 (VPE VC8000E)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(VpeEncCtx),
    .init           = &vpe_h26x_encode_init,
    .close          = &ff_vpe_encode_close,
    .receive_packet = &ff_vpe_encode_receive_packet,
    .priv_class     = &vpe_encode_h264_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_h26x_hw_configs,
    .wrapper_name   = "vpe",
};

AVCodec ff_hevc_vpe_encoder = {
    .name           = "hevcenc_vpe",
    .long_name      = NULL_IF_CONFIG_SMALL("HEVC (VPE VC8000E)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .priv_data_size = sizeof(VpeEncCtx),
    .init           = &vpe_h26x_encode_init,
    .close          = &ff_vpe_encode_close,
    .receive_packet = &ff_vpe_encode_receive_packet,
    .priv_class     = &vpe_encode_hevc_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_h26x_hw_configs,
    .wrapper_name   = "vpe",
};
