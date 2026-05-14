#pragma once

#include <atomic>

#include "controller/keyboard/IKeyboard.hpp"

// Legacy keyboard using sceIme for PS Vita.
//
// Uses sceImeOpen + sceImeUpdate from the main input loop.
// Characters are sent directly via LiSendKeyboardEvent.
class LegacyKeyboard : public IKeyboard
{
  public:
    LegacyKeyboard();
    ~LegacyKeyboard() override;

    void open() override;
    void close() override;
    bool isOpen() const override;
    void update() override;
    bool sendsDirectly() const override { return true; }
    bool usesNonNormalizedVk() const override { return true; }

  private:
    std::atomic<bool> isOpenFlag { false };
};
