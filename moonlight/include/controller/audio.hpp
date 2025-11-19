#pragma once

#include <Limelight.h>

namespace controller
{

int audio_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* audioContext, int arFlags);
void audio_start();
void audio_stop();
void audio_cleanup();
void audio_decode_and_play_sample(char* data, int length);

} // namespace controller
