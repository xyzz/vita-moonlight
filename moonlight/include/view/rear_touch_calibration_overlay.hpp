#pragma once

#include <psp2/touch.h>

#include <borealis.hpp>
#include <string>

#include "ConfigManager.hpp"

class RearTouchCalibrationCanvas : public brls::View
{
  public:
    explicit RearTouchCalibrationCanvas(const RearTouchSettings& initialSettings);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override { return this; }

    void nextEdge();
    void previousEdge();
    void adjustSelected(int delta);
    void toggleEnabled();
    void resetToDefaults();
    void cycleAssignment(int direction);

    const RearTouchSettings& getSettings() const { return current; }

  private:
    RearTouchSettings current {};
    RearTouchSettings defaults {};
    int selectedEdge                       = 0; // 0=top,1=right,2=bottom,3=left
    static constexpr int MIN_ACTIVE_WIDTH  = 200;
    static constexpr int MIN_ACTIVE_HEIGHT = 150;
    void clampBounds();
    void adjustEdge(int edgeIndex, int delta);
    void drawPanel(NVGcontext* vg, float x, float y, float width, float height, const SceTouchData& sample);
    std::string currentEdgeLabel() const;
    std::string currentAssignmentLabel() const;
    void setEdgeAssignment(int edgeIndex, std::uint32_t code);
    std::uint32_t getEdgeAssignment(int edgeIndex) const;
};

class RearTouchCalibrationOverlay : public brls::AppletFrame
{
  public:
    using SaveCallback   = std::function<void(const RearTouchSettings&)>;
    using CancelCallback = std::function<void()>;

    RearTouchCalibrationOverlay(const RearTouchSettings& initialSettings,
        SaveCallback onSave,
        CancelCallback onCancel);
    ~RearTouchCalibrationOverlay() override;

  private:
    RearTouchCalibrationCanvas* canvas = nullptr;
    SaveCallback saveCallback;
    CancelCallback cancelCallback;

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    SceTouchSamplingState previousBackSamplingState = SCE_TOUCH_SAMPLING_STATE_STOP;
    bool restoreSamplingState                       = false;
#endif

    void configureActions();
    void confirm();
    void cancel();
    void increase();
    void decrease();
    void selectNext();
    void selectPrevious();
    void toggleEnabled();
    void resetDefaults();
    void cycleAssignmentForward();
    void cycleAssignmentBackward();
};
