/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "../../third_party/libmoonmic/moonmic.h" // For MOONMIC_DEFAULT_* constants
#include "controller/input_types.hpp"
#include "controller/special_inputs.hpp"

// Estructura de configuración de streaming (similar al legacy)
struct StreamConfiguration
{
    int width                 = 1280;
    int height                = 720;
    int fps                   = 60;
    int bitrate               = -1; // Auto-calculado
    int packetSize            = 1024;
    int streamingRemotely     = 0;
    int audioConfiguration    = 2; // AUDIO_CONFIGURATION_STEREO
    int supportedVideoFormats = 1; // VIDEO_FORMAT_H264

    // Método para validar y ajustar resolución para PS Vita
    void validateAndAdjustResolution()
    {
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
        // Aplicar restricciones de PS Vita: múltiplos de 16, mínimo 64
        width  = (width < 64) ? 64 : ((width + 15) / 16) * 16;
        height = (height < 64) ? 64 : ((height + 15) / 16) * 16;

        // Limitar a resoluciones razonables para PS Vita
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

// Enum para tipos de gamepad soportados
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
    bool enable_network_optimizations  = true; // Optimizaciones de red (IDR smart, pacing, etc.)
    int touchscreen_mode               = 0; // 0=Off, 1=DS4 Touchpad, 2=Mouse Absoluto, 3=Tableta Multitouch
    int double_tap_sprint_step_time    = 200;
    float motion_controls_scalar_x     = 1.2f;
    float motion_controls_scalar_y     = 0.8f;
    int mouse_acceleration             = 150;
    int keyboard_layout                = 0; // 0=EN_US, 1=ES_ES, 2=ES_LATAM

    // Configuración del Trackpad (específica)
    int trackpad_pointer_speed           = 100; // 0-200
    int trackpad_dead_zone               = 50; // 0-200px
    bool trackpad_tap_to_click           = true;
    bool trackpad_two_finger_right_click = true;
    bool trackpad_two_finger_scroll      = true;
    bool trackpad_invert_scroll          = false;
    bool trackpad_multi_touch            = true;
    int trackpad_edge_zone               = 15; // 0-50%

    // Nueva opción: modo de renderizado
    int render_mode       = 0; // 0=Legacy, 1=Modern (FFmpeg)
    int pixel_format_mode = 0; // 0=RGBA directo, 1=YUV420 (pruebas)

    // Nueva opción: tipo de gamepad
    GamepadType gamepad_type = GAMEPAD_TYPE_XBOX; // 0=Xbox, 1=PS4 (para emular controlador)

    bool enable_microphone         = false; // Enable/disable microphone transmission
    std::string microphone_host_ip = ""; // Host IP (empty = use stream host)
    int microphone_port            = MOONMIC_DEFAULT_PORT; // UDP port for mic transmission
    int microphone_sample_rate     = MOONMIC_DEFAULT_SAMPLE_RATE; // Sample rate (Hz) - Vita hardware native
    int microphone_channels        = MOONMIC_DEFAULT_CHANNELS; // 1=mono, 2=stereo
    int microphone_bitrate         = MOONMIC_DEFAULT_BITRATE; // Opus bitrate (bps) - optimal for 16kHz mono VOIP

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

    // --- Gestión de directorio de llaves/dispositivos ---
    std::string getKeysDir() const;
    void setKeysDir(const std::string& dir);

    // Helpers seguros para crear/asegurar directorios relacionados con keys
    // Garantiza que la ruta exista (mkdir -p behaviour). Devuelve true si
    // la ruta existe tras la llamada o false en error.
    bool ensureDirExists(const std::string& path) const;
    bool ensureKeysDirExists() const;
    bool ensureKeyDirExists(const std::string& safeId) const;

    // --- Configuración de streaming (modo legacy) ---
    StreamConfiguration getStreamConfig() const;
    void setStreamConfig(const StreamConfiguration& config);
    VideoSettings getVideoSettings() const;
    void setVideoSettings(const VideoSettings& settings);

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data;
    mutable std::string cachedKeysDir;
    mutable bool keysDirLoaded = false;
};
