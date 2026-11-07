#include "config.h"
#include "sprites.h"
#include "nano_font.h"

// Two 4-bit glyph rows per byte, 24 bytes per scanline, MSB-left.

uint8_t nanoFontColumn(char c, uint8_t column) {
    if (column >= 4) {
        return 0;
    }

    // Convert lowercase to uppercase
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }

    // Special case for colon
    if (c == ':') {
        return (column == 1) ? 0x14 : 0;
    }

    uint8_t index = 0;

    if (c >= '0' && c <= '9') {
        index = c - '0' + 1;
    } else if (c >= 'A' && c <= 'Z') {
        index = c - 'A' + 11;
    } else {
        static const char extra[] PROGMEM = ".,-_(){}[]#";
        for (uint8_t i = 0; i < 11; i++) {
            if (c == (char)pgm_read_byte(extra + i)) {
                index = 37 + i;
                break;
            }
        }
    }

    uint8_t result = 0;
    uint8_t mask = 0x80u >> ((index & 1) * 4 + column);

    for (uint8_t y = 0; y < 6; y++) {
        if (pgm_read_byte(bmp_font + y * 24 + (index >> 1)) & mask) {
            result |= 1u << y;
        }
    }

    return result;
}

