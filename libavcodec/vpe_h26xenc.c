/*
 * Verisilicon VPE H264/HEVC Encoder
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
#include "libavutil/opt.h"
#include "libavcodec/internal.h"
#include "libavutil/hwcontext_vpe.h"
#include "hwconfig.h"

#define MAX_WAIT_DEPTH 78

typedef struct VpeH26xEncFrm {
    /*The state of used or not*/
    int state;

    /*In pass one queue or not*/
    int in_pass_one_queue;

    /*The index of the frame*/
    int frame_index;

    /*The pointer for input AVFrame*/
    AVFrame *frame;
} VpeH26xEncFrm;

typedef struct VpeH26xEncCtx {
    AVClass *class;
    /*The hardware frame context containing the input frames*/
    AVBufferRef *hwframe;

    /*The hardware device context*/
    AVBufferRef *hwdevice;

    /*The name of the device*/
    char *dev_name;

    /*The name of h264 or h265 module*/
    char module_name[20];

    /*VPI context*/
    VpiCtx ctx;

    /*The pointer of the VPE API*/
    VpiApi *api;

    /*VPE h26x encoder configure*/
    H26xEncCfg h26x_enc_cfg;

    /*The queue for the input AVFrames*/
    VpeH26xEncFrm frame_queue_for_enc[MAX_WAIT_DEPTH];

    /*No input frame*/
    int no_input_frm;

    /*The index of the frame*/
    int frame_index;

    /*Params passed from ffmpeg */
    int crf; /*VCE Constant rate factor mode*/
    char *preset; /*Set the encoding preset*/
    char *profile; /*Set the profile of the encoding*/
    char *level; /*Set the level of the encoding*/
    char *enc_params; /*The encoding parameters*/
} VpeH26xEncCtx;

static av_cold int vpe_h26x_encode_close(AVCodecContext *avctx);

/**
 * Output the ES data in VpiPacket to AVPacket
 */
static int vpe_h26xe_output_es_to_avpacket(AVPacket *av_packet,
                                           VpiPacket *vpi_packet)
{
    int ret = 0;
    if (av_new_packet(av_packet, vpi_packet->size))
        return AVERROR(ENOMEM);

    memcpy(av_packet->data, (uint8_t *)vpi_packet->data, vpi_packet->size);
    av_packet->pts = vpi_packet->pts;
    av_packet->dts = vpi_packet->pkt_dts;

    return ret;
}

/**
 * Output the ES data in VpiPacket to AVPacket and control the encoder to
 * update the statistic
 */
static int vpe_h26xe_output_es_update_statis(AVCodecContext *avctx,
                                             AVPacket *av_packet,
                                             VpiPacket *vpi_packet)
{
    int ret                = 0;
    VpeH26xEncCtx *enc_ctx = NULL;
    int stream_size        = vpi_packet->size;
    VpiCtrlCmdParam cmd;

    if ((ret = vpe_h26xe_output_es_to_avpacket(av_packet, vpi_packet)) < 0) {
        return ret;
    }

    enc_ctx  = (VpeH26xEncCtx *)avctx->priv_data;
    cmd.cmd  = VPI_CMD_H26xENC_UPDATE_STATISTIC;
    cmd.data = &stream_size;
    ret      = enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, NULL);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "H26x_enc control STATISTIC failed\n");
        return AVERROR_EXTERNAL;
    }

    return ret;
}

/**
 * Find the needed frame in frame_queue_for_enc[MAX_WAIT_DEPTH] and pass the
 * frame info includes address to VpiFrame
 *
 * @param enc_ctx VpeH26xEnc context
 * @param vpi_frame frame info for VPE encoding
 * @return 0 on success, negative error code AVERROR_xxx on failure
 */
static int vpe_h26xe_find_frame_for_enc(VpeH26xEncCtx *enc_ctx,
                                        VpiFrame *vpi_frame)
{
    VpeH26xEncFrm *frame_queued = NULL;
    VpiFlushState flush_state;
    int frame_index_need = 0;
    int i                = 0;
    int ret              = 0;
    VpiCtrlCmdParam cmd;
    VpiFrame *in_vpi_frame;

    cmd.cmd = VPI_CMD_H26xENC_GET_NEXT_PIC;
    ret = enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, &frame_index_need);
    if (ret != 0) {
        av_log(enc_ctx, AV_LOG_ERROR, "H26x_enc control NEXT_PIC failed\n");
        return AVERROR_EXTERNAL;
    }

    cmd.cmd = VPI_CMD_H26xENC_GET_FLUSHSTATE;
    ret     = enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, &flush_state);
    if (ret != 0) {
        av_log(enc_ctx, AV_LOG_ERROR, "H26x_enc control get STATE failed\n");
        return AVERROR_EXTERNAL;
    }
    /*Find the frame according to frame index */
    if (flush_state == VPIH26X_FLUSH_IDLE) { /*Normal encoding stage*/
        for (i = 0; i < MAX_WAIT_DEPTH; i++) {
            if (enc_ctx->frame_queue_for_enc[i].state == 1) {
                frame_queued = &enc_ctx->frame_queue_for_enc[i];
                if (frame_queued->frame_index == frame_index_need) {
                    frame_queued->in_pass_one_queue = 1;
                    goto find_pic;
                }
            }
        }
        if (i == MAX_WAIT_DEPTH) {
            if (!enc_ctx->no_input_frm) {
                /*No frame available, request new frames by returning EAGAIN.*/
                return AVERROR(EAGAIN);
            }
            /*No frame availbale and no frame input, return 0 for encoder to
              start internal flushing operation*/
            return 0;
        }
    } else if (flush_state == VPIH26X_FLUSH_TRANSPIC) { /*flush_transpic stage*/
        /*Find the frame in the queue at flush_transpic stage
          There is no frame inputs at this stage*/
        for (i = 0; i < MAX_WAIT_DEPTH; i++) {
            if ((enc_ctx->frame_queue_for_enc[i].state == 1) &&
                (enc_ctx->frame_queue_for_enc[i].in_pass_one_queue != 1)) {
                frame_queued = &enc_ctx->frame_queue_for_enc[i];
                if (frame_queued->frame_index == frame_index_need) {
                    frame_queued->in_pass_one_queue = 1;
                    goto find_pic;
                }
            }
        }

        if (i == MAX_WAIT_DEPTH) {
            /*No frame available for this stage, return 0 for encoder to
              continue internal flushing operation*/
            return 0;
        }
    } else { /*Internal flushing stage*/
        /*Return 0 for encoder's internal flushing operation*/
        return 0;
    }

find_pic:
    cmd.cmd = VPI_CMD_H26xENC_SET_FINDPIC;
    ret     = enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, NULL);
    if (ret != 0) {
        av_log(enc_ctx, AV_LOG_ERROR, "H26x_enc control PPIDX failed\n");
        return AVERROR_EXTERNAL;
    }
    /*Fill vpi_frame with the frame info*/
    in_vpi_frame           = (VpiFrame *)frame_queued->frame->data[0];
    vpi_frame->width       = frame_queued->frame->width;
    vpi_frame->height      = frame_queued->frame->height;
    vpi_frame->linesize[0] = frame_queued->frame->linesize[0];
    vpi_frame->linesize[1] = frame_queued->frame->linesize[1];
    vpi_frame->linesize[2] = frame_queued->frame->linesize[2];
    vpi_frame->key_frame   = frame_queued->frame->key_frame;
    vpi_frame->pts         = frame_queued->frame->pts;
    vpi_frame->pkt_dts     = frame_queued->frame->pkt_dts;
    vpi_frame->data[0]     = in_vpi_frame->data[0];
    vpi_frame->data[1]     = in_vpi_frame->data[1];
    vpi_frame->data[2]     = in_vpi_frame->data[2];
    return 0;
}

/**
 * av_frame_free the AVFrame in the AVFrame input queue
 */
static void vpe_h26xe_av_frame_free(AVCodecContext *avctx)
{
    VpeH26xEncCtx *enc_ctx      = (VpeH26xEncCtx *)avctx->priv_data;
    VpeH26xEncFrm *frame_queued = NULL;
    int i                       = 0;
    VpiFrame *cur_frame;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (enc_ctx->frame_queue_for_enc[i].state == 1) {
            frame_queued = &enc_ctx->frame_queue_for_enc[i];
            if (frame_queued->frame) {
                cur_frame = (VpiFrame *)frame_queued->frame->data[0];
                av_frame_free(&frame_queued->frame);
                cur_frame->used_cnt--;
                if (cur_frame->used_cnt == 0) {
                    cur_frame->locked = 0;
                }
            }
            frame_queued->state = 0;
        }
    }
}

/**
 * av_frame_unref the consumed AVFrame in the AVFrame input queue
 */
static void vpe_h26xe_av_frame_unref(AVCodecContext *avctx,
                                     int consumed_frame_index)
{
    VpeH26xEncCtx *enc_ctx      = (VpeH26xEncCtx *)avctx->priv_data;
    VpeH26xEncFrm *frame_queued = NULL;
    int i;
    VpiFrame *cur_frame;

    /*find the need_frame_index */
    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if ((enc_ctx->frame_queue_for_enc[i].state == 1) &&
            (enc_ctx->frame_queue_for_enc[i].in_pass_one_queue == 1)) {
            frame_queued = &enc_ctx->frame_queue_for_enc[i];
            if (frame_queued->frame_index == consumed_frame_index) {
                goto find_pic;
            }
        }
    }

    if (i == MAX_WAIT_DEPTH)
        return;

find_pic:
    frame_queued->frame_index       = -1;
    frame_queued->state             = 0;
    frame_queued->in_pass_one_queue = 0;
    cur_frame                       = (VpiFrame *)frame_queued->frame->data[0];
    cur_frame->used_cnt--;
    if (cur_frame->used_cnt == 0) {
        cur_frame->locked = 0;
    }
    av_frame_unref(frame_queued->frame);
}

/**
 * Output the encoded data to AVPacket
 * The encoded data in VpePaket is outputed to AVPacket, unref/free the
 * consumed AVFrame buffer in the queue, output non-empty AVPacket info
 * according to the return value of the VPE encoding function.
 *
 * @param avctx AVCodec context
 * @param av_packet output AVPacket
 * @param vpi_packet contain the encoded data from VPE encoder
 * @param enc_ret the return value of VPE encoding function
 * @return 0 on success, negative error code AVERROR_xxx on failure
 */
static int vpe_h26xe_output_avpacket(AVCodecContext *avctx, AVPacket *av_packet,
                                     VpiPacket *vpi_packet, int enc_ret)
{
    int ret = 0;

    switch (enc_ret) {
    /*output AVPacket at encoder start stage according to enc_ret*/
    case VPI_ENC_START_OK: /*OK returned at encoder's start stage*/
        if (vpe_h26xe_output_es_to_avpacket(av_packet, vpi_packet) < 0) {
            vpe_h26xe_av_frame_free(avctx);
            return AVERROR_INVALIDDATA;
        }
        break;
    case VPI_ENC_START_ERROR: /*ERROR returned at encoder's start stage*/
        vpe_h26xe_av_frame_free(avctx);
        av_log(avctx, AV_LOG_ERROR, "H26x enocoding start fails. ret = %d\n",
               enc_ret);
        ret = AVERROR_INVALIDDATA;
        break;

    /*output AVPacket at encoder encoding stage according to enc_ret*/
    case VPI_ENC_ENC_OK: /*OK returned at encoder's encoding stage*/
        vpe_h26xe_av_frame_unref(avctx, vpi_packet->index_encoded);
        ret = AVERROR(EAGAIN);
        break;
    case VPI_ENC_ENC_FRM_ENQUEUE: /*FRM_ENQUEUE returned at encoding stage*/
        ret = AVERROR(EAGAIN);
        break;
    case VPI_ENC_ENC_READY: /*READY returned at encoder's encoding stage*/
        if (vpe_h26xe_output_es_update_statis(avctx, av_packet, vpi_packet) <
            0) {
            ret = AVERROR_INVALIDDATA;
            av_log(avctx, AV_LOG_ERROR, "h26xe fails at VPI_ENC_ENC_READY\n");
        }
        vpe_h26xe_av_frame_unref(avctx, vpi_packet->index_encoded);
        break;
    case VPI_ENC_ENC_ERROR: /*ERROR returned at encoder's encoding stage*/
        vpe_h26xe_av_frame_free(avctx);
        ret = AVERROR_INVALIDDATA;
        break;

    /*output AVPacket at encoder flushing stage according to enc_ret*/
    case VPI_ENC_FLUSH_IDLE_READY: /*READY returned at FLUSH_IDLE stage*/
    case VPI_ENC_FLUSH_PREPARE: /*READY at FLUSH_PREPARE stage*/
    case VPI_ENC_FLUSH_TRANSPIC_READY: /*READY at FLUSH_TRANSPIC stage*/
    case VPI_ENC_FLUSH_ENCDATA_READY: /*READY at FLUSH_ENCDATA stage*/
        if (vpe_h26xe_output_es_update_statis(avctx, av_packet, vpi_packet) <
            0) {
            ret = AVERROR_INVALIDDATA;
            av_log(avctx, AV_LOG_ERROR, "h26xe fails at FLUSH READY\n");
        }
        vpe_h26xe_av_frame_unref(avctx, vpi_packet->index_encoded);
        break;
    case VPI_ENC_FLUSH_FINISH_OK: /*OK returned at FLUSH_FINISH stage*/
        if (vpe_h26xe_output_es_to_avpacket(av_packet, vpi_packet) < 0) {
            ret = AVERROR_INVALIDDATA;
            av_log(avctx, AV_LOG_ERROR,
                   "h26xe fails at VPI_ENC_FLUSH_FINISH_OK\n");
        }
        vpe_h26xe_av_frame_free(avctx);
        break;

    case VPI_ENC_FLUSH_IDLE_ERROR: /*ERROR returned at FLUSH_IDLE stage*/
    case VPI_ENC_FLUSH_TRANSPIC_ERROR: /*ERROR at FLUSH_TRANSPIC stage*/
    case VPI_ENC_FLUSH_ENCDATA_ERROR: /*ERROR at FLUSH_ENCDATA stage*/
    case VPI_ENC_FLUSH_FINISH_ERROR: /*ERROR at FLUSH_FINISH stage*/
        vpe_h26xe_av_frame_free(avctx);
        ret = AVERROR_INVALIDDATA;
        break;

    case VPI_ENC_FLUSH_FINISH_END: /*END at FLUSH_FINISH stage*/
        vpe_h26xe_av_frame_free(avctx);
        ret = AVERROR_EOF;
        break;
    }
    return ret;
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
        goto error;
    }

    memset(&enc_ctx->h26x_enc_cfg, 0, sizeof(H26xEncCfg));
    memset(enc_ctx->frame_queue_for_enc, 0,
           sizeof(enc_ctx->frame_queue_for_enc));

    /*Get HW frame. The avctx->hw_frames_ctx is the reference to the
      AVHWFramesContext describing the input frame for h26x encoder*/
    if (avctx->hw_frames_ctx) {
        enc_ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!enc_ctx->hwframe) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    } else {
        ret = AVERROR_INVALIDDATA;
        goto error;
    }

    hwframe_ctx  = (AVHWFramesContext *)enc_ctx->hwframe->data;
    vpeframe_ctx = (AVVpeFramesContext *)hwframe_ctx->hwctx;
    frame_hwctx  = vpeframe_ctx->frame;

    if (avctx->codec->id == AV_CODEC_ID_HEVC) {
        sprintf(&enc_ctx->module_name[0], "%s", "HEVCENC");
    } else if (avctx->codec->id == AV_CODEC_ID_H264) {
        sprintf(&enc_ctx->module_name[0], "%s", "H264ENC");
    }

    /*Initialize the VPE h26x encoder configuration*/
    strcpy(enc_ctx->h26x_enc_cfg.module_name, enc_ctx->module_name);
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
    enc_ctx->h26x_enc_cfg.codec_name     = avctx->codec->name;
    enc_ctx->h26x_enc_cfg.profile        = enc_ctx->profile;
    enc_ctx->h26x_enc_cfg.level          = enc_ctx->level;
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
    enc_ctx->h26x_enc_cfg.enc_params = enc_ctx->enc_params;
    enc_ctx->h26x_enc_cfg.frame_ctx  = frame_hwctx;

    /*Call the VPE h26x encoder initialization function*/
    ret = enc_ctx->api->init(enc_ctx->ctx, &enc_ctx->h26x_enc_cfg);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "vpe_h264x_encode_init failure\n");
        goto error;
    }

    return 0;

error:
    vpe_h26x_encode_close(avctx);
    return ret;
}

static int vpe_h26x_encode_send_frame(AVCodecContext *avctx,
                                      const AVFrame *input_frame)
{
    int i                       = 0;
    int ret                     = 0;
    VpeH26xEncFrm *frame_queued = NULL;
    VpeH26xEncCtx *enc_ctx      = (VpeH26xEncCtx *)avctx->priv_data;
    VpiFrame *vpi_frame;
    VpiCtrlCmdParam cmd;

    if (!input_frame) {
        enc_ctx->no_input_frm = 1;
        cmd.cmd               = VPI_CMD_H26xENC_SET_NO_INFRM;
        cmd.data              = &enc_ctx->no_input_frm;
        ret = enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, NULL);
        if (ret != 0) {
            av_log(enc_ctx, AV_LOG_ERROR, "H26x_enc control NO_INFRM failed\n");
            return AVERROR_EXTERNAL;
        }
        return ret;
    }

    vpi_frame = (VpiFrame *)input_frame->data[0];
    vpi_frame->used_cnt++;

    enc_ctx->no_input_frm = 0;
    /*Look for empty wait member in the queue for input_frame storing*/
    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (enc_ctx->frame_queue_for_enc[i].state == 0) {
            frame_queued = (VpeH26xEncFrm *)&enc_ctx->frame_queue_for_enc[i];

            /*Fill the queue member with input_frame*/
            frame_queued->state             = 1;
            frame_queued->frame_index       = enc_ctx->frame_index;
            frame_queued->in_pass_one_queue = 0;
            if (!frame_queued->frame) {
                frame_queued->frame = av_frame_alloc();
                if (!frame_queued->frame) {
                    av_log(enc_ctx, AV_LOG_ERROR,
                           "No av frame mem alloc for enc data\n");
                    return AVERROR(ENOMEM);
                }
            }
            av_frame_unref(frame_queued->frame);
            ret = av_frame_ref(frame_queued->frame, input_frame);
            enc_ctx->frame_index++;

            break;
        }
    }
    if (i == MAX_WAIT_DEPTH)
        ret = AVERROR(EAGAIN);

    return ret;
}

static int vpe_h26x_encode_receive_packet(AVCodecContext *avctx,
                                          AVPacket *av_pkt)
{
    int ret     = 0;
    int enc_ret = 0;
    VpiFrame vpi_frame;
    VpiPacket vpi_packet;
    VpiCtrlCmdParam cmd;
    VpeH26xEncCtx *enc_ctx = (VpeH26xEncCtx *)avctx->priv_data;

    memset(&vpi_frame, 0, sizeof(VpiFrame));
    memset(&vpi_packet, 0, sizeof(VpiPacket));
    ret = vpe_h26xe_find_frame_for_enc(enc_ctx, &vpi_frame);
    if (ret < 0) {
        return ret;
    }

    /*Call the VPE h26x encoder encoding function*/
    enc_ret = enc_ctx->api->encode(enc_ctx->ctx, &vpi_frame, &vpi_packet);
    if (enc_ret >= VPI_ENC_FLUSH_IDLE_READY) {
        /*No input at flush stage*/
        enc_ctx->no_input_frm = 1;
        cmd.cmd               = VPI_CMD_H26xENC_SET_NO_INFRM;
        cmd.data              = &enc_ctx->no_input_frm;
        if (enc_ctx->api->control(enc_ctx->ctx, (void *)&cmd, NULL) != 0) {
            av_log(avctx, AV_LOG_ERROR, "H26x_enc control NO_INFRM failed");
            return AVERROR_EXTERNAL;
        }
    } else {
        enc_ctx->no_input_frm = 0;
    }

    ret = vpe_h26xe_output_avpacket(avctx, av_pkt, &vpi_packet, enc_ret);

    return ret;
}

static av_cold int vpe_h26x_encode_close(AVCodecContext *avctx)
{
    int ret                = 0;
    VpeH26xEncCtx *enc_ctx = (VpeH26xEncCtx *)avctx->priv_data;

    enc_ctx->api->close(enc_ctx->ctx);
    av_buffer_unref(&enc_ctx->hwframe);
    ret = vpi_destroy(enc_ctx->ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "h26x encoder vpi_destroy failure\n");
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
    .defaults       = vpe_h264_encode_defaults,
    .pix_fmts       =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_h26x_hw_configs,
    .wrapper_name = "vpe",
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
    .defaults       = vpe_hevc_encode_defaults,
    .pix_fmts       =
        (const enum AVPixelFormat[]){ AV_PIX_FMT_VPE, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NONE },
    .hw_configs     = vpe_h26x_hw_configs,
    .wrapper_name = "vpe",
};
