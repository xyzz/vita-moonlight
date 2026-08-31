#include "controller/audio.hpp"

#include <opus/opus_multistream.h>
#include <psp2/audioout.h>

#include <algorithm>
#include <cstring>

#include "debug.hpp"

namespace controller
{

namespace
{
    constexpr int VITA_AUDIO_INIT_OK        = 0;
    constexpr int VITA_AUDIO_ERROR_BAD_OPUS = 0x80020001;
    constexpr int VITA_AUDIO_ERROR_PORT     = 0x80020002;

    constexpr int DEFAULT_FRAME_SIZE  = 240;
    constexpr int MAX_OPUS_FRAME_SIZE = 5760;
    constexpr int VITA_SAMPLES        = 960;
    constexpr int CHANNEL_COUNT       = 2;
    constexpr int SAMPLE_RATE         = 48000;
    constexpr size_t BUFFER_SIZE      = static_cast<size_t>(VITA_SAMPLES) * CHANNEL_COUNT;

    OpusMSDecoder* g_decoder                                  = nullptr;
    short g_buffer[BUFFER_SIZE]                               = { 0 };
    short g_decodeBuffer[MAX_OPUS_FRAME_SIZE * CHANNEL_COUNT] = { 0 };
    int g_decodeOffset                                        = 0;
    int g_channelCount                                        = CHANNEL_COUNT;
    int g_frameSize                                           = DEFAULT_FRAME_SIZE;
    int g_port                                                = -1;
    bool g_active                                             = false;

    void destroy_decoder()
    {
        if (g_decoder != nullptr)
        {
            opus_multistream_decoder_destroy(g_decoder);
            g_decoder = nullptr;
        }
    }

    void release_port()
    {
        if (g_port >= 0)
        {
            sceAudioOutReleasePort(g_port);
            g_port = -1;
        }
    }

} // namespace

int audio_init(int /*audioConfiguration*/, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* /*audioContext*/, int /*arFlags*/)
{
    g_active = false;
    destroy_decoder();
    release_port();
    g_decodeOffset = 0;
    std::memset(g_buffer, 0, sizeof(g_buffer));
    std::memset(g_decodeBuffer, 0, sizeof(g_decodeBuffer));

    if (opusConfig == nullptr)
    {
        vita_log::info("[Audio] Opus config nulo");
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }
    if (opusConfig->sampleRate != SAMPLE_RATE || opusConfig->channelCount < 1 || opusConfig->channelCount > CHANNEL_COUNT)
    {
        vita_log::error("[Audio] Configuración Opus no soportada rate=%d channels=%d",
            opusConfig->sampleRate, opusConfig->channelCount);
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    g_channelCount = opusConfig->channelCount;
    g_frameSize    = opusConfig->samplesPerFrame > 0 && opusConfig->samplesPerFrame <= MAX_OPUS_FRAME_SIZE
           ? opusConfig->samplesPerFrame
           : DEFAULT_FRAME_SIZE;

    int opusStatus = OPUS_OK;
    g_decoder      = opus_multistream_decoder_create(opusConfig->sampleRate,
             opusConfig->channelCount,
             opusConfig->streams,
             opusConfig->coupledStreams,
             opusConfig->mapping,
             &opusStatus);
    if (opusStatus < 0 || g_decoder == nullptr)
    {
        vita_log::error("[Audio] opus_multistream_decoder_create fallo=%d", opusStatus);
        destroy_decoder();
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, VITA_SAMPLES, SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_port < 0)
    {
        vita_log::error("[Audio] sceAudioOutOpenPort fallo=0x%X", g_port);
        destroy_decoder();
        return VITA_AUDIO_ERROR_PORT;
    }

    int volumes[CHANNEL_COUNT] = { SCE_AUDIO_OUT_MAX_VOL, SCE_AUDIO_OUT_MAX_VOL };
    int volumeResult           = sceAudioOutSetVolume(
        g_port,
        static_cast<SceAudioOutChannelFlag>(
            SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH),
        volumes);
    if (volumeResult < 0)
    {
        vita_log::warning("[Audio] sceAudioOutSetVolume fallo=0x%X", volumeResult);
    }

    vita_log::info("[Audio] Puerto abierto id=0x%X channels=%d streams=%d coupled=%d frame_samples=%d",
        g_port, g_channelCount, opusConfig->streams,
        opusConfig->coupledStreams, g_frameSize);
    return VITA_AUDIO_INIT_OK;
}

void audio_start()
{
    g_active = true;
}

void audio_stop()
{
    g_active = false;
}

void audio_cleanup()
{
    g_active = false;
    destroy_decoder();
    release_port();
    g_decodeOffset = 0;
    g_channelCount = CHANNEL_COUNT;
    g_frameSize    = DEFAULT_FRAME_SIZE;
    std::memset(g_buffer, 0, sizeof(g_buffer));
    std::memset(g_decodeBuffer, 0, sizeof(g_decodeBuffer));
}

void audio_decode_and_play_sample(char* data, int length)
{
    if (g_decoder == nullptr || length < 0)
    {
        return;
    }

    bool plc    = data == nullptr || length == 0;
    int decoded = opus_multistream_decode(g_decoder,
        plc ? nullptr : reinterpret_cast<unsigned char*>(data),
        plc ? 0 : length,
        g_decodeBuffer,
        plc ? g_frameSize : MAX_OPUS_FRAME_SIZE,
        0);

    if (decoded < 0)
    {
        vita_log::error("[Audio] Error opus decode=%d", decoded);
        return;
    }
    if (decoded == 0)
    {
        return;
    }
    if (!g_active || g_port < 0)
    {
        g_decodeOffset = 0;
        return;
    }

    int sourceOffset = 0;
    while (sourceOffset < decoded)
    {
        int copySamples = std::min(decoded - sourceOffset, VITA_SAMPLES - g_decodeOffset);
        for (int i = 0; i < copySamples; i++)
        {
            int sourceIndex           = (sourceOffset + i) * g_channelCount;
            short left                = g_decodeBuffer[sourceIndex];
            short right               = g_channelCount == 2 ? g_decodeBuffer[sourceIndex + 1] : left;
            int outputIndex           = (g_decodeOffset + i) * CHANNEL_COUNT;
            g_buffer[outputIndex]     = left;
            g_buffer[outputIndex + 1] = right;
        }

        sourceOffset += copySamples;
        g_decodeOffset += copySamples;
        if (g_decodeOffset == VITA_SAMPLES)
        {
            int res = sceAudioOutOutput(g_port, g_buffer);
            if (res < 0)
            {
                vita_log::error("[Audio] sceAudioOutOutput fallo=0x%X", res);
            }
            g_decodeOffset = 0;
        }
    }
}

} // namespace controller
