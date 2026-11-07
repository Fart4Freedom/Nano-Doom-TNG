
#include <Arduino.h>
#include "config.h"
#include "oled_tng.h"
#include "nano_font.h"
#include "engine.h"

#if NANO_ENABLE_SPRITES
#include "nano_sprites.h"
#endif

// Optional, low-cost walk bob. Can be overridden in config.h.
// Applied to completed OLED columns, so no changes to engine.cpp are needed.
#ifndef NANO_VIEW_BOB
#define NANO_VIEW_BOB 1
#endif
#ifndef NANO_VIEW_BOB_PIXELS
#define NANO_VIEW_BOB_PIXELS 2 // 1 = subtle, 2 = more visible.
#endif
#if NANO_VIEW_BOB && (NANO_VIEW_BOB_PIXELS < 1 || NANO_VIEW_BOB_PIXELS > 3)
#error "NANO_VIEW_BOB_PIXELS must be between 1 and 3"
#endif

static uint32_t lastInput = 0;
static uint32_t frameStart = 0;
static uint32_t renderTime = 0;
static uint32_t lastFrameMs = 0;

static bool rendering = false;
static bool allRaysDone = false;
static bool deathPainted = false;
static bool halfInputDone = false;

static uint8_t spritesPaintedX = 0;
static uint8_t flushedX = 0;
static uint8_t finishedX = 0; // Exclusive end positions.

#if NANO_VIEW_BOB
// Integer phase (no sinf() or float), advancing only while walking.
// The rendered frame keeps the same offset throughout its column transfers.
static uint8_t walkPhase = 0;
static uint32_t lastWalkMs = 0;
static int8_t frameBobY = 0;

static inline void updateWalkBob(uint32_t now, uint32_t dt, bool walking) {
    if (walking) {
        walkPhase += (uint8_t)(dt >> 4); // One cycle in ~512 ms.
        lastWalkMs = now;
    }
}

static inline void beginFrameBob(uint32_t now) {
    frameBobY = 0;
    if ((uint32_t)(now - lastWalkMs) > 130u) return;

    // Alternate down and up, separated by neutral positions.
    switch ((walkPhase >> 3) & 3u) {
        case 1: frameBobY = NANO_VIEW_BOB_PIXELS; break;
        case 3: frameBobY = -NANO_VIEW_BOB_PIXELS; break;
        default: break;
    }
}

// Move each completed column vertically by 1-3 pixels. No extra framebuffer.
// In-place page order preserves the carry between adjacent 8-pixel pages.
static void bobColumns(uint8_t first, uint8_t end) {
    if (frameBobY == 0) return;

    for (uint8_t x = first; x < end; ++x) {
        const uint8_t col = x & (NANO_FB_COLUMNS - 1);
        uint8_t carry = 0;

        if (frameBobY > 0) {
            // Down one pixel: process pages from top to bottom.
            for (uint8_t pg = 0; pg < 8; ++pg) {
                uint8_t &dst = oledFrame[(uint16_t)pg * NANO_FB_COLUMNS + col];
                const uint8_t old = dst;
                dst = (uint8_t)((old << NANO_VIEW_BOB_PIXELS) | carry);
                carry = old >> (8 - NANO_VIEW_BOB_PIXELS);
            }
        } else {
            // Up one pixel: process pages from bottom to top.
            for (uint8_t pg = 8; pg-- > 0;) {
                uint8_t &dst = oledFrame[(uint16_t)pg * NANO_FB_COLUMNS + col];
                const uint8_t old = dst;
                dst = (uint8_t)((old >> NANO_VIEW_BOB_PIXELS) | carry);
                carry = (uint8_t)(old << (8 - NANO_VIEW_BOB_PIXELS));
            }
        }
    }
}
#endif

#if NANO_CROSSHAIR
static inline void drawCrosshair(uint8_t first, uint8_t end) {

    // Draw a 5x3 crosshair.
    // XOR makes it visible against dark and light backgrounds.

    for (uint8_t x = 62; x <= 66; ++x) {
        if (x < first || x >= end) continue;

        const uint8_t col = x & (NANO_FB_COLUMNS - 1);

        // Do not shift the aim point: the raycaster/hitscan horizon is Y=32.
        constexpr uint8_t cy = 32;
        oledFrame[(uint16_t)(cy >> 3) * NANO_FB_COLUMNS + col] ^=
            (uint8_t)(1u << (cy & 7));
        if (x == 64) {
            oledFrame[(uint16_t)((cy - 1) >> 3) * NANO_FB_COLUMNS + col] ^=
                (uint8_t)(1u << ((cy - 1) & 7));
            oledFrame[(uint16_t)((cy + 1) >> 3) * NANO_FB_COLUMNS + col] ^=
                (uint8_t)(1u << ((cy + 1) & 7));
        }
    }
}
#endif

static void gamePixel(uint8_t x, uint8_t y) {
#if DISPLAY_FPS && !NANO_STATUS_HUD
    // Reserve bottom-left area for the overlay, which was drawn BEFORE tracing.
    if (x < 60 && y >= 54) {
        return;
    }
#endif
    oledPixel(x, y);
}

// Fast dithering path: one callback per SSD1306 byte, not one per pixel.
// framebuffer is page-major, and each page byte contains 8 vertical pixels.
// OR preserves already-set pixels and the optional FPS overlay.
static void oledPageOr(uint8_t x, uint8_t page, uint8_t mask) {
#if DISPLAY_FPS && !NANO_STATUS_HUD
    // Protect the same region that oledPixel() reserves for the FPS text.
    if (x < 60) {
        if (page == 7) return;            // y = 56..63
        if (page == 6) mask &= 0x3Fu;    // y = 54..55 excluded
    }
#endif
    oledFrame[(uint16_t)page * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))] |= mask;
}

// Draw only pixels in the current physical column window. A 32/64-column
// ringbuffer cannot hold the complete YOU DIED screen at once.
static void drawText(uint8_t x, uint8_t y, const char *s, uint8_t first = 0, uint8_t last = 127) {
    while (*s && x < 124) {
        for (uint8_t col = 0; col < 4; col++) {
            uint8_t px = x + col;
            if (px < first || px > last) continue;

            uint8_t bits = nanoFontColumn(*s, col);
            for (uint8_t j = 0; j < 6; j++) {
                if (bits & (1u << j)) {
                    oledPixel(px, y + j);
                }
            }
        }
        ++s;
        x += 5;
    }
}

static void drawFps() {
#if DISPLAY_FPS && !NANO_STATUS_HUD
    char s[12] = "FPS:";
    uint16_t fps = lastFrameMs ? 1000u / lastFrameMs : 0;
    s[4] = '0' + (fps / 100) % 10;
    s[5] = '0' + (fps / 10) % 10;
    s[6] = '0' + fps % 10;
    s[7] = 0;
    drawText(0, 55, s);
#endif
}

static void drawDeath() {
    // Render each non-overlapping window independently, then flush it before
    // reusing the same RAM. Handles all three framebuffer widths.
    for (uint8_t first = 0; first < 128; first += NANO_FB_COLUMNS) {
        const uint8_t last = first + NANO_FB_COLUMNS - 1;
        oledClear();
        drawText(43, 25, "YOU DIED", first, last);
        drawText(30, 37, "A:RESTART", first, last);
        oledFlushColumns(first, last);
    }
}

#if defined(NANO_HOST_OLED_TEST)
// Host-only harness for the real death-screen renderer (not compiled on AVR).
void nanoHostDrawDeath() {
    drawDeath();
}
#endif

// Inverting in OLED hardware costs zero framebuffer RAM.
// Nonblocking in game time: single flash for pickup, double for damage.
static uint8_t fxKind = 0;
static uint8_t fxPhase = 0;
static uint32_t fxUntil = 0;
static bool fxInverted = false;

static void updateOledEffects(uint32_t now) {
#if NANO_ENABLE_SPRITES
    const uint8_t newFx = nanoSpritesConsumeEffect();
    if (newFx) {
        fxKind = newFx;
        fxPhase = 0;
        fxUntil = now;
    }
#endif

    if (!fxKind) return;
    if ((int32_t)(now - fxUntil) < 0) return;

    if (fxInverted) {
        oledInvert(false);
        fxInverted = false;
        fxUntil = now + (fxKind == 2 ? 75u : 0u);
        ++fxPhase;
    } else if (fxPhase < (fxKind == 2 ? 2 : 1)) {
        oledInvert(true);
        fxInverted = true;
        fxUntil = now + (fxKind == 2 ? 105u : 65u);
    } else {
        fxKind = 0;
    }
}


// Poll controls even while the renderer is in progress.
static void pollButtons() {
    const uint32_t now = millis();
    uint32_t dt = now - lastInput;

    if (dt < 20u) return;
    lastInput = now;
    if (dt > 125u) dt = 125u;

    const float moveSpeed = 0.0120f * (float)dt;
    const float turnSpeed = 0.0038f * (float)dt;

#if NANO_DIRECT_BUTTON_PORTS
    // ATmega328P / Nano: D3-D7 on PORTD, D8 on PORTB.
    const uint8_t pd = PIND;
    const uint8_t pb = PINB;

    const bool left = !(pd & _BV(PD7));
    const bool right = !(pd & _BV(PD6));
    const bool up = !(pd & _BV(PD3));
    const bool down = !(pb & _BV(PB0));
    const bool buttonA = !(pd & _BV(PD5));
    const bool buttonB = !(pd & _BV(PD4));
#else
    const bool left = digitalRead(BUTTON_LEFT) == LOW;
    const bool right = digitalRead(BUTTON_RIGHT) == LOW;
    const bool up = digitalRead(BUTTON_UP) == LOW;
    const bool down = digitalRead(BUTTON_DOWN) == LOW;
    const bool buttonA = digitalRead(BUTTON_A) == LOW;
    const bool buttonB = digitalRead(BUTTON_B) == LOW;
#endif

    if (left) engineTurn(-turnSpeed);
    if (right) engineTurn(turnSpeed);

    float forward = 0.0f;
    if (up) forward += moveSpeed;
    if (down) forward -= moveSpeed;

    // A = fire / restart, B = use / open door.
    static bool lastUse = false;
    static bool lastFire = false;

#if NANO_ENABLE_SPRITES
    if (nanoSpritesHealth() == 0) {
        if (buttonA && !lastFire) {
            engineInit();
            nanoSpritesReset();
            fxKind = 0;
            fxPhase = 0;
            fxInverted = false;
            oledInvert(false);
            rendering = false;
            allRaysDone = false;
            deathPainted = false;
        }
        lastFire = buttonA;
        lastUse = buttonB;
        return;
    }
#endif

    if (buttonB && !lastUse) engineUse(now);
    lastUse = buttonB;

#if NANO_ENABLE_SPRITES
    if (buttonA && !lastFire) nanoSpritesFire(now);
#endif
    lastFire = buttonA;

#if NANO_VIEW_BOB
    updateWalkBob(now, dt, forward != 0.0f);
#endif
    if (forward != 0.0f) engineMove(forward, 0.0f);
}

void setup() {
#if NANO_DIRECT_GPIO_INIT
    // Input buttons with pull-ups: Nano D3-D7 (PORTD) and D8 (PORTB).
    const uint8_t buttonMaskD =
        _BV(PD3) | _BV(PD4) | _BV(PD5) | _BV(PD6) | _BV(PD7);

    DDRD &= (uint8_t)~buttonMaskD;
    PORTD |= buttonMaskD;
    DDRB &= (uint8_t)~_BV(PB0);
    PORTB |= _BV(PB0);

#if USE_SPI_FLASH
    // D2 flash CS: drive high BEFORE switching to output.
    PORTD |= _BV(PD2);
    DDRD |= _BV(PD2);
#endif
#else
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_A, INPUT_PULLUP);
    pinMode(BUTTON_B, INPUT_PULLUP);

#if USE_SPI_FLASH
    digitalWrite(FLASH_CS, HIGH);
    pinMode(FLASH_CS, OUTPUT);
#endif
#endif

#ifdef USE_SERIAL
    Serial.begin(115200);
#endif

    oledInit();
    engineInit();
    lastInput = millis();

#ifdef USE_SERIAL
#if USE_SPI_FLASH
    Serial.println(engineMapReady() ? F("E1M1 FULL MAP OK (W25Q32)") : F("ERROR: W25Q32 MAP MISSING. Use flash loader first!"));
#else
    Serial.println(engineMapReady() ? F("E1M1 FULL MAP OK (PROGMEM)") : F("ERROR: PROGMEM MAP INVALID"));
#endif

    Serial.print(F("renderer: "));
#if NANO_FIXED_POINT
    Serial.print(F("FIXED Q8/Q10 GRID-DDA "));
#if NANO_SPAN_FILL
    Serial.print(F("SPAN "));
#else
    Serial.print(F("BASE "));
#endif
#else
    Serial.print(F("FLOAT GRID-DDA "));
#endif

    Serial.print(128 / NANO_RENDER_STEP);

#if NANO_DITHER_FILL
    Serial.print(F(" BAYER4"));
#if NANO_DITHER_GRADIENT
    Serial.print(F(" GRAD"));
#endif
#endif

    Serial.print(F(" rays "));
    Serial.print(NANO_STREAM_COLUMNS);
    Serial.println(F(" columns/transfer"));
#endif

}

void loop() {
    engineUpdateDoors(millis());
    updateOledEffects(millis());

    if (!engineMapReady()) return;

    // Polling OLED flush is synchronous. Every transmitted ring slot is reusable.
    if (!rendering && !allRaysDone) {
        pollButtons();

#if NANO_ENABLE_SPRITES
        if (!nanoSpritesHealth()) {
            if (!deathPainted) {
                drawDeath();
                deathPainted = true;
            }
            return;
        }
        deathPainted = false;
#endif

        oledClear();
        drawFps();
#if NANO_VIEW_BOB
        beginFrameBob(millis());
#endif
        engineRenderBegin();

#if NANO_ENABLE_SPRITES
        nanoSpritesBeginFrame(millis());
        spritesPaintedX = 0;
#endif

        frameStart = millis();
        rendering = true;
        flushedX = finishedX = 0;
        halfInputDone = false;
    }

    if (rendering) {
        allRaysDone = engineRenderStep(gamePixel, oledPageOr);

#if NANO_ENABLE_SPRITES
        // The next ray may still change x-1; only paint finalized columns.
        uint8_t finalColumns = allRaysDone ? 128 : (uint8_t)(engineRenderNextX() - NANO_RENDER_STEP);
        if (finalColumns > spritesPaintedX) {
            nanoSpritesPaintColumns(spritesPaintedX, finalColumns, oledFrame);
            spritesPaintedX = finalColumns;
        }
#endif


        // Poll once during the render, in addition to the frame-start poll.
        if (!halfInputDone && engineRenderNextX() >= 32) {
            halfInputDone = true;
            pollButtons();
        }

        // x-1 is finalized by tracing x. NEVER transmit the currently active column before its next ray has been traced.
        uint8_t safe = allRaysDone ? 128 : (uint8_t)(engineRenderNextX() - NANO_RENDER_STEP);
        finishedX = allRaysDone ? 128 : (uint8_t)((safe / NANO_STREAM_COLUMNS) * NANO_STREAM_COLUMNS);

        if (allRaysDone) {
            renderTime = millis() - frameStart;
            rendering = false;
        }
    }
    // A column is safe to reuse only AFTER its synchronous OLED transfer.
    if (finishedX > flushedX) {
        uint8_t end = (uint8_t)(flushedX + NANO_STREAM_COLUMNS);
        if (end > finishedX) end = finishedX;

#if NANO_VIEW_BOB
        bobColumns(flushedX, end);
#endif
        // Overlay the reticle after the view shifts so it stays centered.
#if NANO_CROSSHAIR
        drawCrosshair(flushedX, end);
#endif
#if NANO_STATUS_HUD && NANO_ENABLE_SPRITES
        // Draw stable status windows last, over bobbed 3D pixels.
        nanoSpritesPaintHudColumns(flushedX, end, oledFrame);
#endif
        oledFlushColumns(flushedX, (uint8_t)(end - 1));

#if NANO_FB_COLUMNS < 128
        oledClearColumns(flushedX, (uint8_t)(end - flushedX));
#endif
        flushedX = end;
    }

    if (allRaysDone && flushedX == 128) {
        lastFrameMs = millis() - frameStart;

#ifdef USE_SERIAL
        uint32_t fps100 = lastFrameMs ? 100000UL / lastFrameMs : 0;
        Serial.print(F("Render=")); Serial.print(renderTime);
        Serial.print(F("ms Frame=")); Serial.print(lastFrameMs);
        Serial.print(F("ms FPS=")); Serial.print(fps100 / 100);
        Serial.print('.');

        if (fps100 % 100 < 10) Serial.print('0');
        Serial.print(fps100 % 100);

#if defined(NANO_SSD1309_PAGE_MODE) && NANO_SSD1309_PAGE_MODE
        Serial.print(F(" I2C_err="));
        Serial.print(oledErrors);
#endif
        Serial.println();
#endif
        allRaysDone = false;
    }
}