# Nano-Doom-TNG

**DOOM-inspired 3D gaming on an 8-bit microcontroller and a 128 × 64 monochrome OLED.**

Nano-Doom-TNG is an experimental, resource-constrained first-person shooter for Arduino Nano / ATmega328P-class hardware. It is a **further development inspired by [doom-nano by daveruiz](https://github.com/daveruiz/doom-nano)**, reimagined around a more faithful recreation of DOOM's **E1M1** level on an **SSD1306 or SSD1309 I²C OLED**. The renderer is designed around tight RAM and CPU limits.

> **Status:** Active development / beta. This is a custom DOOM-inspired engine, **not a complete port of the original DOOM game**. Performance and available features depend on the board, clock speed, and compile-time configuration.

## From doom-nano to Nano-Doom-TNG

This project owes its starting point to **[daveruiz/doom-nano](https://github.com/daveruiz/doom-nano)**, an impressive 3D raycasting experiment on the original Arduino Nano. We have **borrowed and adapted sprites, along with several ideas and design concepts**, from doom-nano. Many thanks to **daveruiz** for the original project and inspiration!

The central difference is the **level itself**:

- **doom-nano:** a DOOM-themed game built around a **Wolfenstein 3D-style raycasting world**, rather than a recreation of an original DOOM map. Its author explicitly describes the renderer this way.
- **Nano-Doom-TNG:** aims for a **more authentic DOOM E1M1 experience**, using geometry derived from the actual E1M1 level, including its sector-based layout, varied floor/ceiling heights, and doors. It is still a simplified renderer, adapted to a 128 × 64 monochrome OLED and very limited 8-bit hardware.

Nano-Doom-TNG is **not a full DOOM engine port**, and this repository is **not presented as an official continuation of doom-nano**. It is an independent hobby development that builds on its inspiration and reuses some of its visual and gameplay ideas.

## Features

- 128 × 64 monochrome first-person view
- DOOM E1M1-derived level geometry stored in program flash (`PROGMEM`)
- Portal/wall rendering with fixed-point optimization options
- Ordered dithering and optional gradient shading
- Incremental rendering and OLED column streaming to reduce framebuffer RAM
- 32-, 64-, or 128-column framebuffer configurations
- SSD1306 / SSD1309 support through a lightweight direct-TWI driver (no display library required)
- Imp enemies, basic AI, projectiles, collision and shooting
- Pickups, health, armor and ammunition
- Compact status HUD with **Health / Armor / Ammo** and a centered weapon
- Doors, a crosshair, death/restart screen and optional camera bobbing
- Compile-time switches for speed, visual quality and memory use

## Hardware

- Arduino Nano with an ATmega328P-compatible environment, or an appropriately configured LGT8F328P-based board
- 128 × 64 I²C OLED using SSD1306 or SSD1309 (current I²C address in code: `0x3C`)
- Six momentary push buttons
- Suitable power supply and jumper wires

### Wiring

| Function | Arduino Nano pin | Notes |
| --- | --- | --- |
| OLED SDA | A4 | I²C data |
| OLED SCL | A5 | I²C clock |
| Up / Forward | D3 | Button to GND |
| Down / Backward | D8 | Button to GND |
| Left / Turn left | D7 | Button to GND |
| Right / Turn right | D6 | Button to GND |
| A / Fire / Restart | D5 | Button to GND |
| B / Use / Open door | D4 | Button to GND |
| Flash CS (reserved) | D2 | Defined in `config.h`; not needed for the current PROGMEM map |

Buttons use the microcontroller's internal pull-up resistors: **pressed = LOW**. Connect the OLED power pins according to your particular module's voltage requirements.

## Build and upload

The files currently published here form an **Arduino sketch**; a `platformio.ini` has **not yet been included** in this repository.

### Arduino IDE

1. Clone or download this repository.
2. Keep `nano-doom-tng.ino`, the `.cpp` files, and the `.h` files together in the `nano-doom-tng` sketch folder.
3. Open `nano-doom-tng.ino` in Arduino IDE.
4. Select the appropriate board, processor, clock and upload port. An LGT8F328P board requires its compatible board core.
5. Review `config.h`, then **Verify** and **Upload**.

Depending on your board and selected options, the sketch may need feature reductions or compiler optimizations to fit available flash/RAM. A successful build has **not been independently verified for every supported configuration**.

### PlatformIO

PlatformIO is also used during development, but its project configuration is not yet part of this GitHub snapshot. Add the matching `platformio.ini` and PlatformIO directory layout before building this checkout with `pio run`.

## Controls

| Button | Action |
| --- | --- |
| Up | Move forward |
| Down | Move backward |
| Left / Right | Turn |
| A | Fire; restart after death |
| B | Use / open doors |

## Configuration

Most visual and performance settings live in **[`config.h`](config.h)**.

| Setting | Purpose | Current repository value |
| --- | --- | --- |
| `NANO_SSD1309_PAGE_MODE` | `0` = SSD1306, `1` = SSD1309 | `0` |
| `NANO_STATUS_HUD` | Show the status HUD | `1` |
| `NANO_HUD_HEIGHT` | HUD height, 12 or 16 pixels | `16` |
| `NANO_VIEW_BOB` | Optional walking-camera effect | `0` |
| `NANO_CROSSHAIR` | Show aiming reticle | `1` |
| `NANO_DITHER_FILL` | Dithered wall/surface fills | `1` |
| `NANO_DITHER_GRADIENT` | Gradient-style dithering | `1` |
| `NANO_FIXED_POINT` | Enable fixed-point rendering path | `1` |
| `NANO_RENDER_STEP` | Horizontal render sampling step; smaller = more detail and generally more work | `8` |
| `NANO_FB_COLUMNS` | Framebuffer width: 32, 64 or 128 columns | `64` |
| `NANO_STREAM_COLUMNS` | Number of columns sent in a transfer chunk | `16` |
| `NANO_SKILL` | Difficulty: 1 easy, 2 medium, 3 hard | `2` |
| `DISPLAY_FPS` | Optional FPS display | `0` |

The OLED driver uses direct AVR TWI register access, so I²C timing can depend on CPU clock, wiring, pull-ups, and OLED module. The defaults are development settings rather than guaranteed to work on every board.

## Source files

| File | Purpose |
| --- | --- |
| `nano-doom-tng.ino` | Main Arduino sketch, input, rendering loop and effects |
| `config.h` | Pin assignments and compile-time options |
| `engine.cpp` / `engine.h` | World movement, doors, ray/portal rendering |
| `full_map.cpp` / `full_map.h` | E1M1 map access and structures |
| `full_map_progmem.h` | Generated map data stored in flash |
| `nano_sprites.cpp` / `nano_sprites.h` | Enemies, sprites, pickups, weapon and HUD |
| `oled_tng.cpp` / `oled_tng.h` | SSD1306/SSD1309 display driver |
| `nano_font.cpp` / `nano_font.h` | Small bitmap font |
| `sprites.h`, `nano_sprite_data.h` | Bitmap graphics and sprite resources |

## Roadmap / ideas

- Further improve frame rate and reduce CPU time spent rendering
- Refine HUD layout and visuals
- Reduce program-flash and SRAM requirements
- Improve gameplay and enemy behavior
- Publish a reproducible PlatformIO configuration
- Add screenshots, build measurements and tested-board notes

These are development directions, not promises of completed features.

## Credits and rights

**Original inspiration and borrowed material:** [doom-nano by daveruiz](https://github.com/daveruiz/doom-nano). Nano-Doom-TNG borrows/adapts some sprites and ideas from that project, while pursuing a more DOOM-like E1M1 world instead of its Wolfenstein 3D-style map. Thank you, daveruiz, for sharing your work.

**DOOM rights:** DOOM, its E1M1 level design, names and original visual assets belong to their respective rights holders. This project is **not affiliated with or endorsed by id Software or Bethesda**.

Some repository data and graphics are derived from DOOM assets; **their inclusion does not imply that they are free to redistribute or reuse**. Please verify the rights and provenance of any third-party material before redistributing it.

**License:** No project-wide open-source license has been declared yet. Until a license and the third-party asset situation are clarified, do not assume that all repository content is available for unrestricted reuse.

---

Maintained at **[Fart4Freedom/Nano-Doom-TNG](https://github.com/Fart4Freedom/Nano-Doom-TNG)**.
