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
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <vpe/vpi_api.h>
#include <vpe/vpi_types.h>
#include "internal.h"
#include "libavutil/opt.h"
#include "libavcodec/internal.h"
#include "libavutil/hwcontext_vpe.h"
#include "hwconfig.h"

typedef struct VpeH26xEncFrm {
    /*The state of used or not*/
    int state;

    /*The pointer for input AVFrame*/
    AVFrame *frame;
} VpeH26xEncFrm;

typedef struct VpeH26xEncCtx {
    AVClass *class;
    /*The hardware frame context containing the input frames*/
    AVBufferRef *hwframe;

    /*Dictionary used to parse encoder parameters*/
    AVDictionary *dict;

    /*The name of the device*/
    char *dev_name;

    /*The name of h264 or h265 module*/
    char module_name[20];

    /*VPI context*/
    VpiCtx ctx;

    /*The pointer of the VPE API*/
    VpiApi *api;

    /*VPE h26x encoder configure*/
    VpiH26xEncCfg h26x_enc_cfg;

    /*The queue for the input AVFrames*/
    VpeH26xEncFrm pic_wait_list[MAX_WAIT_DEPTH];

    /*Params passed from ffmpeg */
    int crf; /*VCE Constant rate factor mode*/
    int force_idr; /*Force IDR*/
    char *preset; /*Set the encoding preset*/
    char *profile; /*Set the profile of the encoding*/
    char *level; /*Set the level of the encoding*/
    char *enc_params; /*The encoding parameters*/
} VpeH26xEncCtx;

static int vpe_h26x_encode_create_param_list(AVCodecContext *avctx,
                                             char *enc_params,
                                             VpiH26xEncCfg *psetting)
{
    VpeH26xEncCtx *ctx            = (VpeH26xEncCtx *)avctx->priv_data;
    AVDictionaryEntry *dict_entry = NULL;
    VpiEncParamSet *tail          = NULL;
    VpiEncParamSet *node          = NULL;

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

static void vpe_h26x_encode_release_param_list(AVCodecContext *avctx)
{
    VpeH26xEncCtx *ctx   = (VpeH26xEncCtx *)avctx->priv_data;
    VpiEncParamSet *tail = ctx->h26x_enc_cfg.param_list;
    VpiEncParamSet *node = NULL;

    while (tail != NULL) {
        node = tail->next;
        free(tail);
        tail = node;
    }
    av_dict_free(&ctx->dict);
}

static av_cold int vpe_h26x_encode_init(AVCodecContext *avctx)
{
    int ret = 0;
    VpiFrame *frame_hwctx;
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    VpeH26xEncCtx *enc_ctx = (VpeH26xEncCtx *)avctx->priv_data;

    /*Create context and get the APIs for h26x encoder from VPI layer */
    ret = vpi_create(&enc_ctx->ctx, &enc_ctx->api, H26XENC_VPE);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "h26x_enc vpe create failure %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    memset(&enc_ctx->h26x_enc_cfg, 0, sizeof(VpiH26xEncCfg));

    /*Get HW frame. The avctx->hw_frames_ctx is the reference to the
      AVHWFramesContext describing the input frame for h26x encoder*/
    if (avctx->hw_frames_ctx) {
        enc_ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!enc_ctx->hwframe) {
            return AVERROR(ENOMEM);
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    hwframe_ctx  = (AVHWFramesContext *)enc_ctx->hwframe->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx  = vpeframe_ctx->frame;

    if (avctx->codec->id == AV_CODEC_ID_HEVC) {
        strcpy(enc_ctx->h26x_enc_cfg.module_name, "HEVCENC");
    } else if (avctx->codec->id == AV_CODEC_ID_H264) {
        strcpy(enc_ctx->h26x_enc_cfg.module_name, "H264ENC");
    }

    /*Initialize the VPE h26x encoder configuration*/
    enc_ctx->h26x_enc_cfg.crf    = enc_ctx->crf;
    enc_ctx->h26x_enc_cfg.preset = enc_ctx->preset;
    if (avctx->codec->id == AV_CODEC_ID_HEVC) {
        enc_ctx->h26x_enc_cfg.codec_id = CODEC_ID_HEVC;
    } else if (avctx->codec->id == AV_CODEC_ID_H264) {
        enc_ctx->h26x_enc_cfg.codec_id = CODEC_ID_H264;
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "%s, avctx->codec->id isn't HEVC or H264 \n", __FUNCTION__);
        return AVERROR(EINVAL);
    }
    enc_ctx->h26x_enc_cfg.codec_name = avctx->codec->name;
    enc_ctx->h26x_enc_cfg.profile    = enc_ctx->profile;
    enc_ctx->h26x_enc_cfg.level      = enc_ctx->level;
    enc_ctx->h26x_enc_cfg.force_idr  = enc_ctx->force_idr;

    enc_ctx->h26x_enc_cfg.bit_per_second = avctx->bit_rate;
    /* Input frame rate numerator*/
    enc_ctx->h26x_enc_cfg.input_rate_numer = avctx->framerate.num;
    /* Input frame rate denominator*/
    enc_ctx->h26x_enc_cfg.input_rate_denom = avctx->framerate.den;
    enc_ctx->h26x_enc_cfg.lum_width_src    = avctx->width;
    enc_ctx->h26x_enc_cfg.lum_height_src   = avctx->height;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        enc_ctx->h26x_enc_cfg.input_format = VPI_YUV420_PLANAR;
        break;
    case AV_PIX_FMT_NV12:
        enc_ctx->h26x_enc_cfg.input_format = VPI_YUV420_SEMIPLANAR;
        break;
    case AV_PIX_FMT_NV21:
        enc_ctx->h26x_enc_cfg.input_format = VPI_YUV420_SEMIPLANAR_VU;
        break;
    case AV_PIX_FMT_YUV420P10LE:
        enc_ctx->h26x_enc_cfg.input_format = VPI_YUV420_PLANAR_10BIT_P010;
        break;
    default:
        enc_ctx->h26x_enc_cfg.input_format = VPI_YUV420_PLANAR;
        break;
    }
    enc_ctx->h26x_enc_cfg.frame_ctx = frame_hwctx;

    ret = vpe_h26x_encode_create_param_list(avctx, enc_ctx->enc_params,
                                            &enc_ctx->h26x_enc_cfg);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR,
               "vpe_h26x_encode_create_param_list failed\n");
        return ret;
    }

    /*Call the VPE h26x encoder initialization function*/
    ret = enc_ctx->api->init(enc_ctx->ctx, &enc_ctx->h26x_enc_cfg);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "vpe_h264x_encode_init failure\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int vpe_h26xenc_input_frame(AVFrame *input_image,
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

static int vpe_h26x_encode_send_frame(AVCodecContext *avctx,
                                      const AVFrame *input_frame)
{
    int i                   = 0;
    int ret                 = 0;
    VpeH26xEncFrm *transpic = NULL;
    VpeH26xEncCtx *enc_ctx  = (VpeH26xEncCtx *)avctx->priv_data;
    VpiFrame *vpi_frame     = NULL;
    VpiCtrlCmdParam cmd;

    cmd.cmd  = VPI_CMD_H26xENC_GET_EMPTY_FRAME_SLOT;
    cmd.data = NULL;
    ret = enc_ctx->api->control(enc_ctx->ctx,
                    (void*)&cmd, (void *)&vpi_frame);
    if (ret != 0 || vpi_frame == NULL) {
        return AVERROR_EXTERNAL;
    }

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (enc_ctx->pic_wait_list[i].state == 0) {
            transpic = &enc_ctx->pic_wait_list[i];
            break;
        }
    }

    if (i == MAX_WAIT_DEPTH) {
        return AVERROR_BUFFER_TOO_SMALL;
    }
    transpic->state = 1;
    if (input_frame) {
        if (!transpic->frame) {
            transpic->frame = av_frame_alloc();
            if (!transpic->frame)
                return AVERROR(ENOMEM);
        }
        av_frame_unref(transpic->frame);
        av_frame_ref(transpic->frame, input_frame);
        vpe_h26xenc_input_frame(transpic->frame, vpi_frame);
    } else {
        av_log(enc_ctx, AV_LOG_DEBUG, "input image is empty, received EOF\n");
        vpe_h26xenc_input_frame(NULL, vpi_frame);
    }

    enc_ctx->api->encode_put_frame(enc_ctx->ctx, (void*)vpi_frame);
    return 0;
}

static int vpe_h26xenc_consume_pic(AVCodecContext *avctx,
                                       VpeH26xEncCtx *ctx,
                                       AVFrame *consume_frame)
{
    VpeH26xEncFrm *transpic = NULL;
    int i;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (ctx->pic_wait_list[i].state == 1) {
            transpic = &ctx->pic_wait_list[i];
            if (transpic->frame == consume_frame)
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

    av_frame_unref(transpic->frame);
    transpic->frame = NULL;
    return 0;
}

static av_cold int vpe_h26xenc_free_frames(AVCodecContext *avctx)
{
    VpeH26xEncCtx *ctx   = (VpeH26xEncCtx *)avctx->priv_data;
    VpiCtrlCmdParam cmd;
    AVFrame *frame_ref = NULL;
    int ret = 0;

    do {
        cmd.cmd = VPI_CMD_H26xENC_CONSUME_PIC;
        ret     = ctx->api->control(ctx->ctx, (void *)&cmd,
                                    (void *)&frame_ref);
        if (ret < 0)
            return AVERROR_EXTERNAL;

        if (frame_ref) {
            ret = vpe_h26xenc_consume_pic(avctx, ctx, frame_ref);
            if (ret != 0)
                return ret;
        } else {
            break;
        }
    } while(1);

    return 0;
}

static void vpe_h26xenc_output_packet(VpiPacket *vpi_packet,
                                      AVPacket *out_packet)
{
    out_packet->size = vpi_packet->size;
    out_packet->pts  = vpi_packet->pts;
    out_packet->dts  = vpi_packet->pkt_dts;
}

static int vpe_h26x_encode_receive_packet(AVCodecContext *avctx,
                                          AVPacket *avpkt)
{
    VpeH26xEncCtx *ctx = (VpeH26xEncCtx *)avctx->priv_data;
    VpiPacket vpi_packet;
    VpiCtrlCmdParam cmd;
    int stream_size = 0;
    int ret = 0;

    ret = vpe_h26xenc_free_frames(avctx);
    if (ret != 0) {
        return ret;
    }

    cmd.cmd = VPI_CMD_H26xENC_GET_FRAME_PACKET;
    ret = ctx->api->control(ctx->ctx, &cmd, (void *)&stream_size);
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
    vpi_packet.size = stream_size;
    ret = ctx->api->encode_get_packet(ctx->ctx, (void *)&vpi_packet);
    if (ret == 0) {
        /*Convert output packet from VpiPacket to AVPacket*/
        vpe_h26xenc_output_packet(&vpi_packet, avpkt);
    } else {
        av_log(avctx, AV_LOG_ERROR, "H26x enc encode failed\n");
        ret = AVERROR_EXTERNAL;
    }

    return ret;

}

static av_cold void vpe_h26x_enc_consume_flush(AVCodecContext *avctx)
{
    VpeH26xEncCtx *ctx      = (VpeH26xEncCtx *)avctx->priv_data;
    VpeH26xEncFrm *transpic = NULL;
    int i;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (ctx->pic_wait_list[i].state == 1) {
            transpic = &ctx->pic_wait_list[i];
            if (transpic->frame) {
                av_frame_free(&transpic->frame);
            }
            transpic->state = 0;
        }
    }
}

static av_cold int vpe_h26x_encode_close(AVCodecContext *avctx)
{
    VpeH26xEncCtx *enc_ctx = (VpeH26xEncCtx *)avctx->priv_data;
    int ret                = 0;

    vpe_h26x_encode_release_param_list(avctx);

    if (enc_ctx->ctx) {
        enc_ctx->api->close(enc_ctx->ctx);
    }
    vpe_h26x_enc_consume_flush(avctx);
    av_buffer_unref(&enc_ctx->hwframe);
    if (enc_ctx->ctx) {
        ret = vpi_destroy(enc_ctx->ctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "h26x encoder vpi_destroy failure\n");
        }
    }

    return ret;
}

#define OFFSETOPT(x) offsetof(VpeH26xEncCtx, x)
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
    { "force-idr",
      "If forcing keyframes, force them as IDR frames.",
      OFFSETOPT(force_idr),
      AV_OPT_TYPE_INT,
      { .i64 = 0 },
      0,
      1,
      .flags = FLAGS },
    { "enc_params",
      "Override the VPE h264/hevc configuration using a :-separated list of "
      "key=value parameters. For more details, please refer to the document at"
      "https://github.com/VeriSilicon/VPE/blob/master/doc/enc_params_h26x.md",
      OFFSETOPT(enc_params),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      .flags = FLAGS },
    { NULL },
};

static const AVCodecDefault vpe_h264_encode_defaults[] = {
    { NULL },
};

static const AVCodecDefault vpe_hevc_encode_defaults[] = {
    { NULL },
};

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
    .priv_data_size = sizeof(VpeH26xEncCtx),
    .init           = &vpe_h26x_encode_init,
    .close          = &vpe_h26x_encode_close,
    .send_frame     = &vpe_h26x_encode_send_frame,
    .receive_packet = &vpe_h26x_encode_receive_packet,
    .priv_class     = &vpe_encode_h264_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vpe_h264_encode_defaults,
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
    .priv_data_size = sizeof(VpeH26xEncCtx),
    .init           = &vpe_h26x_encode_init,
    .close          = &vpe_h26x_encode_close,
    .send_frame     = &vpe_h26x_encode_send_frame,
    .receive_packet = &vpe_h26x_encode_receive_packet,
    .priv_class     = &vpe_encode_hevc_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vpe_hevc_encode_defaults,
    .pix_fmts       =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_h26x_hw_configs,
    .wrapper_name   = "vpe",
};
