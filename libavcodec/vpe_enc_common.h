/*
 * Verisilicon VPE Video Encoder Common interface
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

#ifndef AVCODEC_VPE_ENC_COMMON_H
#define AVCODEC_VPE_ENC_COMMON_H

#include <stdint.h>
#include <vpe/vpi_api.h>
#include <vpe/vpi_types.h>
#include "libavutil/frame.h"
#include "libavutil/buffer.h"
#include "libavutil/log.h"
#include "libavcodec/avcodec.h"

typedef struct VpeEncFrm {
    /*The state of used or not*/
    int state;

    /*The pointer for input AVFrame*/
    AVFrame *frame;
} VpeEncFrm;

typedef struct VpeEncCtx {
    AVClass *class;
    /*The hardware frame context containing the input frames*/
    AVBufferRef *hwframe;

    /*Dictionary used to parse encoder parameters*/
    AVDictionary *dict;

    /*VPI context*/
    VpiCtx ctx;

    /*The pointer of the VPE API*/
    VpiApi *vpi;

    /*The vpi frame context*/
    VpiFrame *vpi_frame;

    /*The queue for the input AVFrames*/
    VpeEncFrm pic_wait_list[MAX_WAIT_DEPTH];

    /*VPE encoder public parameters with -enc_params*/
    VpiEncParamSet *param_list;

    int eof;

    /*Encoding preset, superfast/fast/medium/slow/superslow*/
    char *preset;
    /*Profile of the encoding*/
    char *profile;
    /*Level of the encoding*/
    char *level;
    /*Encoding parameters*/
    char *enc_params;
    /*VCE Constant rate factor mode*/
    int crf;
    /*Force IDR*/
    int force_idr;
    /*Encoder effort level*/
    int effort;
    /*Number of frames to lag. */
    int lag_in_frames;
    /*Number of encoding passes*/
    int passes;

    /*Encoder configure*/
    void *enc_cfg;
} VpeEncCtx;

int ff_vpe_encode_init(AVCodecContext *avctx, VpiPlugin type);
av_cold int ff_vpe_encode_close(AVCodecContext *avctx);
av_cold int ff_enc_receive_pic(AVCodecContext *avctx,
                   const AVFrame *input_frame);
int ff_vpe_encode_receive_packet(AVCodecContext *avctx, AVPacket *avpkt);

#endif /*AVCODEC_VPE_ENC_COMMON_H*/