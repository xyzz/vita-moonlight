#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>

    typedef struct FFmpegDecoderContext
    {
        AVCodecContext* avctx;
        AVCodecParserContext* parser;
        AVFrame* frame;
        AVPacket* pkt;
        int initialized;
        bool use_direct_render;
        bool is_vita_hw; // indicates whether h264_vita hw decoder is in use
    } FFmpegDecoderContext;

    int ffmpeg_decoder_init(FFmpegDecoderContext* ctx);
    void ffmpeg_decoder_destroy(FFmpegDecoderContext* ctx);
    int ffmpeg_decoder_decode(FFmpegDecoderContext* ctx, const uint8_t* data, int size);

#ifdef __cplusplus
}
#endif
