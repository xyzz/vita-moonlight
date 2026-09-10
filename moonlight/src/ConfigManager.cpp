/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "ConfigManager.hpp"

#include <errno.h>
#include <ini.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "debug.hpp"
#include "session/stream_config.hpp"

namespace
{

constexpr int TOUCHSCREEN_MODE_SCHEMA = 2;

int parseInt(const std::string& value, int fallback)
{
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

float parseFloat(const std::string& value, float fallback)
{
    try
    {
        return std::stof(value);
    }
    catch (...)
    {
        return fallback;
    }
}

int migrateLegacyTouchscreenMode(int mode)
{
    switch (mode)
    {
        case 0:
            return TOUCHSCREEN_MODE_OFF;
        case 1:
            return TOUCHSCREEN_MODE_DS4_TOUCHPAD;
        case 2:
            return TOUCHSCREEN_MODE_MOUSE_ABSOLUTE;
        case 3:
        case 4:
            return TOUCHSCREEN_MODE_TABLET;
        default:
            return TOUCHSCREEN_MODE_TRACKPAD;
    }
}

}

// Out-of-class definition for static std::string ConfigManager::getConfigPath();
#ifdef _WIN32
#include <windows.h>
#endif

std::string ConfigManager::getConfigPath()
{
#ifdef _WIN32
    // In the same folder as the exe
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    return path + "moonlight-vita.conf";
#elif defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    return "ux0:data/moonlight/moonlight-vita.conf";
#else
    return "moonlight-vita.conf";
#endif
}

// Private Helper: create directory recursively (mkdir -p)
static bool config_mkdir_recursive(const std::string& path, mode_t mode = 0777)
{
    if (path.empty())
        return false;
    std::string cur;
    size_t i = 0;
    if (path[0] == '/')
    {
        cur = "/";
        i   = 1;
    }
    while (i <= path.size())
    {
        if (i == path.size() || path[i] == '/')
        {
            std::string sub = path.substr(0, i);
            if (sub.empty())
            {
                i++;
                continue;
            }
            struct stat st {};
            if (stat(sub.c_str(), &st) != 0)
            {
                if (mkdir(sub.c_str(), mode) != 0)
                {
                    if (errno == EEXIST)
                    { /* ok */
                    }
                    else
                        return false;
                }
            }
            else if (!S_ISDIR(st.st_mode))
            {
                return false;
            }
        }
        i++;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    if (mkdir(path.c_str(), mode) == 0)
        return true;
    if (errno == EEXIST)
        return true;
    return false;
}

bool ConfigManager::ensureDirExists(const std::string& path) const
{
    if (path.empty())
        return false;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    bool ok = config_mkdir_recursive(path);
    if (!ok)
    {
        std::cerr << "[ConfigManager] ensureDirExists fallo en '" << path << "' errno=" << errno << " (" << strerror(errno) << ")\n";
    }
    return ok;
}

bool ConfigManager::ensureKeysDirExists() const
{
    std::string dir = getKeysDir();
    return ensureDirExists(dir);
}

bool ConfigManager::ensureKeyDirExists(const std::string& safeId) const
{
    if (safeId.empty())
        return ensureKeysDirExists();
    if (safeId == "." || safeId == ".." || safeId.find('/') != std::string::npos || safeId.find('\\') != std::string::npos)
        return false;
    std::string base = getKeysDir();
    std::string path = base + "/" + safeId;
    return ensureDirExists(path);
}

static int iniHandler(void* user, const char* section, const char* name, const char* value)
{
    auto* config = reinterpret_cast<ConfigManager*>(user);
    config->set(section, name, value);
    return 1;
}

ConfigManager::ConfigManager() { }

std::recursive_mutex ConfigManager::fileMutex;

// Gets the keys/devices directory, configurable by file or by code
std::string ConfigManager::getKeysDir() const
{
    if (keysDirLoaded)
        return cachedKeysDir;
    // 1. Try to read from the config
    std::string dir = get("general", "keys_dir", "");
    if (!dir.empty())
    {
        cachedKeysDir = dir;
        keysDirLoaded = true;
        return cachedKeysDir;
    }
    // 2. If not in config, use cross-platform default value
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    cachedKeysDir = "ux0:data/moonlight/devices";
#elif defined(_WIN32)
    cachedKeysDir = "moonlight/devices";
#else
    cachedKeysDir = "moonlight/devices";
#endif
    keysDirLoaded = true;
    return cachedKeysDir;
}

void ConfigManager::setKeysDir(const std::string& dir)
{
    set("general", "keys_dir", dir);
    cachedKeysDir = dir;
    keysDirLoaded = true;
}

bool ConfigManager::load()
{
    std::lock_guard<std::recursive_mutex> lock(fileMutex);
    data.clear();
    cachedKeysDir.clear();
    keysDirLoaded    = false;
    std::string path = getConfigPath();
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    vita_log::info("[ConfigManager] Cargando configuración desde: %s", path.c_str());
#else
    std::cout << "[ConfigManager] Cargando configuración desde: " << path << std::endl;
#endif
    int result = ini_parse(path.c_str(), iniHandler, this);
    if (result != 0)
    {
        std::cout << "[ConfigManager] Error al cargar el archivo o no existe." << std::endl;
        return false;
    }
    return true;
}

bool ConfigManager::save() const
{
    std::lock_guard<std::recursive_mutex> lock(fileMutex);
    std::string path     = getConfigPath();
    std::string tempPath = path + ".tmp";
    std::ofstream file(tempPath, std::ios::trunc);
    if (!file.is_open())
    {
        std::cout << "[ConfigManager] No se pudo abrir el archivo para guardar." << std::endl;
        return false;
    }
    for (const auto& sec : data)
    {
        file << "[" << sec.first << "]\n";
        for (const auto& kv : sec.second)
        {
            file << kv.first << "=" << kv.second << "\n";
        }
        file << "\n";
    }
    file.flush();
    if (!file.good())
    {
        file.close();
        std::remove(tempPath.c_str());
        return false;
    }
    file.close();
#ifdef _WIN32
    if (!MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
    if (std::rename(tempPath.c_str(), path.c_str()) != 0)
#endif
    {
        std::remove(tempPath.c_str());
        return false;
    }
    return true;
}

std::string ConfigManager::get(const std::string& section, const std::string& key, const std::string& def) const
{
    auto it = data.find(section);
    if (it != data.end())
    {
        auto it2 = it->second.find(key);
        if (it2 != it->second.end())
            return it2->second;
    }
    return def;
}

void ConfigManager::set(const std::string& section, const std::string& key, const std::string& value)
{
    data[section][key] = value;
}

StreamConfiguration ConfigManager::getStreamConfig() const
{
    StreamConfiguration config;
    config.width  = parseInt(get("stream", "width", "1280"), 1280);
    config.height = parseInt(get("stream", "height", "720"), 720);
    // Older builds rounded Moonmic host resolutions to decoder-aligned dimensions.
    if (config.width == 1152 && config.height == 656)
        config.height = 648;
    else if (config.width == 1280 && config.height == 544)
        config.height = 540;
    else if (config.width == 1360 && config.height == 768)
        config.width = 1366;
    else if (config.width == 1600 && config.height == 896)
        config.height = 900;

    config.streamWidth  = parseInt(get("stream", "stream_width", "960"), 960);
    config.streamHeight = parseInt(get("stream", "stream_height", "544"), 544);

    if (!stream::isSupportedStreamResolution(config.streamWidth, config.streamHeight))
    {
        config.streamWidth  = 960;
        config.streamHeight = 544;
    }
    config.fps                = parseInt(get("stream", "fps", "60"), 60);
    config.bitrate            = parseInt(get("stream", "bitrate", "-1"), -1);
    config.packetSize         = parseInt(get("stream", "packetsize", "1024"), 1024);
    config.streamingRemotely  = parseInt(get("stream", "streaming_remotely", "0"), 0);
    config.audioConfiguration = parseInt(get("stream", "audio_config", "2"), 2);
    return config;
}

void ConfigManager::setStreamConfig(const StreamConfiguration& config)
{
    set("stream", "width", std::to_string(config.width));
    set("stream", "height", std::to_string(config.height));
    set("stream", "stream_width", std::to_string(config.streamWidth));
    set("stream", "stream_height", std::to_string(config.streamHeight));
    set("stream", "fps", std::to_string(config.fps));
    set("stream", "bitrate", std::to_string(config.bitrate));
    set("stream", "packetsize", std::to_string(config.packetSize));
    set("stream", "streaming_remotely", std::to_string(config.streamingRemotely));
    set("stream", "audio_config", std::to_string(config.audioConfiguration));
}

bool ConfigManager::updateStreamConfig(const std::function<void(StreamConfiguration&)>& update)
{
    std::lock_guard<std::recursive_mutex> lock(fileMutex);
    load();
    StreamConfiguration config = getStreamConfig();
    update(config);
    setStreamConfig(config);
    return save();
}

VideoSettings ConfigManager::getVideoSettings() const
{
    VideoSettings settings;
    auto readUint = [this](const std::string& section, const std::string& key, std::uint32_t def) -> std::uint32_t
    {
        std::string value = get(section, key, "");
        if (value.empty())
        {
            return def;
        }
        try
        {
            return static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
        }
        catch (...)
        {
            return def;
        }
    };
    settings.sops                          = get("video", "sops", "true") == "true";
    settings.localaudio                    = get("video", "localaudio", "false") == "true";
    settings.fullscreen                    = get("video", "fullscreen", "true") == "true";
    settings.enable_frame_pacer            = get("video", "enable_frame_pacer", "true") == "true";
    settings.keep_awake_while_streaming    = get("video", "keep_awake_while_streaming", "true") == "true";
    settings.settings_background_opacity   = parseFloat(get("video", "settings_background_opacity", "1.0"), 1.0f);
    settings.settings_background_opacity   = std::clamp(settings.settings_background_opacity, 0.0f, 1.0f);
    settings.center_region_only            = get("video", "center_region_only", "false") == "true";
    settings.show_fps                      = get("video", "show_fps", "false") == "true";
    settings.save_debug_log                = get("video", "save_debug_log", "false") == "true";
    settings.enable_microphone             = get("video", "enable_microphone", "false") == "true";
    settings.microphone_gain               = parseFloat(get("video", "microphone_gain", "10.0"), 10.0f);
    settings.enable_microphone_compression = get("video", "enable_microphone_compression", "false") == "true";
    settings.enable_motion_controls        = get("video", "enable_motion_controls", "false") == "true";
    settings.enable_double_tap_sprint      = get("video", "enable_double_tap_sprint", "false") == "true";
    const bool legacyAbsoluteMouse         = get("video", "absolute_mouse", "false") == "true";
    const std::string touchscreenStr       = get("video", "touchscreen_mode", "");
    const std::string touchscreenSchema    = get("video", "touchscreen_mode_schema", "");
    const bool hasTrackpadConfiguration    = !get("trackpad", "pointer_speed", "").empty();
    const bool usesCurrentTouchscreenIds   = touchscreenSchema == std::to_string(TOUCHSCREEN_MODE_SCHEMA) || hasTrackpadConfiguration;

    if (touchscreenSchema != std::to_string(TOUCHSCREEN_MODE_SCHEMA) && legacyAbsoluteMouse)
    {
        settings.touchscreen_mode = TOUCHSCREEN_MODE_MOUSE_ABSOLUTE;
    }
    else if (touchscreenStr.empty())
    {
        settings.touchscreen_mode = TOUCHSCREEN_MODE_TRACKPAD;
    }
    else if (touchscreenStr == "true")
    {
        settings.touchscreen_mode = TOUCHSCREEN_MODE_MOUSE_ABSOLUTE;
    }
    else if (touchscreenStr == "false")
    {
        settings.touchscreen_mode = TOUCHSCREEN_MODE_OFF;
    }
    else
    {
        const int storedMode      = parseInt(touchscreenStr, TOUCHSCREEN_MODE_TRACKPAD);
        settings.touchscreen_mode = usesCurrentTouchscreenIds ? storedMode : migrateLegacyTouchscreenMode(storedMode);
    }
    settings.touchscreen_mode             = std::clamp(settings.touchscreen_mode, static_cast<int>(TOUCHSCREEN_MODE_OFF), static_cast<int>(TOUCHSCREEN_MODE_TABLET));
    settings.enable_network_optimizations = get("video", "enable_network_optimizations", "true") == "true";
    settings.motion_controls_scalar_x     = parseFloat(get("video", "motion_controls_scalar_x", "1.2"), 1.2f);
    settings.motion_controls_scalar_y     = parseFloat(get("video", "motion_controls_scalar_y", "0.8"), 0.8f);
    settings.keyboard_layout              = parseInt(get("video", "keyboard_layout", "0"), 0);
    settings.keyboard_mode                = parseInt(get("video", "keyboard_mode", "0"), 0);
    settings.keyboard_input_mode          = get("video", "keyboard_input_mode", "false") == "true";
    settings.keyboard_numbers_row         = get("video", "keyboard_numbers_row", "true") == "true";
    settings.keyboard_show_arrows         = get("video", "keyboard_show_arrows", "true") == "true";
    settings.keyboard_legacy_dialog       = get("video", "keyboard_legacy_dialog", "false") == "true";
    settings.keyboard_fullscreen          = get("video", "keyboard_fullscreen", "false") == "true";
    settings.keyboard_text_preview        = get("video", "keyboard_text_preview", "true") == "true";
    settings.keyboard_mouse_passthrough   = get("video", "keyboard_mouse_passthrough", "false") == "true";
    settings.capture_ps_button            = get("video", "capture_ps_button", "true") == "true";
    settings.render_mode                  = parseInt(get("video", "render_mode", "0"), 0);
    settings.pixel_format_mode            = parseInt(get("video", "pixel_format_mode", "1"), 1);
    settings.gamepad_type                 = static_cast<GamepadType>(parseInt(get("video", "gamepad_type", "0"), 0));
    settings.swap_shoulder_buttons        = get("video", "swap_shoulder_buttons", "false") == "true";
    settings.swap_interval                = parseInt(get("video", "swap_interval", "1"), 1);
    // Trackpad Settings
    settings.trackpad_pointer_speed          = parseInt(get("trackpad", "pointer_speed", "100"), 100);
    settings.trackpad_dead_zone              = parseInt(get("trackpad", "dead_zone", "50"), 50);
    settings.trackpad_two_finger_right_click = get("trackpad", "two_finger_right_click", "true") == "true";
    settings.trackpad_two_finger_scroll      = get("trackpad", "two_finger_scroll", "true") == "true";
    settings.trackpad_invert_scroll          = get("trackpad", "invert_scroll", "false") == "true";
    settings.trackpad_multi_touch            = get("trackpad", "multi_touch", "true") == "true";
    settings.trackpad_edge_zone              = parseInt(get("trackpad", "edge_zone", "15"), 15);

    auto readMargin = [this](const std::string& key, int fallback) -> int
    {
        std::string value = get("rear_touch", key, "");
        if (value.empty())
            return fallback;
        return parseInt(value, fallback);
    };

    settings.rear_touch.enabled = get("rear_touch", "enabled", "true") == "true";
    settings.rear_touch.top     = readMargin("top", settings.rear_touch.top);
    settings.rear_touch.right   = readMargin("right", settings.rear_touch.right);
    settings.rear_touch.bottom  = readMargin("bottom", settings.rear_touch.bottom);
    settings.rear_touch.left    = readMargin("left", settings.rear_touch.left);

    settings.rear_touch.top    = std::max(0, settings.rear_touch.top);
    settings.rear_touch.right  = std::max(0, settings.rear_touch.right);
    settings.rear_touch.bottom = std::max(0, settings.rear_touch.bottom);
    settings.rear_touch.left   = std::max(0, settings.rear_touch.left);

    int rearTouchSchema = parseInt(get("rear_touch", "schema_version", "1"), 1);

    constexpr int OLD_MARGIN_DEFAULT = 80;
    if (rearTouchSchema < 2)
    {
        if (settings.rear_touch.top == OLD_MARGIN_DEFAULT && settings.rear_touch.right == OLD_MARGIN_DEFAULT && settings.rear_touch.bottom == OLD_MARGIN_DEFAULT && settings.rear_touch.left == OLD_MARGIN_DEFAULT)
        {
            settings.rear_touch.top    = 0;
            settings.rear_touch.right  = 0;
            settings.rear_touch.bottom = 0;
            settings.rear_touch.left   = 0;
        }
    }
    settings.rear_touch.actionNorthWest = readUint("rear_touch", "action_nw", settings.rear_touch.actionNorthWest);
    settings.rear_touch.actionNorthEast = readUint("rear_touch", "action_ne", settings.rear_touch.actionNorthEast);
    settings.rear_touch.actionSouthWest = readUint("rear_touch", "action_sw", settings.rear_touch.actionSouthWest);
    settings.rear_touch.actionSouthEast = readUint("rear_touch", "action_se", settings.rear_touch.actionSouthEast);

    // Front Touch Settings
    settings.enable_front_touchzones = get("front_touch", "enabled", "false") == "true";
    settings.front_touch_offset      = parseInt(get("front_touch", "offset", "0"), 0);
    settings.front_touch_size        = parseInt(get("front_touch", "size", "150"), 150);
    settings.front_action_northwest  = readUint("front_touch", "action_nw", settings.front_action_northwest);
    settings.front_action_northeast  = readUint("front_touch", "action_ne", settings.front_action_northeast);
    settings.front_action_southwest  = readUint("front_touch", "action_sw", settings.front_action_southwest);
    settings.front_action_southeast  = readUint("front_touch", "action_se", settings.front_action_southeast);

    settings.microphone_host_ip     = get("microphone", "host_ip", settings.microphone_host_ip);
    settings.microphone_port        = parseInt(get("microphone", "port", std::to_string(settings.microphone_port)), settings.microphone_port);
    settings.microphone_sample_rate = parseInt(get("microphone", "sample_rate", std::to_string(settings.microphone_sample_rate)), settings.microphone_sample_rate);
    settings.microphone_channels    = parseInt(get("microphone", "channels", std::to_string(settings.microphone_channels)), settings.microphone_channels);
    settings.microphone_bitrate     = parseInt(get("microphone", "bitrate", std::to_string(settings.microphone_bitrate)), settings.microphone_bitrate);

    settings.microphone_gain          = std::clamp(settings.microphone_gain, 1.0f, 50.0f);
    settings.microphone_port          = std::clamp(settings.microphone_port, 1, 65535);
    settings.microphone_sample_rate   = std::clamp(settings.microphone_sample_rate, 8000, 48000);
    settings.microphone_channels      = std::clamp(settings.microphone_channels, 1, 2);
    settings.microphone_bitrate       = std::clamp(settings.microphone_bitrate, 6000, 512000);
    settings.motion_controls_scalar_x = std::clamp(settings.motion_controls_scalar_x, 0.1f, 3.0f);
    settings.motion_controls_scalar_y = std::clamp(settings.motion_controls_scalar_y, 0.1f, 3.0f);
    settings.keyboard_layout          = std::clamp(settings.keyboard_layout, 0, 2);
    settings.keyboard_mode            = std::clamp(settings.keyboard_mode, 0, 1);
    settings.render_mode              = std::clamp(settings.render_mode, 0, 1);
    settings.pixel_format_mode        = std::clamp(settings.pixel_format_mode, 0, 1);
    settings.swap_interval            = std::clamp(settings.swap_interval, 0, 4);
    settings.trackpad_pointer_speed   = std::clamp(settings.trackpad_pointer_speed, 0, 200);
    settings.trackpad_dead_zone       = std::clamp(settings.trackpad_dead_zone, 0, 200);
    settings.trackpad_edge_zone       = std::clamp(settings.trackpad_edge_zone, 0, 50);
    settings.front_touch_offset       = std::clamp(settings.front_touch_offset, 0, 200);
    settings.front_touch_size         = std::clamp(settings.front_touch_size, 50, 300);
    if (settings.gamepad_type != GAMEPAD_TYPE_XBOX && settings.gamepad_type != GAMEPAD_TYPE_PS4)
        settings.gamepad_type = GAMEPAD_TYPE_XBOX;

    return settings;
}

void ConfigManager::setVideoSettings(const VideoSettings& settings)
{
    set("video", "sops", settings.sops ? "true" : "false");
    set("video", "localaudio", settings.localaudio ? "true" : "false");
    set("video", "fullscreen", settings.fullscreen ? "true" : "false");
    set("video", "enable_frame_pacer", settings.enable_frame_pacer ? "true" : "false");
    set("video", "keep_awake_while_streaming", settings.keep_awake_while_streaming ? "true" : "false");
    set("video", "settings_background_opacity", std::to_string(settings.settings_background_opacity));
    set("video", "center_region_only", settings.center_region_only ? "true" : "false");
    set("video", "show_fps", settings.show_fps ? "true" : "false");
    set("video", "save_debug_log", settings.save_debug_log ? "true" : "false");
    set("video", "enable_microphone", settings.enable_microphone ? "true" : "false");
    set("video", "microphone_gain", std::to_string(settings.microphone_gain));
    set("video", "enable_microphone_compression", settings.enable_microphone_compression ? "true" : "false");
    data["video"].erase("enable_ref_frame_invalidation");
    set("video", "enable_motion_controls", settings.enable_motion_controls ? "true" : "false");
    set("video", "enable_double_tap_sprint", settings.enable_double_tap_sprint ? "true" : "false");
    set("video", "touchscreen_mode", std::to_string(settings.touchscreen_mode));
    set("video", "touchscreen_mode_schema", std::to_string(TOUCHSCREEN_MODE_SCHEMA));
    data["video"].erase("absolute_mouse");
    set("video", "enable_network_optimizations", settings.enable_network_optimizations ? "true" : "false");
    set("video", "motion_controls_scalar_x", std::to_string(settings.motion_controls_scalar_x));
    set("video", "motion_controls_scalar_y", std::to_string(settings.motion_controls_scalar_y));
    set("video", "keyboard_layout", std::to_string(settings.keyboard_layout));
    set("video", "keyboard_mode", std::to_string(settings.keyboard_mode));
    set("video", "keyboard_input_mode", settings.keyboard_input_mode ? "true" : "false");
    set("video", "keyboard_numbers_row", settings.keyboard_numbers_row ? "true" : "false");
    set("video", "keyboard_show_arrows", settings.keyboard_show_arrows ? "true" : "false");
    set("video", "keyboard_legacy_dialog", settings.keyboard_legacy_dialog ? "true" : "false");
    set("video", "keyboard_fullscreen", settings.keyboard_fullscreen ? "true" : "false");
    set("video", "keyboard_text_preview", settings.keyboard_text_preview ? "true" : "false");
    set("video", "keyboard_mouse_passthrough", settings.keyboard_mouse_passthrough ? "true" : "false");
    set("video", "capture_ps_button", settings.capture_ps_button ? "true" : "false");
    set("video", "render_mode", std::to_string(settings.render_mode));
    set("video", "pixel_format_mode", std::to_string(settings.pixel_format_mode));
    set("video", "gamepad_type", std::to_string(static_cast<int>(settings.gamepad_type)));
    set("video", "swap_shoulder_buttons", settings.swap_shoulder_buttons ? "true" : "false");
    set("video", "swap_interval", std::to_string(settings.swap_interval));
    // Trackpad Settings
    set("trackpad", "pointer_speed", std::to_string(settings.trackpad_pointer_speed));
    set("trackpad", "dead_zone", std::to_string(settings.trackpad_dead_zone));
    set("trackpad", "two_finger_right_click", settings.trackpad_two_finger_right_click ? "true" : "false");
    set("trackpad", "two_finger_scroll", settings.trackpad_two_finger_scroll ? "true" : "false");
    set("trackpad", "invert_scroll", settings.trackpad_invert_scroll ? "true" : "false");
    set("trackpad", "multi_touch", settings.trackpad_multi_touch ? "true" : "false");
    set("trackpad", "edge_zone", std::to_string(settings.trackpad_edge_zone));

    set("rear_touch", "enabled", settings.rear_touch.enabled ? "true" : "false");
    set("rear_touch", "top", std::to_string(settings.rear_touch.top));
    set("rear_touch", "right", std::to_string(settings.rear_touch.right));
    set("rear_touch", "bottom", std::to_string(settings.rear_touch.bottom));
    set("rear_touch", "left", std::to_string(settings.rear_touch.left));
    set("rear_touch", "action_nw", std::to_string(settings.rear_touch.actionNorthWest));
    set("rear_touch", "action_ne", std::to_string(settings.rear_touch.actionNorthEast));
    set("rear_touch", "action_sw", std::to_string(settings.rear_touch.actionSouthWest));
    set("rear_touch", "action_se", std::to_string(settings.rear_touch.actionSouthEast));
    set("rear_touch", "schema_version", "2");

    // Front Touch Settings
    set("front_touch", "enabled", settings.enable_front_touchzones ? "true" : "false");
    set("front_touch", "offset", std::to_string(settings.front_touch_offset));
    set("front_touch", "size", std::to_string(settings.front_touch_size));
    set("front_touch", "action_nw", std::to_string(settings.front_action_northwest));
    set("front_touch", "action_ne", std::to_string(settings.front_action_northeast));
    set("front_touch", "action_sw", std::to_string(settings.front_action_southwest));
    set("front_touch", "action_se", std::to_string(settings.front_action_southeast));

    set("microphone", "host_ip", settings.microphone_host_ip);
    set("microphone", "port", std::to_string(settings.microphone_port));
    set("microphone", "sample_rate", std::to_string(settings.microphone_sample_rate));
    set("microphone", "channels", std::to_string(settings.microphone_channels));
    set("microphone", "bitrate", std::to_string(settings.microphone_bitrate));
}

bool ConfigManager::updateVideoSettings(const std::function<void(VideoSettings&)>& update)
{
    std::lock_guard<std::recursive_mutex> lock(fileMutex);
    load();
    VideoSettings settings = getVideoSettings();
    update(settings);
    setVideoSettings(settings);
    return save();
}
