#include "ffmpeg/modules/ffmpeg_decoder.hpp"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ffmpeg.hpp"
#include "legacy/modules/vita_globals.hpp"

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif

#ifdef BOREALIS_USE_GXM
#ifdef __cplusplus
extern "C"
{
#endif
    int get_buffer2_direct(AVCodecContext* avctx, AVFrame* pic, int flags);
#ifdef __cplusplus
}
#endif
#endif

int ffmpeg_decoder_init(FFmpegDecoderContext* ctx)
{
    if (!ctx)
    {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    const AVCodec* codec = avcodec_find_decoder_by_name("h264_vita");
    if (!codec)
    {
        const AVCodec* it = NULL;
        VITA_DEBUG_LOG("[FFMPEG] h264_vita not found, examining registered decoders for AV_CODEC_ID_H264:");
        void* iter = NULL;
        while ((it = av_codec_iterate(&iter)))
        {
            if (it->id == AV_CODEC_ID_H264)
            {
                VITA_DEBUG_LOG("[FFMPEG] found h264 candidate: name=%s long=%s wrapper=%s caps=0x%X", it->name, it->long_name ? it->long_name : "", it->wrapper_name ? it->wrapper_name : "", it->capabilities);
            }
        }
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    }
    if (!codec)
    {
        fprintf(stderr, "FFmpeg decoder: could not find H264 decoder\n");
        return -1;
    }

    VITA_DEBUG_LOG("FFmpeg decoder: using codec %s (%s)", codec->name, codec->long_name ? codec->long_name : "");
    if (!codec)
    {
        fprintf(stderr, "FFmpeg decoder: could not find H264 decoder\n");
        return -1;
    }

    ctx->avctx = avcodec_alloc_context3(codec);
    if (!ctx->avctx)
    {
        ffmpeg_decoder_destroy(ctx);
        return -1;
    }

    ctx->pkt = av_packet_alloc();
    if (!ctx->pkt)
    {
        ffmpeg_decoder_destroy(ctx);
        return -1;
    }

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
    {
        ffmpeg_decoder_destroy(ctx);
        return -1;
    }

    ctx->parser = av_parser_init(codec->id);

    AVDictionary* opts                  = NULL;
    enum AVDiscard targetSkipLoopFilter = AVDISCARD_DEFAULT;
    enum AVDiscard targetSkipIdct       = AVDISCARD_DEFAULT;
    enum AVDiscard targetSkipFrame      = AVDISCARD_DEFAULT;

#ifdef BOREALIS_USE_GXM
    if (codec->id == AV_CODEC_ID_H264)
    {
        int requestedPixelMode = g_video_settings_snapshot.pixel_format_mode;
        ctx->is_vita_hw        = (strcmp(codec->name, "h264_vita") == 0);

        enum AVPixelFormat requestedPixFmt = ctx->is_vita_hw ? AV_PIX_FMT_VITA_NV12 : AV_PIX_FMT_YUV420P;
        ctx->avctx->pix_fmt                = requestedPixFmt;

        VITA_DEBUG_LOG("[FFMPEG] Requested pixel format mode=%d, using pix_fmt=%d (is_vita_hw=%d)",
            requestedPixelMode, (int)requestedPixFmt, ctx->is_vita_hw ? 1 : 0);

        ctx->use_direct_render = ctx->is_vita_hw;
        if (ctx->use_direct_render)
        {
            ctx->avctx->get_buffer2 = get_buffer2_direct;
            av_dict_set(&opts, "vita_h264_dr", "1", 0);
            VITA_DEBUG_LOG("[FFMPEG] Direct render enabled for h264_vita! Assigned get_buffer2_direct and set vita_h264_dr=1");
        }
        else
        {
            VITA_DEBUG_LOG("[FFMPEG] Direct render disabled; software upload path enabled");
        }

        if (ctx->is_vita_hw)
        {
            VITA_DEBUG_LOG("[FFMPEG] Vita hardware codec detected: %s", codec->name);
        }
        VITA_DEBUG_LOG("[FFMPEG] ffmpeg_decoder_init: H264 on GXM: direct_render=%d", ctx->use_direct_render ? 1 : 0);
        // Force low-delay decoding: prevent the decoder from buffering frames internally.
        // Without this, h264_vita retains 1 frame before outputting, adding ~16.67ms of latency
        // that Legacy (direct SceAvcdec) doesn't have.
        ctx->avctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        // h264_vita must stay single-threaded; multiple threads corrupt its decoder state.
        ctx->avctx->thread_count = ctx->is_vita_hw ? 1 : 2;
        ctx->avctx->thread_type  = ctx->is_vita_hw ? 0 : FF_THREAD_SLICE;
        av_dict_set(&opts, "refcounted_frames", "1", 0);
        ctx->avctx->skip_loop_filter = AVDISCARD_DEFAULT;
        ctx->avctx->skip_idct        = AVDISCARD_DEFAULT;
        ctx->avctx->skip_frame       = AVDISCARD_DEFAULT;
        targetSkipLoopFilter         = AVDISCARD_NONREF;
        targetSkipIdct               = AVDISCARD_NONREF;
        targetSkipFrame              = AVDISCARD_DEFAULT;

        if (requestedPixelMode == 1)
        {
            targetSkipLoopFilter = AVDISCARD_ALL;
            targetSkipIdct       = AVDISCARD_ALL;
            targetSkipFrame      = AVDISCARD_DEFAULT; // Do NOT discard B-frames (allow stable 60/60 fps)
            ctx->avctx->flags2 |= AV_CODEC_FLAG2_FAST;
            VITA_DEBUG_LOG("[FFMPEG] perf profile: YUV GPU mode -> default skip_loop_filter=ALL skip_idct=ALL skip_frame=DEFAULT flags2|=FAST");

            if (ctx->is_vita_hw)
            {
                ctx->avctx->thread_count = 1;
                VITA_DEBUG_LOG("[FFMPEG] perf profile: YUV GPU + h264_vita -> thread_count forced to 1 for stability");
            }
        }
        int refcounted_frames_val  = 1;
        const char* threadCountEnv = getenv("MOONLIGHT_FFMPEG_THREAD_COUNT");
        if (threadCountEnv)
        {
            int tcount = atoi(threadCountEnv);
            if (tcount > 0)
                ctx->avctx->thread_count = tcount;
        }
        const char* threadTypeEnv = getenv("MOONLIGHT_FFMPEG_THREAD_TYPE");
        if (threadTypeEnv)
        {
            if (strcmp(threadTypeEnv, "slice") == 0)
                ctx->avctx->thread_type = FF_THREAD_SLICE;
            else if (strcmp(threadTypeEnv, "frame") == 0)
                ctx->avctx->thread_type = FF_THREAD_FRAME;
        }
        const char* skipLoopEnv = getenv("MOONLIGHT_FFMPEG_SKIP_LOOP_FILTER");
        if (skipLoopEnv)
        {
            if (strcmp(skipLoopEnv, "all") == 0)
                targetSkipLoopFilter = AVDISCARD_ALL;
            else if (strcmp(skipLoopEnv, "nonref") == 0)
                targetSkipLoopFilter = AVDISCARD_NONREF;
            else if (strcmp(skipLoopEnv, "none") == 0)
                targetSkipLoopFilter = AVDISCARD_DEFAULT;
        }
        const char* skipIdctEnv = getenv("MOONLIGHT_FFMPEG_SKIP_IDCT");
        if (skipIdctEnv)
        {
            if (strcmp(skipIdctEnv, "all") == 0)
                targetSkipIdct = AVDISCARD_ALL;
            else if (strcmp(skipIdctEnv, "nonref") == 0)
                targetSkipIdct = AVDISCARD_NONREF;
            else if (strcmp(skipIdctEnv, "none") == 0)
                targetSkipIdct = AVDISCARD_DEFAULT;
        }
        const char* skipFrameEnv = getenv("MOONLIGHT_FFMPEG_SKIP_FRAME");
        if (skipFrameEnv)
        {
            if (strcmp(skipFrameEnv, "all") == 0)
                targetSkipFrame = AVDISCARD_ALL;
            else if (strcmp(skipFrameEnv, "nonref") == 0)
                targetSkipFrame = AVDISCARD_NONREF;
            else if (strcmp(skipFrameEnv, "bidir") == 0)
                targetSkipFrame = AVDISCARD_BIDIR;
            else if (strcmp(skipFrameEnv, "none") == 0)
                targetSkipFrame = AVDISCARD_DEFAULT;
        }
        VITA_DEBUG_LOG("[FFMPEG] ffmpeg_decoder_init: thread_count=%d thread_type=%d refcounted=%d skip_loop_filter(open)=%d target_skip_loop_filter=%d target_skip_idct=%d target_skip_frame=%d flags2=0x%X", ctx->avctx->thread_count, ctx->avctx->thread_type, refcounted_frames_val, ctx->avctx->skip_loop_filter, targetSkipLoopFilter, targetSkipIdct, targetSkipFrame, (unsigned)ctx->avctx->flags2);
    }
    else
    {
        ctx->use_direct_render = false;
    }
#else
    ctx->avctx->pix_fmt    = (g_video_settings_snapshot.pixel_format_mode == 0) ? AV_PIX_FMT_RGBA : AV_PIX_FMT_YUV420P;
    ctx->use_direct_render = false;
#endif

    int openRet = avcodec_open2(ctx->avctx, codec, &opts);
    av_dict_free(&opts);
    if (openRet < 0)
    {
#ifdef BOREALIS_USE_GXM
        if (ctx->is_vita_hw && ctx->avctx->skip_loop_filter != AVDISCARD_DEFAULT)
        {
            VITA_DEBUG_LOG("[FFMPEG] avcodec_open2 failed with skip_loop_filter=%d, retrying with default", ctx->avctx->skip_loop_filter);
            // Recreate a clean codec context before retry; some Vita builds don't
            // reliably support reopening after a failed avcodec_open2 on the same avctx.
            int savedThreadCount           = ctx->avctx->thread_count;
            int savedThreadType            = ctx->avctx->thread_type;
            enum AVPixelFormat savedPixFmt = ctx->avctx->pix_fmt;

            avcodec_free_context(&ctx->avctx);
            ctx->avctx = avcodec_alloc_context3(codec);
            if (ctx->avctx)
            {
                ctx->avctx->pix_fmt          = savedPixFmt;
                ctx->avctx->thread_count     = savedThreadCount;
                ctx->avctx->thread_type      = savedThreadType;
                ctx->avctx->skip_loop_filter = AVDISCARD_DEFAULT;

                AVDictionary* retryOpts = nullptr;
                av_dict_set(&retryOpts, "refcounted_frames", "1", 0);
                openRet = avcodec_open2(ctx->avctx, codec, &retryOpts);
                av_dict_free(&retryOpts);
            }

            if (openRet < 0 && ctx->avctx && savedThreadCount > 1)
            {
                VITA_DEBUG_LOG("[FFMPEG] avcodec_open2 still failing with threads=%d, retrying with threads=1", savedThreadCount);
                avcodec_free_context(&ctx->avctx);
                ctx->avctx = avcodec_alloc_context3(codec);
                if (ctx->avctx)
                {
                    ctx->avctx->pix_fmt          = savedPixFmt;
                    ctx->avctx->thread_count     = 1;
                    ctx->avctx->thread_type      = savedThreadType;
                    ctx->avctx->skip_loop_filter = AVDISCARD_DEFAULT;

                    AVDictionary* retrySingleThreadOpts = nullptr;
                    av_dict_set(&retrySingleThreadOpts, "refcounted_frames", "1", 0);
                    openRet = avcodec_open2(ctx->avctx, codec, &retrySingleThreadOpts);
                    av_dict_free(&retrySingleThreadOpts);
                }
            }
        }
#endif
        if (openRet < 0)
        {
            ffmpeg_decoder_destroy(ctx);
            fprintf(stderr, "FFmpeg decoder: failed to open codec %s\n", codec->name);
            return -1;
        }
    }

    ctx->avctx->skip_loop_filter = targetSkipLoopFilter;
    ctx->avctx->skip_idct        = targetSkipIdct;
    ctx->avctx->skip_frame       = targetSkipFrame;
    VITA_DEBUG_LOG("[FFMPEG] ffmpeg_decoder_init: post-open tuning skip_loop_filter=%d skip_idct=%d skip_frame=%d", ctx->avctx->skip_loop_filter, ctx->avctx->skip_idct, ctx->avctx->skip_frame);

    ctx->initialized = 1;
    VITA_DEBUG_LOG("[FFMPEG] codec=%s avctx->get_buffer2=%p avctx->hw_device_ctx=%p use_direct_render=%d", codec->name, (void*)ctx->avctx->get_buffer2, (void*)ctx->avctx->hw_device_ctx, ctx->use_direct_render);
    return 0;
}

void ffmpeg_decoder_destroy(FFmpegDecoderContext* ctx)
{
    if (!ctx)
    {
        return;
    }
    if (ctx->avctx)
    {
        avcodec_free_context(&ctx->avctx);
    }
    if (ctx->parser)
    {
        av_parser_close(ctx->parser);
        ctx->parser = NULL;
    }
    if (ctx->frame)
    {
        av_frame_free(&ctx->frame);
    }
    if (ctx->pkt)
    {
        av_packet_free(&ctx->pkt);
    }
    memset(ctx, 0, sizeof(*ctx));
}

int ffmpeg_decoder_decode(FFmpegDecoderContext* ctx, const uint8_t* data, int size)
{
    if (!ctx || !ctx->initialized || !data || size <= 0)
    {
        return AVERROR(EINVAL);
    }

    if (av_new_packet(ctx->pkt, size) < 0)
    {
        return AVERROR(ENOMEM);
    }

    memcpy(ctx->pkt->data, data, (size_t)size);

    int ret = avcodec_send_packet(ctx->avctx, ctx->pkt);
    av_packet_unref(ctx->pkt);
    return ret;
}
