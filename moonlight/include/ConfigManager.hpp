/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "../../third_party/moonmic/include/moonmic.h"
#include "controller/input_types.hpp"
#include "controller/special_inputs.hpp"
#include "controller/touchscreen_modes.hpp"

struct StreamConfiguration
{
    int width              = 1280; // Moonmic host monitor target
    int height             = 720;
    int streamWidth        = 960;
    int streamHeight       = 544; // Encoded stream resolution
    int fps                = 60;
    int bitrate            = -1; // Automatic bitrate
    int packetSize         = 1024;
    int streamingRemotely  = 0;
    int audioConfiguration = 2; // AUDIO_CONFIGURATION_STEREO
};

struct RearTouchSettings
{
    bool enabled                  = true;
    int top                       = 0;
    int right                     = 0;
    int bottom                    = 0;
    int left                      = 0;
    std::uint32_t actionNorthWest = controller::INPUT_TYPE_ANALOG | controller::ANALOG_LEFT_TRIGGER;
    std::uint32_t actionNorthEast = controller::INPUT_TYPE_ANALOG | controller::ANALOG_RIGHT_TRIGGER;
    std::uint32_t actionSouthWest = controller::INPUT_TYPE_GAMEPAD | controller::GAMEPAD_FLAG_LS;
    std::uint32_t actionSouthEast = controller::INPUT_TYPE_GAMEPAD | controller::GAMEPAD_FLAG_RS;
};

enum GamepadType : int
{
    GAMEPAD_TYPE_XBOX = 0,
    GAMEPAD_TYPE_PS4  = 1,
};

struct VideoSettings
{
    bool sops                         = true;
    bool localaudio                   = false;
    bool fullscreen                   = true;
    bool enable_frame_pacer           = true;
    bool keep_awake_while_streaming   = true;
    float settings_background_opacity = 1.0f;
    bool center_region_only           = false;
    bool show_fps                     = false;
    bool save_debug_log               = false;
    bool enable_motion_controls       = false;
    bool enable_double_tap_sprint     = false;
    bool enable_network_optimizations = true;
    int touchscreen_mode              = TOUCHSCREEN_MODE_TRACKPAD;
    float motion_controls_scalar_x    = 1.2f;
    float motion_controls_scalar_y    = 0.8f;
    int keyboard_layout               = 0; // EN_US, ES_ES, ES_LATAM
    int keyboard_mode                 = 0; // SCE IME or Borealis overlay
    bool keyboard_input_mode          = false; // VK codes or UTF-8 text
    bool keyboard_numbers_row         = true;
    bool keyboard_show_arrows         = true;
    bool keyboard_legacy_dialog       = false;
    bool keyboard_fullscreen          = false;
    bool keyboard_text_preview        = true;
    bool keyboard_mouse_passthrough   = false;
    bool capture_ps_button            = true;

    int trackpad_pointer_speed           = 100; // 0-200
    int trackpad_dead_zone               = 50; // 0-200px
    bool trackpad_two_finger_right_click = true;
    bool trackpad_two_finger_scroll      = true;
    bool trackpad_invert_scroll          = false;
    bool trackpad_multi_touch            = true;
    int trackpad_edge_zone               = 15; // 0-50%

    int render_mode       = 0; // Legacy or FFmpeg
    int pixel_format_mode = 1; // RGBA or YUV420

    GamepadType gamepad_type = GAMEPAD_TYPE_XBOX;

    bool swap_shoulder_buttons = false;

    // Immediate, VBlank, or next-frame presentation.
    int swap_interval = 2;

    bool enable_front_touchzones         = false;
    int front_touch_offset               = 0; // px from screen edge
    int front_touch_size                 = 150; // px square zone size
    std::uint32_t front_action_northwest = controller::INPUT_TYPE_SPECIAL | controller::INPUT_SPECIAL_KEY_PAUSE;
    std::uint32_t front_action_northeast = 0; // None by default
    std::uint32_t front_action_southwest = controller::INPUT_TYPE_GAMEPAD | controller::GAMEPAD_FLAG_SPECIAL;
    std::uint32_t front_action_southeast = 0; // None by default

    bool enable_microphone             = false;
    bool enable_microphone_compression = false;
    std::string microphone_host_ip     = "";
    int microphone_port                = MOONMIC_DEFAULT_PORT;
    int microphone_sample_rate         = MOONMIC_DEFAULT_SAMPLE_RATE;
    int microphone_channels            = MOONMIC_DEFAULT_CHANNELS;
    int microphone_bitrate             = MOONMIC_DEFAULT_BITRATE;
    float microphone_gain              = 10.0f;

    RearTouchSettings rear_touch;
};

class ConfigManager
{
  public:
    ConfigManager();
    bool load();
    bool save() const;
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    static std::string getConfigPath();

    std::string getKeysDir() const;
    void setKeysDir(const std::string& dir);

    bool ensureDirExists(const std::string& path) const;
    bool ensureKeysDirExists() const;
    bool ensureKeyDirExists(const std::string& safeId) const;

    StreamConfiguration getStreamConfig() const;
    void setStreamConfig(const StreamConfiguration& config);
    VideoSettings getVideoSettings() const;
    void setVideoSettings(const VideoSettings& settings);
    bool updateStreamConfig(const std::function<void(StreamConfiguration&)>& update);
    bool updateVideoSettings(const std::function<void(VideoSettings&)>& update);

  private:
    static std::recursive_mutex fileMutex;
    std::map<std::string, std::map<std::string, std::string>> data;
    mutable std::string cachedKeysDir;
    mutable bool keysDirLoaded = false;
};
