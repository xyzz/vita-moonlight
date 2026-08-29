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
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "debug.hpp"

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
    data.clear();
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
    // Verbose logging removed for performance
    return true;
}

bool ConfigManager::save() const
{
    std::string path = getConfigPath();
    std::ofstream file(path);
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
    // Verbose logging removed for performance
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
    config.width                 = std::stoi(get("stream", "width", "1280"));
    config.height                = std::stoi(get("stream", "height", "720"));
    config.fps                   = std::stoi(get("stream", "fps", "60"));
    config.bitrate               = std::stoi(get("stream", "bitrate", "-1"));
    config.packetSize            = std::stoi(get("stream", "packetsize", "1024"));
    config.streamingRemotely     = std::stoi(get("stream", "streaming_remotely", "0"));
    config.audioConfiguration    = std::stoi(get("stream", "audio_config", "2"));
    config.supportedVideoFormats = std::stoi(get("stream", "video_formats", "1"));
    return config;
}

void ConfigManager::setStreamConfig(const StreamConfiguration& config)
{
    set("stream", "width", std::to_string(config.width));
    set("stream", "height", std::to_string(config.height));
    set("stream", "fps", std::to_string(config.fps));
    set("stream", "bitrate", std::to_string(config.bitrate));
    set("stream", "packetsize", std::to_string(config.packetSize));
    set("stream", "streaming_remotely", std::to_string(config.streamingRemotely));
    set("stream", "audio_config", std::to_string(config.audioConfiguration));
    set("stream", "video_formats", std::to_string(config.supportedVideoFormats));
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
    settings.sops                       = get("video", "sops", "true") == "true";
    settings.localaudio                 = get("video", "localaudio", "false") == "true";
    settings.fullscreen                 = get("video", "fullscreen", "true") == "true";
    settings.enable_frame_pacer         = get("video", "enable_frame_pacer", "true") == "true";
    settings.keep_awake_while_streaming = get("video", "keep_awake_while_streaming", "true") == "true";
    try
    {
        settings.settings_background_opacity = std::stof(get("video", "settings_background_opacity", "1.0"));
    }
    catch (...)
    {
        settings.settings_background_opacity = 1.0f;
    }
    settings.settings_background_opacity   = std::max(0.0f, std::min(1.0f, settings.settings_background_opacity));
    settings.center_region_only            = get("video", "center_region_only", "false") == "true";
    settings.show_fps                      = get("video", "show_fps", "false") == "true";
    settings.save_debug_log                = get("video", "save_debug_log", "false") == "true";
    settings.enable_microphone             = get("video", "enable_microphone", "false") == "true";
    settings.microphone_gain               = std::stof(get("video", "microphone_gain", "10.0"));
    settings.enable_microphone_compression = get("video", "enable_microphone_compression", "false") == "true";
    settings.enable_ref_frame_invalidation = get("video", "enable_ref_frame_invalidation", "false") == "true";
    settings.enable_vita_vblank_wait       = get("video", "enable_vita_vblank_wait", "false") == "true";
    settings.enable_motion_controls        = get("video", "enable_motion_controls", "false") == "true";
    settings.enable_double_tap_sprint      = get("video", "enable_double_tap_sprint", "false") == "true";
    settings.absolute_mouse                = get("video", "absolute_mouse", "false") == "true";
    std::string touchscreenStr             = get("video", "touchscreen_mode", "1");
    if (touchscreenStr == "true")
    {
        settings.touchscreen_mode = 3;
    }
    else if (touchscreenStr == "false")
    {
        settings.touchscreen_mode = 0;
    }
    else
    {
        try
        {
            settings.touchscreen_mode = std::stoi(touchscreenStr);
        }
        catch (...)
        {
            settings.touchscreen_mode = 1;
        }
    }
    settings.touchscreen_mode             = std::max(0, std::min(4, settings.touchscreen_mode));
    settings.enable_network_optimizations = get("video", "enable_network_optimizations", "true") == "true";
    settings.double_tap_sprint_step_time  = std::stoi(get("video", "double_tap_sprint_step_time", "200"));
    settings.motion_controls_scalar_x     = std::stof(get("video", "motion_controls_scalar_x", "1.2"));
    settings.motion_controls_scalar_y     = std::stof(get("video", "motion_controls_scalar_y", "0.8"));
    settings.mouse_acceleration           = std::stoi(get("video", "mouse_acceleration", "150"));
    settings.keyboard_layout              = std::stoi(get("video", "keyboard_layout", "0"));
    settings.keyboard_mode                = std::stoi(get("video", "keyboard_mode", "0"));
    settings.keyboard_input_mode          = get("video", "keyboard_input_mode", "false") == "true";
    settings.keyboard_numbers_row         = get("video", "keyboard_numbers_row", "true") == "true";
    settings.keyboard_show_arrows         = get("video", "keyboard_show_arrows", "true") == "true";
    settings.render_mode                  = std::stoi(get("video", "render_mode", "0"));
    settings.pixel_format_mode            = std::stoi(get("video", "pixel_format_mode", "1"));
    settings.gamepad_type                 = static_cast<GamepadType>(std::stoi(get("video", "gamepad_type", "0")));
    settings.swap_shoulder_buttons        = get("video", "swap_shoulder_buttons", "false") == "true";
    settings.swap_interval                = std::stoi(get("video", "swap_interval", "1"));
    // Trackpad Settings
    settings.trackpad_pointer_speed          = std::stoi(get("trackpad", "pointer_speed", "100"));
    settings.trackpad_dead_zone              = std::stoi(get("trackpad", "dead_zone", "50"));
    settings.trackpad_tap_to_click           = get("trackpad", "tap_to_click", "true") == "true";
    settings.trackpad_two_finger_right_click = get("trackpad", "two_finger_right_click", "true") == "true";
    settings.trackpad_two_finger_scroll      = get("trackpad", "two_finger_scroll", "true") == "true";
    settings.trackpad_invert_scroll          = get("trackpad", "invert_scroll", "false") == "true";
    settings.trackpad_multi_touch            = get("trackpad", "multi_touch", "true") == "true";
    settings.trackpad_edge_zone              = std::stoi(get("trackpad", "edge_zone", "15"));

    auto readMargin = [this](const std::string& key, int fallback) -> int
    {
        std::string value = get("rear_touch", key, "");
        if (value.empty())
            return fallback;
        try
        {
            return std::stoi(value);
        }
        catch (...)
        {
            return fallback;
        }
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

    int rearTouchSchema = 1;
    try
    {
        rearTouchSchema = std::stoi(get("rear_touch", "schema_version", "1"));
    }
    catch (...)
    {
        rearTouchSchema = 1;
    }

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
        const_cast<ConfigManager*>(this)->set("rear_touch", "schema_version", "2");
    }
    settings.rear_touch.actionNorthWest = readUint("rear_touch", "action_nw", settings.rear_touch.actionNorthWest);
    settings.rear_touch.actionNorthEast = readUint("rear_touch", "action_ne", settings.rear_touch.actionNorthEast);
    settings.rear_touch.actionSouthWest = readUint("rear_touch", "action_sw", settings.rear_touch.actionSouthWest);
    settings.rear_touch.actionSouthEast = readUint("rear_touch", "action_se", settings.rear_touch.actionSouthEast);

    // Front Touch Settings
    settings.enable_front_touchzones = get("front_touch", "enabled", "false") == "true";
    settings.front_touch_offset      = std::stoi(get("front_touch", "offset", "0"));
    settings.front_touch_size        = std::stoi(get("front_touch", "size", "150"));
    settings.front_action_northwest  = readUint("front_touch", "action_nw", settings.front_action_northwest);
    settings.front_action_northeast  = readUint("front_touch", "action_ne", settings.front_action_northeast);
    settings.front_action_southwest  = readUint("front_touch", "action_sw", settings.front_action_southwest);
    settings.front_action_southeast  = readUint("front_touch", "action_se", settings.front_action_southeast);

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
    set("video", "enable_ref_frame_invalidation", settings.enable_ref_frame_invalidation ? "true" : "false");
    set("video", "enable_vita_vblank_wait", settings.enable_vita_vblank_wait ? "true" : "false");
    set("video", "enable_motion_controls", settings.enable_motion_controls ? "true" : "false");
    set("video", "enable_double_tap_sprint", settings.enable_double_tap_sprint ? "true" : "false");
    set("video", "absolute_mouse", settings.absolute_mouse ? "true" : "false");
    set("video", "touchscreen_mode", std::to_string(settings.touchscreen_mode));
    set("video", "enable_network_optimizations", settings.enable_network_optimizations ? "true" : "false");
    set("video", "double_tap_sprint_step_time", std::to_string(settings.double_tap_sprint_step_time));
    set("video", "motion_controls_scalar_x", std::to_string(settings.motion_controls_scalar_x));
    set("video", "motion_controls_scalar_y", std::to_string(settings.motion_controls_scalar_y));
    set("video", "mouse_acceleration", std::to_string(settings.mouse_acceleration));
    set("video", "keyboard_layout", std::to_string(settings.keyboard_layout));
    set("video", "keyboard_mode", std::to_string(settings.keyboard_mode));
    set("video", "keyboard_input_mode", settings.keyboard_input_mode ? "true" : "false");
    set("video", "keyboard_numbers_row", settings.keyboard_numbers_row ? "true" : "false");
    set("video", "keyboard_show_arrows", settings.keyboard_show_arrows ? "true" : "false");
    set("video", "render_mode", std::to_string(settings.render_mode));
    set("video", "pixel_format_mode", std::to_string(settings.pixel_format_mode));
    set("video", "gamepad_type", std::to_string(static_cast<int>(settings.gamepad_type)));
    set("video", "swap_shoulder_buttons", settings.swap_shoulder_buttons ? "true" : "false");
    set("video", "swap_interval", std::to_string(settings.swap_interval));
    // Trackpad Settings
    set("trackpad", "pointer_speed", std::to_string(settings.trackpad_pointer_speed));
    set("trackpad", "dead_zone", std::to_string(settings.trackpad_dead_zone));
    set("trackpad", "tap_to_click", settings.trackpad_tap_to_click ? "true" : "false");
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
}
