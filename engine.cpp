#include "config.h"
#include "engine.h"
#include "full_map.h"
#include "sprites.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef __AVR__
#ifndef pgm_read_word
#define pgm_read_word(p) (*(p))
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(p) (*(p))
#endif
#endif
// Conservative SquareDoom-inspired spatial candidate filter.
#ifndef NANO_GRID_FILTER
#define NANO_GRID_FILTER 0
#endif
#if NANO_GRID_FILTER
#include "map_grid.h"
#ifndef __AVR__
#define pgm_read_word(p) (*(p))
#define pgm_read_byte(p) (*(p))
#endif
static uint8_t candidateMask[(MAP_WALL_COUNT + 7) / 8];
// Walk grid cells with DDA and mark all potentially intersected wall segments.
// Deliberately conservative: precise intersections are still calculated below.
#ifndef NANO_FIXED_POINT
#define NANO_FIXED_POINT 0
#endif
#if !NANO_FIXED_POINT
static void buildRayCandidateMask(float dx, float dy, float ox, float oy) {
    memset(candidateMask, 0, sizeof(candidateMask));
    const float xx = (ox - GRID_X_MIN) / GRID_CELL_SIZE;
    const float yy = (oy - GRID_Y_MIN) / GRID_CELL_SIZE;
    if (xx < 0.f || yy < 0.f || xx >= GRID_COLS || yy >= GRID_ROWS) {
        // Camera outside the grid: preserve correctness using the original scan.
        memset(candidateMask, 255, sizeof(candidateMask));
        return;
    }
    int16_t cx = (int16_t)xx, cy = (int16_t)yy;
    const int8_t sx = (dx > 0.f) ? 1 : -1, sy = (dy > 0.f) ? 1 : -1;
    const float inf = 1.0e30f;
    const float tx =
        (dx != 0.f) ? ((GRID_X_MIN + (cx + (sx > 0)) * GRID_CELL_SIZE - ox) / dx) : inf;
    const float ty =
        (dy != 0.f) ? ((GRID_Y_MIN + (cy + (sy > 0)) * GRID_CELL_SIZE - oy) / dy) : inf;
    float nextX = tx, nextY = ty;
    const float deltaX = (dx != 0.f) ? (GRID_CELL_SIZE / fabsf(dx)) : inf;
    const float deltaY = (dy != 0.f) ? (GRID_CELL_SIZE / fabsf(dy)) : inf;
    const uint16_t maxCells = (uint16_t)GRID_COLS + GRID_ROWS + 4;
    for (uint16_t step = 0; step < maxCells; ++step) {
        if (cx < 0 || cy < 0 || cx >= GRID_COLS || cy >= GRID_ROWS)
            break;
        uint16_t cell = (uint16_t)cy * GRID_COLS + (uint16_t)cx;
        uint16_t start = pgm_read_word(&gridOffsets[cell]);
        uint16_t end = pgm_read_word(&gridOffsets[cell + 1]);
        for (uint16_t i = start; i < end; ++i) {
            uint8_t index = pgm_read_byte(&gridIndices[i]);
            candidateMask[index >> 3] |= (uint8_t)(1u << (index & 7));
        }
        if (nextX < nextY) {
            cx += sx;
            nextX += deltaX;
        } else if (nextY < nextX) {
            cy += sy;
            nextY += deltaY;
        } else {
            cx += sx;
            cy += sy;
            nextX += deltaX;
            nextY += deltaY;
        }
    }
}
#endif // !NANO_FIXED_POINT
#endif // NANO_GRID_FILTER
#if NANO_GRID_FILTER && NANO_FIXED_POINT
// Bresenham-style DDA. Only multiplication / addition; NO division and NO float.
// Q8 camera coordinates, Q10 ray direction. The mask is conservative near corners.
static inline void addGridCell(int16_t cx, int16_t cy) {
    if (cx < 0 || cy < 0 || cx >= GRID_COLS || cy >= GRID_ROWS)
        return;
    const uint16_t cell = (uint16_t)cy * GRID_COLS + (uint16_t)cx;
    const uint16_t begin = pgm_read_word(&gridOffsets[cell]);
    const uint16_t end = pgm_read_word(&gridOffsets[cell + 1]);
    for (uint16_t i = begin; i < end; ++i) {
        uint8_t index = pgm_read_byte(&gridIndices[i]);
        candidateMask[index >> 3] |= (uint8_t)(1u << (index & 7));
    }
}

static void buildFixedMask(int16_t dx, int16_t dy, int32_t px, int32_t py) {
    memset(candidateMask, 0, sizeof(candidateMask));
    const int32_t gx = px - ((int32_t)GRID_X_MIN * 256);
    const int32_t gy = py - ((int32_t)GRID_Y_MIN * 256);
    if (gx < 0 || gy < 0 || gx >= (int32_t)GRID_COLS * GRID_CELL_SIZE * 256 ||
        gy >= (int32_t)GRID_ROWS * GRID_CELL_SIZE * 256) {
        memset(candidateMask, 255, sizeof(candidateMask));
        return;
    }
    int16_t cx = (int16_t)(gx / (GRID_CELL_SIZE * 256));
    int16_t cy = (int16_t)(gy / (GRID_CELL_SIZE * 256));
    const int8_t sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const uint16_t ax = dx >= 0 ? dx : -dx, ay = dy >= 0 ? dy : -dy;
    const int32_t cellQ8 = (int32_t)GRID_CELL_SIZE * 256;
    int32_t rx = sx > 0 ? ((int32_t)(cx + 1) * cellQ8 - gx) : (gx - (int32_t)cx * cellQ8);
    int32_t ry = sy > 0 ? ((int32_t)(cy + 1) * cellQ8 - gy) : (gy - (int32_t)cy * cellQ8);
    for (uint8_t i = 0; i < (uint8_t)(GRID_COLS + GRID_ROWS + 4); i++) {
        if (cx < 0 || cy < 0 || cx >= GRID_COLS || cy >= GRID_ROWS)
            break;
        addGridCell(cx, cy);
        if (dx == 0) {
            cy += sy;
            continue;
        }
        if (dy == 0) {
            cx += sx;
            continue;
        }
        const int32_t xx = rx * ay, yy = ry * ax;
        // Only add the side cells if the integer approximation is close to a corner.
        // This avoids missing narrow edge-aligned geometry after direction rounding.
        const int32_t diff = xx - yy;
        if (diff > -4096 && diff < 4096) {
            addGridCell(cx + sx, cy);
            addGridCell(cx, cy + sy);
            cx += sx;
            cy += sy;
            rx += cellQ8;
            ry += cellQ8;
        } else if (diff < 0) {
            cx += sx;
            rx += cellQ8;
        } else {
            cy += sy;
            ry += cellQ8;
        }
    }
}
#endif // fixed grid
#if NANO_PROFILE
static uint32_t candidateChecks = 0;
static uint32_t exactChecks = 0;

void engineResetProfile() {
    candidateChecks = exactChecks = 0;
}

uint32_t engineCandidateChecks() {
    return candidateChecks;
}

uint32_t engineExactChecks() {
    return exactChecks;
}
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef __AVR__
// Host-side PROGMEM equivalent for Linux regression tests.
#define memcpy_P memcpy
#endif

// Optional monochrome fill. Compiled out entirely in the outline baseline.
#ifndef NANO_DITHER_FILL
#define NANO_DITHER_FILL 0
#endif
#ifndef NANO_DITHER_GRADIENT
#define NANO_DITHER_GRADIENT 1
#endif
#if NANO_DITHER_FILL
#include "dither_patterns.h"
#if defined(NANO_PC_PORT) && !defined(__AVR__)
#include "../pc/dither_pc.h"
#endif
#endif
static constexpr uint8_t WIDTH = 128, HEIGHT = 64;
#ifndef NANO_RENDER_STEP
#define NANO_RENDER_STEP 1
#endif
static_assert(NANO_RENDER_STEP == 1 || NANO_RENDER_STEP == 2 || NANO_RENDER_STEP == 4 ||
                  NANO_RENDER_STEP == 8,
              "Supported render steps: 1, 2, 4, 8");
static constexpr float FOCAL = 87.0f; // approximately 73° horizontal FOV
static constexpr float PI = 3.14159265358979323846f;
static constexpr float EYE_ABOVE_FLOOR = 5.125f; // 41 Doom units / 8
static constexpr float STEP_UP = 3.0f;           // 24 Doom units / 8
static constexpr float BODY_HEIGHT = 7.0f;       // 56 Doom units / 8
static constexpr float BODY_RADIUS = 0.28f;
static constexpr uint8_t MAX_RAY_HITS = 32;

static uint8_t previousColumn[HEIGHT];
static uint8_t currentColumn[HEIGHT];
static float playerX = 0, playerY = 0, angle = PI * .5f;
static float dirX = 0, dirY = 1, eye = EYE_ABOVE_FLOOR;
// Cached fixed-point camera. Updated only when player moves or rotates.
static int32_t fixedPX = 0, fixedPY = 0;    // Q8 world position
static int16_t fixedDX = 0, fixedDY = 1024; // Q10 unit direction
static int16_t fixedEye8 = 41;              // eye height * 8

static inline int32_t toQ8(float value) {
    return (int32_t)(value * 256.0f + (value >= 0 ? 0.5f : -0.5f));
}

static void refreshFixedPosition() {
    fixedPX = toQ8(playerX);
    fixedPY = toQ8(playerY);
    fixedEye8 = (int16_t)(eye * 8.0f + (eye >= 0 ? 0.5f : -0.5f));
}

static void refreshFixedAngle() {
    fixedDX = (int16_t)(dirX * 1024.0f + (dirX >= 0 ? 0.5f : -0.5f));
    fixedDY = (int16_t)(dirY * 1024.0f + (dirY >= 0 ? 0.5f : -0.5f));
}

static uint8_t playerSector = MAP_START_SECTOR;

// W25Q32 map and small in-RAM door state; no RAM map copies.
static bool mapReady = false;
static uint8_t doorSector[4] = {255, 255, 255, 255};
static int8_t doorTarget[4] = {0, 0, 0, 0};
static int8_t doorCeiling[4] = {0, 0, 0, 0};
static int8_t doorFloor[4] = {0, 0, 0, 0};
static uint8_t doorMode[4] = {0, 0, 0, 0}; // 0=closed,1=opening,2=waiting,3=closing
static uint32_t doorTick[4] = {0, 0, 0, 0};
static uint32_t doorHold[4] = {0, 0, 0, 0};

bool engineMapReady() {
    return mapReady;
}

static MapWall getWall(uint16_t index) {
    MapWall w;
    fullMapWall(index, &w);
    return w;
}

static MapSector getSector(uint8_t index) {
    MapSector s;
    fullMapSector(index, &s);
    if (s.door >= 1 && s.door <= 4) {
        s.ceil_z = doorCeiling[s.door - 1];
        s.ceil_id = (uint8_t)(224 + ((s.ceil_z + 64) & 31));
    }
    return s;
}

// Run once per main loop. 80ms per 1/8-Doom-unit height step.
// Doors retain their current height without updating an 83-sector RAM array.
void engineUpdateDoors(uint32_t now) {
    if (!mapReady)
        return;
    for (uint8_t i = 0; i < 4; i++) {
        if (doorMode[i] == 0)
            continue;
        if (doorMode[i] == 2) {
            if (playerSector == doorSector[i]) {
                doorHold[i] = now;
                continue;
            }
            if ((uint32_t)(now - doorHold[i]) >= 3200u) {
                doorMode[i] = 3;
                doorTick[i] = now;
            }
            continue;
        }
        uint32_t elapsed = now - doorTick[i];
        if (elapsed < 80)
            continue;
        uint8_t steps = (uint8_t)(elapsed / 80u > 20u ? 20u : elapsed / 80u);
        doorTick[i] += (uint32_t)steps * 80u;
        if (doorMode[i] == 1) {
            int16_t z = (int16_t)doorCeiling[i] + steps;
            if (z >= doorTarget[i]) {
                doorCeiling[i] = doorTarget[i];
                doorMode[i] = 2;
                doorHold[i] = now;
            } else
                doorCeiling[i] = (int8_t)z;
        } else if (doorMode[i] == 3) {
            if (playerSector == doorSector[i]) {
                doorMode[i] = 1;
                doorTick[i] = now;
                continue;
            }
            int16_t z = (int16_t)doorCeiling[i] - steps;
            if (z <= doorFloor[i]) {
                doorCeiling[i] = doorFloor[i];
                doorMode[i] = 0;
            } else
                doorCeiling[i] = (int8_t)z;
        }
    }
}

static float dist2Segment(float x, float y, const MapWall &w) {
    float dx = w.bx - w.ax, dy = w.by - w.ay;
    float length = dx * dx + dy * dy;
    if (length <= 0.001f)
        return 1e10f;
    float t = ((x - w.ax) * dx + (y - w.ay) * dy) / length;
    if (t < 0)
        t = 0;
    else if (t > 1)
        t = 1;
    float ex = w.ax + t * dx - x, ey = w.ay + t * dy - y;
    return ex * ex + ey * ey;
}

// Use: closest DR door linedef within 8 map units and in front of player.
// Triggered by the B (USE) button.
void engineUse(uint32_t now) {
    if (!mapReady)
        return;
    const MapSector s = getSector(playerSector);
    float bestD2 = 64.0f;
    uint8_t bestDoor = 0;
    for (uint8_t j = 0; j < s.count; j++) {
        MapWall w = getWall(s.first + j);
        if (w.door < 1 || w.door > 4)
            continue;
        float cx = ((float)w.ax + w.bx) * 0.5f - playerX;
        float cy = ((float)w.ay + w.by) * 0.5f - playerY;
        if (cx * dirX + cy * dirY < 0.f)
            continue;
        float d2 = dist2Segment(playerX, playerY, w);
        if (d2 < bestD2) {
            bestD2 = d2;
            bestDoor = w.door;
        }
    }
    if (bestDoor) {
        uint8_t i = (uint8_t)(bestDoor - 1);
        doorMode[i] = 1;
        doorTick[i] = now;
        doorHold[i] = now;
    }
}

static void refreshEye() {
    eye = (float)getSector(playerSector).floor_z + EYE_ABOVE_FLOOR;
    refreshFixedPosition();
}

static void refreshDirection() {
    dirX = cosf(angle);
    dirY = sinf(angle);
    refreshFixedAngle();
}

void engineInit() {
    mapReady = fullMapOpen(doorSector, doorTarget);
    for (uint8_t i = 0; i < 4; i++) {
        if (mapReady) {
            MapSector door;
            fullMapSector(doorSector[i], &door);
            doorFloor[i] = door.floor_z;
            doorCeiling[i] = door.ceil_z;
        }
        doorMode[i] = 0;
        doorTick[i] = doorHold[i] = 0;
    }
    playerX = 0;
    playerY = 0;
    playerSector = MAP_START_SECTOR;
    angle = (float)MAP_START_ANGLE * (PI / 180.0f);
    refreshEye();
    refreshDirection();
}

void engineTeleport(float x, float y, uint8_t sector, float degrees) {
    if (sector >= MAP_SECTOR_COUNT)
        return;
    playerX = x;
    playerY = y;
    playerSector = sector;
    angle = degrees * (PI / 180.f);
    refreshEye();
    refreshDirection();
}

float engineX() {
    return playerX;
}

float engineY() {
    return playerY;
}

float engineAngle() {
    return angle;
}

uint8_t engineSector() {
    return playerSector;
}

void engineTurn(float rad) {
    angle += rad;
    while (angle >= 2 * PI)
        angle -= 2 * PI;
    while (angle < 0)
        angle += 2 * PI;
    refreshDirection();
}

// Boundary crossing on the path, not point-in-polygon on overlapping loops.
// Test vertical opening and 24-unit (3 scaled units) step height separately.
static bool portalWalkable(uint8_t here, const MapWall &w) {
    // A valid back sector does NOT imply a passable wall: Doom has
    // BLOCKING two-sided LINEDEFS (railings, hidden outdoor boundaries).
    if (w.door & MAP_WALL_IMPASSABLE)
        return false;
    const uint8_t dest = w.next;
    if (dest == 255 || dest >= MAP_SECTOR_COUNT)
        return false;
    MapSector a = getSector(here), b = getSector(dest);
    if ((float)b.floor_z > (float)a.floor_z + STEP_UP + 0.001f)
        return false;
    float bottom = a.floor_z > b.floor_z ? a.floor_z : b.floor_z;
    float top = a.ceil_z < b.ceil_z ? a.ceil_z : b.ceil_z;
    return top - bottom >= BODY_HEIGHT;
}

static bool intersectsMove(float ax, float ay, float bx, float by, const MapWall &w, float &t) {
    const float dx = bx - ax, dy = by - ay, ex = w.bx - w.ax, ey = w.by - w.ay;
    const float den = dx * ey - dy * ex;
    if (fabsf(den) < 0.0000001f)
        return false;
    const float vx = w.ax - ax, vy = w.ay - ay;
    const float tx = (vx * ey - vy * ex) / den;
    if (tx <= 0.00001f || tx > 1.f)
        return false;
    const float u = (vx * dy - vy * dx) / den;
    if (u < -.00001f || u > 1.00001f)
        return false;
    t = tx;
    return true;
}

// Visible if the ray from player's center can cross only WALKABLE portals.
// Fast whole-object culling for low-memory sprite proof of concept.
// Used for AI and hitscan. Fixed-point visual rendering additionally clips
// each sampled sprite column during the portal walk.
bool engineCanSeeSprite(float targetX, float targetY) {
    float ax = playerX, ay = playerY;
    uint8_t here = playerSector;
    for (uint8_t hop = 0; hop < 16; hop++) {
        float dx = targetX - ax, dy = targetY - ay;
        if (dx * dx + dy * dy < 0.00001f)
            return true;
        MapSector s = getSector(here);
        float nearest = 1.00001f;
        int16_t wallIndex = -1;
        for (uint8_t i = 0; i < s.count; i++) {
            const uint16_t idx = s.first + i;
            MapWall w = getWall(idx);
            float t;
            if (intersectsMove(ax, ay, targetX, targetY, w, t) && t < nearest) {
                nearest = t;
                wallIndex = (int16_t)idx;
            }
        }
        if (wallIndex < 0)
            return true;
        const MapWall crossed = getWall((uint16_t)wallIndex);
        if (!portalWalkable(here, crossed))
            return false;
        const float advance = nearest + .002f;
        if (advance >= 1.f)
            return true;
        ax += dx * advance;
        ay += dy * advance;
        here = crossed.next;
    }
    return false;
}

static bool clearOfSolidWalls(float x, float y, uint8_t sector) {
    MapSector s = getSector(sector);
    const float r2 = BODY_RADIUS * BODY_RADIUS;
    for (uint8_t i = 0; i < s.count; i++) {
        MapWall w = getWall(s.first + i);
        if (portalWalkable(sector, w))
            continue;
        if (dist2Segment(x, y, w) < r2)
            return false;
    }
    return true;
}

// Shared portal/collision code for the player and AI actors.
// The same rules prevent monsters from walking through closed doors.
static void movePathActor(float &posX, float &posY, uint8_t &sectorId, float targetX,
                          float targetY) {
    float ax = posX, ay = posY;
    uint8_t here = sectorId;
    for (uint8_t hop = 0; hop < 10; hop++) {
        float dx = targetX - ax, dy = targetY - ay;
        if (dx * dx + dy * dy < 0.0000001f) {
            posX = ax;
            posY = ay;
            sectorId = here;
            return;
        }
        MapSector sector = getSector(here);
        float nearestT = 1.00001f;
        int16_t nearest = -1;
        for (uint8_t i = 0; i < sector.count; i++) {
            const uint16_t idx = sector.first + i;
            MapWall w = getWall(idx);
            float t;
            if (intersectsMove(ax, ay, targetX, targetY, w, t) && t < nearestT) {
                nearest = (int16_t)idx;
                nearestT = t;
            }
        }
        if (nearest < 0) {
            if (clearOfSolidWalls(targetX, targetY, here)) {
                ax = targetX;
                ay = targetY;
            }
            break;
        }
        MapWall crossed = getWall((uint16_t)nearest);
        if (!portalWalkable(here, crossed)) {
            const float f = nearestT > 0.003f ? nearestT - 0.003f : 0.f;
            float x = ax + f * dx, y = ay + f * dy;
            if (clearOfSolidWalls(x, y, here)) {
                ax = x;
                ay = y;
            }
            break;
        }
        // Traverse a real portal; epsilon prevents selecting the same edge twice.
        float f = nearestT + .001f;
        if (f > 1.f)
            f = 1.f;
        ax += dx * f;
        ay += dy * f;
        here = crossed.next;
        if (f >= 1.f)
            break;
    }
    posX = ax;
    posY = ay;
    sectorId = here;
}

static void movePath(float targetX, float targetY) {
    movePathActor(playerX, playerY, playerSector, targetX, targetY);
    refreshEye();
}

// Movement for a monster: independent position and sector, same solid-wall
// checks as engineMove(). Sprite collisions with each other are omitted.
void engineMoveMonster(float *x, float *y, uint8_t *sector, float dx, float dy) {
    if (!mapReady || !x || !y || !sector || *sector >= MAP_SECTOR_COUNT)
        return;
    movePathActor(*x, *y, *sector, *x + dx, *y);
    movePathActor(*x, *y, *sector, *x, *y + dy);
}

float engineEye() {
    return eye;
}

int8_t engineSectorFloor(uint8_t sector) {
    return sector < MAP_SECTOR_COUNT ? getSector(sector).floor_z : 0;
}

void engineMove(float forward, float strafe) {
    // Preserve the Nano's two-axis sliding behavior, and avoid diagonal tunnelling.
    float dx = forward * dirX + strafe * dirY;
    float dy = forward * dirY - strafe * dirX;
    movePath(playerX + dx, playerY);
    movePath(playerX, playerY + dy);
}

int8_t engineSectorCeiling(uint8_t sector) {
    return getSector(sector).ceil_z;
}

static SpriteClipTarget *spriteTargets = nullptr;
static uint8_t spriteTargetCount = 0;

void engineSetSpriteTargets(SpriteClipTarget *targets, uint8_t count) {
    spriteTargets = targets;
    spriteTargetCount = count;
}

// Classify the first surface hit by one pixel ray, using horizontal projections
// at the consecutive walls hit by the ray. This matches the reference PC's
// sector-bounded geometry, but NO per-pixel floating-point and NO frame-sized
// face-id buffer are necessary. All surfaces are 8-bit IDs.
#ifndef NANO_TRACE_OPTIMIZED
#define NANO_TRACE_OPTIMIZED 1
#endif
#if NANO_FIXED_POINT
#ifndef NANO_SPAN_FILL
#define NANO_SPAN_FILL 0
#endif
// Q8 camera, Q10 ray, Q6 distance, Q8 projected screen y.
// The costly math in traceColumn() uses signed 16/32-bit integer operations.
// float is intentionally retained in player movement/collision, NOT inside tracing.
// 128 offsets take 256 bytes of PROGMEM, generated from original 87px FOV.
#include "ray_offsets_q10.h"

static int16_t projY8(int16_t height8, uint16_t distance64) {
    if (distance64 == 0)
        distance64 = 1;
    const int32_t z = (int32_t)height8 * 87 * 2048;
    const int32_t y = 8192 - z / distance64;
    if (y < -32768)
        return -32768;
    if (y > 32767)
        return 32767;
    return (int16_t)y;
}

// Integer ceil(y_screen - 0.5) and floor(y_screen - 0.5) + 1,
// clamped to the viewport. Equivalents of the optimized float path.
static uint8_t ceilRow(int16_t edgeQ8) {
    const int32_t v = (int32_t)edgeQ8 + 127;
    if (v <= 0)
        return 0;
    if (v >= (64L << 8))
        return 64;
    return (uint8_t)(v >> 8);
}

static uint8_t belowRow(int16_t edgeQ8) {
    const int32_t v = (int32_t)edgeQ8 - 128;
    if (v < 0)
        return 0;
    if (v >= (63L << 8))
        return 64;
    return (uint8_t)((v >> 8) + 1);
}

static void traceColumn(uint8_t x, uint8_t *result) {
    memset(result, 0, HEIGHT);
    if (!mapReady)
        return;
    const uint8_t clipSlot = (x / NANO_RENDER_STEP) & 1;
    for (uint8_t i = 0; i < spriteTargetCount; i++)
        spriteTargets[i].lo[clipSlot] = spriteTargets[i].hi[clipSlot] = 0;
    uint8_t portalLo = 0, portalHi = 64;
#if NANO_SPAN_FILL
    uint8_t pendingLo = 0, pendingHi = HEIGHT;
#endif
    const int16_t off = (int16_t)pgm_read_word(&rayOffsetsQ10[x]);
    const int16_t dx = (int16_t)(fixedDX + ((int32_t)fixedDY * off) / 1024);
    const int16_t dy = (int16_t)(fixedDY - ((int32_t)fixedDX * off) / 1024);
#if NANO_GRID_FILTER
    buildFixedMask(dx, dy, fixedPX, fixedPY);
#endif
    uint8_t here = playerSector;
    uint16_t last64 = 0;
    for (uint8_t hop = 0; hop < MAX_RAY_HITS; ++hop) {
        const MapSector s = getSector(here);
        uint16_t best64 = 65535;
        uint16_t bestIndex = 0;
        bool found = false;
        for (uint8_t j = 0; j < s.count; j++) {
            const uint16_t index = s.first + j;
#if NANO_PROFILE
            ++candidateChecks;
#endif
#if NANO_GRID_FILTER
            if (!(candidateMask[index >> 3] & (uint8_t)(1u << (index & 7))))
                continue;
#endif
#if NANO_PROFILE
            ++exactChecks;
#endif
            const MapWall w = getWall(index);
            const int16_t ex = w.bx - w.ax, ey = w.by - w.ay;
            int32_t den = (int32_t)dx * ey - (int32_t)dy * ex;
            if (den == 0)
                continue;
            const int32_t ax = ((int32_t)w.ax * 256) - fixedPX;
            const int32_t ay = ((int32_t)w.ay * 256) - fixedPY;
            int32_t num = ax * ey - ay * ex;
            int32_t uNum = ax * dy - ay * dx;
            if (den < 0) {
                den = -den;
                num = -num;
                uNum = -uNum;
            }
            if (num <= 0 || uNum < 0 || uNum > den * 256)
                continue; // uNum Q18 vs den Q10
            // Segment u = uNum / (den*256): Q8 position vs Q10 ray.
            // t in units/64 = (num<<8)/den. Almost always 32-bit on this map.
            uint32_t t64;
            if (num <= 0x7fffffL)
                t64 = ((uint32_t)num << 8) / (uint32_t)den;
            else {
                // Exact quotient/remainder split avoids 64-bit AVR division.
                // |den| <= 2048*(|ex|+|ey|); validated E1M1 bounds keep rem<<8 safe.
                uint32_t q = (uint32_t)num / (uint32_t)den, r = (uint32_t)num % (uint32_t)den;
                t64 = (q << 8) + (r << 8) / (uint32_t)den;
            }
            if (t64 <= last64 || t64 >= best64)
                continue;
            best64 = (uint16_t)t64;
            bestIndex = index;
            found = true;
        }
        // Register sprite windows at their depth BEFORE the next wall. This
        // reuses the portal walk and needs no extra world ray or Z framebuffer.
        for (uint8_t i = 0; i < spriteTargetCount; i++) {
            SpriteClipTarget &c = spriteTargets[i];
            if (c.depth64 <= last64 || c.depth64 > best64)
                continue;
            uint8_t lo = ceilRow(projY8((int16_t)s.ceil_z * 8 - fixedEye8, c.depth64));
            uint8_t hi = belowRow(projY8((int16_t)s.floor_z * 8 - fixedEye8, c.depth64));
            c.lo[clipSlot] = lo > portalLo ? lo : portalLo;
            c.hi[clipSlot] = hi < portalHi ? hi : portalHi;
        }
        if (!found)
            break;
        const MapWall hit = getWall(bestIndex);
        const uint8_t neighbor = hit.next;
        const bool portal = neighbor != 255 && neighbor < MAP_SECTOR_COUNT && neighbor != here;
        uint8_t openTop = 0, openBottom = 0;
        if (portal) {
            const MapSector n = getSector(neighbor);
            const int8_t floor = s.floor_z > n.floor_z ? s.floor_z : n.floor_z;
            const int8_t ceil = s.ceil_z < n.ceil_z ? s.ceil_z : n.ceil_z;
            openTop = ceilRow(projY8((int16_t)ceil * 8 - fixedEye8, best64));
            openBottom = belowRow(projY8((int16_t)floor * 8 - fixedEye8, best64));
        }
        const uint8_t wallId = (uint8_t)(1 + (bestIndex % 189u));
        bool textured = false;
        uint8_t texX = 0;
        int16_t doorTop = 0, doorHeight = 1;
        int32_t texY0 = 0;
        uint16_t texStep = 0;
#if DOOM_INCLUDE_DOOR
        if (portal && hit.door >= 1 && hit.door <= 4 &&
            (fullMapNormalDoors() & (1u << (hit.door - 1))) &&
            getSector(neighbor).door == hit.door) {
            textured = true;
            const uint8_t d = hit.door - 1;
            doorHeight = doorTarget[d] - doorFloor[d];
            if (doorHeight < 1)
                doorHeight = 1;
            doorTop = doorCeiling[d] + doorHeight;
            texStep = (uint32_t)best64 * 128 / (87u * doorHeight);
            // Door texture's top edge follows the *moving* physical door.
            // 32 is the exact screen centre; the old 63/2 offset was 31.5.
            texY0 = ((int32_t)doorTop * 8 - fixedEye8) * 1024 / doorHeight - (int32_t)texStep * 32;
            int16_t ex = hit.bx - hit.ax, ey = hit.by - hit.ay;
            int32_t pos, extent;
            if (abs(ex) >= abs(ey)) {
                pos = fixedPX + (int32_t)dx * best64 / 256 - (int32_t)hit.ax * 256;
                extent = (int32_t)ex * 256;
            } else {
                pos = fixedPY + (int32_t)dy * best64 / 256 - (int32_t)hit.ay * 256;
                extent = (int32_t)ey * 256;
            }
            int32_t u = extent ? pos * 32 / extent : 0;
            texX = (uint8_t)(u < 0 ? 0 : u > 31 ? 31 : u);
        }
#endif
        auto doorPixel = [&](uint8_t y) -> uint8_t {
#if DOOM_INCLUDE_DOOR
            if (textured && y < openTop) {
                // Sample at pixel centre. Outside 0..31 is the surrounding
                // upper wall, NOT a clamped copy of the door's first/last row.
                int32_t v = (texY0 + ((int32_t)y * texStep) + texStep / 2) >> 8;
                if (v < 0 || v >= 32)
                    return wallId;
                uint8_t ty = (uint8_t)v;
                return (pgm_read_byte(bmp_door_bits + (uint16_t)ty * 4 + (texX >> 3)) &
                        (0x80u >> (texX & 7)))
                           ? 191
                           : 190;
            }
#endif
            return wallId;
        };
        const uint8_t topRow = ceilRow(projY8((int16_t)s.ceil_z * 8 - fixedEye8, best64));
        const uint8_t bottomRow = belowRow(projY8((int16_t)s.floor_z * 8 - fixedEye8, best64));
        const bool upper = (int16_t)s.ceil_z * 8 > fixedEye8;
        const bool lower = (int16_t)s.floor_z * 8 < fixedEye8;
#if NANO_SPAN_FILL
        // Unclassified pixels always form one contiguous window. Previous
        // portals have already resolved everything outside [pendingLo,pendingHi).
        // Fill only the portions newly covered by this sector; never revisit
        // finished pixels on subsequent portal traversals.
        if (upper && topRow > pendingLo) {
            const uint8_t hi = (topRow < pendingHi) ? topRow : pendingHi;
            for (uint8_t y = pendingLo; y < hi; ++y)
                result[y] = s.ceil_id;
            pendingLo = hi;
        }
        if (lower && bottomRow < pendingHi) {
            const uint8_t lo = (bottomRow > pendingLo) ? bottomRow : pendingLo;
            for (uint8_t y = lo; y < pendingHi; ++y)
                result[y] = s.floor_id;
            pendingHi = lo;
        }
        if (pendingLo >= pendingHi)
            break;
        if (!portal) {
            for (uint8_t y = pendingLo; y < pendingHi; ++y)
                result[y] = doorPixel(y);
            pendingHi = pendingLo;
            break;
        }
        if (openTop > pendingLo) {
            const uint8_t hi = (openTop < pendingHi) ? openTop : pendingHi;
            for (uint8_t y = pendingLo; y < hi; ++y)
                result[y] = doorPixel(y);
            pendingLo = hi;
        }
        if (openBottom < pendingHi) {
            const uint8_t lo = (openBottom > pendingLo) ? openBottom : pendingLo;
            for (uint8_t y = lo; y < pendingHi; ++y)
                result[y] = doorPixel(y);
            pendingHi = lo;
        }
        if (pendingLo >= pendingHi)
            break;
#else
        for (uint8_t y = 0; y < HEIGHT; y++) {
            if (result[y])
                continue;
            if (upper && y < topRow)
                result[y] = s.ceil_id;
            else if (lower && y >= bottomRow)
                result[y] = s.floor_id;
            else if (!portal || y < openTop || y >= openBottom)
                result[y] = doorPixel(y);
        }
        if (!portal)
            break;
#endif
        if (openTop > portalLo)
            portalLo = openTop;
        if (openBottom < portalHi)
            portalHi = openBottom;
        last64 = best64;
        here = neighbor;
    }
    const MapSector s = getSector(here);
#if NANO_SPAN_FILL
    for (uint8_t y = pendingLo; y < pendingHi; ++y)
        result[y] = (y < 32) ? s.ceil_id : s.floor_id;
#else
    for (uint8_t y = 0; y < HEIGHT; y++)
        if (!result[y])
            result[y] = (y < 32) ? s.ceil_id : s.floor_id;
#endif
}
#elif NANO_TRACE_OPTIMIZED
// Compute the first y with (y + 0.5) >= edge. Clamp to [0,64].
// Avoid float conversions for EVERY pixel: do one conversion per wall hit.
static uint8_t firstAtOrBelow(float edge) {
    if (edge <= 0.5f)
        return 0;
    if (edge >= 64.5f)
        return HEIGHT;
    const float z = edge - 0.5f;
    uint8_t n = (uint8_t)z;
    return (uint8_t)(n + ((float)n < z)); // ceil(z), including exact half-pixels
}

// Compute the first y with (y + 0.5) > edge.
static uint8_t firstBelow(float edge) {
    if (edge < 0.5f)
        return 0;
    if (edge >= 63.5f)
        return HEIGHT;
    return (uint8_t)((uint8_t)(edge - 0.5f) + 1u); // floor(z)+1
}

// All parallel/intersection/segment tests avoid float division.
// Only the selected NEAREST wall is divided to obtain distance.
static void traceColumn(uint8_t x, uint8_t *result) {
    memset(result, 0, HEIGHT);
    const float off = ((float)x + 0.5f - 64.f) * (1.0f / FOCAL);
    const float dx = dirX + dirY * off;
    const float dy = dirY - dirX * off;
#if NANO_GRID_FILTER
    buildRayCandidateMask(dx, dy, playerX, playerY);
#endif
    uint8_t here = playerSector;
    float last = 0.f;
    for (uint8_t hop = 0; hop < MAX_RAY_HITS; hop++) {
        MapSector s = getSector(here);
        float bestNum = 0.f, bestDen = 1.f;
        uint16_t bestIndex = 0;
        bool found = false;
        const float minimumDistance = last + 0.001f;
        for (uint8_t j = 0; j < s.count; j++) {
            const uint16_t index = s.first + j;
#if NANO_PROFILE
            ++candidateChecks;
#endif
#if NANO_GRID_FILTER
            if (!(candidateMask[index >> 3] & (uint8_t)(1u << (index & 7))))
                continue;
#endif
#if NANO_PROFILE
            ++exactChecks;
#endif
            MapWall w = getWall(index);
            const float ex = (float)w.bx - w.ax, ey = (float)w.by - w.ay;
            float den = dx * ey - dy * ex;
            if (fabsf(den) < 0.00001f)
                continue;
            const float ax = (float)w.ax - playerX, ay = (float)w.ay - playerY;
            float num = ax * ey - ay * ex;
            const bool reversed = (den < 0.f);
            if (reversed) {
                den = -den;
                num = -num;
            }
            // This is the same positive-t check as before, without t=num/den.
            if (num <= minimumDistance * den)
                continue;
            float un = ax * dy - ay * dx;
            if (reversed)
                un = -un;
            // 0 <= u=un/den <= 1 (tolerance matches the original).
            if (un < -0.00001f * den || un > 1.00001f * den)
                continue;
            // Compare distances as ratios; no per-wall division.
            if (found && num * bestDen >= bestNum * den)
                continue;
            bestNum = num;
            bestDen = den;
            bestIndex = index;
            found = true;
        }
        if (!found)
            break;
        const float best = bestNum / bestDen; // once per sector, not per candidate wall
        const MapWall hit = getWall(bestIndex);
        const float projection = FOCAL / best; // one projection division instead of 2-4
        const float top = 32.f - ((float)s.ceil_z - eye) * projection;
        const float bottom = 32.f - ((float)s.floor_z - eye) * projection;
        const uint8_t neighbor = hit.next;
        const bool portal = (neighbor != 255 && neighbor < MAP_SECTOR_COUNT && neighbor != here);
        uint8_t openingTopRow = 0, openingBottomRow = 0;
        if (portal) {
            MapSector n = getSector(neighbor);
            const int8_t higherFloor = s.floor_z > n.floor_z ? s.floor_z : n.floor_z;
            const int8_t lowerCeil = s.ceil_z < n.ceil_z ? s.ceil_z : n.ceil_z;
            const float openTop = 32.f - ((float)lowerCeil - eye) * projection;
            const float openBottom = 32.f - ((float)higherFloor - eye) * projection;
            openingTopRow = firstAtOrBelow(openTop);
            openingBottomRow = firstBelow(openBottom);
        }
        const uint8_t wallId = (uint8_t)(1u + bestIndex % 189u);
        const uint8_t topRow = firstAtOrBelow(top);
        const uint8_t bottomRow = firstBelow(bottom);
        const bool upper = ((float)s.ceil_z > eye), lower = ((float)s.floor_z < eye);
        for (uint8_t y = 0; y < HEIGHT; y++) {
            if (result[y])
                continue;
            if (upper && y < topRow)
                result[y] = s.ceil_id;
            else if (lower && y >= bottomRow)
                result[y] = s.floor_id;
            else if (!portal || y < openingTopRow || y >= openingBottomRow)
                result[y] = wallId;
        }
        if (!portal)
            break;
        here = neighbor;
        last = best;
    }
    const MapSector s = getSector(here);
    for (uint8_t y = 0; y < HEIGHT; y++) {
        if (!result[y])
            result[y] = (y < 32) ? s.ceil_id : s.floor_id;
    }
}

#else // Comparison build: original slow renderer
static void traceColumn(uint8_t x, uint8_t *result) {
    memset(result, 0, HEIGHT);
    const float off = ((float)x + .5f - 64.f) / FOCAL;
    const float dx = dirX + dirY * off;
    const float dy = dirY - dirX * off;
#if NANO_GRID_FILTER
    buildRayCandidateMask(dx, dy, playerX, playerY);
#endif
    uint8_t here = playerSector;
    float last = 0.f;
    for (uint8_t hop = 0; hop < MAX_RAY_HITS; hop++) {
        MapSector s = getSector(here);
        float best = 1e20f;
        uint16_t bestIndex = 0;
        bool found = false;
        for (uint8_t j = 0; j < s.count; j++) {
            const uint16_t index = s.first + j;
#if NANO_PROFILE
            ++candidateChecks;
#endif
#if NANO_GRID_FILTER
            if (!(candidateMask[index >> 3] & (uint8_t)(1u << (index & 7))))
                continue;
#endif
#if NANO_PROFILE
            ++exactChecks;
#endif
            MapWall w = getWall(index);
            const float ex = (float)w.bx - w.ax, ey = (float)w.by - w.ay;
            float den = dx * ey - dy * ex;
            if (fabsf(den) < 0.00001f)
                continue;
            float ax = w.ax - playerX, ay = w.ay - playerY;
            float t = (ax * ey - ay * ex) / den;
            if (t <= last + .001f || t >= best)
                continue;
            float u = (ax * dy - ay * dx) / den;
            if (u < -.00001f || u > 1.00001f)
                continue;
            best = t;
            bestIndex = index;
            found = true;
        }
        if (!found)
            break;
        MapWall hit = getWall(bestIndex);
        // A wall blocks different vertical heights depending on distance.
        const float top = 32.f - FOCAL * ((float)s.ceil_z - eye) / best;
        const float bottom = 32.f - FOCAL * ((float)s.floor_z - eye) / best;
        const uint8_t neighbor = hit.next;
        bool portal = (neighbor != 255 && neighbor < MAP_SECTOR_COUNT && neighbor != here);
        float openingTop = 0, openingBottom = 0;
        if (portal) {
            MapSector n = getSector(neighbor);
            int8_t higherFloor = s.floor_z > n.floor_z ? s.floor_z : n.floor_z;
            int8_t lowerCeil = s.ceil_z < n.ceil_z ? s.ceil_z : n.ceil_z;
            openingTop = 32.f - FOCAL * ((float)lowerCeil - eye) / best;
            openingBottom = 32.f - FOCAL * ((float)higherFloor - eye) / best;
        }
        const uint8_t wallId = (uint8_t)(1u + bestIndex % 189u);
        for (uint8_t y = 0; y < HEIGHT; y++) {
            if (result[y])
                continue;
            float yy = (float)y + .5f;
            if ((float)s.ceil_z > eye && yy < top)
                result[y] = s.ceil_id;
            else if ((float)s.floor_z < eye && yy > bottom)
                result[y] = s.floor_id;
            else if (!portal || yy < openingTop || yy > openingBottom)
                result[y] = wallId;
        }
        if (!portal)
            break;
        here = neighbor;
        last = best;
    }
    MapSector s = getSector(here);
    for (uint8_t y = 0; y < HEIGHT; y++) {
        if (!result[y])
            result[y] = ((float)y + 0.5f < 32.f) ? s.ceil_id : s.floor_id;
    }
}
#endif

#if NANO_DITHER_FILL
// Face IDs: walls 1..189, door black/white 190/191, floor 192..223, ceiling 224..255.
// 610 wall sides are hashed into wall labels to preserve the Nano's 128-byte
// column buffers. Rare label collisions can hide an outline, not geometry.
static constexpr uint8_t FIRST_CEILING_ID = 224;
#ifndef NANO_DITHER_WALL_LEVEL
#define NANO_DITHER_WALL_LEVEL 6
#endif
#ifndef NANO_DITHER_FLOOR_LEVEL
#define NANO_DITHER_FLOOR_LEVEL 3
#endif
#ifndef NANO_DITHER_CEIL_LEVEL
#define NANO_DITHER_CEIL_LEVEL 1
#endif
// Nano-Doom TNG v14: keep a 1-bit, 32x32 door bitmap in PROGMEM,
// but render its dark/light texels as Bayer levels 1/15 rather than 0/16.
// These constants add no image RAM or secondary texture.
#ifndef NANO_DOOR_DARK_LEVEL
#define NANO_DOOR_DARK_LEVEL 1
#endif
#ifndef NANO_DOOR_LIGHT_LEVEL
#define NANO_DOOR_LIGHT_LEVEL 15
#endif
static_assert(NANO_DITHER_WALL_LEVEL <= 16 && NANO_DITHER_FLOOR_LEVEL <= 15 &&
                  NANO_DITHER_CEIL_LEVEL <= 15 && NANO_DOOR_DARK_LEVEL <= 16 &&
                  NANO_DOOR_LIGHT_LEVEL <= 16,
              "Dither level must be between 0 and 16");

#if defined(NANO_PC_PORT) && !defined(__AVR__)
static inline uint8_t surfaceBrightness(uint8_t surfaceId, uint8_t screenY) {
    if (surfaceId == 190 || surfaceId == 191) {
        // PC-only experiments: door-source bits remain genuine 1bpp.
        // Dither either shade before converting to final 1bpp OLED pixels.
        const uint8_t black[4] = {0, 2, 3, 1}, white[4] = {16, 14, 12, 15};
        return surfaceId == 190 ? black[pcDither.doorStyle] : white[pcDither.doorStyle];
    }
    if (surfaceId == 0)
        return 0;
    if (surfaceId < 192)
        return (uint8_t)pcDither.wall;
    if (surfaceId < FIRST_CEILING_ID)
        return (uint8_t)(pcDither.floor + (pcDither.gradient && screenY >= 48));
    return (uint8_t)(pcDither.ceiling + (pcDither.gradient && screenY >= 16));
}

static const uint8_t bayer8[64] = {0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31,
                                   8,  56, 4,  52, 11, 59, 7,  55, 40, 24, 36, 20, 43, 27, 39, 23,
                                   2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30, 33, 17, 45, 29,
                                   10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21};
// Nonperiodic-looking, evenly scattered binary threshold mask. Permutation 0..63.
static const uint8_t scattered8[64] = {
    34, 19, 59, 31, 49, 36, 29, 1,  24, 43, 8,  15, 56, 4,  17, 51, 7,  61, 33, 40, 25, 44,
    58, 37, 52, 28, 0,  53, 11, 20, 30, 12, 18, 46, 22, 35, 47, 60, 3,  41, 57, 9,  63, 16,
    6,  26, 50, 32, 5,  38, 27, 42, 55, 39, 14, 23, 54, 13, 48, 2,  21, 10, 62, 45};
static const uint8_t cluster4[16] = {12, 5, 6, 13, 4, 0, 1, 7, 11, 3, 2, 8, 15, 10, 9, 14};

// One binary pixel on/off at a time, always in display (not source) coordinates.
static inline bool pcDitherPixel(uint8_t mode, uint8_t level, uint8_t x, uint8_t y,
                                 uint8_t surface) {
    if (!level || mode == 5)
        return false;
    if (level >= 16)
        return true;
    if (mode == 0) {
        uint8_t m = bayer4RowMasks[level * 4 + (y & 3)];
        return (m & (1u << (x & 3))) != 0;
    }
    if (mode == 6)
        mode = surface < 192 ? 1 : 0; // fine walls, original floor and ceiling
    if (mode == 1)
        return bayer8[(y & 7) * 8 + (x & 7)] < level * 4;
    if (mode == 2)
        return scattered8[(y & 7) * 8 + (x & 7)] < level * 4;
    if (mode == 3)
        return cluster4[(y & 3) * 4 + (x & 3)] < level;
    // Diagonal scanline / engraving pattern; 4x4 staggered rank.
    static const uint8_t hatch4[16] = {0, 4, 8, 12, 13, 1, 5, 9, 10, 14, 2, 6, 7, 11, 15, 3};
    return hatch4[(y & 3) * 4 + (x & 3)] < level;
}
#else
static inline uint8_t surfaceBrightness(uint8_t surfaceId, uint8_t screenY) {
    if (surfaceId == 0)
        return 0;
    if (surfaceId == 190)
        return NANO_DOOR_DARK_LEVEL;
    if (surfaceId == 191)
        return NANO_DOOR_LIGHT_LEVEL;
    if (surfaceId < 192)
        return NANO_DITHER_WALL_LEVEL; // default 38% wall
    if (surfaceId < FIRST_CEILING_ID) {
        // Floor: default 19% at horizon to 25% near bottom (screen-space gradient).
        const uint8_t base = NANO_DITHER_FLOOR_LEVEL;
#if NANO_DITHER_GRADIENT
        return (uint8_t)(base + (screenY >= 48));
#else
        return base;
#endif
    }
    // Ceiling: default 6% at top to 13% towards horizon.
#if NANO_DITHER_GRADIENT
    return (uint8_t)(NANO_DITHER_CEIL_LEVEL + (screenY >= 16));
#else
    return NANO_DITHER_CEIL_LEVEL;
#endif
}

#endif

// Assemble the Bayer pattern in four 8-bit vertical masks, then write
// an ENTIRE OLED PAGE BYTE at a time for each physical column.
// One ray covers NANO_RENDER_STEP physical columns (1/2/4/8).
// Critical: putByte must OR the mask, not erase existing HUD/crosshair pixels.
#if defined(NANO_PC_PORT) && !defined(__AVR__)
static void paintDitherColumn(uint8_t x, const uint8_t *faceIds, void (*putPixel)(uint8_t, uint8_t),
                              void (*putPageByte)(uint8_t, uint8_t, uint8_t)) {
    for (uint8_t page = 0; page < 8; page++) {
        const uint8_t y0 = page * 8;
        for (uint8_t k = 0; k < NANO_RENDER_STEP; k++) {
            const uint8_t sx = x + k;
            uint8_t out = 0;
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint8_t y = y0 + bit;
                uint8_t level = surfaceBrightness(faceIds[y], y);
                if (pcDitherPixel((uint8_t)pcDither.pattern, level, sx, y, faceIds[y]))
                    out |= 1u << bit;
            }
            if (out) {
                if (putPageByte)
                    putPageByte(sx, page, out);
                else
                    for (uint8_t b = 0; b < 8; b++)
                        if (out & (1u << b))
                            putPixel(sx, y0 + b);
            }
        }
    }
}
#else
static void paintDitherColumn(uint8_t x, const uint8_t *faceIds, void (*putPixel)(uint8_t, uint8_t),
                              void (*putPageByte)(uint8_t, uint8_t, uint8_t)) {
    for (uint8_t page = 0; page < 8; page++) {
        uint8_t masks[4] = {0, 0, 0, 0};
        const uint8_t firstY = (uint8_t)(page << 3);
        for (uint8_t bit = 0; bit < 8; bit++) {
            const uint8_t y = (uint8_t)(firstY + bit);
            const uint8_t level = surfaceBrightness(faceIds[y], y);
            const uint8_t rowMask = pgm_read_byte(&bayer4RowMasks[(uint8_t)(level * 4 + (y & 3))]);
            const uint8_t pixelMask = (uint8_t)(1u << bit);
            for (uint8_t phase = 0; phase < 4; phase++) {
                if (rowMask & (1u << phase))
                    masks[phase] |= pixelMask;
            }
        }
        for (uint8_t k = 0; k < NANO_RENDER_STEP; k++) {
            const uint8_t column = (uint8_t)(x + k);
            const uint8_t b = masks[column & 3];
            if (!b)
                continue;
            if (putPageByte)
                putPageByte(column, page, b);
            else {
                // Portable reference/fallback, no framebuffer API required.
                for (uint8_t bit = 0; bit < 8; bit++)
                    if (b & (1u << bit))
                        putPixel(column, (uint8_t)(firstY + bit));
            }
        }
    }
}
#endif

#endif

// The previous full-frame loop is now a resumable, single-column renderer.
// Keeping these buffers across calls is essential for correct vertical outlines.
static uint8_t renderX = 0;

static uint8_t edgeFace(uint8_t v) {
    return v == 191 ? 190 : v;
}

void engineRenderBegin() {
    renderX = 0;
    memset(previousColumn, 0, sizeof(previousColumn));
}

uint8_t engineRenderNextX() {
    return renderX;
}

bool engineRenderStep(void (*putPixel)(uint8_t, uint8_t),
                      void (*putPageByte)(uint8_t, uint8_t, uint8_t)) {
    if (renderX >= WIDTH)
        return true;
    const uint8_t x = renderX;
    traceColumn((uint8_t)(x + NANO_RENDER_STEP / 2), currentColumn);
#if NANO_DITHER_FILL
    // Fill first, then overlay the original WHITE contour lines below.
    paintDitherColumn(x, currentColumn, putPixel, putPageByte);
#else
    (void)putPageByte;
    for (uint8_t y = 0; y < HEIGHT; y++)
        if (currentColumn[y] == 191)
            for (uint8_t k = 0; k < NANO_RENDER_STEP; k++)
                putPixel(x + k, y);
#endif
    for (uint8_t y = 0; y < HEIGHT; y++) {
        if (y + 1 < HEIGHT && edgeFace(currentColumn[y]) != edgeFace(currentColumn[y + 1])) {
            for (uint8_t k = 0; k < NANO_RENDER_STEP; k++)
                putPixel(x + k, y);
        }
        // A boundary at x-1 becomes final only once column x is traced.
        if (x > 0 && edgeFace(currentColumn[y]) != edgeFace(previousColumn[y]))
            putPixel(x - 1, y);
    }
    memcpy(previousColumn, currentColumn, HEIGHT);
    renderX = (uint8_t)(renderX + NANO_RENDER_STEP);
    return renderX >= WIDTH;
}

void engineRender(void (*putPixel)(uint8_t, uint8_t),
                  void (*putPageByte)(uint8_t, uint8_t, uint8_t)) {
    engineRenderBegin();
    while (!engineRenderStep(putPixel, putPageByte)) {
    }
}
