/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "../../third_party/moonmic/moonmic.h" // For MOONMIC_DEFAULT_* constants
#include "controller/input_types.hpp"
#include "controller/special_inputs.hpp"

// Streaming configuration structure (similar to legacy)
struct StreamConfiguration
{
    int width                 = 1280;
    int height                = 720;
    int fps                   = 60;
    int bitrate               = -1; // Automatic bitrate
    int packetSize            = 1024;
    int streamingRemotely     = 0;
    int audioConfiguration    = 2; // AUDIO_CONFIGURATION_STEREO
    int supportedVideoFormats = 1; // VIDEO_FORMAT_H264

    // Method to validate and adjust resolution for PS Vita
    void validateAndAdjustResolution()
    {
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
        // Apply PS Vita restrictions: multiples of 16, minimum 64
        width  = (width < 64) ? 64 : ((width + 15) / 16) * 16;
        height = (height < 64) ? 64 : ((height + 15) / 16) * 16;

        // Limit to reasonable resolutions for PS Vita
        if (width > 1920)
            width = 1920;
        if (height > 1080)
            height = 1080;
#endif
    }
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
    GAMEPAD_TYPE_XBOX = 0, // Emular Xbox 360 (A/B/X/Y, LB/RB)
    GAMEPAD_TYPE_PS4  = 1, // Emular PS4 (Cross/Circle/Square/Triangle, L1/R1)
};

struct VideoSettings
{
    bool sops                          = true; // Stream Optimization
    bool localaudio                    = false;
    bool fullscreen                    = true;
    bool enable_frame_pacer            = true;
    bool center_region_only            = false;
    bool show_fps                      = false;
    bool save_debug_log                = false;
    bool enable_ref_frame_invalidation = false;
    bool enable_vita_vblank_wait       = false;
    bool enable_motion_controls        = false;
    bool enable_double_tap_sprint      = false;
    bool absolute_mouse                = false;
    bool enable_network_optimizations  = true; // Network optimizations (IDR smart, pacing, etc.)
    int touchscreen_mode               = 1; // 0=Off, 1=Trackpad relativo, 2=DS4 Touchpad, 3=Mouse Absoluto, 4=Tableta Multitouch
    int double_tap_sprint_step_time    = 200;
    float motion_controls_scalar_x     = 1.2f;
    float motion_controls_scalar_y     = 0.8f;
    int mouse_acceleration             = 150;
    int keyboard_layout                = 0; // 0=EN_US, 1=ES_ES, 2=ES_LATAM, 3=FR, 4=DE, 5=IT, 6=PT_BR, 7=UK
    int keyboard_mode                  = 0; // 0=Legacy (SCE IME), 1=Modern (Borealis overlay)
    bool keyboard_input_mode           = false; // false=VK (default), true=force UTF-8 text input
    bool keyboard_numbers_row          = true; // Modern: show numeric row
    bool keyboard_show_arrows          = true; // Modern: show arrow keys

    int trackpad_pointer_speed           = 100; // 0-200
    int trackpad_dead_zone               = 50; // 0-200px
    bool trackpad_tap_to_click           = true;
    bool trackpad_two_finger_right_click = true;
    bool trackpad_two_finger_scroll      = true;
    bool trackpad_invert_scroll          = false;
    bool trackpad_multi_touch            = true;
    int trackpad_edge_zone               = 15; // 0-50%

    int render_mode       = 0; // 0=Legacy, 1=Modern (FFmpeg)
    int pixel_format_mode = 1; // 0=RGBA direct, 1=YUV420 (testing)

    GamepadType gamepad_type = GAMEPAD_TYPE_XBOX; // 0=Xbox, 1=PS4 (to emulate controller)

    bool swap_shoulder_buttons = false;

    // Swap interval (V-Sync): 0 = Sin V-Sync, 1 = 60 FPS, 2 = 30 FPS, etc.
    int swap_interval = 0;

    bool enable_front_touchzones         = true;
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

    // --- Key/device directory management ---
    std::string getKeysDir() const;
    void setKeysDir(const std::string& dir);

    // Secure helpers to create/secure key-related directories
    // Ensure that the route exists (mkdir -p behavior). Returns true if
    // the route exists after the call or false on error.
    bool ensureDirExists(const std::string& path) const;
    bool ensureKeysDirExists() const;
    bool ensureKeyDirExists(const std::string& safeId) const;

    // --- Streaming configuration (legacy mode) ---
    StreamConfiguration getStreamConfig() const;
    void setStreamConfig(const StreamConfiguration& config);
    VideoSettings getVideoSettings() const;
    void setVideoSettings(const VideoSettings& settings);

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data;
    mutable std::string cachedKeysDir;
    mutable bool keysDirLoaded = false;
};
