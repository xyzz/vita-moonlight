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

    AVDictionary* opts = NULL;

#ifdef BOREALIS_USE_GXM
    if (codec->id == AV_CODEC_ID_H264)
    {
        // Direct-render with h264_vita has shown runtime instability on some
        // sessions (GPU crash). Keep it as opt-in via env var while defaulting
        // to the safer software upload path.
        bool enableDirectRender = false;
        const char* drEnv       = getenv("MOONLIGHT_FFMPEG_DIRECT_RENDER");
        if (drEnv && strcmp(drEnv, "0") != 0 && strcmp(drEnv, "false") != 0 && strcmp(drEnv, "off") != 0)
        {
            enableDirectRender = true;
        }

        if (enableDirectRender)
        {
            av_dict_set(&opts, "vita_h264_dr", "1", 0);
            ctx->avctx->get_buffer2 = get_buffer2_direct;
            ctx->use_direct_render  = true;
            VITA_DEBUG_LOG("[FFMPEG] Direct render ENABLED by MOONLIGHT_FFMPEG_DIRECT_RENDER=%s", drEnv ? drEnv : "1");
        }
        else
        {
            ctx->use_direct_render = false;
            ctx->avctx->pix_fmt    = AV_PIX_FMT_YUV420P;
            VITA_DEBUG_LOG("[FFMPEG] Direct render DISABLED by default (set MOONLIGHT_FFMPEG_DIRECT_RENDER=1 to enable)");
        }

        // Mark if this is the hardware assisted vita decoder
        ctx->is_vita_hw = (strcmp(codec->name, "h264_vita") == 0);
        if (ctx->is_vita_hw)
        {
            VITA_DEBUG_LOG("[FFMPEG] Vita hardware codec detected: %s", codec->name);
        }
        VITA_DEBUG_LOG("[FFMPEG] ffmpeg_decoder_init: H264 on GXM: direct_render=%d", ctx->use_direct_render ? 1 : 0);
        // Set conservative fixed defaults for the Vita: slice threading and skip loop filter
        // We default to 1 thread and slice-type threading to avoid oversubscription and high latency.
        ctx->avctx->thread_count = 1;
        ctx->avctx->thread_type  = FF_THREAD_SLICE;
        av_dict_set(&opts, "refcounted_frames", "1", 0);
        // Skip loop filter to reduce CPU workload (lower quality but faster)
        av_dict_set(&opts, "skip_loop_filter", "all", 0);
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
                ctx->avctx->skip_loop_filter = AVDISCARD_ALL;
            else if (strcmp(skipLoopEnv, "nonref") == 0)
                ctx->avctx->skip_loop_filter = AVDISCARD_NONREF;
            else if (strcmp(skipLoopEnv, "none") == 0)
                ctx->avctx->skip_loop_filter = AVDISCARD_DEFAULT;
        }
        VITA_DEBUG_LOG("[FFMPEG] ffmpeg_decoder_init: thread_count=%d thread_type=%d refcounted=%d skip_loop_filter=%d", ctx->avctx->thread_count, ctx->avctx->thread_type, refcounted_frames_val, ctx->avctx->skip_loop_filter);
    }
    else
    {
        // Force output pixel format to YUV420P for other decoders and use
        // default buffer allocation to keep behavior stable.
        ctx->avctx->pix_fmt    = AV_PIX_FMT_YUV420P;
        ctx->use_direct_render = false;
    }
#else
    ctx->avctx->pix_fmt    = AV_PIX_FMT_YUV420P;
    ctx->use_direct_render = false;
#endif

    if (avcodec_open2(ctx->avctx, codec, &opts) < 0)
    {
        av_dict_free(&opts);
        ffmpeg_decoder_destroy(ctx);
        fprintf(stderr, "FFmpeg decoder: failed to open codec %s\n", codec->name);
        return -1;
    }

    av_dict_free(&opts);

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
