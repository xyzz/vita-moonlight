#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "moonmic.h" // For moonmic_config_t

namespace moonmic
{

/**
 * @brief Central bridge for all Moonmic features
 *
 * Manages microphone streaming, display resolution configuration,
 * and other Moonmic-related functionality. Singleton pattern.
 */
class MoonmicBridge
{
  public:
    struct SunshineValidationInfo
    {
        int pair_status = 0;
        std::string uniqueid;
        std::string devicename = "Vita";
    };

    /**
     * @brief Get the singleton instance
     */
    static MoonmicBridge& getInstance();

    // Delete copy/move constructors
    MoonmicBridge(const MoonmicBridge&)            = delete;
    MoonmicBridge& operator=(const MoonmicBridge&) = delete;
    MoonmicBridge(MoonmicBridge&&)                 = delete;
    MoonmicBridge& operator=(MoonmicBridge&&)      = delete;

    /**
     * @brief Set the target display resolution for streaming
     * @param width Target width (e.g., 1280, 1920)
     * @param height Target height (e.g., 720, 1080)
     *
     * This resolution will be sent to moonmic-host in the handshake,
     * which will configure Sunshine to render at this resolution
     * and downscale to Vita's native 960x544.
     */
    void setTargetResolution(uint16_t width, uint16_t height);

    /**
     * @brief Get the current target display resolution
     * @return Pair of (width, height)
     */
    std::pair<uint16_t, uint16_t> getTargetResolution() const;

    /**
     * @brief Build Sunshine validation payload (uniqueid, pair status)
     */
    SunshineValidationInfo buildSunshineValidation(const std::string& hostIp);

    /**
     * @brief Send raw resolution handshake without starting capture
     */
    struct HandshakeResult
    {
        bool success            = false;
        bool mismatch           = false;
        uint16_t current_width  = 0;
        uint16_t current_height = 0;
    };

    struct TypingFocusResult
    {
        bool success       = false;
        bool caret         = false;
        float normalized_x = 0.5f;
        float normalized_y = 0.5f;
    };

    /**
     * @brief Send raw resolution handshake without starting capture
     * @param force Force update even if resolution differs
     */
    HandshakeResult sendResolutionHandshake(const std::string& hostIp, int port, bool force = false);
    bool isAvailable(const std::string& hostIp, int port) const;
    TypingFocusResult queryTypingFocus(const std::string& hostIp, int port);

    /**
     * @brief Load configuration from persistent storage
     */
    void loadConfig();

    /**
     * @brief Save configuration to persistent storage
     */
    void saveConfig();

  private:
    MoonmicBridge();
    ~MoonmicBridge() = default;

    void setAvailability(const std::string& hostIp, int port, bool available);

    // Configuration
    uint16_t target_width_  = 1280; // Default: 720p
    uint16_t target_height_ = 720;

    std::string config_file_;
    mutable std::mutex availability_mutex_;
    std::string available_host_;
    int available_port_ = 0;
    bool available_     = false;
};

} // namespace moonmic
