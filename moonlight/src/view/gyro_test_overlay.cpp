#include "view/gyro_test_overlay.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

#include "debug.hpp"

// Vita aspect ratio (screen 960x544)
static constexpr float VITA_ASPECT = 960.0f / 544.0f;

GyroTestCanvas::GyroTestCanvas(GyroManager* gyro)
    : gyroManager(gyro)
{
    this->setFocusable(true);
}

static inline float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float barWidth(float val, float maxAbs, float barMaxW)
{
    float ratio = clamp(std::fabs(val) / maxAbs, 0.0f, 1.0f);
    return ratio * barMaxW;
}

struct Vec3
{
    float x, y, z;
};

static inline Vec3 qrot(float qx, float qy, float qz, float qw, const Vec3& v)
{
    // q * v * q'
    float tx = 2.0f * (qy * v.z - qz * v.y);
    float ty = 2.0f * (qz * v.x - qx * v.z);
    float tz = 2.0f * (qx * v.y - qy * v.x);
    return {
        v.x + qw * tx + (qy * tz - qz * ty),
        v.y + qw * ty + (qz * tx - qx * tz),
        v.z + qw * tz + (qx * ty - qy * tx)
    };
}

static inline Vec3 project(const Vec3& v, float focal, float dist)
{
    float scale = focal / (v.z + dist);
    return { v.x * scale, v.y * scale, v.z };
}

void GyroTestCanvas::drawVitaBox3D(NVGcontext* vg, float cx, float cy, float size, const MotionSensorData& data)
{
    // Proportions: Vita ~ 221 x 103 x 18 mm
    float hw = 2.21f * size * 0.5f;
    float hh = 1.03f * size * 0.5f;
    float hd = 0.18f * size * 0.5f;

    Vec3 verts[8] = {
        { -hw, -hh, -hd }, { hw, -hh, -hd },
        { hw, hh, -hd }, { -hw, hh, -hd },
        { -hw, -hh, hd }, { hw, -hh, hd },
        { hw, hh, hd }, { -hw, hh, hd }
    };

    // Rotate by device quaternion (w,x,y,z)
    Vec3 rot[8];
    for (int i = 0; i < 8; ++i)
    {
        rot[i] = qrot(data.quatX, data.quatY, data.quatZ, data.quatW, verts[i]);
    }

    float focal = 200.0f;
    float dist  = 300.0f;
    Vec3 proj[8];
    for (int i = 0; i < 8; ++i)
    {
        proj[i] = project(rot[i], focal, dist);
    }

    int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // back face
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // front face
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } // connecting edges
    };

    nvgSave(vg);
    nvgTranslate(vg, cx, cy);

    // Draw front face (filled)
    nvgBeginPath(vg);
    nvgMoveTo(vg, proj[4].x, proj[4].y);
    nvgLineTo(vg, proj[5].x, proj[5].y);
    nvgLineTo(vg, proj[6].x, proj[6].y);
    nvgLineTo(vg, proj[7].x, proj[7].y);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(40, 45, 55, 200));
    nvgFill(vg);

    // Draw screen area (inset on front face)
    float sw         = hw * 0.78f;
    float sh         = sw / VITA_ASPECT;
    float sd         = hd + 0.5f;
    Vec3 scrVerts[4] = {
        { -sw, -sh, sd }, { sw, -sh, sd },
        { sw, sh, sd }, { -sw, sh, sd }
    };
    Vec3 scrRot[4], scrProj[4];
    for (int i = 0; i < 4; ++i)
    {
        scrRot[i]  = qrot(data.quatX, data.quatY, data.quatZ, data.quatW, scrVerts[i]);
        scrProj[i] = project(scrRot[i], focal, dist);
    }
    nvgBeginPath(vg);
    nvgMoveTo(vg, scrProj[0].x, scrProj[0].y);
    nvgLineTo(vg, scrProj[1].x, scrProj[1].y);
    nvgLineTo(vg, scrProj[2].x, scrProj[2].y);
    nvgLineTo(vg, scrProj[3].x, scrProj[3].y);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(20, 24, 32, 230));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(100, 110, 130, 180));
    nvgStroke(vg);

    // Motion indicator dot on screen plane
    float dotOffX = clamp(data.gyroX * 0.008f, -sw * 0.4f, sw * 0.4f);
    float dotOffY = clamp(data.gyroY * 0.008f, -sh * 0.4f, sh * 0.4f);
    Vec3 dotV     = qrot(data.quatX, data.quatY, data.quatZ, data.quatW, (Vec3) { dotOffX, dotOffY, sd });
    Vec3 dotP     = project(dotV, focal, dist);
    nvgBeginPath(vg);
    nvgCircle(vg, dotP.x, dotP.y, size * 0.06f);
    nvgFillColor(vg, nvgRGBA(0, 220, 120, 220));
    nvgFill(vg);

    // Edges
    nvgStrokeWidth(vg, 2.5f);
    nvgStrokeColor(vg, nvgRGBA(140, 150, 170, 220));
    nvgBeginPath(vg);
    for (int e = 0; e < 12; ++e)
    {
        nvgMoveTo(vg, proj[edges[e][0]].x, proj[edges[e][0]].y);
        nvgLineTo(vg, proj[edges[e][1]].x, proj[edges[e][1]].y);
    }
    nvgStroke(vg);

    // Front face edges brighter
    nvgStrokeWidth(vg, 2.0f);
    nvgStrokeColor(vg, nvgRGBA(200, 210, 230, 240));
    nvgBeginPath(vg);
    for (int e = 4; e < 8; ++e)
    {
        nvgMoveTo(vg, proj[edges[e][0]].x, proj[edges[e][0]].y);
        nvgLineTo(vg, proj[edges[e][1]].x, proj[edges[e][1]].y);
    }
    nvgStroke(vg);

    nvgRestore(vg);
}

void GyroTestCanvas::drawSensorBars(NVGcontext* vg, float x, float y, float w, const MotionSensorData& data)
{
    const float barH    = 8.0f;
    const float barMaxW = w * 0.35f;
    const float gap     = 14.0f;

    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 14.0f);

    struct SensorAxis
    {
        const char* label;
        float val;
        float maxAbs;
        NVGcolor color;
    };

    SensorAxis axes[] = {
        { "GYR X", data.gyroX, 90.0f, nvgRGBA(0, 200, 255, 220) },
        { "GYR Y", data.gyroY, 90.0f, nvgRGBA(0, 200, 255, 220) },
        { "GYR Z", data.gyroZ, 90.0f, nvgRGBA(0, 200, 255, 220) },
        { "ACC X", data.accelX, 20.0f, nvgRGBA(255, 180, 40, 220) },
        { "ACC Y", data.accelY, 20.0f, nvgRGBA(255, 180, 40, 220) },
        { "ACC Z", data.accelZ, 20.0f, nvgRGBA(255, 180, 40, 220) },
    };

    float rowY = y;
    for (const auto& a : axes)
    {
        // Label
        nvgFillColor(vg, nvgRGBA(200, 200, 210, 230));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x, rowY, a.label, nullptr);

        // Bar background
        float barX = x + 55.0f;
        nvgBeginPath(vg);
        nvgRect(vg, barX, rowY - barH * 0.5f, barMaxW, barH);
        nvgFillColor(vg, nvgRGBA(40, 42, 48, 180));
        nvgFill(vg);

        // Bar fill
        float fillW = barWidth(a.val, a.maxAbs, barMaxW);
        float fillX = a.val >= 0 ? barX : barX - fillW;
        nvgBeginPath(vg);
        nvgRect(vg, fillX, rowY - barH * 0.5f, fillW, barH);
        nvgFillColor(vg, a.color);
        nvgFill(vg);

        // Value text
        char valStr[32];
        snprintf(valStr, sizeof(valStr), "%.1f", a.val);
        nvgFillColor(vg, nvgRGBA(220, 220, 230, 230));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, barX + barMaxW + 8.0f, rowY, valStr, nullptr);

        rowY += gap;
    }
}

void GyroTestCanvas::drawSensorText(NVGcontext* vg, float x, float y, const MotionSensorData& data)
{
    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGBA(180, 185, 195, 220));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    char buf[256];
    float rowY        = y;
    const float lineH = 16.0f;

    snprintf(buf, sizeof(buf), "Quat  W:%.3f X:%.3f Y:%.3f Z:%.3f", data.quatW, data.quatX, data.quatY, data.quatZ);
    nvgText(vg, x, rowY, buf, nullptr);
    rowY += lineH;

    snprintf(buf, sizeof(buf), "Basic X:%.0f Y:%.0f Z:%.0f", data.basicX, data.basicY, data.basicZ);
    nvgText(vg, x, rowY, buf, nullptr);
    rowY += lineH;

    const char* magLabel = "Unknown";
    switch (data.magStability)
    {
        case 0:
            magLabel = "Unstable";
            break;
        case 1:
            magLabel = "Unused";
            break;
        case 2:
            magLabel = "Stable";
            break;
    }
    snprintf(buf, sizeof(buf), "Mag   Stability: %s", magLabel);
    nvgText(vg, x, rowY, buf, nullptr);
    rowY += lineH;
}

void GyroTestCanvas::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    nvgSave(vg);

    // Dark background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(18, 20, 26, 250));
    nvgFill(vg);

    MotionSensorData data {};
    bool haveData = false;
    if (gyroManager)
    {
        haveData = gyroManager->readMotionData(data);
    }

    if (!haveData)
    {
        nvgFontFaceId(vg, 0);
        nvgFontSize(vg, 20.0f);
        nvgFillColor(vg, nvgRGBA(255, 80, 80, 220));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width * 0.5f, y + height * 0.5f, "No sensor data", nullptr);
        nvgRestore(vg);
        return;
    }

    // Title
    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 20.0f);
    nvgFillColor(vg, nvgRGBA(220, 225, 235, 230));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgText(vg, x + width * 0.5f, y + 12.0f, "Gyroscope / Accelerometer Test", nullptr);

    // Vita 3D box area
    float boxSize = std::min(width * 0.35f, height * 0.45f);
    float cx      = x + width * 0.28f;
    float cy      = y + height * 0.35f;
    drawVitaBox3D(vg, cx, cy, boxSize, data);

    // Sensor bars (right side)
    float barsX = x + width * 0.56f;
    float barsY = y + height * 0.18f;
    drawSensorBars(vg, barsX, barsY, width * 0.42f, data);

    // Text readout (bottom)
    float textY = y + height * 0.72f;
    drawSensorText(vg, x + 20.0f, textY, data);

    nvgRestore(vg);
}

GyroTestOverlay::GyroTestOverlay(GyroManager* gyro)
{
    this->setTitle(brls::getStr("moonlight/gyro/test_title"));
    canvas = new GyroTestCanvas(gyro);
    this->setContentView(canvas);
    configureActions();
}

void GyroTestOverlay::configureActions()
{
    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}
