/*
 * Verisilicon VPE Video Encoder Common Interface
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

#include "libavutil/hwcontext_vpe.h"
#include "libavutil/time.h"

#include "vpe_enc_common.h"

/**
 * Create the vpe encoder param list
 */
static int vpe_enc_create_param_list(VpeEncCtx *enc_ctx)
{
    char *enc_params              = enc_ctx->enc_params;
    AVDictionaryEntry *dict_entry = NULL;
    VpiEncParamSet *tail          = NULL;
    VpiEncParamSet *node          = NULL;

    if (!av_dict_parse_string(&enc_ctx->dict, enc_params, "=", ":", 0)) {
        while ((dict_entry = av_dict_get(enc_ctx->dict, "", dict_entry,
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
                enc_ctx->param_list = tail = node;
            }
        }
    }
    return 0;
}

/**
 * Release the vpe encoder param list
 */
static void vpe_enc_release_param_list(VpeEncCtx *enc_ctx)
{
    VpiEncParamSet *tail = enc_ctx->param_list;
    VpiEncParamSet *node = NULL;

    while (tail != NULL) {
        node = tail->next;
        free(tail);
        tail = node;
    }
    av_dict_free(&enc_ctx->dict);
}

/**
 * Init vpe enc hw ctx and create vpe context
 */
int ff_vpe_encode_init(AVCodecContext *avctx, VpiPlugin type)
{
    AVHWFramesContext *hwframe_ctx;
    AVVpeFramesContext *vpeframe_ctx;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;
    int ret;

    /*Get HW frame. The avctx->hw_frames_ctx is the reference to the
      AVHWFramesContext describing the input frame for vpe encoder*/
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

    if (!avctx->hw_device_ctx) {
        vpedev_ctx = hwframe_ctx->device_ctx->hwctx;
    } else {
        hwdevice_ctx = (AVHWDeviceContext *)avctx->hw_device_ctx->data;
        vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;
    }

    /*Create context and get the APIs for encoder from VPI layer */
    if (vpi_create(&enc_ctx->ctx, &enc_ctx->vpi, vpedev_ctx->device, type)) {
        av_log(avctx, AV_LOG_ERROR,
               "encoder vpe create failed, error=%s(%d)\n", vpi_error_str(ret),
               ret);
        return AVERROR_EXTERNAL;
    }

    ret = vpe_enc_create_param_list(enc_ctx);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR,
               "vpe_enc_create_param_list failed\n");
        return ret;
    }

    enc_ctx->frame = av_frame_alloc();
    if (!enc_ctx->frame)
        return AVERROR(ENOMEM);

    enc_ctx->vpi_frame = vpeframe_ctx->frame;

    return 0;
}

/**
 * Convert input AVFrame struct to VpiFrame struct
 */
static void vpe_enc_input_frame(AVFrame *input_image,
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
        out_frame->vpi_opaque  = (void *)in_vpi_frame;
    } else {
        memset(out_frame, 0, sizeof(VpiFrame));
    }
}

/**
 * dump pic usage for debug
 */
static void vpe_dump_pic(AVCodecContext *avctx, const char *str,
                         const char *sep)
{
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;
    int i = 0;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if ((enc_ctx->pic_wait_list[i].state == 0) &&
            (enc_ctx->pic_wait_list[i].frame == NULL))
            continue;

        av_log(avctx, AV_LOG_TRACE, "pic[%d] state=%d, data=%p\n", i,
               enc_ctx->pic_wait_list[i].state,
               enc_ctx->pic_wait_list[i].frame);
    }
}

/**
 * Release the AVFrame
 */
static int vpe_enc_consume_pic(AVCodecContext *avctx, AVFrame *consume_frame)
{
    VpeEncCtx *enc_ctx  = (VpeEncCtx *)avctx->priv_data;
    VpeEncFrm *transpic = NULL;
    int i;

    vpe_dump_pic(avctx, "vpe_enc_consume_pic", " --->");
    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (enc_ctx->pic_wait_list[i].state == 1) {
            transpic = &enc_ctx->pic_wait_list[i];
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

/**
 * Get the released frame buffer info from external encoder,
 * do release opertion
 */
static av_cold int vpe_enc_free_frames(AVCodecContext *avctx)
{
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;
    VpiCtrlCmdParam cmd;
    AVFrame *frame_ref = NULL;
    int ret = 0;

    do {
        cmd.cmd = VPI_CMD_ENC_CONSUME_PIC;
        ret     = enc_ctx->vpi->control(enc_ctx->ctx, (void *)&cmd,
                                        (void *)&frame_ref);
        if (ret < 0)
            return AVERROR_EXTERNAL;

        if (frame_ref) {
            ret = vpe_enc_consume_pic(avctx, frame_ref);
            if (ret != 0)
                return ret;
        } else {
            break;
        }
    } while(1);

    return 0;
}

/**
 * Fetch new AVFrame and put to external encoder
 */
static int vpe_enc_receive_pic(AVCodecContext *avctx)
{
    int i               = 0;
    int ret             = 0;
    VpeEncFrm *transpic = NULL;
    VpeEncCtx *enc_ctx  = (VpeEncCtx *)avctx->priv_data;
    VpiFrame *vpi_frame = NULL;
    AVFrame *frame      = enc_ctx->frame;
    VpiCtrlCmdParam cmd;

    do {
        for (i = 0; i < MAX_WAIT_DEPTH; i++) {
            if (enc_ctx->pic_wait_list[i].state == 0) {
                transpic = &enc_ctx->pic_wait_list[i];
                break;
            }
        }

        if (i == MAX_WAIT_DEPTH) {
            av_usleep(500);
            ret = vpe_enc_free_frames(avctx);
            if (ret != 0) {
                return ret;
            }
        } else {
            break;
        }
    } while (1);

    if (!frame->buf[0]) {
        ret = ff_encode_get_frame(avctx, frame);
        if (ret < 0 && ret != AVERROR_EOF) {
            return ret;
        }
    }
    if (ret == AVERROR_EOF)
        frame = NULL;

    transpic->state = 1;

    cmd.cmd  = VPI_CMD_ENC_GET_EMPTY_FRAME_SLOT;
    cmd.data = NULL;
    ret = enc_ctx->vpi->control(enc_ctx->ctx,
                    (void*)&cmd, (void *)&vpi_frame);
    if (ret != 0 || vpi_frame == NULL) {
        return AVERROR_EXTERNAL;
    }
    if (frame) {
        if (!transpic->frame) {
            transpic->frame = av_frame_alloc();
            if (!transpic->frame)
                return AVERROR(ENOMEM);
        }
        av_frame_unref(transpic->frame);
        av_frame_move_ref(transpic->frame, frame);
        vpe_enc_input_frame(transpic->frame, vpi_frame);
    } else {
        av_log(enc_ctx, AV_LOG_DEBUG, "input image is empty, received EOF\n");
        vpe_enc_input_frame(NULL, vpi_frame);
        enc_ctx->eof = 1;
    }

    ret = enc_ctx->vpi->encode_put_frame(enc_ctx->ctx, (void*)vpi_frame);
    if (ret) {
        return AVERROR_EXTERNAL;
    }

    vpe_dump_pic(avctx, "vpe_enc_receive_pic", " <---");
    return 0;
}

/**
 * Translate VpiPacket to AVPacket
 */
static void vpe_enc_output_packet(VpiPacket *vpi_packet,
                                  AVPacket *out_packet)
{
    out_packet->size = vpi_packet->size;
    out_packet->pts  = vpi_packet->pts;
    out_packet->dts  = vpi_packet->pkt_dts;
}

/**
 * Get encoded packet from external encoder
 */
int ff_vpe_encode_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;
    VpiPacket vpi_packet;
    VpiCtrlCmdParam cmd;
    int stream_size = 0;
    int ret = 0;

    ret = vpe_enc_free_frames(avctx);
    if (ret != 0) {
        return ret;
    }

    if (enc_ctx->eof == 0) {
        ret = vpe_enc_receive_pic(avctx);
        if (ret != 0) {
            return ret;
        }
    }

    cmd.cmd = VPI_CMD_ENC_GET_FRAME_PACKET;
    ret = enc_ctx->vpi->control(enc_ctx->ctx, &cmd, (void *)&stream_size);
    if (ret == -1) {
        return AVERROR(EAGAIN);
    } else if (ret == 1) {
        av_log(NULL, AV_LOG_DEBUG, "received EOF from enc\n");
        return AVERROR_EOF;
    }

    /*Allocate AVPacket bufffer*/
    ret = av_new_packet(avpkt, stream_size);
    if (ret != 0)
        return ret;

    vpi_packet.data = avpkt->data;
    vpi_packet.size = stream_size;
    ret = enc_ctx->vpi->encode_get_packet(enc_ctx->ctx, (void *)&vpi_packet);
    if (ret == 0) {
        /*Convert output packet from VpiPacket to AVPacket*/
        vpe_enc_output_packet(&vpi_packet, avpkt);
    } else {
        av_log(avctx, AV_LOG_ERROR, "enc encode failed, error=%s(%d)\n",
               vpi_error_str(ret), ret);
        ret = AVERROR_EXTERNAL;
    }

    return ret;
}

/**
 * Release all the AVFrames
 */
static av_cold void vpe_enc_consume_flush(AVCodecContext *avctx)
{
    VpeEncCtx *enc_ctx  = (VpeEncCtx *)avctx->priv_data;
    VpeEncFrm *transpic = NULL;
    int i;

    for (i = 0; i < MAX_WAIT_DEPTH; i++) {
        if (enc_ctx->pic_wait_list[i].state == 1) {
            transpic = &enc_ctx->pic_wait_list[i];
            if (transpic->frame) {
                av_frame_free(&transpic->frame);
            }
            transpic->state = 0;
        }
    }
}

av_cold int ff_vpe_encode_close(AVCodecContext *avctx)
{
    AVHWFramesContext *hwframe_ctx;
    AVHWDeviceContext *hwdevice_ctx;
    AVVpeDeviceContext *vpedev_ctx;
    VpeEncCtx *enc_ctx = (VpeEncCtx *)avctx->priv_data;

    if (!avctx->hw_device_ctx) {
        hwframe_ctx = (AVHWFramesContext *)enc_ctx->hwframe->data;
        vpedev_ctx  = hwframe_ctx->device_ctx->hwctx;
    } else {
        hwdevice_ctx = (AVHWDeviceContext *)avctx->hw_device_ctx->data;
        vpedev_ctx   = (AVVpeDeviceContext *)hwdevice_ctx->hwctx;
    }

    if (avctx->extradata) {
        av_freep(&avctx->extradata);
    }

    vpe_enc_release_param_list(enc_ctx);
    if (enc_ctx->ctx) {
        enc_ctx->vpi->close(enc_ctx->ctx);
    }
    vpe_enc_consume_flush(avctx);
    av_frame_free(&enc_ctx->frame);
    av_buffer_unref(&enc_ctx->hwframe);
    if (enc_ctx->enc_cfg) {
        free(enc_ctx->enc_cfg);
    }
    if (enc_ctx->ctx) {
        if (vpi_destroy(enc_ctx->ctx, vpedev_ctx->device)) {
            av_log(avctx, AV_LOG_ERROR, "encoder vpi_destroy failure\n");
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}