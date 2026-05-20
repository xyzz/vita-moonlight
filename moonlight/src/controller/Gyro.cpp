#include "controller/Gyro.hpp"

#include <psp2/motion.h>

#include <chrono>
#include <cmath>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "debug.hpp"

using namespace std::chrono;
static constexpr float kGyroBaseScale = 28.0f;

GyroManager::GyroManager()
{
    sceMotionReset();

    int res = sceMotionStartSampling();
    if (res < 0 && res != (int)SCE_MOTION_ERROR_ALREADY_SAMPLING)
    {
        vita_debug_log("[Gyro] sceMotionStartSampling failed: 0x%08X", res);
        sensorAvailable = false;
        initialized     = false;
        return;
    }

    sensorAvailable = true;
    initialized     = true;
    vita_debug_log("[Gyro] Motion sensor ready");
}

static uint64_t nowUs()
{
    return (uint64_t)duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

void GyroManager::update()
{
    if (!initialized || !sensorAvailable)
        return;

    // Rate limit sends to ~125Hz by default (matches input polling)
    const uint64_t now           = nowUs();
    const uint64_t minIntervalUs = 8000; // 125Hz
    if (lastSendUs != 0 && (now - lastSendUs) < minIntervalUs)
        return;

    extern ::VideoSettings g_video_settings_snapshot;
    if (!g_video_settings_snapshot.enable_motion_controls)
    {
        return;
    }

    const ::VideoSettings& settings = g_video_settings_snapshot;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (!readPlatformSensor(gx, gy, gz, ax, ay, az))
    {
        return;
    }

    // Apply base scale (legacy MOTION_SCALE) and per-axis config scalars
    float sx = settings.motion_controls_scalar_x;
    float sy = settings.motion_controls_scalar_y;

    float sx_v = gx * kGyroBaseScale * sx;
    float sy_v = gy * kGyroBaseScale * sy;
    float sz_v = gz * kGyroBaseScale;

    if (LiSendControllerMotionEvent(0, LI_MOTION_TYPE_GYRO, sx_v, sy_v, sz_v) != 0)
    {
        vita_debug_log("[Gyro] LiSendControllerMotionEvent (gyro) fallo");
    }

    float ax_v = ax * kGyroBaseScale * sx;
    float ay_v = ay * kGyroBaseScale * sy;
    float az_v = az * kGyroBaseScale;

    if (LiSendControllerMotionEvent(0, LI_MOTION_TYPE_ACCEL, ax_v, ay_v, az_v) != 0)
    {
        vita_debug_log("[Gyro] LiSendControllerMotionEvent (accel) fallo");
    }

    lastSendUs = now;
}

bool GyroManager::readMotionData(MotionSensorData& out)
{
    if (!sensorAvailable)
    {
        return false;
    }

    SceMotionState motionState;
    int res = sceMotionGetState(&motionState);
    if (res < 0)
    {
        vita_debug_log("[Gyro] sceMotionGetState failed: 0x%08X", res);
        return false;
    }

    // Gyroscope with legacy axis remapping
    out.gyroX = motionState.angularVelocity.x;
    out.gyroY = motionState.angularVelocity.z;
    out.gyroZ = motionState.angularVelocity.y * -1.0f;

    // Accelerometer with legacy axis remapping
    out.accelX = motionState.acceleration.x;
    out.accelY = motionState.acceleration.z;
    out.accelZ = motionState.acceleration.y * -1.0f;

    // Quaternion orientation
    out.quatW = motionState.deviceQuat.w;
    out.quatX = motionState.deviceQuat.x;
    out.quatY = motionState.deviceQuat.y;
    out.quatZ = motionState.deviceQuat.z;

    // Basic orientation relative to gravity (-1, 0, 1)
    out.basicX = motionState.basicOrientation.x;
    out.basicY = motionState.basicOrientation.y;
    out.basicZ = motionState.basicOrientation.z;

    // Magnetometer field stability
    out.magStability = motionState.magFieldStability;

    // Timestamp
    out.hostTimestamp = motionState.hostTimestamp;

    return true;
}

bool GyroManager::readPlatformSensor(float& gx, float& gy, float& gz,
    float& ax, float& ay, float& az)
{
    MotionSensorData data;
    if (!readMotionData(data))
    {
        return false;
    }
    gx = data.gyroX;
    gy = data.gyroY;
    gz = data.gyroZ;
    ax = data.accelX;
    ay = data.accelY;
    az = data.accelZ;
    return true;
}
