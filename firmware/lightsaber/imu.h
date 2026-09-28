// MPU-6050 motion engine: raw sensor -> blade-frame quantities -> gesture events.
#pragma once
#include <stdint.h>

// One-shot events, OR-ed into motion.events by imuUpdate() and cleared by the consumer.
enum : uint8_t {
  IMU_SWING = 0x01, // swing accent: motion.swingPeak holds its peak dps
  IMU_CLASH = 0x02, // impact: motion.clashLevel holds its strength (x0.1 g)
  IMU_STAB  = 0x04, // thrust along the blade
  IMU_TWIST = 0x08, // back-and-forth roll ("revving the throttle")
  IMU_MOVED = 0x10, // any handling at all
  IMU_SAMPLE = 0x20, // a fresh sample arrived this pass (dtMs is valid)
};

struct Motion {
  uint16_t swingDps;   // rotation speed perpendicular to the blade
  int16_t rollDps;     // rotation about the blade axis
  int16_t along;       // low-passed gravity along the blade, 2048 = 1 g (tip up = +)
  int8_t pitch;        // blade elevation in degrees, -90 (down) .. +90 (up)
  uint8_t swing;       // 0-255 swing intensity for effects (eased)
  bool swinging;       // inside a swing (hysteresis)
  uint16_t swingPeak;  // peak of the last IMU_SWING
  uint8_t clashLevel;  // of the last IMU_CLASH, x0.1 g
  uint8_t shock;       // current high-pass accel magnitude, x0.1 g
  int8_t tempC;
  uint8_t events;
  uint8_t dtMs;        // time since the previous sample
  uint32_t lastMotion; // millis() of the last handling
};

extern Motion motion;

bool imuInit();                 // false if the MPU-6050 doesn't answer
bool imuCalibrateBias(bool save); // hold-still gyro bias; true if the saber was still
bool imuCalibrateAxis();        // blade pointing up: find which sensor axis is the blade
void imuUpdate(uint32_t now);
void imuSetLowPower(bool on);
