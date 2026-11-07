#pragma once

// Buttons
#define FLASH_CS     2
#define BUTTON_UP    3
#define BUTTON_DOWN  8
#define BUTTON_LEFT  7
#define BUTTON_RIGHT 6
#define BUTTON_A     5
#define BUTTON_B     4

#define NANO_DIRECT_BUTTON_PORTS  1
#define NANO_DIRECT_GPIO_INIT     1


// Display
#define NANO_SSD1309_PAGE_MODE  0 // 0 for SSD1306 -  1 for SSD1309
#define SSD1309_TWBR            ((F_CPU >= 32000000UL) ? 10 : 1) // ~889 kHz I2C clock at 16/32 MHz
#define OLED_TWI_TIMEOUT        60000u
#define NANO_ENABLE_SPRITES         1  // enable sprites 
#define DOOM_INCLUDE_DECORATIONS    0  // 1 = enable decorations
#define NANO_SKILL                  2  // skill: 1 easy, 2 medium, 3 hard.
#define NANO_SHARE_SPRITE_ART       1  // share the Bullet Box art with all shotgun ammo/pickups 
#define NANO_DITHER_FILL            1  // 1: fill patterns to the walls. 0 lines only
#define NANO_DITHER_GRADIENT        1  
#define NANO_CROSSHAIR              1  // Centered in the view above the HUD.
#define NANO_STATUS_HUD             1  // HUD'ish
#define NANO_HUD_HEIGHT             16 // 12 or 16 pixels; side panels only.
#define NANO_VIEW_BOB_PIXELS        1  // 1 = subtle, 2 = more visible.
#define NANO_VIEW_BOB               0  // 1 = subtle walk bob / drunk doom guy, 0 = disabled
#define DOOM_INCLUDE_MONSTERS       1  // enable enemies - For now only imp's from daveruiz doom-nano
#define NANO_IMP_PROJECTILES        1  // imp fireballs
#define DOOM_INCLUDE_DOOR           1  // map texture to doors  
#define NANO_IMP_AI                 1  // bot
#define NANO_VISIBLE_OBJECTS        5 
#define NANO_FB_COLUMNS             64 // screen buffer
#define NANO_STREAM_COLUMNS         16
#define NANO_RENDER_STEP            8  // 2, 4, 8. |  2,and 4 slower but straighter lines
#define NANO_SPAN_FILL              1
#define NANO_MIDFRAME_INPUT         1
#define NANO_FIXED_POINT            1
#define NANO_TRACE_OPTIMIZED        1
#define DISPLAY_FPS                 0

#define NANO_PROFILE                0

#if NANO_STATUS_HUD && NANO_ENABLE_SPRITES
#if NANO_HUD_HEIGHT != 12 && NANO_HUD_HEIGHT != 16
#error "NANO_HUD_HEIGHT must be 12 or 16"
#endif
#endif
