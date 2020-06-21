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

#include <stdint.h>
#include <vpe/vpi_api.h>
#include <vpe/vpi_types.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/log.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"
#include "libavutil/buffer.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "hwconfig.h"

#define OFFSET(x) (offsetof(VpeEncVp9Ctx, x))
#define FLAGS \
    (AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_EXPORT)
#define DEFAULT       -255

typedef struct {
    int state;
    AVFrame *trans_pic;
} VpeEncVp9Pic;

/*
 * Private VPE VP9 encoder hardware structure
 */
typedef struct {
    const AVClass *av_class;
    /*The hardware device context*/
    AVBufferRef *hw_device;
    /*The hardware frame context containing the input frames*/
    AVBufferRef *hw_frame;
    /*Dictionary used to parse encoder parameters*/
    AVDictionary *dict;

    /*VPI main context*/
    VpiCtx ctx;
    /*VPE codec function pointer*/
    VpiApi *vpi;
    /*Input avframe queue*/
    VpeEncVp9Pic pic_wait_list[MAX_WAIT_DEPTH];
    /*Input avframe index*/
    int poc;

    /*Encoder config*/
    VpiEncVp9Opition vp9cfg;
    /*Set the encoding preset, superfast/fast/medium/slow/superslow*/
    char *preset;
    /*Encoder effort level*/
    int effort;
    /*Number of frames to lag. */
    int lag_in_frames;
    /*"Number of encoding passes*/
    int passes;
    /*More encoding parameters*/
    char *enc_params;
    /*Encoder flush state*/
    int flush_state;
} VpeEncVp9Ctx;

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

    /*Detail parameters described in
      https://github.com/VeriSilicon/VPE/blob/master/doc/enc_params_vp9.md */
    { "enc_params",
      "Override the enc configuration",
      OFFSET(enc_params),
      AV_OPT_TYPE_STRING,
      { 0 },
      0,
      0,
      FLAGS },
    { NULL },
};

static int vpe_vp9enc_init_hwctx(AVCodecContext *avctx)
{
    int ret = 0;
    AVHWFramesContext *hwframe_ctx;
    VpeEncVp9Ctx *ctx = avctx->priv_data;

    if (avctx->hw_frames_ctx) {
        ctx->hw_frame = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hw_frame) {
            ret = AVERROR(ENOMEM);
            goto error;
        }

        hwframe_ctx    = (AVHWFramesContext *)ctx->hw_frame->data;
        ctx->hw_device = av_buffer_ref(hwframe_ctx->device_ref);
        if (!ctx->hw_device) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    } else {
        if (avctx->hw_device_ctx) {
            ctx->hw_device = av_buffer_ref(avctx->hw_device_ctx);
            if (!ctx->hw_device) {
                ret = AVERROR(ENOMEM);
                goto error;
            }
        } else {
            ret = av_hwdevice_ctx_create(&ctx->hw_device, AV_HWDEVICE_TYPE_VPE,
                                         NULL, NULL, 0);
            if (ret < 0)
                goto error;
        }

        ctx->hw_frame = av_hwframe_ctx_alloc(ctx->hw_device);
        if (!ctx->hw_frame) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    }

    return ret;
error:
    return ret;
}

static void vpe_vp9enc_output_packet(VpiPacket *vpi_packet,
                                     AVPacket *out_packet)
{
    out_packet->size = vpi_packet->size;
    out_packet->pts  = vpi_packet->pts;
    out_packet->dts  = vpi_packet->pkt_dts;
}

static int vpe_vp9enc_input_frame(AVFrame *input_image,
                                  VpiFrame *out_frame)
{
    VpiFrame *in_vpi_frame;
    if (input_image) {
        in_vpi_frame           = (VpiFrame *)input_image->data[0];
        out_frame->width       = input_image->width;
        out_frame->height      = input_image->height;
        out_frame->linesize[0] = input_image->linesize[0];
        out_frame->linesize[1] = input_image->linesize[1];
        out_frame->linesize[2] = input_image->linesize[2];
        out_frame->key_frame   = input_image->key_frame;
        out_frame->pts         = input_image->pts;
        out_frame->pkt_dts     = input_image->pkt_dts;
        out_frame->data[0]     = in_vpi_frame->data[0];
        out_frame->data[1]     = in_vpi_frame->data[1];
        out_frame->data[2]     = in_vpi_frame->data[2];
        out_frame->opaque      = (void *)input_image;
    } else {
        memset(out_frame, 0, sizeof(VpiFrame));
    }

    return 0;
}

static void vpe_dump_pic(AVCodecContext *avctx, const char *str, const char *sep, VpeEncVp9Ctx *ctx)
{
    int i = 0;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if ((ctx->pic_wait_list[i].state == 0) &&
            (ctx->pic_wait_list[i].trans_pic == NULL))
            continue;

        av_log(avctx, AV_LOG_TRACE, "pic[%d] state=%d, data=%p", i,
               ctx->pic_wait_list[i].state,
               ctx->pic_wait_list[i].trans_pic);

        av_log(ctx, AV_LOG_TRACE, "\n");
    }
    av_log(avctx, AV_LOG_TRACE, "\n");
}

static int vpe_vp9enc_receive_pic(AVCodecContext *avctx, VpeEncVp9Ctx *ctx,
                                  const AVFrame *input_image)
{
    VpeEncVp9Pic *transpic = NULL;
    VpiFrame *vpi_frame    = NULL;
    VpiCtrlCmdParam cmd;
    int ret;
    int i;

    cmd.cmd  = VPI_CMD_VP9ENC_GET_EMPTY_FRAME_SLOT;
    cmd.data = NULL;
    ret = ctx->vpi->control(ctx->ctx,
                    (void*)&cmd, (void *)&vpi_frame);
    if (ret != 0 || vpi_frame == NULL) {
        return AVERROR_EXTERNAL;
    }

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (ctx->pic_wait_list[i].state == 0) {
            transpic = &ctx->pic_wait_list[i];
            break;
        }
    }

    if (i == MAX_WAIT_DEPTH) {
        return AVERROR_BUFFER_TOO_SMALL;
    }

    transpic->state = 1;
    if (input_image) {
        if (!transpic->trans_pic) {
            transpic->trans_pic = av_frame_alloc();
            if (!transpic->trans_pic)
                return AVERROR(ENOMEM);
        }
        av_frame_unref(transpic->trans_pic);
        av_frame_ref(transpic->trans_pic, input_image);
        vpe_vp9enc_input_frame(transpic->trans_pic, vpi_frame);
    } else {
        av_log(ctx, AV_LOG_DEBUG, "input image is empty, received EOF\n");
        vpe_vp9enc_input_frame(NULL, vpi_frame);
    }

    ctx->vpi->encode_put_frame(ctx->ctx, (void*)vpi_frame);

    vpe_dump_pic(avctx, "vpe_vp9enc_receive_pic", " <---", ctx);
    return 0;
}

static int vpe_vp9enc_consume_pic(AVCodecContext *avctx, VpeEncVp9Ctx *ctx,
                                  AVFrame *consume_frame)
{
    VpeEncVp9Pic *transpic = NULL;
    int i;

    vpe_dump_pic(avctx, "vpe_vp9enc_consume_pic", " --->", ctx);
    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (ctx->pic_wait_list[i].state == 1) {
            transpic = &ctx->pic_wait_list[i];
            if (transpic->trans_pic == consume_frame)
                goto find_pic;
        }
    }
    if (i == MAX_WAIT_DEPTH) {
        av_log(avctx, AV_LOG_ERROR,
               "avframe %p not matched\n", consume_frame);
        return AVERROR(EINVAL);
    }

find_pic:
    transpic->state = 0;

    av_frame_unref(transpic->trans_pic);
    transpic->trans_pic = NULL;
    return 0;
}

static int vpe_enc_vp9_create_param_list(AVCodecContext *avctx, char *enc_params,
                                      VpiEncVp9Opition *psetting)
{
    VpeEncVp9Ctx *ctx             = (VpeEncVp9Ctx *)avctx->priv_data;
    AVDictionaryEntry *dict_entry = NULL;
    VpiEncParamSet *tail        = NULL;
    VpiEncParamSet *node        = NULL;

    if (!av_dict_parse_string(&ctx->dict, enc_params, "=", ":", 0)) {
        while ((dict_entry = av_dict_get(ctx->dict, "", dict_entry,
                                         AV_DICT_IGNORE_SUFFIX))) {
            node = malloc(sizeof(VpiEncParamSet));
            if (!node)
                return AVERROR(ENOMEM);
            node->key   = dict_entry->key;
            node->value = dict_entry->value;
            node->next  = NULL;
            if (tail != NULL) {
                tail->next = node;
                tail       = node;
            } else {
                psetting->param_list = tail = node;
            }
        }
    }
    return 0;
}

static void vpe_enc_vp9_release_param_list(AVCodecContext *avctx)
{
    VpeEncVp9Ctx *ctx             = (VpeEncVp9Ctx *)avctx->priv_data;
    VpiEncParamSet *tail        = ctx->vp9cfg.param_list;
    VpiEncParamSet *node        = NULL;

    while (tail != NULL) {
        node = tail->next;
        free(tail);
        tail = node;
    }
    av_dict_free(&ctx->dict);
}

static av_cold int vpe_enc_vp9_free_frames(AVCodecContext *avctx)
{
    VpeEncVp9Ctx *ctx   = (VpeEncVp9Ctx *)avctx->priv_data;
    VpiCtrlCmdParam cmd;
    AVFrame *frame_ref = NULL;
    int ret = 0;

    do {
        cmd.cmd = VPI_CMD_VP9ENC_CONSUME_PIC;
        ret     = ctx->vpi->control(ctx->ctx, (void *)&cmd, (void *)&frame_ref);
        if (ret < 0)
            return AVERROR_EXTERNAL;

        if (frame_ref) {
            ret = vpe_vp9enc_consume_pic(avctx, ctx, frame_ref);
            if (ret != 0)
                return ret;
        } else {
            break;
        }
    } while(1);

    return 0;
}

static av_cold void vpe_enc_vp9_consume_flush(AVCodecContext *avctx)
{
    VpeEncVp9Ctx *ctx      = (VpeEncVp9Ctx *)avctx->priv_data;
    VpeEncVp9Pic *transpic = NULL;
    int i;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (ctx->pic_wait_list[i].state == 1) {
            transpic = &ctx->pic_wait_list[i];
            if (transpic->trans_pic) {
                av_frame_free(&transpic->trans_pic);
            }
            transpic->state = 0;
        }
    }
}


static av_cold int vpe_enc_vp9_close(AVCodecContext *avctx)
{
    VpeEncVp9Ctx *ctx = avctx->priv_data;

    vpe_enc_vp9_release_param_list(avctx);
    if (ctx->ctx) {
        ctx->vpi->close(ctx->ctx);
    }
    vpe_enc_vp9_consume_flush(avctx);
    av_buffer_unref(&ctx->hw_frame);
    av_buffer_unref(&ctx->hw_device);
    if (ctx->ctx) {
        if (vpi_destroy(ctx->ctx)) {
            return AVERROR_EXTERNAL;
        }
    }
    return 0;
}

static av_cold int vpe_enc_vp9_init(AVCodecContext *avctx)
{
    VpeEncVp9Ctx *ctx = avctx->priv_data;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpiEncVp9Opition *psetting = &ctx->vp9cfg;
    int ret                    = 0;

    ret = vpe_vp9enc_init_hwctx(avctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "vpe_encvp9_init_hwctx failure\n");
        return ret;
    }
    hwframe_ctx  = (AVHWFramesContext *)ctx->hw_frame->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;

    avctx->pix_fmt          = AV_PIX_FMT_YUV420P;
    psetting->preset        = ctx->preset;
    psetting->effort        = ctx->effort;
    psetting->lag_in_frames = ctx->lag_in_frames;
    psetting->passes        = ctx->passes;
    psetting->framectx      = vpeframe_ctx->frame;
    psetting->width         = avctx->width;
    psetting->height        = avctx->height;

    if ((avctx->bit_rate >= 10000) && (avctx->bit_rate <= 60000000)) {
        psetting->bit_rate = avctx->bit_rate;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid bit_rate=%d\n",
               psetting->bit_rate);
    }

    if ((avctx->bit_rate_tolerance >= 10000) &&
        (avctx->bit_rate_tolerance <= 60000000)) {
        psetting->bit_rate_tolerance = avctx->bit_rate_tolerance;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid bit_rate_tolerance=%d\n",
               avctx->bit_rate_tolerance);
    }

    if ((avctx->framerate.num > 0) && (avctx->framerate.num < 1048576)) {
        psetting->frame_rate_numer = avctx->framerate.num;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid framerate.num=%d\n",
               avctx->framerate.num);
    }
    if ((avctx->framerate.den > 0) && (avctx->framerate.den < 1048576)) {
        psetting->frame_rate_denom = avctx->framerate.den;
    } else {
        av_log(avctx, AV_LOG_WARNING, "invalid framerate.den=%d\n",
               avctx->framerate.den);
    }

    if (avctx->profile == FF_PROFILE_VP9_0 ||
        avctx->profile == FF_PROFILE_VP9_1) {
        psetting->force_8bit = 1;
    }

    if (avctx->profile == FF_PROFILE_VP9_2 ||
        avctx->profile == FF_PROFILE_VP9_3) {
        if (psetting->force_8bit == 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "error:In profiles 2 and 3, only > 8 bits is allowed.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    ret = vpi_create(&ctx->ctx, &ctx->vpi, VP9ENC_VPE);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "VP9 enc vpi_create failed\n");
        return AVERROR_EXTERNAL;
    }

    ret = vpe_enc_vp9_create_param_list(avctx, ctx->enc_params, psetting);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "vpe_enc_vp9_create_param_list failed\n");
        return ret;
    }

    ret = ctx->vpi->init(ctx->ctx, psetting);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "VP9 enc init failed\n");
        return AVERROR_EXTERNAL;
    }

    ctx->flush_state = 0;
    memset(ctx->pic_wait_list, 0, sizeof(ctx->pic_wait_list));
    return 0;
}

static int vpe_enc_vp9_send_frame(AVCodecContext *avctx,
                                  const AVFrame *input_frame)
{
    VpeEncVp9Ctx *ctx = (VpeEncVp9Ctx *)avctx->priv_data;
    int ret           = 0;

    /* Store input pictures */
    ret = vpe_vp9enc_receive_pic(avctx, ctx, input_frame);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

static int vpe_enc_vp9_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    VpeEncVp9Ctx *ctx = (VpeEncVp9Ctx *)avctx->priv_data;
    VpiPacket vpi_packet;
    VpiCtrlCmdParam cmd;
    int stream_size = 0;
    int ret = 0;

    ret = vpe_enc_vp9_free_frames(avctx);
    if (ret != 0) {
        return ret;
    }

    cmd.cmd = VPI_CMD_VP9ENC_GET_FRAME_PACKET;
    ret = ctx->vpi->control(ctx->ctx, &cmd, (void *)&stream_size);
    if (ret == -1) {
        return AVERROR(EAGAIN);
    } else if (ret == 1) {
        return AVERROR_EOF;
    }

    /*Allocate AVPacket bufffer*/
    ret = av_new_packet(avpkt, stream_size);
    if (ret != 0)
       return ret;

    vpi_packet.data = avpkt->data;
    ret = ctx->vpi->encode_get_packet(ctx->ctx, (void *)&vpi_packet);
    if (ret == 0) {
        /*Convert output packet from VpiPacket to AVPacket*/
        vpe_vp9enc_output_packet(&vpi_packet, avpkt);
    } else {
        av_log(avctx, AV_LOG_ERROR, "VP9 enc encode failed\n");
        ret = AVERROR_EXTERNAL;
    }

    return ret;

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
    .priv_data_size = sizeof(VpeEncVp9Ctx),
    .init           = &vpe_enc_vp9_init,
    .send_frame     = &vpe_enc_vp9_send_frame,
    .receive_packet = &vpe_enc_vp9_receive_packet,
    .close          = &vpe_enc_vp9_close,
    .priv_class     = &vpe_enc_vp9_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_vp9_hw_configs,
    .wrapper_name = "vpe",
};