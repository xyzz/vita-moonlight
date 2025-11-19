#include "controller/audio.hpp"

#include <opus/opus_multistream.h>
#include <psp2/audioout.h>

#include <cstring>

#include "debug.hpp"

namespace controller
{

namespace
{
    constexpr int VITA_AUDIO_INIT_OK        = 0;
    constexpr int VITA_AUDIO_ERROR_BAD_OPUS = 0x80020001;
    constexpr int VITA_AUDIO_ERROR_PORT     = 0x80020002;

    constexpr int FRAME_SIZE     = 240;
    constexpr int VITA_SAMPLES   = 960;
    constexpr int CHANNEL_COUNT  = 2;
    constexpr int SAMPLE_RATE    = 48000;
    constexpr size_t BUFFER_SIZE = static_cast<size_t>(VITA_SAMPLES) * CHANNEL_COUNT;

    OpusMSDecoder* g_decoder    = nullptr;
    short g_buffer[BUFFER_SIZE] = { 0 };
    int g_decodeOffset          = 0;
    int g_port                  = -1;
    bool g_active               = false;

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
    destroy_decoder();
    release_port();
    g_decodeOffset = 0;
    std::memset(g_buffer, 0, sizeof(g_buffer));

    if (opusConfig == nullptr)
    {
        vita_debug_log("[Audio] Opus config nulo");
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    int opusStatus = OPUS_OK;
    g_decoder      = opus_multistream_decoder_create(opusConfig->sampleRate,
             opusConfig->channelCount,
             opusConfig->streams,
             opusConfig->coupledStreams,
             opusConfig->mapping,
             &opusStatus);
    if (opusStatus < 0 || g_decoder == nullptr)
    {
        vita_debug_log("[Audio] opus_multistream_decoder_create fallo=%d", opusStatus);
        destroy_decoder();
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, VITA_SAMPLES, SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_port < 0)
    {
        vita_debug_log("[Audio] sceAudioOutOpenPort fallo=0x%X", g_port);
        destroy_decoder();
        return VITA_AUDIO_ERROR_PORT;
    }

    vita_debug_log("[Audio] Puerto abierto id=0x%X", g_port);
    g_active = true;
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
}

void audio_decode_and_play_sample(char* data, int length)
{
    if (g_decoder == nullptr || data == nullptr || length <= 0)
    {
        return;
    }

    opus_int16* writePtr = g_buffer + (g_decodeOffset * CHANNEL_COUNT);
    int decoded          = opus_multistream_decode(g_decoder,
                 reinterpret_cast<unsigned char*>(data),
                 length,
                 writePtr,
                 FRAME_SIZE,
                 0);

    if (decoded <= 0)
    {
        vita_debug_log("[Audio] Error opus decode=%d", decoded);
        return;
    }

    if (decoded != FRAME_SIZE)
    {
        // Opus entregó menos samples de lo esperado; para mantener el timing, ignoramos este fragmento.
        vita_debug_log("[Audio] Decode parcial=%d", decoded);
        return;
    }

    g_decodeOffset += decoded;
    if (g_decodeOffset >= VITA_SAMPLES)
    {
        g_decodeOffset = 0;
        if (g_active && g_port >= 0)
        {
            int res = sceAudioOutOutput(g_port, g_buffer);
            if (res < 0)
            {
                vita_debug_log("[Audio] sceAudioOutOutput fallo=0x%X", res);
            }
        }
    }
}

} // namespace controller
