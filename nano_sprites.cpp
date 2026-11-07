#include "config.h"
#include "nano_sprites.h"
#include "nano_sprite_data.h"
#include "sprites.h"
#include "nano_font.h"
#include "engine.h"
#include "full_map.h"
#include <math.h>
#include <string.h>

#ifndef NANO_FB_COLUMNS
#define NANO_FB_COLUMNS 128
#endif 

#ifndef NANO_IMP_AI
#define NANO_IMP_AI 1
#endif

#ifndef NANO_SKILL
#define NANO_SKILL 2 // skill: 1 easy, 2 medium, 3 hard.
#endif

#ifndef NANO_IMP_PROJECTILES
#define NANO_IMP_PROJECTILES 1
#endif

#ifndef NANO_ACTIVE_IMPS
#define NANO_ACTIVE_IMPS 3
#endif

#ifndef NANO_VISIBLE_OBJECTS
#define NANO_VISIBLE_OBJECTS 3
#endif

#ifndef NANO_RENDER_STEP
#define NANO_RENDER_STEP 4
#endif

// share the Bullet Box art with all shotgun ammo/pickups and
// the Light Column art with the Candelabra. Object types/values
// and E1M1 THINGS are unchanged. Only the rendered artwork differs.
#ifndef NANO_SHARE_SPRITE_ART
#define NANO_SHARE_SPRITE_ART 0
#endif

static_assert(NANO_SKILL >= 1 && NANO_SKILL <= 3, "skill: 1 easy, 2 medium, 3 hard");
static_assert(NANO_ACTIVE_IMPS > 0 && NANO_ACTIVE_IMPS <= 6, "active Imp limit");
static_assert(NANO_VISIBLE_OBJECTS >= 0 && NANO_VISIBLE_OBJECTS <= 6, "object limit");

static const uint8_t MAX_VISIBLE = NANO_ACTIVE_IMPS + NANO_VISIBLE_OBJECTS;

#if NANO_IMP_PROJECTILES
static const uint8_t MAX_BALLS = 2;

struct Fireball {
    float x, y, vx, vy;
    uint32_t last;
    uint8_t sector;
    bool active;
};

static Fireball balls[MAX_BALLS];
#endif

struct Imp {
    float x, y;
    uint32_t nextAttack;
    uint8_t sector, spawn, hp;
    bool awake, los;
};

struct View {
    uint16_t depth64;
    int16_t left, top;
    uint8_t w, h, kind, frame, actor;
};

struct Art {
    const uint8_t *bits, *mask;
    uint8_t w, h, frames, worldHeight;
};

enum {
    NONE,
    IMP,
    ARMOR,
    HEALTH_BONUS,
    ARMOR_BONUS,
    STIM,
    MEDKIT,
    CLIP,
    BULLETS,
    SHELLS,
    SHELLBOX,
    SHOTGUN,
    BARREL,
    LIGHT,
    CANDELABRA,
    FIREBALL,
    CORPSE
};

static Imp imps[NANO_ACTIVE_IMPS];
static View views[MAX_VISIBLE];
static SpriteClipTarget clips[MAX_VISIBLE];
static uint8_t viewCount = 0;

// Two bits per original THING: enemy HP (3..0), collected pickup (0).
static uint8_t states[(MAP_THING_COUNT + 3) / 4];

static uint8_t playerHP = 100, ammo = 50, armor = 0, keys = 0;
static uint32_t lastAI = 0, lastShot = 0;
static bool initialised = false, shotOnce = false, showFlash = false, showHurt = false;
static uint8_t flashFrames = 0, hurtFrames = 0, eventFx = 0;

// Tiny 32x8 flattened silhouette. Bitmap and mask occupy just 64 flash bytes.
static const uint8_t corpseBits[32] PROGMEM = {
    0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x81, 0x80, 0, 0x07, 0xE7, 0xF0, 0,
    0x0E, 0xFF, 0xFC, 0, 0x3F, 0xFF, 0xFE, 0, 0x7F, 0xFF, 0xFF, 0x80, 0x1F, 0x7E, 0xF8, 0
};

static const uint8_t corpseMask[32] PROGMEM = {
    0, 0, 0, 0, 0x01, 0x81, 0x80, 0, 0x07, 0xE7, 0xF0, 0, 0x1F, 0xFF, 0xFC, 0,
    0x3F, 0xFF, 0xFE, 0, 0x7F, 0xFF, 0xFF, 0x80, 0x7F, 0xFF, 0xFF, 0x80, 0x1F, 0x7E, 0xF8, 0
};

static uint8_t state(uint8_t i) {
    return (states[i >> 2] >> ((i & 3) * 2)) & 3;
}

static void setState(uint8_t i, uint8_t value) {
    uint8_t shift = (i & 3) * 2;
    states[i >> 2] = (states[i >> 2] & ~(3u << shift)) | (value << shift);
}

static bool enabled(const MapThing &t) {
    return !(t.flags & 16) && (t.flags & (1u << (NANO_SKILL - 1)));
}

static bool enemy(uint16_t t) {
#if !DOOM_INCLUDE_MONSTERS
    return false;
#else
    return t == 3001 || t == 3004 || t == 9; // all enemy types present in E1M1
#endif
}

static uint8_t kind(uint16_t t) {
    switch (t) {
#if DOOM_INCLUDE_PICKUPS
        case 2018:
        case 2019:
            return ARMOR;
        case 2014:
            return HEALTH_BONUS;
        case 2015:
            return ARMOR_BONUS;
        case 2011:
            return STIM;
        case 2012:
            return MEDKIT;
        case 2007:
            return CLIP;
        case 2048:
            return BULLETS;
        case 2008:
            return SHELLS;
        case 2049:
            return SHELLBOX;
        case 2001:
            return SHOTGUN;
#endif
#if DOOM_INCLUDE_DECORATIONS
        case 2035:
            return BARREL;
        case 2028:
            return LIGHT;
        case 35:
            return CANDELABRA;
#endif
        default:
            return NONE;
    }
}

static Art art(uint8_t k) {
#if NANO_FULL_ANIMATION
#define FRAMES(n) n
#else
#define FRAMES(n) 1
#endif

#define A(name, w, h, n, z) {bmp_##name##_bits, bmp_##name##_mask, w, h, FRAMES(n), z}
#define EMPTY {nullptr, nullptr, 0, 0, 1, 0}

    static const Art table[] PROGMEM = {
        EMPTY,
#if DOOM_INCLUDE_MONSTERS
        {bmp_imp_bits, bmp_imp_mask, BMP_IMP_WIDTH, 32, 2, 7},
#else
        EMPTY,
#endif
#if DOOM_INCLUDE_PICKUPS
        {bmp_green_and_blue_armor_bits, bmp_green_and_bkue_armor_mask, 32, 20, FRAMES(2), 4},
        A(health_bonus, 16, 20, 4, 2),
        A(armor_bonus, 16, 16, 4, 2),
        A(stimpack, 16, 16, 1, 2),
        A(medikit, 32, 20, 1, 3),
        A(ammo_clip, 16, 16, 1, 2),
        A(bullet_box, 32, 16, 1, 3),
#if NANO_SHARE_SPRITE_ART
        // Alias all shotgun-related art to Bullet Box; retain original
        // worldHeight so pickups still appear at roughly their former scale.
        A(bullet_box, 32, 16, 1, 1),
        A(bullet_box, 32, 16, 1, 2),
        A(bullet_box, 32, 16, 1, 1),
#else
        A(shotgun_shells, 16, 8, 1, 1),
        A(shell_box, 32, 16, 1, 2),
        A(shotgun_pickup, 32, 8, 1, 1),
#endif
#else
        EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
#endif
#if DOOM_INCLUDE_DECORATIONS
        A(barrel, 24, 32, 2, 5),
        A(light_column, 24, 48, 1, 6),
#if NANO_SHARE_SPRITE_ART
        A(light_column, 24, 48, 1, 6), // Candelabra: shared pixels and mask
#else
        A(candelabra, 24, 48, 1, 6),
#endif
#else
        EMPTY, EMPTY, EMPTY,
#endif
#if DOOM_INCLUDE_MONSTERS
#if NANO_IMP_PROJECTILES
        {bmp_fireball_bits, bmp_fireball_mask, 16, 16, 1, 2},
#else
        EMPTY,
#endif
        {corpseBits, corpseMask, 32, 8, 1, 1}
#else
        EMPTY, EMPTY
#endif
    };

    Art result;
#ifdef __AVR__
    memcpy_P(&result, &table[k], sizeof(result));
#else
    result = table[k];
#endif
    return result;

#undef A
#undef EMPTY
#undef FRAMES
}

void nanoSpritesReset() {
    memset(imps, 0, sizeof(imps));
    for (uint8_t i = 0; i < NANO_ACTIVE_IMPS; i++) imps[i].spawn = 255;
    
    memset(states, 255, sizeof(states));
    viewCount = 0;
    playerHP = 100;
    ammo = 50;
    armor = keys = 0;
    lastAI = lastShot = 0;
    shotOnce = false;
    flashFrames = hurtFrames = eventFx = 0;
    showFlash = showHurt = false;
    initialised = true;

#if NANO_IMP_PROJECTILES
    memset(balls, 0, sizeof(balls));
#endif

    engineSetSpriteTargets(clips, 0);
}

uint8_t nanoSpritesHealth() { return playerHP; }
uint8_t nanoSpritesAmmo() { return ammo; }
uint8_t nanoSpritesArmor() { return armor; }

uint8_t nanoSpritesConsumeEffect() {
    uint8_t e = eventFx;
    eventFx = 0;
    return e;
}

uint8_t nanoSpritesActive() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < NANO_ACTIVE_IMPS; i++) 
        n += imps[i].hp != 0;
    return n;
}

uint8_t nanoSpritesThingState(uint8_t i) {
    return i < MAP_THING_COUNT ? state(i) : 0;
}

uint8_t nanoSpritesAlive() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAP_THING_COUNT; i++) {
        MapThing t;
        fullMapThing(i, &t);
        n += enabled(t) && enemy(t.type) && state(i) != 0;
    }
    return n;
}

static uint8_t addCapped(uint8_t value, uint8_t amount, uint8_t limit) {
    uint16_t v = (uint16_t)value + amount;
    return v > limit ? limit : v;
}

static bool pickup(uint8_t k, uint16_t type) {
    switch (k) {
        case STIM:
        case MEDKIT:
            if (playerHP >= 100) return false;
            playerHP = addCapped(playerHP, k == STIM ? 10 : 25, 100);
            break;
        case HEALTH_BONUS:
            if (playerHP >= 200) return false;
            ++playerHP;
            break;
        case ARMOR: {
            uint8_t target = type == 2019 ? 200 : 100;
            if (armor >= target) return false;
            armor = target;
            break;
        }
        case ARMOR_BONUS:
            if (armor == 200) return false;
            ++armor;
            break;
        case CLIP:
        case BULLETS:
        case SHELLS:
        case SHELLBOX:
        case SHOTGUN:
            if (ammo == 200) return false;
            ammo = addCapped(ammo, k == BULLETS ? 50 : k == SHELLBOX ? 20 : k == CLIP ? 10 : k == SHOTGUN ? 8 : 4, 200);
            break;
        default:
            return false;
    }
    eventFx = 1;
    return true;
}

static void applyDamage(uint8_t damage) {
    uint8_t absorb = damage / 3;
    if (absorb > armor) absorb = armor;
    armor -= absorb;
    damage -= absorb;
    playerHP = playerHP > damage ? playerHP - damage : 0;
    hurtFrames = 2;
    eventFx = 2;
}

#if NANO_IMP_PROJECTILES
static void launchBall(const Imp &m, float dx, float dy, float distance, uint32_t now) {
    if (distance < 0.001f) return;
    for (uint8_t i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active) {
            Fireball &b = balls[i];
            b.x = m.x;
            b.y = m.y;
            b.sector = m.sector;
            b.vx = dx / distance * 12.0f;
            b.vy = dy / distance * 12.0f;
            b.last = now;
            b.active = true;
            return;
        }
    }
}

static void updateBalls(uint32_t now, float px, float py) {
    for (uint8_t i = 0; i < MAX_BALLS; i++) {
        Fireball &b = balls[i];
        if (!b.active) continue;

        uint32_t dt = now - b.last;
        if (dt < 30) continue;
        if (dt > 250) dt = 250;
        b.last = now;

        float step = dt * .001f;
        float x = b.x, y = b.y;
        engineMoveMonster(&b.x, &b.y, &b.sector, b.vx * step, b.vy * step);

        float movedX = b.x - x, movedY = b.y - y;
        if (movedX * movedX + movedY * movedY < step * step * 4.0f) {
            b.active = false;
            continue;
        }

        float dx = b.x - px, dy = b.y - py;
        if (dx * dx + dy * dy < 4.0f) {
            applyDamage(8);
            b.active = false;
            continue;
        }
        if ((dx * dx + dy * dy) > 10000.0f) b.active = false;
    }
}
#endif

static void updateAI(uint32_t now) {
    uint32_t dt = now - lastAI;
    if (dt < 160) return;
    lastAI = now;
    if (dt > 320) dt = 320;

    if (!playerHP) return;

    const float px = engineX(), py = engineY();

#if NANO_IMP_PROJECTILES
    updateBalls(now, px, py);
#endif

    for (uint8_t i = 0; i < NANO_ACTIVE_IMPS; i++) {
        Imp &m = imps[i];
        if (!m.hp) continue;

        float dx = px - m.x, dy = py - m.y, d2 = dx * dx + dy * dy;
        if (d2 > 10000) {
            setState(m.spawn, m.hp);
            m.hp = 0;
            m.spawn = 255;
            continue;
        }

        m.los = d2 < 8100 && engineCanSeeSprite(m.x, m.y);

#if NANO_IMP_AI
        if (m.los && d2 <= 3025) m.awake = true;
        if (!m.awake || !m.los) continue;

        float d = sqrtf(d2);
        // Stop at attack range; keep v12's inexpensive hitscan attack.
        if (d > 17) {
            float step = 3.5f * (dt * .001f);
            engineMoveMonster(&m.x, &m.y, &m.sector, dx * step / d, dy * step / d);
        }

        if (d2 <= 900 && (int32_t)(now - m.nextAttack) >= 0) {
#if NANO_IMP_PROJECTILES
            launchBall(m, dx, dy, d, now);
#else
            if (d2 <= 289) applyDamage(d < 6 ? 8 : 4);
#endif
            m.nextAttack = now + 2300u + (uint16_t)i * 120u;
        }
#endif
    }

    const int16_t ix = (int16_t)px, iy = (int16_t)py;
    for (uint8_t i = 0; i < MAP_THING_COUNT; i++) {
        if (!state(i)) continue;

        MapThing t;
        fullMapThing(i, &t);
        if (!enabled(t)) continue;

        int16_t dx = t.x - ix, dy = t.y - iy;
        if (dx < -55 || dx > 55 || dy < -55 || dy > 55) continue;

        uint16_t d2 = (int32_t)dx * dx + (int32_t)dy * dy;
        if (enemy(t.type)) {
            if (d2 > 3025) continue;

            bool active = false;
            int8_t free = -1;
            for (uint8_t j = 0; j < NANO_ACTIVE_IMPS; j++) {
                if (imps[j].spawn == i) active = true;
                if (!imps[j].hp) free = j;
            }

            if (active || free < 0) continue;

            Imp &m = imps[(uint8_t)free];
            m.x = t.x;
            m.y = t.y;
            m.sector = t.sector;
            m.spawn = i;
            m.hp = state(i);
            m.nextAttack = now + 1000;
            m.awake = false;
            m.los = engineCanSeeSprite(m.x, m.y);
        } else if (d2 <= 4 && engineSector() == t.sector && engineCanSeeSprite(t.x, t.y) && pickup(kind(t.type), t.type)) {
            setState(i, 0);
        }
    }
}

static int16_t cameraX64, cameraY64, cameraFX, cameraFY, cameraEye8;

static void project(int16_t x64, int16_t y64, uint8_t sector, uint8_t k, uint8_t actor, uint32_t now) {
    const int16_t dx = x64 - cameraX64, dy = y64 - cameraY64;
    const int16_t qfx = cameraFX, qfy = cameraFY;

    int16_t depth = (int32_t(dx) * qfx + int32_t(dy) * qfy) / 1024;
    if (depth <= 64 || depth >= 5760) return;

    Art a = art(k);
    if (!a.bits) return;

    int16_t side = (int32_t(dx) * qfy - int32_t(dy) * qfx) / 1024;
    int16_t center = 64 + (int32_t)87 * side / depth;

    uint16_t height = (uint32_t)5568 * a.worldHeight / depth;
    if (!height) return;
    if (height > 56) height = 56;

    uint8_t h = height, w = (uint16_t)h * a.w / a.h;
    if (!w) w = 1;

    int16_t left = center - w / 2;
    if (left + w <= 0 || left >= 128) return;

#if !NANO_FIXED_POINT
    if (!engineCanSeeSprite(x64 / 64.f, y64 / 64.f)) return;
#endif

    View v;
    v.depth64 = depth;
    v.left = left;
    v.w = w;
    v.h = h;
    v.kind = k;
    v.actor = actor;

    int16_t eye8 = cameraEye8, floor8 = (int16_t)engineSectorFloor(sector) * 8;
    v.top = k == FIREBALL ? 32 - (h / 2) : 32 + (int32_t)696 * (eye8 - floor8) / depth - h;

#if NANO_FULL_ANIMATION
    v.frame = (now / 400u) % a.frames;
#else
    v.frame = 0;
#endif
    if (k == IMP) v.frame = (now / 500u + actor) & 1u;

    // Keep nearest visible candidates. Small array; no heap and no full map copy.
    uint8_t pos = viewCount;
    if (pos == MAX_VISIBLE) {
        if (v.depth64 >= views[0].depth64) return;
        pos = 0;
    } else {
        ++viewCount;
    }

    views[pos] = v;
    for (uint8_t i = 1; i < viewCount; i++) {
        View q = views[i];
        uint8_t j = i;
        while (j && views[j - 1].depth64 < q.depth64) {
            views[j] = views[j - 1];
            --j;
        }
        views[j] = q;
    }
}

void nanoSpritesBeginFrame(uint32_t now) {
    if (!initialised) {
        nanoSpritesReset();
        lastAI = now - 160;
    }
    updateAI(now);
    showFlash = flashFrames > 0;
    if (flashFrames) --flashFrames;
    showHurt = hurtFrames > 0;
    if (hurtFrames) --hurtFrames;

    viewCount = 0;
    const float angle = engineAngle();
    cameraFX = (int16_t)(cosf(angle) * 1024);
    cameraFY = (int16_t)(sinf(angle) * 1024);
    cameraX64 = (int16_t)(engineX() * 64);
    cameraY64 = (int16_t)(engineY() * 64);
    cameraEye8 = (int16_t)(engineEye() * 8);

    for (uint8_t i = 0; i < NANO_ACTIVE_IMPS; i++) {
        if (imps[i].hp) project((int16_t)(imps[i].x * 64), (int16_t)(imps[i].y * 64), imps[i].sector, IMP, i, now);
    }

#if NANO_IMP_PROJECTILES
    for (uint8_t i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) project((int16_t)(balls[i].x * 64), (int16_t)(balls[i].y * 64), balls[i].sector, FIREBALL, 255, now);
    }
#endif

#if NANO_VISIBLE_OBJECTS > 0
    // Restrict integer candidates before any projection or division.
    const int16_t px = (int16_t)engineX(), py = (int16_t)engineY();
    for (uint8_t i = 0; i < MAP_THING_COUNT; i++) {
        MapThing t;
        fullMapThing(i, &t);
        if (!enabled(t)) continue;

        uint8_t k = state(i) ? kind(t.type) : (enemy(t.type) ? uint8_t(CORPSE) : uint8_t(NONE));
        if (!k) continue;

        int16_t dx = t.x - px, dy = t.y - py;
        if (dx < -70 || dx > 70 || dy < -70 || dy > 70) continue;
        project(t.x * 64, t.y * 64, t.sector, k, 255, now);
    }
#endif

    for (uint8_t i = 0; i < viewCount; i++) {
        clips[i].depth64 = views[i].depth64;
        clips[i].lo[0] = clips[i].lo[1] = 0;
        clips[i].hi[0] = clips[i].hi[1] = 64;
    }
    engineSetSpriteTargets(clips, viewCount);
}

void nanoSpritesFire(uint32_t now) {
    if (!initialised) nanoSpritesReset();
    if (!playerHP || !ammo) return;
    if (shotOnce && (uint32_t)(now - lastShot) < 240) return;

    shotOnce = true;
    lastShot = now;
    flashFrames = 2;
    --ammo;

    // Sorted far-to-near. First eligible actor encountered in reverse wins.
    for (int8_t i = viewCount - 1; i >= 0; --i) {
        const View &v = views[(uint8_t)i];
        if (v.actor == 255 || v.depth64 > 65u * 64u || v.left > 66 || v.left + v.w < 62 || v.top > 34 || v.top + v.h < 30) continue;

        Imp &m = imps[v.actor];
        if (!m.hp || !engineCanSeeSprite(m.x, m.y)) continue;

        --m.hp;
        setState(m.spawn, m.hp);
        if (!m.hp) m.spawn = 255;
        return;
    }
}

static void paintWorldColumn(uint8_t x, uint8_t *fb, uint8_t index) {
    const View &v = views[index];
    if (x < v.left || x >= v.left + v.w) return;

    const uint8_t slot = (x / NANO_RENDER_STEP) & 1;
    uint8_t lo = clips[index].lo[slot], hi = clips[index].hi[slot];
    if (lo >= hi) return;

    Art a = art(v.kind);
    uint8_t sx = (uint16_t)(x - v.left) * a.w / v.w, bit = 0x80u >> (sx & 7), stride = (a.w + 7) / 8;
    const uint16_t base = (uint16_t)v.frame * stride * a.h;
    uint16_t yq = 0, step = ((uint16_t)a.h << 8) / v.h;

    for (uint8_t sy = 0; sy < v.h; sy++, yq += step) {
        int16_t y = v.top + sy;
        if (y < lo || y >= hi) continue;

        uint16_t off = base + (yq >> 8) * stride + (sx >> 3);
        if (!(pgm_read_byte(a.mask + off) & bit)) continue;

        uint8_t mask = 1u << (y & 7);
        uint8_t &dst = fb[(uint16_t)(y >> 3) * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))];

        if (pgm_read_byte(a.bits + off) & bit) dst |= mask;
        else dst &= ~mask;
    }
}


static inline void paintGunColumn(uint8_t x, uint8_t *fb) {
    if (x < 48 || x >= 80) return;

    const uint8_t bx = (uint8_t)(x - 48);
    for (uint8_t pg = 0; pg < 3; pg++) {
        const uint8_t ix = (uint8_t)(pg * 32u + bx);

        const uint8_t mask = pgm_read_byte(&nano_gun_mask[ix]);
        const uint8_t value = pgm_read_byte(&nano_gun_data[ix]);

        // The bottom eight pixels of the gun will be hidden by the ammo bar
        // when NANO_STATUS_HUD is enabled, leaving a smaller visible weapon.
        uint8_t &dst = fb[
            (uint16_t)(pg + 5u) * NANO_FB_COLUMNS +
            (x & (NANO_FB_COLUMNS - 1))
        ];

        dst = (uint8_t)((dst & ~mask) | (value & mask));
    }
}

static inline void paintFlashColumn(uint8_t x, uint8_t *fb) {
    if (!showFlash || x < 52 || x >= 76) return;
    const uint8_t srcX = (uint8_t)(x - 52);
    const uint8_t bit = (uint8_t)(0x80u >> (srcX & 7));
    for (uint8_t row = 0; row < 20; row++) {
        const uint16_t ix = (uint16_t)row * 3u + (srcX >> 3);
        if (pgm_read_byte(&nano_fire_data[ix]) & bit) {
           // const uint8_t y = (uint8_t)(17 + row);
            const uint8_t y = (uint8_t)(25 + row);
            fb[(uint16_t)(y >> 3) * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))] |= (uint8_t)(1u << (y & 7));
        }
    }
}

// The original 1-row HUD stays available when NANO_STATUS_HUD=0.
#if !NANO_STATUS_HUD
static void paintHudColumn(uint8_t x, uint8_t *fb) {
    uint8_t start, value;
    char label;
    if (x >= 108) {
        start = 108;
        value = playerHP;
        label = 'H';
    } else if (x >= 65 && x < 85) {
        start = 65;
        value = ammo;
        label = 'A';
    } else if (x < 20) {
        start = 0;
        value = armor;
        label = 'R';
    } else return;

    uint8_t dx = x - start, col = dx % 5, digit = dx / 5;
    char c = label;
    if (digit) {
        c = '0' + (digit == 1 ? value / 100 : digit == 2 ? (value / 10) % 10 : value % 10);
    }
    fb[x & (NANO_FB_COLUMNS - 1)] = col < 4 ? nanoFontColumn(c, col) : 0;
   //   fb[7u * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))]  = col < 4 ? nanoFontColumn(c, col) : 0;
}

#else
// Bottom status windows: side wings hide the world; the middle window is
// shallow (8 px), leaving the gun and 3D image visible above it.
// Every operation uses the current ringbuffer column; NO new SRAM buffer.
static const char healthLabel[] PROGMEM = "HEALTH";
static const char ammoLabel[]   PROGMEM = "AMMO";
static const char armorLabel[]  PROGMEM = "ARMOR";

// Render one 4x6 glyph's vertical column at display X (font has 5px pitch).
static uint8_t hudLabelBits(uint8_t x, uint8_t start, uint8_t letters,
                            const char *label) {
    if (x < start || x >= (uint8_t)(start + letters * 5u)) return 0;
    const uint8_t dx = (uint8_t)(x - start);
    const uint8_t col = dx % 5u;
    if (col == 4) return 0;
    return nanoFontColumn((char)pgm_read_byte(label + dx / 5u), col);
}

static uint8_t hudNumberBits(uint8_t x, uint8_t start, uint8_t value) {
    if (x < start || x >= (uint8_t)(start + 15u)) return 0;
    const uint8_t dx = (uint8_t)(x - start);
    const uint8_t col = dx % 5u;
    if (col == 4) return 0;
    const uint8_t digit = dx / 5u;
    const uint8_t number = digit == 0 ? value / 100u :
                           digit == 1 ? (value / 10u) % 10u : value % 10u;
    return nanoFontColumn((char)('0' + number), col);
}

// HUD drawn AFTER view-bob, overwriting pixels in the status windows.
// Left/Right: 16 or 12 pixels. Centre: bottom 8 pixels only.
void nanoSpritesPaintHudColumns(uint8_t beginX, uint8_t endX, uint8_t *fb) {
    for (uint8_t x = beginX; x < endX; ++x) {
        const uint8_t col = x & (NANO_FB_COLUMNS - 1);
        uint8_t &p6 = fb[6u * NANO_FB_COLUMNS + col];
        uint8_t &p7 = fb[7u * NANO_FB_COLUMNS + col];

        if (x >= 40 && x < 88) {
            // Open window: preserve Y=48..55, including 3D and gun.
            // A compact AMMO/nnn row sits in the bottom 8 pixels.
            p7 = (uint8_t)(0x01u |
                (hudLabelBits(x, 43, 4, ammoLabel) << 1) |
                (hudNumberBits(x, 70, ammo) << 1));
            if (x == 40 || x == 87) p7 = 0xff;
        } else {
            // Solid side wings hide the lower world without redrawing the 3D.
            const bool isHealth = x < 40;
            const uint8_t labelX = isHealth ? 4u : 95u;
            const uint8_t valueX = isHealth ? 12u : 100u;
            const uint8_t glyph = hudLabelBits(x, labelX, isHealth ? 6u : 5u,
                                                isHealth ? healthLabel : armorLabel);
            const uint8_t num = hudNumberBits(x, valueX,
                                               isHealth ? playerHP : armor);
#if NANO_HUD_HEIGHT == 16
            // Border at Y=48, label at Y=49..54, value at Y=57..62.
            p6 = (uint8_t)(0x01u | (glyph << 1));
            p7 = (uint8_t)(num << 1);
#elif NANO_HUD_HEIGHT == 12
            // Keep Y=48..51; label at Y=52..57, value at Y=58..63.
            p6 = (uint8_t)((p6 & 0x0fu) | (glyph << 4));
            p7 = (uint8_t)((glyph >> 4) | (num << 2));
#endif
            if (x == 0 || x == 39 || x == 88 || x == 127) {
#if NANO_HUD_HEIGHT == 16
                p6 = 0xff;
#else
                p6 |= 0xf0;
#endif
                p7 = 0xff;
            }
        }
    }
}
#endif

void nanoSpritesPaintColumns(uint8_t beginX, uint8_t endX, uint8_t *fb) {
    for (uint8_t x = beginX; x < endX; x++) {
        for (uint8_t i = 0; i < viewCount; i++) paintWorldColumn(x, fb, i);

        paintGunColumn(x, fb);
        paintFlashColumn(x, fb);
#if !NANO_STATUS_HUD
        paintHudColumn(x, fb);
#endif

        if (showHurt) {
            fb[x & (NANO_FB_COLUMNS - 1)] |= 0x80;
            fb[7u * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))] |= 0x80;
            if (x == 0 || x == 127) {
                for (uint8_t p = 0; p < 8; p++) {
                    fb[(uint16_t)p * NANO_FB_COLUMNS + (x & (NANO_FB_COLUMNS - 1))] = 255;
                }
            }
        }
    }
}
