#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "model/HostStorage.hpp"
#include "utils/overlay_utils.hpp"

// Side overlay for pause menu (appears from the right)
class VitaPauseOverlay : public BaseOverlay
{
  public:
    // onClose will be called when the overlay closes (so that the caller
    // can reset its "open overlay" flag).
    VitaPauseOverlay(std::function<void()> onClose, const HostInfo& hostInfo);
    ~VitaPauseOverlay() override;
    const char* describe() const { return "VitaPauseOverlay"; }

  private:
    void resume();
    void disconnect();
    void closeApp();

    std::function<void()> onClose;
    HostInfo host;
};
