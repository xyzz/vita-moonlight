#pragma once

#include <cstdint>

class GyroManager
{
  public:
    GyroManager();
    ~GyroManager();

    // Called every input poll to sample sensors and send motion events when needed
    void update();

  private:
    uint64_t lastSendUs  = 0;
    bool initialized     = false;
    bool sensorAvailable = false;

    // Platform-specific sensor read implementation (gyro + accelerometer)
    bool readPlatformSensor(float& gx, float& gy, float& gz,
        float& ax, float& ay, float& az);
};
