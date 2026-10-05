// OLED user interface: every screen is drawn as scanlines and streamed a slice
// per loop() pass, so the display never blocks the blade or the motion engine.
#pragma once
#include <stdint.h>

void uiInit();
void uiPump(uint32_t now);                 // call every loop()
void uiToast(const char* msgP, int16_t num = -1, uint16_t ms = 1300);
void uiFlash(uint32_t now);                // hardware-inverted flash (clash)
void uiSetFlip(bool flip);
void uiRedraw();                           // restart the frame immediately

// exposed for the PC simulator
void uiBeginFrame(uint32_t now);
uint32_t uiRow(uint8_t y);
