#include "view/rear_touch_calibration_overlay.hpp"

#include <fmt/format.h>
#include <psp2/touch.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

#include "debug.hpp"

namespace
{
constexpr float PANEL_WIDTH  = 960.0f;
constexpr float PANEL_HEIGHT = 544.0f;
constexpr float MAX_TOUCH_X  = 1919.0f;
constexpr float MAX_TOUCH_Y  = 1087.0f;

std::string getEdgeLabel(int index)
{
    switch (index)
    {
        case 0:
            return brls::getStr("moonlight/rear_touch/calibration/edge_labels/0");
        case 1:
            return brls::getStr("moonlight/rear_touch/calibration/edge_labels/1");
        case 2:
            return brls::getStr("moonlight/rear_touch/calibration/edge_labels/2");
        case 3:
            return brls::getStr("moonlight/rear_touch/calibration/edge_labels/3");
        default:
            return "";
    }
}

inline float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}
}

RearTouchCalibrationCanvas::RearTouchCalibrationCanvas(const RearTouchSettings& initialSettings)
{
    this->setFocusable(true);
    current  = initialSettings;
    defaults = RearTouchSettings();
    clampBounds();
}

void RearTouchCalibrationCanvas::clampBounds()
{
    current.top    = std::max(0, current.top);
    current.bottom = std::max(0, current.bottom);
    current.left   = std::max(0, current.left);
    current.right  = std::max(0, current.right);

    int maxTop    = static_cast<int>(PANEL_HEIGHT) - current.bottom - MIN_ACTIVE_HEIGHT;
    int maxBottom = static_cast<int>(PANEL_HEIGHT) - current.top - MIN_ACTIVE_HEIGHT;
    int maxLeft   = static_cast<int>(PANEL_WIDTH) - current.right - MIN_ACTIVE_WIDTH;
    int maxRight  = static_cast<int>(PANEL_WIDTH) - current.left - MIN_ACTIVE_WIDTH;

    maxTop    = std::max(0, maxTop);
    maxBottom = std::max(0, maxBottom);
    maxLeft   = std::max(0, maxLeft);
    maxRight  = std::max(0, maxRight);

    current.top    = std::min(current.top, maxTop);
    current.bottom = std::min(current.bottom, maxBottom);
    current.left   = std::min(current.left, maxLeft);
    current.right  = std::min(current.right, maxRight);
}

void RearTouchCalibrationCanvas::adjustEdge(int edgeIndex, int delta)
{
    switch (edgeIndex)
    {
        case 0:
            current.top += delta;
            break;
        case 1:
            current.right += delta;
            break;
        case 2:
            current.bottom += delta;
            break;
        case 3:
            current.left += delta;
            break;
        default:
            break;
    }
    clampBounds();
    this->invalidate();
}

void RearTouchCalibrationCanvas::nextEdge()
{
    selectedEdge = (selectedEdge + 1) % 4;
    this->invalidate();
}

void RearTouchCalibrationCanvas::previousEdge()
{
    selectedEdge = (selectedEdge + 3) % 4;
    this->invalidate();
}

void RearTouchCalibrationCanvas::adjustSelected(int delta)
{
    adjustEdge(selectedEdge, delta);
}

void RearTouchCalibrationCanvas::toggleEnabled()
{
    current.enabled = !current.enabled;
    this->invalidate();
}

void RearTouchCalibrationCanvas::resetToDefaults()
{
    current = defaults;
    clampBounds();
    this->invalidate();
}

std::string RearTouchCalibrationCanvas::currentEdgeLabel() const
{
    return getEdgeLabel(selectedEdge);
}

std::uint32_t RearTouchCalibrationCanvas::getEdgeAssignment(int edgeIndex) const
{
    switch (edgeIndex)
    {
        case 0:
            return current.actionNorthWest;
        case 1:
            return current.actionNorthEast;
        case 2:
            return current.actionSouthWest;
        case 3:
            return current.actionSouthEast;
        default:
            return 0;
    }
}

void RearTouchCalibrationCanvas::setEdgeAssignment(int edgeIndex, std::uint32_t code)
{
    switch (edgeIndex)
    {
        case 0:
            current.actionNorthWest = code;
            break;
        case 1:
            current.actionNorthEast = code;
            break;
        case 2:
            current.actionSouthWest = code;
            break;
        case 3:
            current.actionSouthEast = code;
            break;
        default:
            break;
    }
}

void RearTouchCalibrationCanvas::cycleAssignment(int direction)
{
    if (direction == 0)
    {
        return;
    }
    const auto& options = controller::getSelectableSpecialInputOptions();
    if (options.empty())
    {
        return;
    }

    const std::uint32_t currentCode = getEdgeAssignment(selectedEdge);
    std::size_t index               = controller::getSelectableIndexForCode(currentCode);
    const std::size_t size          = options.size();

    if (direction > 0)
    {
        index = (index + 1) % size;
    }
    else
    {
        index = (index + size - 1) % size;
    }

    setEdgeAssignment(selectedEdge, options[index].code);
    this->invalidate();
}

std::string RearTouchCalibrationCanvas::currentAssignmentLabel() const
{
    return controller::getDisplayNameForCode(getEdgeAssignment(selectedEdge));
}

void RearTouchCalibrationCanvas::drawPanel(NVGcontext* vg, float panelX, float panelY, float width, float height, const SceTouchData& sample)
{
    const float activeLeft   = panelX + (static_cast<float>(current.left) / PANEL_WIDTH) * width;
    const float activeRight  = panelX + width - (static_cast<float>(current.right) / PANEL_WIDTH) * width;
    const float activeTop    = panelY + (static_cast<float>(current.top) / PANEL_HEIGHT) * height;
    const float activeBottom = panelY + height - (static_cast<float>(current.bottom) / PANEL_HEIGHT) * height;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, panelX, panelY, width, height, 12.0f);
    nvgFillColor(vg, nvgRGBA(30, 30, 35, 220));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, activeLeft, activeTop, activeRight - activeLeft, activeBottom - activeTop, 8.0f);
    nvgFillColor(vg, current.enabled ? nvgRGBA(70, 160, 250, 80) : nvgRGBA(180, 60, 60, 60));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, (activeLeft + activeRight) * 0.5f, activeTop);
    nvgLineTo(vg, (activeLeft + activeRight) * 0.5f, activeBottom);
    nvgMoveTo(vg, activeLeft, (activeTop + activeBottom) * 0.5f);
    nvgLineTo(vg, activeRight, (activeTop + activeBottom) * 0.5f);
    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 60));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgStrokeWidth(vg, 4.0f);
    NVGcolor highlight = nvgRGBA(255, 215, 0, current.enabled ? 220 : 120);
    switch (selectedEdge)
    {
        case 0: // top
            nvgMoveTo(vg, activeLeft, activeTop);
            nvgLineTo(vg, activeRight, activeTop);
            break;
        case 1: // right
            nvgMoveTo(vg, activeRight, activeTop);
            nvgLineTo(vg, activeRight, activeBottom);
            break;
        case 2: // bottom
            nvgMoveTo(vg, activeLeft, activeBottom);
            nvgLineTo(vg, activeRight, activeBottom);
            break;
        case 3: // left
            nvgMoveTo(vg, activeLeft, activeTop);
            nvgLineTo(vg, activeLeft, activeBottom);
            break;
    }
    nvgStrokeColor(vg, highlight);
    nvgStroke(vg);

    for (int i = 0; i < sample.reportNum; ++i)
    {
        const float normX = clamp01(static_cast<float>(sample.report[i].x) / MAX_TOUCH_X);
        const float normY = clamp01(static_cast<float>(sample.report[i].y) / MAX_TOUCH_Y);
        float touchX      = panelX + normX * width;
        float touchY      = panelY + normY * height;
        nvgBeginPath(vg);
        nvgCircle(vg, touchX, touchY, 12.0f);
        nvgFillColor(vg, nvgRGBA(255, 180, 40, 200));
        nvgFill(vg);
    }

    if (!current.enabled)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, panelX, panelY, width, height, 12.0f);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 120));
        nvgFill(vg);
    }
}

void RearTouchCalibrationCanvas::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    nvgSave(vg);

    // Fondo
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(15, 18, 26, 240));
    nvgFill(vg);

    const float topMargin = 40.0f;
    float panelWidth      = width * 0.7f;
    float panelHeight     = panelWidth * (PANEL_HEIGHT / PANEL_WIDTH);
    if (panelHeight > height * 0.6f)
    {
        panelHeight = height * 0.6f;
        panelWidth  = panelHeight * (PANEL_WIDTH / PANEL_HEIGHT);
    }
    const float panelX = x + (width - panelWidth) * 0.5f;
    const float panelY = y + topMargin;

    SceTouchData backData;
    memset(&backData, 0, sizeof(SceTouchData));
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &backData, 1);

    drawPanel(vg, panelX, panelY, panelWidth, panelHeight, backData);

    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 18.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
    std::string status = current.enabled ? brls::getStr("moonlight/rear_touch/calibration/status_enabled") : brls::getStr("moonlight/rear_touch/calibration/status_disabled");
    nvgText(vg, x + 30.0f, panelY - 24.0f, status.c_str(), nullptr);

    std::string selected = brls::getStr("moonlight/rear_touch/calibration/selected_edge") + currentEdgeLabel();
    nvgText(vg, x + 30.0f, panelY + panelHeight + 30.0f, selected.c_str(), nullptr);

    std::string assignment = brls::getStr("moonlight/rear_touch/calibration/current_action") + currentAssignmentLabel();
    nvgText(vg, x + 30.0f, panelY + panelHeight + 55.0f, assignment.c_str(), nullptr);

    std::string marginsLabel = brls::getStr("moonlight/rear_touch/calibration/margins");
    std::string topLabel     = brls::getStr("moonlight/rear_touch/detail_top");
    std::string rightLabel   = brls::getStr("moonlight/rear_touch/detail_right");
    std::string bottomLabel  = brls::getStr("moonlight/rear_touch/detail_bottom");
    std::string leftLabel    = brls::getStr("moonlight/rear_touch/detail_left");
    std::string values       = marginsLabel + " - " + topLabel + ": " + std::to_string(current.top) + " | " + rightLabel + ": " + std::to_string(current.right) + " | " + bottomLabel + ": " + std::to_string(current.bottom) + " | " + leftLabel + ": " + std::to_string(current.left);
    nvgText(vg, x + 30.0f, panelY + panelHeight + 80.0f, values.c_str(), nullptr);

    std::string assignPrefix  = brls::getStr("moonlight/rear_touch/calibration/assignments_prefix");
    std::string nwLabel       = brls::getStr("moonlight/rear_touch/calibration/zone_labels/nw");
    std::string neLabel       = brls::getStr("moonlight/rear_touch/calibration/zone_labels/ne");
    std::string swLabel       = brls::getStr("moonlight/rear_touch/calibration/zone_labels/sw");
    std::string seLabel       = brls::getStr("moonlight/rear_touch/calibration/zone_labels/se");
    std::string mappingTop    = assignPrefix + " - " + nwLabel + ": " + controller::getDisplayNameForCode(current.actionNorthWest) + " | " + neLabel + ": " + controller::getDisplayNameForCode(current.actionNorthEast);
    std::string mappingBottom = swLabel + ": " + controller::getDisplayNameForCode(current.actionSouthWest) + " | " + seLabel + ": " + controller::getDisplayNameForCode(current.actionSouthEast);
    nvgText(vg, x + 30.0f, panelY + panelHeight + 105.0f, mappingTop.c_str(), nullptr);
    nvgText(vg, x + 30.0f, panelY + panelHeight + 130.0f, mappingBottom.c_str(), nullptr);

    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, nvgRGBA(200, 200, 200, 220));
    std::string instructions = brls::getStr("moonlight/rear_touch/calibration/instructions");
    nvgText(vg, x + 30.0f, panelY + panelHeight + 165.0f, instructions.c_str(), nullptr);

    nvgRestore(vg);
}

RearTouchCalibrationOverlay::RearTouchCalibrationOverlay(const RearTouchSettings& initialSettings,
    SaveCallback onSave,
    CancelCallback onCancel)
    : saveCallback(std::move(onSave))
    , cancelCallback(std::move(onCancel))
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    SceTouchSamplingState currentState = SCE_TOUCH_SAMPLING_STATE_STOP;
    if (sceTouchGetSamplingState(SCE_TOUCH_PORT_BACK, &currentState) == 0)
    {
        previousBackSamplingState = currentState;
        restoreSamplingState      = true;
    }
    if (currentState != SCE_TOUCH_SAMPLING_STATE_START)
    {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    }
#endif
    std::string overlayTitle          = brls::getStr("moonlight/rear_touch/calibration/overlay_title");
    auto* root                        = new brls::Box(brls::Axis::COLUMN);
    root->getAppletFrameItem()->title = overlayTitle;
    this->setTitle(overlayTitle);
    this->getAppletFrameItem()->title = overlayTitle;
    // Use vita_debug_log so the log can be toggled on Vita via g_debug_log_enabled
    vita_debug_log("[RearTouchCalibrationOverlay] constructor set title='%s'", overlayTitle.c_str());
    root->setGrow(1.0f);
    canvas                              = new RearTouchCalibrationCanvas(initialSettings);
    canvas->getAppletFrameItem()->title = brls::getStr("moonlight/rear_touch/calibration/title");
    canvas->setGrow(1.0f);
    root->addView(canvas);
    this->setContentView(root);

    configureActions();

    brls::sync([this]()
        { brls::Application::giveFocus(this->canvas); });
}

RearTouchCalibrationOverlay::~RearTouchCalibrationOverlay()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    if (restoreSamplingState)
    {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, previousBackSamplingState);
    }
#endif
}

void RearTouchCalibrationOverlay::configureActions()
{
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/save"), brls::BUTTON_A, [this](brls::View*)
        {
        confirm();
        return true; });
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/cancel"), brls::BUTTON_B, [this](brls::View*)
        {
        cancel();
        return true; });
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/next_edge"), brls::BUTTON_DOWN, [this](brls::View*)
        {
        selectNext();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/prev_edge"), brls::BUTTON_UP, [this](brls::View*)
        {
        selectPrevious();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/increase_margin"), brls::BUTTON_RIGHT, [this](brls::View*)
        {
        increase();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/decrease_margin"), brls::BUTTON_LEFT, [this](brls::View*)
        {
        decrease();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/next_action"), brls::BUTTON_RB, [this](brls::View*)
        {
        cycleAssignmentForward();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/prev_action"), brls::BUTTON_LB, [this](brls::View*)
        {
        cycleAssignmentBackward();
        return true; }, true, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/toggle_enabled"), brls::BUTTON_X, [this](brls::View*)
        {
        toggleEnabled();
        return true; }, true);
    this->registerAction(brls::getStr("moonlight/rear_touch/calibration/actions/reset"), brls::BUTTON_Y, [this](brls::View*)
        {
        resetDefaults();
        return true; }, true);
}

void RearTouchCalibrationOverlay::confirm()
{
    if (saveCallback)
    {
        saveCallback(canvas->getSettings());
    }
    brls::Application::popActivity(brls::TransitionAnimation::FADE);
}

void RearTouchCalibrationOverlay::cancel()
{
    if (cancelCallback)
    {
        cancelCallback();
    }
    brls::Application::popActivity(brls::TransitionAnimation::FADE);
}

void RearTouchCalibrationOverlay::increase()
{
    constexpr int STEP_PIXELS = 10;
    canvas->adjustSelected(STEP_PIXELS);
}

void RearTouchCalibrationOverlay::decrease()
{
    constexpr int STEP_PIXELS = 10;
    canvas->adjustSelected(-STEP_PIXELS);
}

void RearTouchCalibrationOverlay::selectNext()
{
    canvas->nextEdge();
}

void RearTouchCalibrationOverlay::selectPrevious()
{
    canvas->previousEdge();
}

void RearTouchCalibrationOverlay::toggleEnabled()
{
    canvas->toggleEnabled();
}

void RearTouchCalibrationOverlay::resetDefaults()
{
    canvas->resetToDefaults();
}

void RearTouchCalibrationOverlay::cycleAssignmentForward()
{
    canvas->cycleAssignment(1);
}

void RearTouchCalibrationOverlay::cycleAssignmentBackward()
{
    canvas->cycleAssignment(-1);
}
