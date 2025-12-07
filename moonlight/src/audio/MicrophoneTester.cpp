/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "MicrophoneTester.hpp"

#include <borealis.hpp>

#ifdef __vita__
#include <malloc.h>
#include <opus/opus.h>
#include <psp2/audioin.h>
#include <psp2/audioout.h>

#include <cstring>
#endif

MicrophoneTester& MicrophoneTester::getInstance()
{
    static MicrophoneTester instance;
    return instance;
}

MicrophoneTester::MicrophoneTester()
{
    brls::Logger::info("[MicrophoneTester] Initialized");
}

MicrophoneTester::~MicrophoneTester()
{
    stop();
    brls::Logger::info("[MicrophoneTester] Destroyed");
}

bool MicrophoneTester::start()
{
#ifndef __vita__
    brls::Logger::warning("[MicrophoneTester] Not supported on this platform");
    return false;
#else
    if (running_)
    {
        brls::Logger::warning("[MicrophoneTester] Already running");
        return true;
    }

    brls::Logger::info("[MicrophoneTester] Starting loopback test");

    running_         = true;
    loopback_thread_ = std::thread(&MicrophoneTester::loopbackThreadFunc, this);

    return true;
#endif
}

void MicrophoneTester::stop()
{
    if (!running_)
    {
        return;
    }

    brls::Logger::info("[MicrophoneTester] Stopping loopback test");

    running_ = false;

    if (loopback_thread_.joinable())
    {
        loopback_thread_.join();
    }

    brls::Logger::info("[MicrophoneTester] Loopback test stopped");
}

bool MicrophoneTester::isRunning() const
{
    return running_;
}

void MicrophoneTester::setOpusMode(bool enabled)
{
    use_opus_ = enabled;
    brls::Logger::info("[MicrophoneTester] Opus mode: {}", enabled ? "ENABLED" : "DISABLED");
}

void MicrophoneTester::setGain(float gain)
{
    if (gain < 1.0f)
        gain = 1.0f;
    if (gain > 50.0f)
        gain = 50.0f;

    gain_ = gain;
    brls::Logger::info("[MicrophoneTester] Gain set to: {:.1f}x", gain);
}

#ifdef __vita__
void MicrophoneTester::loopbackThreadFunc()
{
    brls::Logger::info("[MicrophoneTester] Loopback thread started");

    struct SampleRateTest
    {
        int rate;
        int grain;
        const char* name;
    };

    SampleRateTest tests[] = {
        { 48000, 768, "48kHz (max quality)" },
        { 24000, 384, "24kHz (half)" },
        { 16000, 256, "16kHz (basic)" }
    };

    int SAMPLE_RATE = 16000; // Default fallback
    int GRAIN       = 256;

    brls::Logger::info("[MicrophoneTester] Testing microphone sample rates...");

    for (const auto& test : tests)
    {
        brls::Logger::info("[MicrophoneTester] Testing: {} ({} samples)", test.name, test.grain);

        int test_port = sceAudioInOpenPort(
            SCE_AUDIO_IN_PORT_TYPE_VOICE,
            test.grain,
            test.rate,
            SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);

        if (test_port >= 0)
        {
            sceAudioInReleasePort(test_port);
            SAMPLE_RATE = test.rate;
            GRAIN       = test.grain;
            brls::Logger::info("[MicrophoneTester] ✓ {} SUPPORTED - using this", test.name);
            break;
        }
        else
        {
            brls::Logger::warning("[MicrophoneTester] ✗ {} failed (error: 0x{:08X})",
                test.name, test_port);
        }
    }

    brls::Logger::info("[MicrophoneTester] Selected: {}Hz, grain={} samples ({:.1f}ms)",
        SAMPLE_RATE, GRAIN, (float)GRAIN * 1000.0f / SAMPLE_RATE);

    int in_port = sceAudioInOpenPort(
        SCE_AUDIO_IN_PORT_TYPE_VOICE,
        GRAIN,
        SAMPLE_RATE,
        SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);

    if (in_port < 0)
    {
        brls::Logger::error("[MicrophoneTester] Failed to open audio input: 0x{:08X}", in_port);
        running_ = false;
        return;
    }

    brls::Logger::info("[MicrophoneTester] Audio input opened: port={}", in_port);

    int out_port = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_BGM, // Use BGM port instead of MAIN
        GRAIN,
        SAMPLE_RATE,
        SCE_AUDIO_OUT_MODE_STEREO);

    if (out_port < 0)
    {
        brls::Logger::error("[MicrophoneTester] Failed to open audio output: 0x{:08X}", out_port);
        sceAudioInReleasePort(in_port);
        running_ = false;
        return;
    }

    brls::Logger::info("[MicrophoneTester] Audio output opened: port={}", out_port);

    int16_t* input_buffer  = (int16_t*)memalign(256, GRAIN * sizeof(int16_t));
    int16_t* output_buffer = (int16_t*)memalign(256, GRAIN * 2 * sizeof(int16_t)); // Stereo = 2 channels

    if (!input_buffer || !output_buffer)
    {
        brls::Logger::error("[MicrophoneTester] Failed to allocate buffers");
        if (input_buffer)
            free(input_buffer);
        if (output_buffer)
            free(output_buffer);
        sceAudioOutReleasePort(out_port);
        sceAudioInReleasePort(in_port);
        running_ = false;
        return;
    }

    memset(input_buffer, 0, GRAIN * sizeof(int16_t));
    memset(output_buffer, 0, GRAIN * 2 * sizeof(int16_t));

    brls::Logger::info("[MicrophoneTester] Audio config: {}Hz, grain={} samples ({:.1f}ms)",
        SAMPLE_RATE, GRAIN, (float)GRAIN * 1000.0f / SAMPLE_RATE);

    OpusEncoder* encoder       = nullptr;
    OpusDecoder* decoder       = nullptr;
    unsigned char* opus_packet = nullptr;
    int16_t* opus_decoded      = nullptr;
    int16_t* opus_input_padded = nullptr; // Padded input for Opus (needs 20ms frames)

    const int OPUS_FRAME_SIZE = (SAMPLE_RATE == 48000) ? 960 : 320; // 20ms @ 48kHz or 16kHz

    if (use_opus_)
    {
        brls::Logger::info("[MicrophoneTester] Opus mode: creating encoder/decoder");
        brls::Logger::info("[MicrophoneTester] Opus frame size: {} samples (20ms @ {}Hz)",
            OPUS_FRAME_SIZE, SAMPLE_RATE);

        int error = 0;
        encoder   = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_AUDIO, &error);
        if (error != OPUS_OK || !encoder)
        {
            brls::Logger::error("[MicrophoneTester] Failed to create Opus encoder: {}", error);
            free(output_buffer);
            free(input_buffer);
            sceAudioOutReleasePort(out_port);
            sceAudioInReleasePort(in_port);
            running_ = false;
            return;
        }

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(96000)); // 96kbps
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10)); // Max quality
        opus_encoder_ctl(encoder, OPUS_SET_VBR(1)); // Variable bitrate
        opus_encoder_ctl(encoder, OPUS_SET_DTX(0)); // No discontinuous transmission

        brls::Logger::info("[MicrophoneTester] Opus encoder configured: 96kbps, complexity=10, VBR");

        decoder = opus_decoder_create(SAMPLE_RATE, 1, &error);
        if (error != OPUS_OK || !decoder)
        {
            brls::Logger::error("[MicrophoneTester] Failed to create Opus decoder: {}", error);
            opus_encoder_destroy(encoder);
            free(output_buffer);
            free(input_buffer);
            sceAudioOutReleasePort(out_port);
            sceAudioInReleasePort(in_port);
            running_ = false;
            return;
        }

        opus_packet  = (unsigned char*)malloc(4000);
        opus_decoded = (int16_t*)memalign(256, OPUS_FRAME_SIZE * sizeof(int16_t));

        if (GRAIN < OPUS_FRAME_SIZE)
        {
            opus_input_padded = (int16_t*)memalign(256, OPUS_FRAME_SIZE * sizeof(int16_t));
            memset(opus_input_padded, 0, OPUS_FRAME_SIZE * sizeof(int16_t));
            brls::Logger::info("[MicrophoneTester] Using padding: grain {} -> Opus frame {}",
                GRAIN, OPUS_FRAME_SIZE);
        }

        if (!opus_packet || !opus_decoded || (GRAIN < OPUS_FRAME_SIZE && !opus_input_padded))
        {
            brls::Logger::error("[MicrophoneTester] Failed to allocate Opus buffers");
            if (opus_input_padded)
                free(opus_input_padded);
            if (opus_packet)
                free(opus_packet);
            if (opus_decoded)
                free(opus_decoded);
            opus_decoder_destroy(decoder);
            opus_encoder_destroy(encoder);
            free(output_buffer);
            free(input_buffer);
            sceAudioOutReleasePort(out_port);
            sceAudioInReleasePort(in_port);
            running_ = false;
            return;
        }

        brls::Logger::info("[MicrophoneTester] Opus encoder/decoder created ({}Hz VOIP, bitrate=auto)",
            SAMPLE_RATE);
    }

    brls::Logger::info("[MicrophoneTester] Buffers allocated, starting loopback loop (mode: {})",
        use_opus_ ? "OPUS" : "RAW");

    while (running_)
    {
        int result = sceAudioInInput(in_port, input_buffer);
        if (result < 0)
        {
            brls::Logger::error("[MicrophoneTester] Audio input error: 0x{:08X}", result);
            break;
        }

        int16_t* audio_source = input_buffer;

        if (use_opus_ && encoder && decoder)
        {
            int16_t* encode_source = input_buffer;

            if (GRAIN < OPUS_FRAME_SIZE && opus_input_padded)
            {
                memcpy(opus_input_padded, input_buffer, GRAIN * sizeof(int16_t));
                memset(opus_input_padded + GRAIN, 0, (OPUS_FRAME_SIZE - GRAIN) * sizeof(int16_t));
                encode_source = opus_input_padded;
            }

            int encoded_bytes = opus_encode(encoder, encode_source, OPUS_FRAME_SIZE, opus_packet, 4000);
            if (encoded_bytes < 0)
            {
                brls::Logger::error("[MicrophoneTester] Opus encode error: {} (frame_size={}, rate={})",
                    encoded_bytes, OPUS_FRAME_SIZE, SAMPLE_RATE);
                break;
            }

            brls::Logger::debug("[MicrophoneTester] Encoded {} bytes", encoded_bytes);

            int decoded_samples = opus_decode(decoder, opus_packet, encoded_bytes, opus_decoded, OPUS_FRAME_SIZE, 0);
            if (decoded_samples < 0)
            {
                brls::Logger::error("[MicrophoneTester] Opus decode error: {}", decoded_samples);
                break;
            }

            audio_source = opus_decoded;
        }

        float current_gain = gain_.load();

        for (int i = 0; i < GRAIN; i++)
        {
            int32_t sample = static_cast<int32_t>(audio_source[i] * current_gain);

            if (sample > 32767)
                sample = 32767;
            if (sample < -32768)
                sample = -32768;

            int16_t amplified = static_cast<int16_t>(sample);

            output_buffer[i * 2]     = amplified;
            output_buffer[i * 2 + 1] = amplified;
        }

        result = sceAudioOutOutput(out_port, output_buffer);
        if (result < 0)
        {
            brls::Logger::error("[MicrophoneTester] Audio output error: 0x{:08X}", result);
            break;
        }
    }

    brls::Logger::info("[MicrophoneTester] Cleaning up resources");

    if (use_opus_)
    {
        if (opus_input_padded)
            free(opus_input_padded);
        if (opus_decoded)
            free(opus_decoded);
        if (opus_packet)
            free(opus_packet);
        if (decoder)
            opus_decoder_destroy(decoder);
        if (encoder)
            opus_encoder_destroy(encoder);
    }

    free(output_buffer);
    free(input_buffer);
    sceAudioOutReleasePort(out_port);
    sceAudioInReleasePort(in_port);

    running_ = false;

    brls::Logger::info("[MicrophoneTester] Loopback thread finished");
}
#endif
