/*
 * Verisilicon VPE VP9 Decoder
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

#include "hwconfig.h"
#include "internal.h"
#include "libavutil/pixfmt.h"

#include "vpe_dec_common.h"

static av_cold int vpe_vp9_decode_init(AVCodecContext *avctx)
{
    return ff_vpe_decode_init(avctx, VP9DEC_VPE);
}

static const AVClass vpe_vp9_decode_class = {
    .class_name = "vp9d_vpe",
    .item_name  = av_default_item_name,
    .option     = vpe_decode_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecHWConfigInternal *vpe_hw_configs[] =
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

AVCodec ff_vp9_vpe_decoder = {
    .name           = "vp9_vpe",
    .long_name      = NULL_IF_CONFIG_SMALL("VP9 (VPE VC8000D)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VpeDecCtx),
    .init           = &vpe_vp9_decode_init,
    .receive_frame  = &ff_vpe_decode_receive_frame,
    .close          = &ff_vpe_decode_close,
    .priv_class     = &vpe_vp9_decode_class,
    .capabilities =
        AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_NONE },
    .hw_configs     = vpe_hw_configs,
};
