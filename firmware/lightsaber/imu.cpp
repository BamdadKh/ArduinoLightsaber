#include "imu.h"
#include "twi.h"
#include "settings.h"
#include "config.h"
#include "mathx.h"

#define MPU_ADDR 0x68

Motion motion;

static int32_t gravQ[3];      // low-passed accel, raw << 4 (tau ~120 ms)
static int16_t lastRawGyro[3];
static uint8_t stillCount;

// sensitivity 1-9, as a threshold scale, x64 (5 = 1.0)
static const uint8_t SENS_K[10] PROGMEM = {128, 107, 91, 80, 71, 64, 58, 53, 49, 46};

static bool readRaw(int16_t a[3], int16_t* temp, int16_t g[3]) {
  uint8_t b[14];
  if (!twiReadRegs(MPU_ADDR, 0x3B, b, 14)) return false;
  for (uint8_t i = 0; i < 3; i++) {
    a[i] = (int16_t)(b[2 * i] << 8 | b[2 * i + 1]);
    g[i] = (int16_t)(b[8 + 2 * i] << 8 | b[9 + 2 * i]);
  }
  *temp = (int16_t)(b[6] << 8 | b[7]);
  return true;
}

bool imuInit() {
  if (!twiWriteReg(MPU_ADDR, 0x6B, 0x80)) return false; // reset
  delay(60);
  twiWriteReg(MPU_ADDR, 0x6B, 0x01); // wake, PLL on gyro X
  delay(10);
  twiWriteReg(MPU_ADDR, 0x19, 0x00); // 1 kHz sample rate
  twiWriteReg(MPU_ADDR, 0x1A, 0x02); // DLPF ~94 Hz: drops buzz
  twiWriteReg(MPU_ADDR, 0x1B, 0x18); // gyro +-2000 dps (16.4 LSB/dps)
  twiWriteReg(MPU_ADDR, 0x1C, 0x18); // accel +-16 g (2048 LSB/g)
  int16_t a[3], t, g[3];
  if (!readRaw(a, &t, g)) return false;
  for (uint8_t i = 0; i < 3; i++) gravQ[i] = (int32_t)a[i] << 4;
  motion.lastMotion = millis();
  return true;
}

// Average the gyro while the saber lies still; false if it moved (the old bias is kept).
bool imuCalibrateBias(bool save) {
  int32_t sum[3] = {0, 0, 0};
  int16_t first[3];
  for (uint8_t n = 0; n < 64; n++) {
    int16_t a[3], t, g[3];
    if (!readRaw(a, &t, g)) return false;
    for (uint8_t i = 0; i < 3; i++) {
      if (!n) first[i] = g[i];
      else if (g[i] - first[i] > 60 || first[i] - g[i] > 60) return false; // moved
      sum[i] += g[i];
    }
    delay(4);
  }
  for (uint8_t i = 0; i < 3; i++) cfg.gyroBias[i] = sum[i] >> 6;
  if (save) settingsDirty();
  return true;
}

void imuUpdate(uint32_t now) {
  static uint32_t last;
  static uint32_t lastAccent;
  static uint16_t peak;
  static bool armed;
  static uint32_t armT;

  uint32_t dt = now - last;
  if (dt < 3) return;
  int16_t a[3], t, g[3];
  if (!readRaw(a, &t, g)) return;
  last = now;

  // --- gyro in deg/s
  int16_t dps[3];
  bool still = true;
  for (uint8_t i = 0; i < 3; i++) {
    int16_t r = g[i] - cfg.gyroBias[i];
    dps[i] = (int16_t)(((int32_t)r * 125) >> 11);
    if (dps[i] > 3 || dps[i] < -3) still = false;
    lastRawGyro[i] = g[i];
  }
  // Total rotation speed: no sensor axis to calibrate, however the board sits in the hilt.
  uint16_t sw = isqrt32((int32_t)dps[0] * dps[0] + (int32_t)dps[1] * dps[1] + (int32_t)dps[2] * dps[2]);
  motion.swingDps = sw;

  // --- accel: gravity estimate and high-pass "shock"
  int32_t shock2 = 0;
  int16_t hp[3];
  for (uint8_t i = 0; i < 3; i++) {
    gravQ[i] += (((int32_t)a[i] << 4) - gravQ[i]) >> 4;
    hp[i] = a[i] - (int16_t)(gravQ[i] >> 4);
    int16_t h = hp[i] >> 2;
    shock2 += (int32_t)h * h;
  }
  uint8_t shock = (uint8_t)min((uint16_t)255, (uint16_t)((isqrt32(shock2) * 5u) >> 8)); // x0.1 g

  // --- slow gyro drift tracking while resting
  if (still && shock < 2) {
    if (stillCount < 255) stillCount++;
    if (stillCount > 120) {
      for (uint8_t i = 0; i < 3; i++) {
        if (lastRawGyro[i] > cfg.gyroBias[i]) cfg.gyroBias[i]++;
        else if (lastRawGyro[i] < cfg.gyroBias[i]) cfg.gyroBias[i]--;
      }
    }
  } else {
    stillCount = 0;
  }

  uint8_t k = pgm_read_byte(&SENS_K[cfg.sens]);
  uint16_t thrStart = (SWING_START_DPS * k) >> 6;
  uint16_t thrEnd = (SWING_END_DPS * k) >> 6;

  // --- handling / idle detection
  if (sw > MOTION_DPS || shock > 2) {
    motion.lastMotion = now;
  }

  // --- swing: hysteresis + peak detection gives one accent per stroke
  if (!motion.swinging) {
    if (sw > thrStart) {
      motion.swinging = true;
      armed = true;
      peak = sw;
    }
  } else if (sw < thrEnd) {
    motion.swinging = false;
  }
  if (motion.swinging && !armed && now - lastAccent > SWING_GAP_MS * 3 / 2 && sw > thrStart * 2) {
    armed = true; // continuous spin: re-accent
    peak = sw;
  }
  if (armed) {
    if (!armT) armT = now;
    if (sw > peak) peak = sw;
    if (sw + 30 < peak || now - armT > 80 || !motion.swinging) {
      armed = false;
      armT = 0;
      if (now - lastAccent > SWING_GAP_MS) { // a new swing sound would cut the one still playing
        motion.events |= IMU_SWING;
        motion.swingPeak = peak;
        lastAccent = now;
      }
    }
  }
  int16_t target = sw > thrEnd ? (int16_t)(((uint32_t)(sw - thrEnd) * 73) >> 8) : 0; // full at +900 dps
  if (target > 255) target = 255;
  int16_t s = motion.swing;
  s += target > s ? (target - s + 1) / 2 : -((s - target + 7) / 8);
  motion.swing = clamp8(s);
}
