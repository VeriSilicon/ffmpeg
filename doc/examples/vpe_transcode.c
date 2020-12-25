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
//#define LATENCY_PROFILE

static const int avloglevel = AV_LOG_INFO;
static const char *device   = "/dev/transcoder0";
static char *enc_codec;
static char *output_file;

static const AVDictionaryEntry device_options[] = { { "priority", "vod" },
                                                    { "vpeloglevel", "0" } };
static const AVDictionaryEntry input_options[] = { { "video_size", "1280x720" },
                                                   { "pixel_format", "nv12" } };

static const char *vfilter_specs[OUTPUT_NUM]             = { "vpe_pp" };
static const char *afilter_specs[OUTPUT_NUM]             = { "anull" };
static const AVDictionaryEntry enc_options[][OUTPUT_NUM] = {
    { { "preset", "superfast" } },
    { { "b:v", "10000000" } },
    { { "enc_params", "intra_pic_rate=100:gop_size=1" } },
};

static FILE *fp                   = NULL;
static AVBufferRef *hw_device_ctx = NULL;
static AVFormatContext *ifmt_ctx;
static AVCodecContext **dec_ctx;
static pthread_t output_threads[OUTPUT_NUM];
static int thread_num = 0;

struct timeval *in_time = NULL;
struct timeval *out_time = NULL;

typedef struct OutputContext {
    AVFormatContext *ifmt_ctx;
    AVCodecContext **dec_ctx;

    AVFormatContext *ofmt_ctx;
    AVFilterContext **buffersink_ctx;
    AVFilterContext **buffersrc_ctx;
    AVFilterGraph **filter_graph;
    AVCodecContext **enc_ctx;
    int *eof;

    sem_t frame_sem;
    AVFrame *frame;
    int stream_idx;

    int out_idx;
    char *output_filename;
    int output_init;

    struct timeval thread_start_time;

    int frame_out_cnt;
} OutputContext;

static OutputContext *output_ctx[OUTPUT_NUM] = { NULL };

static int64_t get_time_duration(struct timeval *ctime, struct timeval *ltime)
{
    int64_t duration;
    duration = (ctime->tv_sec - ltime->tv_sec) * 1000000;
    duration += ctime->tv_usec - ltime->tv_usec;
    return duration;
}

static void statistic_info(OutputContext *octx, int force)
{
    static struct timeval last_time = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval curr_time;
    int64_t duration;
    double fps;

    if (octx->out_idx == 0) {
        gettimeofday(&curr_time, NULL);
        if (force ||
            (get_time_duration(&curr_time, &last_time) > (1 * 1000 * 1000))) {
            duration = get_time_duration(&curr_time, &octx->thread_start_time);
            fps      = (double)octx->frame_out_cnt /
                  ((double)duration / (1 * 1000 * 1000));
            av_log(NULL, AV_LOG_INFO, "\rframe %d, fps=%0.1f",
                   octx->frame_out_cnt, fps);
            last_time = curr_time;
        }
    }
}

static int open_input_file(const char *filename)
{
    int ret;
    unsigned int i;
    AVDictionary *opts = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(input_options); i++) {
        av_dict_set(&opts, input_options[i].key, input_options[i].value, 0);
    }

    ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, &opts)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    dec_ctx = av_mallocz_array(ifmt_ctx->nb_streams, sizeof(*dec_ctx));
    if (!dec_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *stream = ifmt_ctx->streams[i];
        AVCodec *dec     = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx;
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }
        codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to allocate the decoder context for stream #%u\n",
                   i);
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy decoder parameters to input decoder context "
                   "for stream #%u\n",
                   i);
            return ret;
        }
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
                codec_ctx->framerate =
                    av_guess_frame_rate(ifmt_ctx, stream, NULL);
            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
        dec_ctx[i] = codec_ctx;
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
    av_log(NULL, AV_LOG_DEBUG, "Open input file success: '%s'\n", filename);

    return 0;
}

static int open_output_file(OutputContext *octx)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;
    AVBufferRef *hwfrm;
    AVDictionary *opts = NULL;
    int idx;

    avformat_alloc_output_context2(&octx->ofmt_ctx, NULL, NULL,
                                   octx->output_filename);
    if (!octx->ofmt_ctx) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not create output context for output\n");
        return AVERROR_UNKNOWN;
    }

    av_log(NULL, AV_LOG_TRACE, "%s(%d)\n", __FUNCTION__, __LINE__);

    for (i = 0; i < octx->ifmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(octx->ofmt_ctx, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        in_stream = octx->ifmt_ctx->streams[i];
        dec_ctx   = octx->dec_ctx[i];

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
                encoder = avcodec_find_encoder_by_name(enc_codec);
            else
                encoder = avcodec_find_encoder(dec_ctx->codec_id);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }
            enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                av_log(NULL, AV_LOG_FATAL,
                       "Failed to allocate the encoder context\n");
                return AVERROR(ENOMEM);
            }

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                // enc_ctx->height = dec_ctx->height;
                enc_ctx->height = octx->buffersink_ctx[i]->inputs[0]->h;
                // enc_ctx->width = dec_ctx->width;
                enc_ctx->width = octx->buffersink_ctx[i]->inputs[0]->w;
                // enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                enc_ctx->sample_aspect_ratio =
                    octx->buffersink_ctx[i]->inputs[0]->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported
                 * by encoder */
                av_log(NULL, AV_LOG_DEBUG, "dec_ctx->framerate = (%d, %d)\n",
                       dec_ctx->framerate.num, dec_ctx->framerate.den);
                enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
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
                    av_dict_set(&opts, enc_options[idx][octx->out_idx].key,
                                enc_options[idx][octx->out_idx].value, 0);
                }

                enc_ctx->hw_device_ctx =
                    av_buffer_ref(octx->buffersink_ctx[i]->hw_device_ctx);
                if (!enc_ctx->hw_device_ctx) {
                    av_log(NULL, AV_LOG_ERROR,
                           "A hardware device reference create failed.\n");
                    return AVERROR(ENOMEM);
                }

                hwfrm =
                    av_buffersink_get_hw_frames_ctx(octx->buffersink_ctx[i]);
                av_log(NULL, AV_LOG_DEBUG, "hwfrm = %p\n", hwfrm);

                if (hwfrm &&
                    ((AVHWFramesContext *)hwfrm->data)->format ==
                        av_buffersink_get_format(octx->buffersink_ctx[i])) {
                    av_log(NULL, AV_LOG_DEBUG, "set enc hwframe\n");
                    enc_ctx->hw_frames_ctx = av_buffer_ref(hwfrm);
                    if (!enc_ctx->hw_frames_ctx)
                        return AVERROR(ENOMEM);
                }
            } else {
                enc_ctx->sample_rate    = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels =
                    av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base  = (AVRational){ 1, enc_ctx->sample_rate };
            }

            if (octx->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, &opts);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to copy encoder parameters to output stream "
                       "#%u\n",
                       i);
                return ret;
            }

            out_stream->time_base = enc_ctx->time_base;
            octx->enc_ctx[i]      = enc_ctx;
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL,
                   "Elementary stream #%d is of unknown type, cannot proceed\n",
                   i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(out_stream->codecpar,
                                          in_stream->codecpar);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            out_stream->time_base = in_stream->time_base;
        }
    }
    av_dump_format(octx->ofmt_ctx, octx->out_idx, octx->output_filename, 1);

    if (!(octx->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&octx->ofmt_ctx->pb, octx->output_filename,
                        AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'\n",
                   octx->output_filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(octx->ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    av_log(NULL, AV_LOG_DEBUG, "Open output file for output %d success: '%s'\n",
           octx->out_idx, octx->output_filename);

    return 0;
}

static int init_filter(OutputContext *octx, int stream_idx,
                       const char *filter_spec)
{
    char args[512];
    int ret                         = 0;
    const AVFilter *buffersrc       = NULL;
    const AVFilter *buffersink      = NULL;
    AVFilterContext *buffersrc_ctx  = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs          = avfilter_inout_alloc();
    AVFilterInOut *inputs           = avfilter_inout_alloc();
    AVFilterGraph *filter_graph     = avfilter_graph_alloc();
    AVCodecContext *dec_ctx;
    int i;

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_log(NULL, AV_LOG_DEBUG, "filter_spec '%s' for stream #%d\n", filter_spec,
           stream_idx);

    dec_ctx = octx->dec_ctx[stream_idx];

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
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
                 dec_ctx->time_base.num, dec_ctx->time_base.den,
                 dec_ctx->sample_aspect_ratio.num,
                 dec_ctx->sample_aspect_ratio.den);

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
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc  = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR,
                   "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
                 "time_base=%d/"
                 "%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
                 dec_ctx->time_base.num, dec_ctx->time_base.den,
                 dec_ctx->sample_rate,
                 av_get_sample_fmt_name(dec_ctx->sample_fmt),
                 dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                           args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                           NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

    } else {
        ret = AVERROR_UNKNOWN;
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

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs,
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

    /* Fill FilteringContext */
    octx->buffersrc_ctx[stream_idx]  = buffersrc_ctx;
    octx->buffersink_ctx[stream_idx] = buffersink_ctx;
    octx->filter_graph[stream_idx]   = filter_graph;

    av_log(NULL, AV_LOG_INFO,
           "Apply filter graph for output #%d stream #%d success: '%s'\n",
           octx->out_idx, stream_idx, filter_spec);

end:
    av_log(NULL, AV_LOG_DEBUG, "got error for output %d stream #%d: %s\n",
           octx->out_idx, stream_idx, av_err2str(ret));

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int encode_write_frame(OutputContext *octx, AVFrame *filt_frame,
                              int *got_frame)
{
    int ret;
    int got_frame_local = 0;
    AVPacket enc_pkt;

    if (!got_frame)
        got_frame = &got_frame_local;

    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);

    if (octx->ifmt_ctx->streams[octx->stream_idx]->codecpar->codec_type ==
        AVMEDIA_TYPE_VIDEO) {
        av_log(NULL, AV_LOG_TRACE, "Sending vframe %d %p\n", octx->out_idx,
               filt_frame);

        ret = avcodec_send_frame(octx->enc_ctx[octx->stream_idx], filt_frame);
        av_log(NULL, AV_LOG_TRACE, "got ret %d %s\n", octx->out_idx,
               av_err2str(ret));
        if (ret < 0) {
            av_frame_free(&filt_frame);
            return ret;
        }
        av_frame_free(&filt_frame);

        while (1) {
            *got_frame = 0;
            av_log(NULL, AV_LOG_TRACE, "Receiving vpacket %d\n", octx->out_idx);
            ret = avcodec_receive_packet(octx->enc_ctx[octx->stream_idx],
                                         &enc_pkt);
            av_log(NULL, AV_LOG_TRACE, "got ret %d %s\n", octx->out_idx,
                   av_err2str(ret));
            if (ret == AVERROR(EAGAIN)) {
                ret = 0;
                break;
            }
            if (ret == AVERROR_EOF) {
                octx->eof[octx->stream_idx] = 1;
                return 0;
            }
            if (ret < 0) {
                return ret;
            }
            if (enc_pkt.size > 0) {
                *got_frame = 1;

#ifdef LATENCY_PROFILE
                static int frameout_count = 0;
                gettimeofday(out_time + frameout_count, NULL);
                av_log(NULL, AV_LOG_INFO, "Frame %d, lantency: %dms\n",
                    frameout_count,
                    get_time_duration(out_time+frameout_count, in_time+frameout_count)/1000);
                frameout_count ++;
#endif

                if (fp) {
                    av_log(NULL, AV_LOG_TRACE, "got vpacket %d, %d\n",
                           octx->out_idx, enc_pkt.size);
                    fwrite(enc_pkt.data, enc_pkt.size, 1, fp);
                    fflush(fp);

                    /* prepare packet for muxing */
                    enc_pkt.stream_index = octx->stream_idx;
                    av_packet_rescale_ts(&enc_pkt,
                                         octx->enc_ctx[octx->stream_idx]
                                             ->time_base,
                                         octx->ofmt_ctx
                                             ->streams[octx->stream_idx]
                                             ->time_base);

                    av_log(NULL, AV_LOG_TRACE, "Muxing vframe %d\n",
                           octx->out_idx);
                    /* mux encoded frame */
                    ret = av_interleaved_write_frame(octx->ofmt_ctx, &enc_pkt);
                }

                octx->frame_out_cnt++;

                statistic_info(octx, 0);
                if (!octx->enc_ctx[octx->stream_idx]->internal->draining)
                    break;
            }

            if (ret < 0)
                return ret;
        }

    } else {
        av_log(NULL, AV_LOG_TRACE, "Encoding aframe %d\n", octx->out_idx);
        ret = avcodec_encode_audio2(octx->enc_ctx[octx->stream_idx], &enc_pkt,
                                    filt_frame, got_frame);

        av_frame_free(&filt_frame);
        if (ret < 0)
            return ret;
        if (!(*got_frame)) {
            octx->eof[octx->stream_idx] = 1;
            return 0;
        }

        /* prepare packet for muxing */
        enc_pkt.stream_index = octx->stream_idx;
        av_packet_rescale_ts(&enc_pkt,
                             octx->enc_ctx[octx->stream_idx]->time_base,
                             octx->ofmt_ctx->streams[octx->stream_idx]
                                 ->time_base);
        av_log(NULL, AV_LOG_TRACE, "Muxing aframe %d\n", octx->out_idx);
        /* mux encoded frame */
        ret = av_interleaved_write_frame(octx->ofmt_ctx, &enc_pkt);
    }

    return ret;
}

static int filter_encode_write_frame(OutputContext *octx)
{
    int ret;
    AVFrame *filt_frame;

    av_log(NULL, AV_LOG_TRACE, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(octx->buffersrc_ctx[octx->stream_idx],
                                       octx->frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        filt_frame = av_frame_alloc();
        if (!filt_frame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_log(NULL, AV_LOG_TRACE, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(octx->buffersink_ctx[octx->stream_idx],
                                      filt_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filt_frame);
            break;
        }

        if (octx->output_init == 0) {
            if ((ret = open_output_file(octx)) < 0)
                return ret;
            av_log(NULL, AV_LOG_TRACE, "open_output_file success\n");
            octx->output_init = 1;
        }

        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret                   = encode_write_frame(octx, filt_frame, NULL);
        if (ret < 0)
            break;
    }

    return ret;
}

static int flush_encoder(OutputContext *octx)
{
    int ret;
    int got_frame = 0;
    int eof;
    int i;

    if (!(octx->enc_ctx[octx->stream_idx]->codec->capabilities &
          AV_CODEC_CAP_DELAY))
        return 0;

    av_log(NULL, AV_LOG_DEBUG, "Flushing output #%d stream #%u \n",
           octx->out_idx, octx->stream_idx);
    while (1) {
        ret = encode_write_frame(octx, NULL, &got_frame);
        if (ret < 0)
            break;

        eof = 1;
        for (i = 0; i < ifmt_ctx->nb_streams; i++) {
            eof &= octx->eof[i];
        }
        if (eof) {
            return 0;
        }

        octx->stream_idx = (octx->stream_idx + 1) % ifmt_ctx->nb_streams;
    }
    return ret;
}

#include <time.h>
#include <error.h>

static int vpi_usleep(unsigned usec)
{
    struct timespec ts = { usec / 1000000, usec % 1000000 * 1000 };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR);
    return 0;
}

static int send_frame(OutputContext *octx, int stream_idx, AVFrame *frame)
{
    int ret;

    if (octx == NULL)
        return -1;

    while (octx->frame)
        vpi_usleep(100);

    if (frame) {
        octx->frame = av_frame_alloc();
        if (octx->frame == NULL)
            return AVERROR(ENOMEM);

        ret = av_frame_ref(octx->frame, frame);
        if (ret)
            return ret;
    }

    octx->stream_idx = stream_idx;
    sem_post(&octx->frame_sem);

    return 0;
}

static int receive_frame(OutputContext *octx)
{
    if (sem_wait(&octx->frame_sem) < 0)
        return -1;

    return 0;
}

static void *output_thread_run(void *arg)
{
    OutputContext *octx = arg;
    int ret;
    AVPacket packet = { .data = NULL, .size = 0 };
    unsigned int stream_index;
    unsigned int i;

    gettimeofday(&octx->thread_start_time, NULL);

    while (1) {
        ret = receive_frame(octx);
        if (ret)
            break;

        if (octx->frame) {
            octx->frame->pts = octx->frame->best_effort_timestamp;
            ret              = filter_encode_write_frame(octx);
            if (ret < 0) {
                av_frame_free(&octx->frame);
                break;
            }
            av_freep(&octx->frame);
        } else {
            break;
        }
    }

    /* flush filters and encoders */
    ret = filter_encode_write_frame(octx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
        goto end;
    }

    /* flush encoder */
    ret = flush_encoder(octx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
        goto end;
    }
    av_write_trailer(octx->ofmt_ctx);

end:
    return NULL;
}

static int add_thread()
{
    int nb_streams;
    OutputContext *octx = NULL;
    int i;
    int ret;
    int idx = thread_num;
    char name[512];
    const char *filter_spec;

    if (idx >= OUTPUT_NUM) {
        return -1;
    }

    av_log(NULL, AV_LOG_DEBUG, "%s(%d)\n", __FUNCTION__, __LINE__);

    output_ctx[idx] = av_mallocz(sizeof(*output_ctx[idx]));
    if (output_ctx[idx] == NULL) {
        return AVERROR(ENOMEM);
    }

    octx           = output_ctx[idx];
    octx->ifmt_ctx = ifmt_ctx;
    nb_streams     = ifmt_ctx->nb_streams;

    octx->dec_ctx = av_mallocz_array(nb_streams, sizeof(*octx->dec_ctx));
    if (octx->dec_ctx == NULL) {
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < nb_streams; i++) {
        octx->dec_ctx[i] = dec_ctx[i];
    }

    // init filters
    octx->buffersrc_ctx =
        av_mallocz_array(nb_streams, sizeof(*octx->buffersrc_ctx));
    if (octx->buffersrc_ctx == NULL) {
        return AVERROR(ENOMEM);
    }

    octx->buffersink_ctx =
        av_mallocz_array(nb_streams, sizeof(*octx->buffersink_ctx));
    if (octx->buffersink_ctx == NULL) {
        return AVERROR(ENOMEM);
    }

    octx->filter_graph =
        av_mallocz_array(nb_streams, sizeof(*octx->filter_graph));
    if (octx->filter_graph == NULL) {
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < nb_streams; i++) {
        if (!(octx->ifmt_ctx->streams[i]->codecpar->codec_type ==
                  AVMEDIA_TYPE_AUDIO ||
              octx->ifmt_ctx->streams[i]->codecpar->codec_type ==
                  AVMEDIA_TYPE_VIDEO))
            continue;

        if (octx->ifmt_ctx->streams[i]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO)
            filter_spec = vfilter_specs[idx];
        else
            filter_spec = afilter_specs[idx];

        ret = init_filter(octx, i, filter_spec);
        if (ret)
            return ret;
    }
    av_log(NULL, AV_LOG_DEBUG, "init filters success\n");

    octx->enc_ctx = av_mallocz_array(nb_streams, sizeof(*octx->enc_ctx));
    if (octx->enc_ctx == NULL) {
        return AVERROR(ENOMEM);
    }
    octx->eof = av_mallocz_array(nb_streams, sizeof(*octx->eof));
    if (octx->eof == NULL) {
        return AVERROR(ENOMEM);
    }

    octx->out_idx = idx;
    sprintf(name, "test_%d.mp4", idx);
    octx->output_filename = av_strdup(name);

    sem_init(&octx->frame_sem, 0, 0);

    ret = pthread_create(&output_threads[idx], NULL, output_thread_run,
                         (void *)octx);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "Create thread %d failed\n", idx);
        return AVERROR_EXIT;
    }

    thread_num++;

    return 0;
}

static void finish_thread(OutputContext *octx)
{
    int i;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (octx->ofmt_ctx && octx->ofmt_ctx->nb_streams > i &&
            octx->ofmt_ctx->streams[i] && octx->enc_ctx[i])
            avcodec_free_context(&octx->enc_ctx[i]);
        if (octx->filter_graph[i])
            avfilter_graph_free(&octx->filter_graph[i]);
    }

    if (octx->ofmt_ctx && !(octx->ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&octx->ofmt_ctx->pb);
    avformat_free_context(octx->ofmt_ctx);

    sem_destroy(&octx->frame_sem);

    av_freep(&octx->dec_ctx);
    av_freep(&octx->buffersink_ctx);
    av_freep(&octx->buffersrc_ctx);
    av_freep(&octx->filter_graph);
    av_freep(&octx->enc_ctx);
    av_freep(&octx->eof);

    av_freep(&octx->output_filename);

    av_free(octx);
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

int main(int argc, char **argv)
{
    int ret;
    AVPacket packet = { .data = NULL, .size = 0 };
    AVFrame *frame  = NULL;
    enum AVMediaType type;
    unsigned int stream_index;
    unsigned int i;
    int got_frame;
    int (*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
    int out_index;
    int need_decode;
    int thread_idx;

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

    in_time = malloc(10000 * sizeof(struct timeval));
    if (!in_time) {
        av_log(NULL, AV_LOG_ERROR, "malloc error\n");
        return -1;
    }
    out_time = malloc(10000 * sizeof(struct timeval));
    if (!out_time) {
        av_log(NULL, AV_LOG_ERROR, "malloc error\n");
        return -1;
    }

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

    while (1) {
        av_log(NULL, AV_LOG_DEBUG, "av_read_frame\n");
        ret = av_read_frame(ifmt_ctx, &packet);
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(NULL, AV_LOG_DEBUG, "ret : %s\n", av_err2str(ret));
            break;
        }

        if (ret != AVERROR_EOF) {
            stream_index = packet.stream_index;
            type = ifmt_ctx->streams[packet.stream_index]->codecpar->codec_type;
            av_log(NULL, AV_LOG_DEBUG,
                   "Demuxer gave frame of stream_index %u\n", stream_index);

            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
            frame = av_frame_alloc();
            if (!frame) {
                ret = AVERROR(ENOMEM);
                break;
            }
            av_packet_rescale_ts(&packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 dec_ctx[stream_index]->time_base);
            dec_func = (type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 :
                                                      avcodec_decode_audio4;
#ifdef LATENCY_PROFILE
            static int framein_count = 0;
            gettimeofday(in_time + framein_count, NULL);
            framein_count ++;
#endif
            ret = dec_func(dec_ctx[stream_index], frame, &got_frame, &packet);
            if (ret < 0) {
                av_frame_free(&frame);
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            if (got_frame) {
                if (thread_num == 0) {
                    add_thread();
                } else {
                    struct timeval curr_time;

                    gettimeofday(&curr_time, NULL);
                    if (get_time_duration(&curr_time,
                                          &output_ctx[thread_num - 1]
                                               ->thread_start_time) >
                        10 * 1000 * 1000) {
                        add_thread();
                    }
                }

                for (thread_idx = 0; thread_idx < thread_num; thread_idx++) {
                    ret = send_frame(output_ctx[thread_idx], stream_index, frame);
                    if (ret < 0)
                        goto end;
                }
                av_frame_free(&frame);
            }
            av_packet_unref(&packet);
        } else {
            for (thread_idx = 0; thread_idx < thread_num; thread_idx++) {
                for (i = 0; i < ifmt_ctx->nb_streams; i++) {
                    av_log(NULL, AV_LOG_DEBUG,
                           "send EOF to output %d stream %d\n", thread_idx, i);
                    ret = send_frame(output_ctx[thread_idx], i, NULL);
                    if (ret < 0)
                        goto end;
                }
            }
            break;
        }
    }

    for (thread_idx = 0; thread_idx < thread_num; thread_idx++) {
        pthread_join(output_threads[thread_idx], NULL);
    }

    statistic_info(output_ctx[0], 1);
    av_log(NULL, AV_LOG_INFO, "\n");

end:
    av_packet_unref(&packet);
    av_frame_free(&frame);
    free(in_time);
    free(out_time);

    for (thread_idx = 0; thread_idx < thread_num; thread_idx++) {
        if (output_ctx[thread_idx])
            finish_thread(output_ctx[thread_idx]);
    }

    if (dec_ctx) {
        for (i = 0; i < ifmt_ctx->nb_streams; i++) {
            avcodec_free_context(&dec_ctx[i]);
        }
    }
    av_freep(&dec_ctx);

    avformat_close_input(&ifmt_ctx);
    av_buffer_unref(&hw_device_ctx);

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    free(enc_codec);

    if (fp) {
        fclose(fp);
        free(output_file);
    }

    return ret ? 1 : 0;
}
