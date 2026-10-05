// MPU-6050 motion engine: raw sensor -> blade-frame quantities -> gesture events.
#pragma once
#include <stdint.h>

// One-shot events, OR-ed into motion.events by imuUpdate() and cleared by the consumer.
enum : uint8_t {
  IMU_SWING = 0x01, // swing accent: motion.swingPeak holds its peak dps
};

struct Motion {
  uint16_t swingDps;   // total rotation speed
  uint8_t swing;       // 0-255 swing intensity for effects (eased)
  bool swinging;       // inside a swing (hysteresis)
  uint16_t swingPeak;  // peak of the last IMU_SWING
  uint8_t events;
  uint32_t lastMotion; // millis() of the last handling
};

extern Motion motion;

bool imuInit();                 // false if the MPU-6050 doesn't answer
bool imuCalibrateBias(bool save); // hold-still gyro zero point; true if the saber was still
void imuUpdate(uint32_t now);
