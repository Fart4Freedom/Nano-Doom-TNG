#include "config.h"
#include "full_map.h"
#include <string.h>

// All 6796 bytes are stored in AVR program flash (.progmem.data), not SRAM.
#include "full_map_progmem.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

static void flashRead(uint32_t adr, void *dest, uint16_t len) {
    if (adr >= sizeof(E1M1_FULL_MAP_DATA) || 
        len > sizeof(E1M1_FULL_MAP_DATA) - adr) {
        memset(dest, 0, len);
        return;
    }

#ifdef __AVR__
    memcpy_P(dest, E1M1_FULL_MAP_DATA + (uint16_t)adr, len);
#else
    memcpy(dest, E1M1_FULL_MAP_DATA + (size_t)adr, len);
#endif
}

static uint8_t normalDoors;
static uint16_t sectorBase = 32, wallBase = 696;

bool fullMapOpen(uint8_t *doorSector, int8_t *doorTarget) {
    uint8_t header[32];
    flashRead(0, header, 32);

    if (memcmp(header, "NDM1", 4) != 0 || 
        header[4] != 2 || 
        header[5] != MAP_SECTOR_COUNT || 
        (uint16_t)(header[6] | (header[7] << 8)) != MAP_WALL_COUNT || 
        (uint16_t)(header[8] | (header[9] << 8)) != MAP_START_SECTOR) {
        return false;
    }

    if ((uint16_t)(header[24] | (header[25] << 8)) != MAP_THING_BASE || 
        (uint16_t)(header[26] | (header[27] << 8)) != MAP_THING_COUNT) {
        return false;
    }

    normalDoors = header[22];
    sectorBase = (uint16_t)(header[10] | (header[11] << 8));
    wallBase = (uint16_t)(header[12] | (header[13] << 8));

    if (sectorBase != 32 || wallBase != (uint16_t)(32 + 8 * MAP_SECTOR_COUNT)) {
        return false;
    }

    for (uint8_t i = 0; i < 4; i++) {
        doorSector[i] = header[14 + 2 * i];
        doorTarget[i] = (int8_t)header[15 + 2 * i];
    }

    return true;
}

void fullMapWall(uint16_t idx, MapWall *out) {
    if (idx >= MAP_WALL_COUNT) {
        memset(out, 0, sizeof(*out));
        out->next = 255;
        return;
    }
    flashRead((uint32_t)wallBase + (uint32_t)idx * 10u, out, 10);
}

void fullMapSector(uint8_t idx, MapSector *out) {
    if (idx >= MAP_SECTOR_COUNT) {
        memset(out, 0, sizeof(*out));
        return;
    }
    flashRead((uint32_t)sectorBase + (uint16_t)idx * 8u, out, 8);
}

void fullMapThing(uint8_t index, MapThing *out) {
    if (index >= MAP_THING_COUNT) {
        memset(out, 0, sizeof(*out));
        return;
    }
    flashRead(MAP_THING_BASE + (uint16_t)index * 8u, out, 8);
}

uint8_t fullMapNormalDoors() {
    return normalDoors;
}
