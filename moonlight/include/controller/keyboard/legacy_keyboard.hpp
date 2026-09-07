#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "controller/keyboard/IKeyboard.hpp"

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
    std::atomic<bool> stopRequested { false };
    std::thread imeThread;
    std::mutex startMutex;
    std::condition_variable startCondition;
    bool startFinished  = false;
    bool startSucceeded = false;

    void runIme();
};
