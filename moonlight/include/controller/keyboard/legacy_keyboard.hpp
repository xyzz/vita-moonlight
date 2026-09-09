#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "controller/keyboard/IKeyboard.hpp"

struct NVGcontext;

bool open_legacy_dialog_keyboard();
bool is_legacy_dialog_keyboard_active();

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
    bool allowsStreamTouch() const override;
    int streamTouchHeight() const override;
    bool blocksStreamTouch() const override;
    void drawOverlay(NVGcontext* vg);
    bool beginFocusButtonTouch(float x, float y);
    void endFocusButtonTouch();

  private:
    std::atomic<bool> isOpenFlag { false };
    std::atomic<bool> stopRequested { false };
    std::thread imeThread;
    std::mutex startMutex;
    std::condition_variable startCondition;
    bool startFinished      = false;
    bool startSucceeded     = false;
    bool showBottomVideo    = false;
    bool focusButtonPressed = false;
    bool manualVideoFocus   = false;
    int viewportHeight      = 0;

    void runIme();
    void updateVideoViewport();
    void toggleVideoFocus();
    void focusButtonPosition(float& x, float& y) const;
};
