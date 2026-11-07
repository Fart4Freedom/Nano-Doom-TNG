#pragma once
#include <stdint.h>
#include "config.h"

// MCU-independent E1M1 outline engine. OLED pixel callback receives 0..127, 0..63.
// The renderer streams just TWO columns of 8-bit surface IDs (128 bytes RAM).
void engineInit();
bool engineMapReady();
void engineUse(uint32_t now);
void engineUpdateDoors(uint32_t now);
void engineTurn(float radians);
void engineMove(float forward, float strafe);
// Low-memory Imp AI helpers; pass actor coordinates and current sector.
void engineMoveMonster(float *x, float *y, uint8_t *sector,float dx,float dy);
float engineEye();
int8_t engineSectorFloor(uint8_t sector);
// Incremental renderer: call Begin once, then Step until it returns true.
// Step renders one sample column (two screen columns when STEP=2).
// After Step, columns [0, engineRenderNextX()-NANO_RENDER_STEP-1]
// are final. The last Step finalizes all 128 columns.
void engineRenderBegin();
// putPageByte is optional; with dithering it ORs 8 vertical OLED pixels
// into the framebuffer at the given page (0..7). nullptr falls back
// to ordinary putPixel(), for portable PC tests.
bool engineRenderStep(void (*putPixel)(uint8_t x, uint8_t y),
                      void (*putPageByte)(uint8_t x, uint8_t page, uint8_t mask) = nullptr);
uint8_t engineRenderNextX();
void engineRender(void (*putPixel)(uint8_t x, uint8_t y),
                  void (*putPageByte)(uint8_t x, uint8_t page, uint8_t mask) = nullptr);
float engineX();
float engineY();
float engineAngle();
uint8_t engineSector();
bool engineCanSeeSprite(float x, float y); // conservative portal LOS, 1 test/frame
void engineTeleport(float x, float y, uint8_t sector, float degrees); // host diagnostics

#if NANO_PROFILE
void engineResetProfile();
uint32_t engineCandidateChecks();
uint32_t engineExactChecks();
#endif

// Two ray slots: the streamer paints the preceding group after tracing the next.
struct SpriteClipTarget {uint16_t depth64; uint8_t lo[2],hi[2];};
void engineSetSpriteTargets(SpriteClipTarget *targets,uint8_t count);
int8_t engineSectorCeiling(uint8_t sector);
