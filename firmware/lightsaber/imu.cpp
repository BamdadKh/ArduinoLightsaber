#include "imu.h"
#include "twi.h"
#include "settings.h"
#include "config.h"
#include "mathx.h"

#define MPU_ADDR 0x68

Motion motion;

static int32_t gravQ[3];      // low-passed accel, raw << 4 (tau ~120 ms)
static int16_t lastRawGyro[3];
static int16_t calAccel[3];   // mean accel from the last bias calibration
static uint8_t stillCount;

// asin() for 0..16/16 in degrees, used for the blade pitch readout
// sensitivity 1-9 -> threshold scale, x64 (5 = 1.0)
static const uint8_t SENS_K[10] PROGMEM = {128, 107, 91, 80, 71, 64, 58, 53, 49, 46};
static const uint8_t ASIN_DEG[17] PROGMEM = {0, 4, 7, 11, 14, 18, 22, 26, 30, 34, 39, 43, 49, 54, 61, 69, 90};

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
  twiWriteReg(MPU_ADDR, 0x1A, 0x02); // DLPF ~94 Hz: keeps clash spikes, drops buzz
  twiWriteReg(MPU_ADDR, 0x1B, 0x18); // gyro +-2000 dps (16.4 LSB/dps)
  twiWriteReg(MPU_ADDR, 0x1C, 0x18); // accel +-16 g (2048 LSB/g), clashes are violent
  int16_t a[3], t, g[3];
  if (!readRaw(a, &t, g)) return false;
  for (uint8_t i = 0; i < 3; i++) gravQ[i] = (int32_t)a[i] << 4;
  motion.lastMotion = millis();
  return true;
}

bool imuCalibrateBias(bool save) {
  int32_t sum[6] = {0, 0, 0, 0, 0, 0}; // gyro xyz, accel xyz
  int16_t first[3];
  for (uint8_t n = 0; n < 32; n++) {
    int16_t v[6], t;
    if (!readRaw(v + 3, &t, v)) return false;
    for (uint8_t i = 0; i < 3; i++) {
      if (!n) first[i] = v[i];
      else if (v[i] - first[i] > 60 || first[i] - v[i] > 60) return false; // moved: keep the old bias
    }
    for (uint8_t i = 0; i < 6; i++) sum[i] += v[i];
    delay(4);
  }
  for (uint8_t i = 0; i < 3; i++) {
    cfg.gyroBias[i] = sum[i] >> 5;
    calAccel[i] = sum[i + 3] >> 5;
  }
  if (save) settingsDirty();
  return true;
}

bool imuCalibrateAxis() {
  if (!imuCalibrateBias(false)) return false;
  uint8_t best = 0;
  for (uint8_t i = 1; i < 3; i++)
    if (abs(calAccel[i]) > abs(calAccel[best])) best = i;
  if (abs(calAccel[best]) < 1600) return false; // not pointing (near) straight up or down
  cfg.axis = best * 2 + (calAccel[best] < 0 ? 1 : 0);
  settingsDirty();
  return true;
}

void imuUpdate(uint32_t now) {
  static uint32_t last;
  static uint32_t lastAccent, lastClash, lastStab, twistT, lastTwist;
  static uint16_t peak;
  static bool armed;
  static uint32_t armT;
  static int8_t twistDir;

  uint32_t dt = now - last;
  if (dt < 3) return;
  int16_t a[3], t, g[3];
  if (!readRaw(a, &t, g)) return;
  last = now;
  motion.events |= IMU_SAMPLE;
  motion.dtMs = dt > 60 ? 60 : dt;
  motion.tempC = (int8_t)((((int32_t)t + 12420) * 3) >> 10); // /340 + 36.5

  // --- gyro in deg/s, blade frame
  int16_t dps[3];
  bool still = true;
  for (uint8_t i = 0; i < 3; i++) {
    int16_t r = g[i] - cfg.gyroBias[i];
    dps[i] = (int16_t)(((int32_t)r * 125) >> 11);
    if (dps[i] > 3 || dps[i] < -3) still = false;
    lastRawGyro[i] = g[i];
  }
  uint8_t ax = cfg.axis >> 1;
  bool neg = cfg.axis & 1;
  uint8_t p1 = ax == 0 ? 1 : 0, p2 = ax == 2 ? 1 : 2;
  int16_t roll = neg ? -dps[ax] : dps[ax];
  motion.rollDps = roll;
  uint16_t sw = isqrt32((int32_t)dps[p1] * dps[p1] + (int32_t)dps[p2] * dps[p2]);
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
  motion.shock = shock;
  int16_t along = (int16_t)(gravQ[ax] >> 4);
  if (neg) along = -along;
  motion.along = along;
  {
    int16_t r = along < -2048 ? -2048 : along > 2048 ? 2048 : along;
    uint16_t m = r < 0 ? -r : r;
    int8_t deg = pgm_read_byte(&ASIN_DEG[(m + 64) >> 7]);
    motion.pitch = r < 0 ? -deg : deg;
  }

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

  uint8_t k = pgm_read_byte(&SENS_K[cfg.swingSens]);
  uint16_t thrStart = (SWING_START_DPS * k) >> 6;
  uint16_t thrEnd = (SWING_END_DPS * k) >> 6;

  // --- handling / idle detection
  int16_t aroll = roll < 0 ? -roll : roll;
  if (sw > MOTION_DPS || aroll > MOTION_DPS * 2 || shock > 2) {
    motion.lastMotion = now;
    motion.events |= IMU_MOVED;
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
  if (motion.swinging && !armed && now - lastAccent > 450 && sw > thrStart * 2) {
    armed = true; // continuous spin: re-accent
    peak = sw;
  }
  if (armed) {
    if (!armT) armT = now;
    if (sw > peak) peak = sw;
    if (sw + 30 < peak || now - armT > 80 || !motion.swinging) {
      armed = false;
      armT = 0;
      if (now - lastAccent > 160) {
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

  // --- stab: sharp thrust along the blade with little rotation
  int16_t thrust = (int16_t)(((int32_t)(neg ? -hp[ax] : hp[ax]) * 5) >> 10); // x0.1 g
  if (thrust > STAB_G_X10 && sw < 200 && (int32_t)thrust * thrust * 2 > (int32_t)shock * shock &&
      now - lastStab > 500) {
    motion.events |= IMU_STAB;
    lastStab = now;
  }

  // --- clash: high-pass shock over a threshold that rises with swing speed
  uint16_t clashThr = ((CLASH_G_X10 * pgm_read_byte(&SENS_K[cfg.clashSens])) >> 6) + (sw >> 7);
  if (shock > clashThr && now - lastClash > 140 && now - lastStab > 160) {
    motion.events |= IMU_CLASH;
    motion.clashLevel = shock;
    lastClash = now;
  }

  // --- twist: roll one way then the other within 400 ms, with the blade otherwise steady
  if (aroll > (int16_t)TWIST_DPS && aroll > (int16_t)sw) {
    int8_t dir = roll > 0 ? 1 : -1;
    if (twistDir == -dir && now - twistT < 400 && now - lastTwist > 700) {
      motion.events |= IMU_TWIST;
      lastTwist = now;
      twistDir = 0;
    } else if (twistDir != -dir || now - twistT >= 400) {
      twistDir = dir;
    }
    if (twistDir == dir) twistT = now;
  }
}
