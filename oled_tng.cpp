#include "config.h"
#include "oled_tng.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

#ifndef SSD1309_TWBR
#define SSD1309_TWBR ((F_CPU >= 32000000UL) ? 10 : 1) // ~889 kHz at 16/32 MHz
#endif

#ifndef NANO_SSD1309_PAGE_MODE
#define NANO_SSD1309_PAGE_MODE 1
#endif

#ifndef OLED_TWI_TIMEOUT
#define OLED_TWI_TIMEOUT 60000u
#endif

uint8_t oledFrame[8 * NANO_FB_COLUMNS];
uint8_t oledErrors = 0;

// Poll hardware completion, and verify ACK / bus status. Never spin forever.
static bool waitTwi() {
    uint16_t timeout = OLED_TWI_TIMEOUT;

    while (!(TWCR & _BV(TWINT))) {
        if (!--timeout) {
            ++oledErrors;
            return false;
        }
    }

    return true;
}

static bool start() {
    uint16_t stopTimeout = OLED_TWI_TIMEOUT;

    while (TWCR & _BV(TWSTO)) {
        if (!--stopTimeout) {
            ++oledErrors;
            return false;
        }
    }

    TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);

    if (!waitTwi()) {
        return false;
    }

    const uint8_t status = TWSR & 0xF8;
    if (status != 0x08 && status != 0x10) {
        ++oledErrors;
        return false;
    }

    TWDR = 0x3C << 1;
    TWCR = _BV(TWINT) | _BV(TWEN);

    if (!waitTwi()) {
        return false;
    }

    if ((TWSR & 0xF8) != 0x18) {
        ++oledErrors;
        return false;
    }

    return true;
}

static bool byte(uint8_t v) {
    TWDR = v;
    TWCR = _BV(TWINT) | _BV(TWEN);

    if (!waitTwi()) {
        return false;
    }

    if ((TWSR & 0xF8) != 0x28) {
        ++oledErrors;
        return false;
    }

    return true;
}

static void stop() {
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
}

static void commands(const uint8_t *ptr, uint8_t n) {
    if (start() && byte(0x00)) {
        while (n--) {
            if (!byte(pgm_read_byte(ptr++))) {
                break;
            }
        }
    }
    stop();
}

static void command(uint8_t c) {
    if (start() && byte(0x00)) {
        byte(c);
    }
    stop();
}

void oledInvert(bool inverted) {
    command(inverted ? 0xA7 : 0xA6);
}

void oledClear() {
    memset(oledFrame, 0, sizeof(oledFrame));
}

void oledPixel(uint8_t x, uint8_t y) {
    if (x < 128 && y < 64) {
        oledFrame[(uint16_t)(y >> 3) * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))] |= 1u << (y & 7);
    }
}

void oledClearColumns(uint8_t first, uint8_t count) {
    for (uint8_t pg = 0; pg < 8; pg++) {
        for (uint8_t i = 0; i < count; i++) {
            oledFrame[(uint16_t)pg * NANO_FB_COLUMNS + ((first + i) & (NANO_FB_COLUMNS - 1))] = 0;
        }
    }
}

void oledInit() {
    TWSR &= (uint8_t)~(_BV(TWPS0) | _BV(TWPS1));
    TWBR = SSD1309_TWBR;
//    TWBR = 12;
    TWCR = _BV(TWEN);

#if NANO_SSD1309_PAGE_MODE
    static const uint8_t init[] PROGMEM = {
        0xAE, 0x2E, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x6F,
        0xD9, 0xD3, 0xDB, 0x34, 0xA4, 0xA6, 0xAF
    };
#else
    // SSD1306 also supports page addressing; retain charge pump initialization.
    static const uint8_t init[] PROGMEM = {
        0xAE, 0x2E, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF,
        0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
#endif

    commands(init, sizeof(init));

    // The ring contains zeros, so wrapping each page is safe here:
    // this clears all 128 visible columns even with a 32/64-column buffer.
    oledClear();
    oledFlushColumns(0, 127);
}

// SSD1309 page mode. All data bytes for a page in one I2C transaction.
// Returning guarantees buffer can safely be overwritten.
void oledFlushColumns(uint8_t first, uint8_t last) {
    if (first > last || last >= 128) {
        return;
    }

    for (uint8_t pg = 0; pg < 8; pg++) {
        if (!start()) {
            stop();
            return;
        }

        if (!byte(0x00) || !byte(0xB0 | pg) || !byte(first & 0x0F) || !byte(0x10 | (first >> 4))) {
            stop();
            return;
        }

        stop();

        if (!start()) {
            stop();
            return;
        }

        if (!byte(0x40)) {
            stop();
            return;
        }

        // Physical x is 0..127, but the framebuffer may wrap every
        // 32 or 64 columns. Never let the source pointer cross its page.
        const uint8_t *p = oledFrame + (uint16_t)pg * NANO_FB_COLUMNS;

        for (uint8_t x = first;; x++) {
            if (!byte(p[x & (NANO_FB_COLUMNS - 1)])) {
                stop();
                return;
            }
            if (x == last) {
                break;
            }
        }

        stop();
    }
}
