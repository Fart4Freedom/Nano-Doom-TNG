#pragma once
#include "config.h"
#include <stdint.h>

// Nano-Doom TNG v15 beta: polling-only page-mode SSD1309/SSD1306 driver.
// No TWI ISR, Wire, dynamic allocation or C++ display object.

#ifndef NANO_FB_COLUMNS
#define NANO_FB_COLUMNS 128
#endif

static_assert(NANO_FB_COLUMNS == 32 || NANO_FB_COLUMNS == 64 || NANO_FB_COLUMNS == 128,
              "Framebuffer must be 32, 64 or 128 columns");

// External variables
extern uint8_t oledFrame[8 * NANO_FB_COLUMNS];
extern uint8_t oledErrors;

// Function prototypes
void oledInit();
void oledClear();
void oledPixel(uint8_t x, uint8_t y);
void oledClearColumns(uint8_t first, uint8_t count);
void oledFlushColumns(uint8_t first, uint8_t last); // synchronous, inclusive; ring-safe source indices
void oledInvert(bool inverted);                    // hardware A6/A7; call outside transfer
