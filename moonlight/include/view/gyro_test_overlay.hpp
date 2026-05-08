#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

#include "controller/Gyro.hpp"

class GyroTestCanvas : public brls::View
{
  public:
    GyroTestCanvas(GyroManager* gyro);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override { return this; }

  private:
    GyroManager* gyroManager = nullptr;

    void drawVitaBox3D(NVGcontext* vg, float cx, float cy, float size, const MotionSensorData& data);
    void drawSensorBars(NVGcontext* vg, float x, float y, float w, const MotionSensorData& data);
    void drawSensorText(NVGcontext* vg, float x, float y, const MotionSensorData& data);
};

class GyroTestOverlay : public brls::AppletFrame
{
  public:
    GyroTestOverlay(GyroManager* gyro);

  private:
    GyroTestCanvas* canvas = nullptr;
    void configureActions();
};
