/*
 * Verisilicon VPE VP9 Encoder
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

#define OFFSET(x) (offsetof(VpeEncCtx, x))
#define FLAGS \
    (AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_EXPORT)
#define DEFAULT       -255

const static AVOption vpe_enc_vp9_options[] = {
    { "preset",
      "Set the encoding preset, superfast/fast/medium/slow/superslow",
      OFFSET(preset),
      AV_OPT_TYPE_STRING,
      { .str = "fast" },
      0,
      0,
      FLAGS },
    { "effort",
      "Encoder effort level, 0=fastest, 5=best quality",
      OFFSET(effort),
      AV_OPT_TYPE_INT,
      { .i64 = DEFAULT },
      DEFAULT,
      5,
      FLAGS },
    { "lag_in_frames",
      "Number of frames to lag. Up to 25. [0]",
      OFFSET(lag_in_frames),
      AV_OPT_TYPE_INT,
      { .i64 = DEFAULT },
      DEFAULT,
      25,
      FLAGS },
    { "passes",
      "Number of passes (1/2). [1]",
      OFFSET(passes),
      AV_OPT_TYPE_INT,
      { .i64 = DEFAULT },
      DEFAULT,
      2,
      FLAGS },

    /* Detail parameters is described in
      https://github.com/VeriSilicon/ffmpeg#VP9-Encoder*/
    { "enc_params",
      "Override the VP9 encoder configuration",
      OFFSET(enc_params),
      AV_OPT_TYPE_STRING,
      { 0 },
      0,
      0,
      FLAGS },
    { NULL },
};

static av_cold int vpe_vp9_encode_init(AVCodecContext *avctx)
{
    VpeEncCtx *enc_ctx = avctx->priv_data;
    VpiVp9EncCfg *vp9_enc_cfg;
    VpiCtrlCmdParam cmd;
    int ret;

    ret = ff_vpe_encode_init(avctx, VP9ENC_VPE);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_vpe_encode_init VP9 failed\n");
        return ret;
    }

    // get the encoder cfg struct
    cmd.cmd = VPI_CMD_ENC_INIT_OPTION;
    ret = enc_ctx->vpi->control(enc_ctx->ctx, (void *)&cmd,
                                (void *)&enc_ctx->enc_cfg);
    if (ret != 0) {
        return AVERROR(ENOMEM);
    }
    vp9_enc_cfg = (VpiVp9EncCfg *)enc_ctx->enc_cfg;

    vp9_enc_cfg->preset        = enc_ctx->preset;
    vp9_enc_cfg->effort        = enc_ctx->effort;
    vp9_enc_cfg->lag_in_frames = enc_ctx->lag_in_frames;
    vp9_enc_cfg->passes        = enc_ctx->passes;
    vp9_enc_cfg->width         = avctx->width;
    vp9_enc_cfg->height        = avctx->height;

    if ((avctx->bit_rate >= 10000) && (avctx->bit_rate <= 60000000)) {
        vp9_enc_cfg->bit_rate = avctx->bit_rate;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid bit_rate=%d\n",
               vp9_enc_cfg->bit_rate);
    }

    if ((avctx->bit_rate_tolerance >= 10000) &&
        (avctx->bit_rate_tolerance <= 60000000)) {
        vp9_enc_cfg->bit_rate_tolerance = avctx->bit_rate_tolerance;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid bit_rate_tolerance=%d\n",
               avctx->bit_rate_tolerance);
    }

    if ((avctx->framerate.num > 0) && (avctx->framerate.num < 1048576)) {
        vp9_enc_cfg->frame_rate_numer = avctx->framerate.num;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid framerate.num=%d\n",
               avctx->framerate.num);
    }
    if ((avctx->framerate.den > 0) && (avctx->framerate.den < 1048576)) {
        vp9_enc_cfg->frame_rate_denom = avctx->framerate.den;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid framerate.den=%d\n",
               avctx->framerate.den);
    }

    if (avctx->profile == FF_PROFILE_VP9_0 ||
        avctx->profile == FF_PROFILE_VP9_1) {
        vp9_enc_cfg->force_8bit = 1;
    }

    if (avctx->profile == FF_PROFILE_VP9_2 ||
        avctx->profile == FF_PROFILE_VP9_3) {
        if (vp9_enc_cfg->force_8bit == 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "error:In profiles 2 and 3, only > 8 bits is allowed.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    ret = enc_ctx->vpi->init(enc_ctx->ctx, vp9_enc_cfg);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "VP9 enc init failed, error=%s(%d)\n",
               vpi_error_str(ret), ret);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static const AVClass vpe_enc_vp9_class = {
    .class_name = "vp9enc_vpe",
    .item_name  = av_default_item_name,
    .option     = vpe_enc_vp9_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecHWConfigInternal *vpe_vp9_hw_configs[] =
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

AVCodec ff_vp9_vpe_encoder = {
    .name           = "vp9enc_vpe",
    .long_name      = NULL_IF_CONFIG_SMALL("VP9 (VPE BIGSEA)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VpeEncCtx),
    .init           = &vpe_vp9_encode_init,
    .receive_packet = &ff_vpe_encode_receive_packet,
    .close          = &ff_vpe_encode_close,
    .priv_class     = &vpe_enc_vp9_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_vp9_hw_configs,
    .wrapper_name = "vpe",
};
