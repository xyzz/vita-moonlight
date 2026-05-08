#pragma once

#include <cstdint>

struct MotionSensorData
{
    float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
    float accelX = 0.0f, accelY = 0.0f, accelZ = 0.0f;
    float quatW = 0.0f, quatX = 0.0f, quatY = 0.0f, quatZ = 0.0f;
    float basicX = 0.0f, basicY = 0.0f, basicZ = 0.0f;
    uint8_t magStability   = 0;
    uint64_t hostTimestamp = 0;
};

class GyroManager
{
  public:
    GyroManager();

    void update();
    bool readMotionData(MotionSensorData& out);

    bool isSensorAvailable() const { return sensorAvailable; }

  private:
    uint64_t lastSendUs  = 0;
    bool initialized     = false;
    bool sensorAvailable = false;

    bool readPlatformSensor(float& gx, float& gy, float& gz,
        float& ax, float& ay, float& az);
};
