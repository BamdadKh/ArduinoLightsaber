/*
  Lightsaber State Machine with Swing Detection, DFPlayer Mini Sound,
  OneButton input, and WS2812B blade with *only* ignite/extinguish animations.
  - Ignite: LEDs turn on one-by-one (simple)
  - Extinguish: LEDs turn off one-by-one (simple)
  - All other visual animations removed (no pulse, no flicker, no tip flash)

  Hardware:
  - Arduino Nano (AVR)
  - MPU6050 on I2C (A4 = SDA, A5 = SCL)
  - DFPlayer Mini on Software Serial (D10 = TX, D11 = RX)
  - Power button on D2 (OneButton)
  - WS2812B on D6
*/

#include <Wire.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <OneButton.h>
#include <FastLED.h>

#define PIN_BTN_POWER 2
#define PIN_DFPLAYER_TX 11  // Arduino TX -> DFPlayer RX
#define PIN_DFPLAYER_RX 10  // Arduino RX -> DFPlayer TX
#define PIN_DFPLAYER_BUSY 7 // DFPlayer BUSY -> Arduino pin 7

// WS2812B settings - change to your pin / LED count
#define LED_PIN 6
#define NUM_LEDS 144
#define LED_BRIGHTNESS 160 // 0-255

CRGB leds[NUM_LEDS];

const uint8_t MPU_ADDR = 0x68;
const uint8_t REG_PWR_MGMT_1 = 0x6B;
const uint8_t REG_ACCEL_XOUT_H = 0x3B;

enum SoundFile {
  SOUND_HUM = 2,
  SOUND_IGNITE = 1,
  SOUND_SWING = 3,
  SOUND_RETRACT = 4
};

const float ACCEL_SCALE = 16384.0f; // LSB/g for ±2g
const unsigned long IMU_SAMPLE_MS = 10; // 100 Hz sampling

const float SWING_START_DELTA_G = 0.60f;
const float SWING_END_DELTA_G = 0.25f;
const float MOVE_DETECT_DELTA_G = 0.05f;
const unsigned long SWING_MIN_DURATION_MS = 100;
const unsigned long SWING_COOLDOWN_MS = 1000;
const uint8_t ACCEL_MOVAVG_WINDOW = 10;

const unsigned long IDLE_TIMEOUT_MS = 60000; // 1 min no motion -> Full off

enum LightsaberState {
  STATE_FULL_OFF,
  STATE_BLADE_OFF,
  STATE_BLADE_ON,
  STATE_SWING
};

struct AccelData {
  float ax = 0;
  float ay = 0;
  float az = 0;
  float mag = 0;
};

const char* getStateName(LightsaberState state) {
  switch (state) {
    case STATE_FULL_OFF: return "FULL_OFF";
    case STATE_BLADE_OFF: return "BLADE_OFF";
    case STATE_BLADE_ON: return "BLADE_ON";
    case STATE_SWING: return "SWING";
    default: return "UNKNOWN";
  }
}

LightsaberState currentState = STATE_FULL_OFF;
AccelData accelBaseline;
AccelData accelBuffer[ACCEL_MOVAVG_WINDOW];
uint8_t accelIdx = 0;

unsigned long lastIMUTime = 0;
unsigned long lastSwingTime = 0;
unsigned long swingStartTime = 0;
unsigned long lastSwingStartTime = 0;
unsigned long lastMoveTime = 0;

// LED / color state
uint8_t colorIndex = 0;
CRGB colorPalette[] = {
  CRGB::Blue,
  CRGB::Green,
  CRGB::Red,
  CRGB::Purple,
  CRGB::Cyan,
  CRGB::Orange,
  CRGB::White
};
const uint8_t COLOR_COUNT = sizeof(colorPalette) / sizeof(colorPalette[0]);

uint8_t bladeLength = NUM_LEDS; // full length when blade on
uint8_t currentLit = 0; // number of lit LEDs (for animation)

// DFPlayer + OneButton
SoftwareSerial dfSerial(PIN_DFPLAYER_RX, PIN_DFPLAYER_TX);
DFRobotDFPlayerMini dfPlayer;

// OneButton instance: activeLow=true, enable internal pullup=true
OneButton powerButton(PIN_BTN_POWER, true, true);

// Sound state flags (to avoid cutting/restarting swing)
bool isHumPlaying = false;
bool isSwingPlaying = false;

// Forward declarations
void transitionToFullOff();
void transitionToBladeOff();
void transitionToBladeOn(bool printMessage = true);
void transitionToSwing(float delta, unsigned long now);
void handleButtonClick();
void handleButtonLongPressStart();
void handleButtonDoubleClick();

void dfInit();
void playIgniteSound();
void playHumSound();
void playSwingSound();
void playRetractSound();
void stopAllSounds();

bool readMPUBytes(uint8_t devAddr, uint8_t regAddr, uint8_t length, uint8_t* buffer);
bool readAccelData(AccelData& data);
void updateMovingAverage(const AccelData& newData);
AccelData getSmoothedAccel();
float calculateDeltaFromBaseline(const AccelData& current);
bool isSwingStartCondition(float delta, unsigned long now);
bool isMoving(float delta, unsigned long now);
bool isSwingEndCondition(float delta, unsigned long now);
bool isIdleTimeout(unsigned long now);
void resetSwingDetection();
void calibrateBaseline();
void initializeMPU();

void igniteAnimation(unsigned long durationMs = 700);
void extinguishAnimation(unsigned long durationMs = 600);
void setBladeColor(const CRGB &c);
void bladeClear();

//////////////////////
// DFPlayer helpers //
//////////////////////

void dfInit() {
  dfSerial.begin(9600);
  Serial.println(F("Initializing DFPlayer..."));
  delay(1000); // Wait a bit for DFPlayer to start
  
  if (!dfPlayer.begin(dfSerial)) {
    Serial.println(F("DFPlayer begin() failed!"));
    Serial.println(F("1. Check wiring (TX/RX, 1K resistor on RX)"));
    Serial.println(F("2. Check power supply"));
    Serial.println(F("3. Check SD card is inserted"));
    while(true) {
      delay(1000); // Halt if DFPlayer fails
    }
  }
  
  Serial.println(F("DFPlayer begin() OK"));
  delay(200);
  
  int fileCount = dfPlayer.readFileCounts();
  Serial.print(F("Files on SD card: "));
  Serial.println(fileCount);
  
  if (fileCount <= 0) {
    Serial.println(F("ERROR: No files found on SD card!"));
    while(true) {
      delay(1000); // Halt if no files
    }
  }
  
  dfPlayer.volume(20); // Set default volume (0-30)
  delay(200);
  Serial.println(F("DFPlayer ready!"));
}

void playIgniteSound() {
  Serial.println(F("Playing ignite sound (track 1)"));
  dfPlayer.play(SOUND_IGNITE);
  isHumPlaying = false;
  isSwingPlaying = false;
  delay(50);
}

void playHumSound() {
  Serial.println(F("Playing hum sound (track 2 loop)"));
  dfPlayer.play(SOUND_HUM);
  isHumPlaying = true;
  isSwingPlaying = false;
  delay(50);
}

void playSwingSound() {
  Serial.println(F("Playing swing sound (track 3 - full)"));
  dfPlayer.play(SOUND_SWING);
  isSwingPlaying = true;
  isHumPlaying = false;
  delay(50);
}

void playRetractSound() {
  Serial.println(F("Playing retract sound (track 4)"));
  dfPlayer.play(SOUND_RETRACT);
  isHumPlaying = false;
  isSwingPlaying = false;
  delay(50);
}

void stopAllSounds() {
  Serial.println(F("Stopping all sounds"));
  dfPlayer.stop();
  isHumPlaying = false;
  isSwingPlaying = false;
  delay(50);
}

//////////////////////
// MPU helpers //
//////////////////////

bool readMPUBytes(uint8_t devAddr, uint8_t regAddr, uint8_t length, uint8_t* buffer) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((int)devAddr, (int)length);
  uint8_t i = 0;
  while (Wire.available() && i < length) buffer[i++] = Wire.read();
  return (i == length);
}

bool readAccelData(AccelData& data) {
  uint8_t buf[14];
  if (!readMPUBytes(MPU_ADDR, REG_ACCEL_XOUT_H, 14, buf)) return false;
  
  auto s16 = [&](int idx) -> int16_t { 
    return (int16_t)((buf[idx] << 8) | buf[idx + 1]); 
  };
  
  data.ax = (float)s16(0) / ACCEL_SCALE;
  data.ay = (float)s16(2) / ACCEL_SCALE;
  data.az = (float)s16(4) / ACCEL_SCALE;
  data.mag = sqrt(data.ax * data.ax + data.ay * data.ay + data.az * data.az);
  
  return true;
}

void updateMovingAverage(const AccelData& newData) {
  accelBuffer[accelIdx] = newData;
  accelIdx = (accelIdx + 1) % ACCEL_MOVAVG_WINDOW;
}

AccelData getSmoothedAccel() {
  AccelData smoothed;
  for (uint8_t i = 0; i < ACCEL_MOVAVG_WINDOW; ++i) {
    smoothed.ax += accelBuffer[i].ax;
    smoothed.ay += accelBuffer[i].ay;
    smoothed.az += accelBuffer[i].az;
  }
  smoothed.ax /= ACCEL_MOVAVG_WINDOW;
  smoothed.ay /= ACCEL_MOVAVG_WINDOW;
  smoothed.az /= ACCEL_MOVAVG_WINDOW;
  smoothed.mag = sqrt(smoothed.ax * smoothed.ax + 
                      smoothed.ay * smoothed.ay + 
                      smoothed.az * smoothed.az);
  return smoothed;
}

float calculateDeltaFromBaseline(const AccelData& current) {
  return fabs(current.mag - accelBaseline.mag);
}

bool isSwingStartCondition(float delta, unsigned long now) {
  return delta >= SWING_START_DELTA_G && 
         (now - lastSwingStartTime) > SWING_COOLDOWN_MS;
}

bool isMoving(float delta, unsigned long now) {
  bool move = (delta >= MOVE_DETECT_DELTA_G);
  if (move) {
    lastMoveTime = now;
  }
  return move;
}

bool isSwingEndCondition(float delta, unsigned long now) {
  unsigned long duration = now - swingStartTime;
  return delta <= SWING_END_DELTA_G && duration >= SWING_MIN_DURATION_MS;
}

bool isIdleTimeout(unsigned long now) {
  return (now - lastMoveTime) > IDLE_TIMEOUT_MS;
}

void resetSwingDetection() {
  swingStartTime = 0;
  for (uint8_t i = 0; i < ACCEL_MOVAVG_WINDOW; ++i) {
    accelBuffer[i] = accelBaseline;
  }
  accelIdx = 0;
}

void calibrateBaseline() {
  const int CAL_SAMPLES = 80;
  AccelData sum;
  sum.ax = 0;
  sum.ay = 0;
  sum.az = 0;
  sum.mag = 0;
  
  Serial.println(F("Calibrating baseline... keep device still"));
  
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    AccelData sample;
    if (readAccelData(sample)) {
      sum.ax += sample.ax;
      sum.ay += sample.ay;
      sum.az += sample.az;
      sum.mag += sample.mag;
    }
    delay(8);
  }
  
  accelBaseline.ax = sum.ax / CAL_SAMPLES;
  accelBaseline.ay = sum.ay / CAL_SAMPLES;
  accelBaseline.az = sum.az / CAL_SAMPLES;
  accelBaseline.mag = sum.mag / CAL_SAMPLES;
  
  Serial.print(F("Baseline calibrated (g): "));
  Serial.println(accelBaseline.mag, 4);
  
  for (uint8_t i = 0; i < ACCEL_MOVAVG_WINDOW; ++i) {
    accelBuffer[i] = accelBaseline;
  }
}

void initializeMPU() {
  Serial.println(F("Initializing MPU6050..."));
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00); // Wake up MPU6050
  Wire.endTransmission();
  delay(50);
  Serial.println(F("MPU6050 initialized"));
}

//////////////////////
// Blade animations //
//////////////////////

void bladeClear() {
  for (uint8_t i = 0; i < NUM_LEDS; ++i) leds[i] = CRGB::Black;
  FastLED.show();
}

void setBladeColor(const CRGB &c) {
  for (uint8_t i = 0; i < currentLit; ++i) {
    leds[i] = c;
  }
  for (uint8_t i = currentLit; i < NUM_LEDS; ++i) leds[i] = CRGB::Black;
  FastLED.show();
}

// Very simple ignite: light LEDs one-by-one from currentLit -> bladeLength-1
void igniteAnimation(unsigned long durationMs) {
  uint8_t startLit = currentLit;
  uint8_t endLit = bladeLength;
  uint8_t steps = (endLit > startLit) ? (endLit - startLit) : 0;
  unsigned long perLedDelay = (steps > 0) ? (durationMs / steps) : durationMs;

  for (uint8_t i = startLit; i < endLit; ++i) {
    leds[i] = colorPalette[colorIndex];
    FastLED.show();
    if (perLedDelay) delay(perLedDelay);
    currentLit = i + 1;
  }
  currentLit = endLit;
}

// Very simple extinguish: turn LEDs off one-by-one from currentLit-1 -> 0
void extinguishAnimation(unsigned long durationMs) {
  uint8_t startLit = (currentLit > 0) ? currentLit : bladeLength;
  unsigned long perLedDelay = (startLit > 0) ? (durationMs / startLit) : durationMs;

  for (int i = (int)startLit - 1; i >= 0; --i) {
    leds[i] = CRGB::Black;
    FastLED.show();
    if (perLedDelay) delay(perLedDelay);
    currentLit = i;
  }
  currentLit = 0;
  bladeClear();
}

//////////////////////
// State machine    //
//////////////////////

void handleButtonClick() {
  // single click = toggle blade (ignite if off, extinguish if on or swinging)
  Serial.println(F("Button single-click (toggle blade)"));
  if (currentState == STATE_BLADE_ON || currentState == STATE_SWING) {
    // extinguish
    transitionToBladeOff();
  } else {
    // ignite
    transitionToBladeOn();
  }
}

void handleButtonLongPressStart() {
  Serial.println(F("Button long-press -> FULL_OFF"));
  transitionToFullOff();
}

void handleButtonDoubleClick() {
  // cycle color
  colorIndex = (colorIndex + 1) % COLOR_COUNT;
  Serial.print(F("Color changed to index "));
  Serial.println(colorIndex);
  if (currentLit > 0) {
    setBladeColor(colorPalette[colorIndex]);
  }
}

void handleStateFullOff(unsigned long now) {
  AccelData rawAccel;
  if (readAccelData(rawAccel)) {
    updateMovingAverage(rawAccel);
    AccelData smoothed = getSmoothedAccel();
    float delta = calculateDeltaFromBaseline(smoothed);
    if (isMoving(delta, now)) {
      transitionToBladeOff();
    }
  }
}

void handleStateBladeOff(unsigned long now) {
  AccelData rawAccel;
  if (!readAccelData(rawAccel)) return;
  updateMovingAverage(rawAccel);
  AccelData smoothed = getSmoothedAccel();
  float delta = calculateDeltaFromBaseline(smoothed);
  if (isMoving(delta, now)) {
    lastSwingTime = now;
  }
  if (isIdleTimeout(now)) {
    transitionToFullOff();
    return;
  }
}

void handleStateBladeOn(unsigned long now) {
  AccelData rawAccel;
  if (!readAccelData(rawAccel)) return;
  updateMovingAverage(rawAccel);
  AccelData smoothed = getSmoothedAccel();
  float delta = calculateDeltaFromBaseline(smoothed);
  if (isMoving(delta, now)) {
    lastSwingTime = now;
  }
  if (isSwingStartCondition(delta, now)) {
    transitionToSwing(delta, now);
    return;
  }
  // Ensure hum is playing (but only if not in swing)
  if (!isHumPlaying && !isSwingPlaying) {
    playHumSound();
  }
  // Keep blade steady color (no visual pulse)
  if (currentLit > 0) setBladeColor(colorPalette[colorIndex]);
  if (isIdleTimeout(now)) {
    transitionToFullOff();
  }
}

void handleStateSwing(unsigned long now) {
  AccelData rawAccel;
  if (!readAccelData(rawAccel)) return;
  updateMovingAverage(rawAccel);
  AccelData smoothed = getSmoothedAccel();
  float delta = calculateDeltaFromBaseline(smoothed);

  // No visual flare during swing (per request)

  if (isSwingEndCondition(delta, now)) {
    isSwingPlaying = false; // signal software that swing event ended
    transitionToBladeOn();
  }
}

void transitionToFullOff() {
  Serial.print(F("Transitioning from "));
  Serial.print(getStateName(currentState));
  Serial.println(F(" to FULL_OFF"));
  currentState = STATE_FULL_OFF;
  stopAllSounds();
  extinguishAnimation();
  bladeClear();
}

void transitionToBladeOff() {
  Serial.print(F("Transitioning from "));
  Serial.print(getStateName(currentState));
  Serial.println(F(" to BLADE_OFF"));
  LightsaberState lastState = currentState;
  currentState = STATE_BLADE_OFF;
  lastMoveTime = millis();

  // Play retract sound but DO NOT forcibly stop swing sound if it's currently playing.
  playRetractSound();

  // Visual extinguish (simple)
  extinguishAnimation();
}

void transitionToBladeOn(bool printMessage) {
  Serial.print(F("Transitioning from "));
  Serial.print(getStateName(currentState));
  Serial.println(F(" to BLADE_ON"));
  LightsaberState prevState = currentState;
  currentState = STATE_BLADE_ON;
  lastMoveTime = millis();
  resetSwingDetection();
  bladeLength = NUM_LEDS;

  if (prevState == STATE_BLADE_OFF || prevState == STATE_FULL_OFF) {
    playIgniteSound();
    igniteAnimation(700);
    // start hum immediately after ignite (hum is a loop/continuous track)
    playHumSound();
  } else if (prevState == STATE_SWING) {
    // Coming back from swing: ensure we set visual and hum once swing done
    playHumSound();
    currentLit = bladeLength;
    setBladeColor(colorPalette[colorIndex]);
  } else {
    playHumSound();
    currentLit = bladeLength;
    setBladeColor(colorPalette[colorIndex]);
  }
}

void transitionToSwing(float delta, unsigned long now) {
  Serial.print(F("Transitioning from "));
  Serial.print(getStateName(currentState));
  Serial.print(F(" to SWING (delta g): "));
  Serial.println(delta, 3);
  currentState = STATE_SWING;
  swingStartTime = now;
  lastSwingStartTime = now;

  // Start the swing sound and mark that swing sound is playing.
  playSwingSound();

  // No visual tip flash - keep blade as-is

  // small, short pause to let the swing sound start (retain previous behavior)
  delay(900);
}

//////////////////////
// main loop / setup //
//////////////////////

void updateStateMachine() {
  unsigned long now = millis();
  powerButton.tick(); // process OneButton events
  switch (currentState) {
    case STATE_FULL_OFF:
      handleStateFullOff(now);
      break;
    case STATE_BLADE_OFF:
      handleStateBladeOff(now);
      break;
    case STATE_BLADE_ON:
      handleStateBladeOn(now);
      break;
    case STATE_SWING:
      handleStateSwing(now);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait up to 3s for Serial

  Serial.println(F("\n=== Lightsaber Controller (minimal animations) ==="));

  Wire.begin();

  pinMode(PIN_BTN_POWER, INPUT_PULLUP);
  pinMode(PIN_DFPLAYER_BUSY, INPUT);

  powerButton.attachClick(handleButtonClick);
  powerButton.attachDoubleClick(handleButtonDoubleClick); // double click cycles color
  powerButton.attachLongPressStart(handleButtonLongPressStart); // long press = full off

  // FastLED init
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  bladeClear();

  dfInit();
  initializeMPU();
  calibrateBaseline();

  currentState = STATE_BLADE_OFF; // start in blade off
  lastMoveTime = millis();
  lastIMUTime = millis();

  Serial.println(F("Ready. Single-click: ignite/extinguish. Double-click: change color. Long-press: full off."));
}

void loop() {
  unsigned long now = millis();
  powerButton.tick(); // keep button responsive

  if (currentState == STATE_BLADE_ON || currentState == STATE_SWING) {
    if (now - lastIMUTime >= IMU_SAMPLE_MS) {
      lastIMUTime = now;
      updateStateMachine();
    }
  } else {
    updateStateMachine();
    delay(40);
  }
}
