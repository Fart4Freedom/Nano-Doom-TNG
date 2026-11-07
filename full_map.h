#pragma once
#include "config.h"
#include <stdint.h>

// E1M1 geometry and THINGS, NDM1 v2. Default PROGMEM or external W25Q.

struct __attribute__((packed)) MapWall {
    int16_t ax, ay, bx, by;
    uint8_t next; // 255 = solid/void
    uint8_t door; // bits 0..2: DR door 1..4; bit 7: Doom impassable linedef
};

struct __attribute__((packed)) MapSector {
    uint16_t first;
    uint8_t count;
    int8_t floor_z, ceil_z; // scaled 1/8 Doom unit
    uint8_t floor_id, ceil_id, door;
};

static constexpr uint8_t MAP_SECTOR_COUNT = 83;
static constexpr uint16_t MAP_WALL_COUNT = 610;
static constexpr uint8_t MAP_START_SECTOR = 50;
static constexpr uint16_t MAP_START_ANGLE = 90;
static constexpr uint8_t MAP_WALL_IMPASSABLE = 0x80; // original Doom LINEDEFS flag bit 0
static constexpr uint8_t MAP_WALL_DOOR_MASK = 0x07;  // 1..4 for DR use

static_assert(sizeof(MapWall) == 10, "Wall layout mismatch");
static_assert(sizeof(MapSector) == 8, "Sector layout mismatch");

bool fullMapOpen(uint8_t *doorSector, int8_t *doorTarget);
void fullMapWall(uint16_t idx, MapWall *out);
void fullMapSector(uint8_t idx, MapSector *out);

#include "map_things_meta.h"

struct __attribute__((packed)) MapThing {
    int16_t x, y;
    uint16_t type;
    uint8_t flags, sector;
};

static_assert(sizeof(MapThing) == 8, "THING layout");

void fullMapThing(uint8_t index, MapThing *out);
uint8_t fullMapNormalDoors();
