#pragma once
#include "config.h"
#include <stdint.h>
// Imp AI and bitmap overlay use the existing page-major ringbuffer (32/64/128 columns).
// Move/update monsters only at frame start, BEFORE streamed OLED columns.


void nanoSpritesBeginFrame(uint32_t now);
void nanoSpritesPaintColumns(uint8_t beginX,uint8_t endX,uint8_t *framebuffer);
#if NANO_STATUS_HUD
void nanoSpritesPaintHudColumns(uint8_t beginX, uint8_t endX, uint8_t *framebuffer);
#endif
void nanoSpritesFire(uint32_t now);             // BUTTON_A, rising edge
uint8_t nanoSpritesHealth();                    // player HP 0..100
uint8_t nanoSpritesAlive();                     // remaining monsters
void nanoSpritesReset();                        // host tests / optional restart

uint8_t nanoSpritesAmmo();
uint8_t nanoSpritesArmor();
uint8_t nanoSpritesActive();
uint8_t nanoSpritesThingState(uint8_t index);

// 0=no event, 1=pickup, 2=damage; consumes pending OLED hardware-flash event.
uint8_t nanoSpritesConsumeEffect();
