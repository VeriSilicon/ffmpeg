/*
 * Verisilicon VPE Video Decoder Common interface
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

#ifndef AVCODEC_VPE_DEC_COMMON_H
#define AVCODEC_VPE_DEC_COMMON_H

#include <stdint.h>

#include <vpe/vpi_api.h>
#include <vpe/vpi_types.h>

#include "avcodec.h"
#include "libavutil/log.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vpe.h"
#include "libavutil/buffer.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

extern const AVOption vpe_decode_options[];

typedef struct {
    int state;
    AVBufferRef *buf_ref;
} VpeDecPacket;

typedef struct VpeDecFrame {
    AVFrame *av_frame;
    // frame structure used for external codec
    VpiFrame *vpi_frame;
    // vpi_frame has been used
    int used;
    // the next linked list item
    struct VpeDecFrame *next;
} VpeDecFrame;

/**
 * Communicating VPE parameters between libavcodec and the caller.
 */
typedef struct VpeDecCtx {
    AVClass *av_class;
    AVCodecContext *avctx;

    // VPI codec/filter context
    VpiCtx ctx;
    // VPI codec/filter API
    VpiApi *vpi;

    // VPE decodec setting parameters
    VpiDecOption *dec_setting;

    // VPE decoder resieze config
    uint8_t *pp_setting;
    // VPE transcode enable
    int transcode;

    // buffered packet feed to external decoder
    VpiPacket *buffered_pkt;

    // VPE frame linked list
    VpeDecFrame *frame_list;

    // Input AvPacket buffer_ref
    VpeDecPacket packet_buf_wait_list[MAX_WAIT_DEPTH];
} VpeDecCtx;

int ff_vpe_decode_init(AVCodecContext *avctx, VpiPlugin type);
int ff_vpe_decode_receive_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_vpe_decode_close(AVCodecContext *avctx);

#endif /*AVCODEC_VPE_DEC_COMMON_H*/
