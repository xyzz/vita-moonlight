#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "moonmic.h" // For moonmic_config_t

namespace moonmic
{

class MoonmicBridge
{
  public:
    struct SunshineValidationInfo
    {
        int pair_status = 0;
        std::string uniqueid;
        std::string devicename = "Vita";
    };

    static MoonmicBridge& getInstance();

    MoonmicBridge(const MoonmicBridge&)            = delete;
    MoonmicBridge& operator=(const MoonmicBridge&) = delete;
    MoonmicBridge(MoonmicBridge&&)                 = delete;
    MoonmicBridge& operator=(MoonmicBridge&&)      = delete;

    void setTargetResolution(uint16_t width, uint16_t height);
    std::pair<uint16_t, uint16_t> getTargetResolution() const;
    SunshineValidationInfo buildSunshineValidation(const std::string& hostIp);
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

    HandshakeResult sendResolutionHandshake(const std::string& hostIp, int port, bool force = false);
    bool isAvailable(const std::string& hostIp, int port) const;
    TypingFocusResult queryTypingFocus(const std::string& hostIp, int port);

    void loadConfig();

  private:
    MoonmicBridge();
    ~MoonmicBridge() = default;

    void setAvailability(const std::string& hostIp, int port, bool available);

    mutable std::mutex target_mutex_;
    uint16_t target_width_  = 1280;
    uint16_t target_height_ = 720;
    mutable std::mutex availability_mutex_;
    std::string available_host_;
    int available_port_ = 0;
    bool available_     = false;
};

} // namespace moonmic
