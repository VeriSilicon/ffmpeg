/*
 * Verisilicon VPI Video codec
 * Copyright (c) 2019 VeriSilicon, Inc.
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

#ifndef AVUTIL_HWCONTEXT_VPE_H
#define AVUTIL_HWCONTEXT_VPE_H

#include <vpe/vpi_api.h>
#include <vpe/vpi_types.h>

/**
 * @file
 * An API-specific header for AV_HWDEVICE_TYPE_VPE.
 */

/**
 * This struct is allocated as AVHWDeviceContext.hwctx
 * It will save some device level info
 */

typedef struct AVVpeDeviceContext {
    int device;
    VpiApi *func;
} AVVpeDeviceContext;

/**
 * This struct is allocated as AVHWFramesContext.hwctx
 * It will save some frame level info
 */
typedef struct AVVpeFramesContext {
    VpiFrame *frame;
    int frame_size;
    int pic_info_size;
} AVVpeFramesContext;
#endif /* AVUTIL_HWCONTEXT_VPE_H */
