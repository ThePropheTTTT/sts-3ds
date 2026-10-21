/* Shared types for the 3DS port. */
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "font_zhs.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define ATLAS_PX ((float)STS_FONT_ATLAS_SIZE)

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)

/* Palette used by the screen mockups. */
#define COL_INK RGBA(238, 228, 208, 255)
#define COL_DIM RGBA(168, 160, 142, 255)
#define COL_GOLD RGBA(230, 196, 110, 255)
#define COL_HP RGBA(176, 56, 46, 255)
#define COL_HP_BACK RGBA(70, 22, 20, 255)
#define COL_BLOCK RGBA(120, 160, 200, 255)
#define COL_PANEL RGBA(20, 19, 18, 240)
#define COL_CARD_BG RGBA(30, 26, 23, 255)
#define COL_LIT RGBA(240, 190, 90, 255)

/* Font sizes baked into assets/font (see tools/bake_font.py). */
enum { FS_SMALL = 0, FS_MID = 1, FS_BIG = 2 };

/* Loads a sheet from romfs, falling back to sdmc:/3ds/sts3ds/ for emulator
 * testing (some 3dsx loaders do not mount the embedded romfs). */
C2D_SpriteSheet sheet_load(const char *filename);

void font_init(void);
float text_draw(int size, const char *text, float x, float y, u32 color);
float text_width(int size, const char *text);
/* Draws up to max_lines wrapped lines, returns the height consumed. */
float text_draw_wrapped(int size, const char *text, float x, float y, float max_width, int max_lines, u32 color);
/* Same, with the caller's line pitch (a long body can be squeezed to fit). */
float text_draw_wrapped_pitch(int size, const char *text, float x, float y, float max_width,
                              int max_lines, float pitch, u32 color);
/* Number of lines the text wraps to at max_width. */
int text_wrap_lines(int size, const char *text, float max_width);

void rect_fill(float x, float y, float w, float h, u32 color, float depth);
float text_draw_depth(int size, const char *text, float x, float y, u32 color, float depth);
void rect_frame(float x, float y, float w, float h, float thickness, u32 color, float depth);
