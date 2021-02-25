/*
 * Verisilicon sample for encoding with VPE suppprted avcodec API.
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

#include <sys/time.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/internal.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>

#define OUTPUT_NUM 1

#define LATENCY_PROFILE

#ifdef LATENCY_PROFILE
#define NFRAMES  10000
struct timeval *in_time = NULL;
struct timeval *out_time = NULL;
#endif

static const int avloglevel = AV_LOG_INFO;
static const char *device   = "/dev/transcoder0";
static char *enc_codec;
static char *output_file;

static const AVDictionaryEntry device_options[] = { { "priority", "vod" },
                                                    { "vpeloglevel", "0" } };
static const AVDictionaryEntry input_options[] = { { "video_size", "1280x720" },
                                                   { "pixel_format", "nv12" } };

static const char *filter_descr = "vpe_pp";

static const AVDictionaryEntry enc_options[][OUTPUT_NUM] = {
    { { "preset", "fast" } },
    { { "b", "10000000" } },
    { { "enc_params", "intra_pic_rate=100:gop_size=1" } },
};

static FILE *fp                   = NULL;
static AVBufferRef *hw_device_ctx = NULL;

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
static AVCodecContext *enc_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = -1;
static int64_t last_pts = AV_NOPTS_VALUE;
static int frame_out_cnt;
struct timeval start_time;

static int64_t get_time_duration(struct timeval *ctime, struct timeval *ltime)
{
    int64_t duration;
    duration = (ctime->tv_sec - ltime->tv_sec) * 1000000;
    duration += ctime->tv_usec - ltime->tv_usec;
    return duration;
}

static void statistic_info(int force)
{
    static struct timeval last_time = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval curr_time;
    int64_t duration;
    double fps;

    gettimeofday(&curr_time, NULL);
    if (force ||
        (get_time_duration(&curr_time, &last_time) > (1 * 1000 * 1000))) {
        duration = get_time_duration(&curr_time, &start_time);
        fps      = (double)frame_out_cnt /
              ((double)duration / (1 * 1000 * 1000));
        av_log(NULL, AV_LOG_INFO, "\rframe %5d, fps=%3.1f", frame_out_cnt, fps);
#ifdef LATENCY_PROFILE
        av_log(NULL, AV_LOG_INFO, " latency=%3dms ",
               get_time_duration(out_time+frame_out_cnt-1, in_time+frame_out_cnt-1)/1000);
#endif
        last_time = curr_time;
    }
}

static int open_input_file(const char *filename)
{
    int i;
    int ret;
    AVCodec *dec;
    AVDictionary *opts = NULL;
    AVStream *stream;

    for (i = 0; i < FF_ARRAY_ELEMS(input_options); i++) {
        av_dict_set(&opts, input_options[i].key, input_options[i].value, 0);
    }

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, &opts)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index = ret;

    /* create decoding context */
    stream = fmt_ctx->streams[video_stream_index];
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    dec_ctx->framerate = av_guess_frame_rate(fmt_ctx, stream, NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
        return ret;
    }

    return 0;
}

static int open_output_file()
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodec *enc;
    int ret;
    unsigned int i;
    AVBufferRef *hwfrm;
    AVDictionary *opts = NULL;
    int idx;

    av_log(NULL, AV_LOG_TRACE, "%s(%d)\n", __FUNCTION__, __LINE__);

    enc = avcodec_find_encoder_by_name(enc_codec);
    if (!enc) {
        av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
        return AVERROR_INVALIDDATA;
    }
    enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx) {
        av_log(NULL, AV_LOG_FATAL,
                "Failed to allocate the encoder context\n");
        return AVERROR(ENOMEM);
    }

    /* In this example, we transcode to same properties (picture size,
     * sample rate etc.). These properties can be changed for output
     * streams easily using filters */

    enc_ctx->height = buffersink_ctx->inputs[0]->h;
    enc_ctx->width = buffersink_ctx->inputs[0]->w;
    enc_ctx->sample_aspect_ratio =
        buffersink_ctx->inputs[0]->sample_aspect_ratio;
    /* take first format from list of supported formats */
    if (enc->pix_fmts)
        enc_ctx->pix_fmt = enc->pix_fmts[0];
    else
        enc_ctx->pix_fmt = dec_ctx->pix_fmt;
    /* video time_base can be set to whatever is handy and supported
     * by encoder */
    av_log(NULL, AV_LOG_DEBUG, "dec_ctx->framerate = (%d, %d)\n",
            dec_ctx->framerate.num, dec_ctx->framerate.den);
    enc_ctx->time_base = buffersink_ctx->inputs[0]->time_base; //av_inv_q(dec_ctx->framerate);
    enc_ctx->framerate = dec_ctx->framerate;

    av_log(NULL, AV_LOG_DEBUG, "enc_ctx->height = %d\n",
           enc_ctx->height);
    av_log(NULL, AV_LOG_DEBUG, "enc_ctx->width  = %d\n",
           enc_ctx->width);
    av_log(NULL, AV_LOG_DEBUG,
           "enc_ctx->sample_aspect_ratio = (%d, %d)\n",
            enc_ctx->sample_aspect_ratio.num,
            enc_ctx->sample_aspect_ratio.den);
    av_log(NULL, AV_LOG_DEBUG, "enc_ctx->pix_fmt = %s\n",
            av_get_pix_fmt_name(enc_ctx->pix_fmt));
    av_log(NULL, AV_LOG_DEBUG, "enc_ctx->time_base = (%d, %d)\n",
            enc_ctx->time_base.num, enc_ctx->time_base.den);
    av_log(NULL, AV_LOG_DEBUG, "enc_ctx->framerate = (%d, %d)\n",
            enc_ctx->framerate.num, enc_ctx->framerate.den);

    for (idx = 0; idx < FF_ARRAY_ELEMS(enc_options); idx++) {
        av_dict_set(&opts, enc_options[idx][0].key,
                    enc_options[idx][0].value, 0);
    }

    enc_ctx->hw_device_ctx =
        av_buffer_ref(buffersink_ctx->hw_device_ctx);
    if (!enc_ctx->hw_device_ctx) {
        av_log(NULL, AV_LOG_ERROR,
              "A hardware device reference create failed.\n");
        return AVERROR(ENOMEM);
    }

    hwfrm =
        av_buffersink_get_hw_frames_ctx(buffersink_ctx);
        av_log(NULL, AV_LOG_DEBUG, "hwfrm = %p\n", hwfrm);

    if (hwfrm &&
        ((AVHWFramesContext *)hwfrm->data)->format ==
        av_buffersink_get_format(buffersink_ctx)) {
        av_log(NULL, AV_LOG_DEBUG, "set enc hwframe\n");
        enc_ctx->hw_frames_ctx = av_buffer_ref(hwfrm);
        if (!enc_ctx->hw_frames_ctx)
            return AVERROR(ENOMEM);
    }

    /* Third parameter can be used to pass settings to encoder */
    ret = avcodec_open2(enc_ctx, enc, &opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Cannot open video encoder for stream #%u\n", i);
        return ret;
    }

    return 0;
}

static int init_filters(const char *filters_spec)
{
    char args[512];
    int ret                         = 0;
    const AVFilter *buffersrc       = NULL;
    const AVFilter *buffersink      = NULL;
    AVFilterInOut *outputs          = avfilter_inout_alloc();
    AVFilterInOut *inputs           = avfilter_inout_alloc();
    AVFilterGraph *filter_graph     = avfilter_graph_alloc();

    //AVCodecContext *dec_ctx;
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    int i;

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_log(NULL, AV_LOG_DEBUG, "filters_spec '%s' for stream\n", filters_spec);

    buffersrc  = avfilter_get_by_name("buffer");
    buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        av_log(NULL, AV_LOG_ERROR,
               "filtering source or sink element not found\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/"
             "%d",
             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
             time_base.num, time_base.den,
             dec_ctx->sample_aspect_ratio.num,
             dec_ctx->sample_aspect_ratio.den);
    printf("args %s\n", args);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_log(NULL, AV_LOG_DEBUG, "to avfilter_graph_parse_ptr\n");

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_spec, &inputs,
                                        &outputs, NULL)) < 0)
        goto end;

    av_log(NULL, AV_LOG_DEBUG, "to set hwdevice to filter\n");

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO && hw_device_ctx) {
        for (i = 0; i < filter_graph->nb_filters; i++) {
            filter_graph->filters[i]->hw_device_ctx =
                av_buffer_ref(hw_device_ctx);
            if (!filter_graph->filters[i]->hw_device_ctx) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        }
    }

    av_log(NULL, AV_LOG_DEBUG, "to avfilter_graph_config\n");

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int init_vpe_device()
{
    int ret;
    AVDictionary *opts = NULL;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(device_options); i++) {
        av_dict_set(&opts, device_options[i].key, device_options[i].value, 0);
    }

    ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VPE, device,
                                 opts, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed to create a VPE device. Error code: %s\n",
               av_err2str(ret));
        return -1;
    }
    return 0;
}

static int encode_frame(AVFrame *frame, AVPacket *pkt)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        av_log(NULL, AV_LOG_DEBUG, "Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            return ret;
        }

        av_log(NULL, AV_LOG_DEBUG, "Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);

        if (fp) {
            fwrite(pkt->data, 1, pkt->size, fp);
            fflush(fp);
        }
#ifdef LATENCY_PROFILE
        if (frame_out_cnt < NFRAMES ) {
            gettimeofday(out_time + frame_out_cnt, NULL);
        }
#endif
        frame_out_cnt++;
        av_packet_unref(pkt);
    }

    return 0;
}

static int encode_flush(AVPacket *pkt)
{
    int ret;

    for (;;) {
        while ((ret = avcodec_receive_packet(enc_ctx, pkt)) == AVERROR(EAGAIN)) {
            ret = avcodec_send_frame(enc_ctx, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_FATAL, "encoding failed: %s\n", av_err2str(ret));
                return -1;
            }
        }
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(NULL, AV_LOG_FATAL, "encoding failed: %s\n", av_err2str(ret));
            return -1;
        }
        if (fp && ret != AVERROR_EOF) {
            fwrite(pkt->data, 1, pkt->size, fp);
            fflush(fp);
        }
        frame_out_cnt++;
        av_packet_unref(pkt);
        if (ret == AVERROR_EOF) {
            break;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int ret;
    AVPacket packet = { .data = NULL, .size = 0 };
    AVFrame *frame;
    AVFrame *filt_frame;
    AVPacket *enc_pkt;

    av_log_set_level(avloglevel);

    if (argc <= 2) {
        av_log(NULL, AV_LOG_ERROR,
               "Usage: %s <input file> <codec name> <output file>\n", argv[0]);
        return 1;
    }
    ret = init_vpe_device();
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to init a VPE device\n");
        return -1;
    }

    enc_codec = malloc(128);
    strcpy(enc_codec, argv[2]);

#ifdef LATENCY_PROFILE
    in_time = malloc(NFRAMES * sizeof(struct timeval));
    if (!in_time) {
        av_log(NULL, AV_LOG_ERROR, "malloc error\n");
        return -1;
    }
    out_time = malloc(NFRAMES * sizeof(struct timeval));
    if (!out_time) {
        av_log(NULL, AV_LOG_ERROR, "malloc error\n");
        return -1;
    }
#endif

    if (argc <= 3) {
        output_file = NULL;
    } else {
        output_file = malloc(128);
        strcpy(output_file, argv[3]);
        fp = fopen(output_file, "wb");
        if (fp == NULL) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open output file\n");
            return -1;
        }
    }

    if ((ret = open_input_file(argv[1])) < 0)
        goto end;
    av_log(NULL, AV_LOG_DEBUG, "open_input_file success\n");

    if ((ret = init_filters(filter_descr)) < 0)
        goto end;

    ret = open_output_file();
    if (ret < 0)
        goto end;

    frame = av_frame_alloc();
    filt_frame = av_frame_alloc();
    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        goto end;
    }

    enc_pkt = av_packet_alloc();
    if (!enc_pkt)
        goto end;

    frame_out_cnt = 0;

    gettimeofday(&start_time, NULL);
    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;

        if (packet.stream_index == video_stream_index) {

#ifdef LATENCY_PROFILE
            static int framein_count = 0;
            if (framein_count < NFRAMES) {
                gettimeofday(in_time + framein_count, NULL);
                 framein_count ++;
            }
#endif
            ret = avcodec_send_packet(dec_ctx, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                frame->pts = frame->best_effort_timestamp;

                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
                    if (encode_frame(filt_frame, enc_pkt) < 0)
                        goto end;
                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
        statistic_info(0);
    }

    if (encode_flush(enc_pkt) < 0)
        goto end;

    statistic_info(1);

    av_log(NULL, AV_LOG_INFO, "\n");

end:
    av_packet_unref(&packet);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    av_packet_free(&enc_pkt);
#ifdef LATENCY_PROFILE
    free(in_time);
    free(out_time);
#endif

    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avfilter_graph_free(&filter_graph);
    avformat_close_input(&fmt_ctx);
    av_buffer_unref(&hw_device_ctx);

    free(enc_codec);
    if (fp) {
        fclose(fp);
    }

    return ret ? 1 : 0;
}