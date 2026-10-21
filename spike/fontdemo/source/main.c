/*
 * Font demo: prove on real hardware that the baked glyph atlas renders, that
 * the 3DS touch numbers behave, and that the aim mapping in docs/03 works.
 *
 * Top screen: sample text at 12/16/20 px, plus a glyph-grid mode (A) that draws
 * every baked glyph so atlas overlaps or holes are obvious. L/R page the grid.
 * Bottom screen: live touch readout + the computed target index for the two
 * enemy slots used by the battle layout.
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "font_zhs.h"

#define ATLAS ((float)STS_FONT_ATLAS_SIZE)

#define SCREEN_W 400.0f
#define SCREEN_H 240.0f
#define BOTTOM_W 320.0f

#define TOUCH_ACTIVATE_PX 8   /* rise needed before aim mode engages */
#define TOUCH_SWITCH_PX   20  /* horizontal step that changes the locked enemy */
#define TOUCH_EDGE_PX     4   /* resistive edge band: release here cancels */

/* Battle layout. */
#define ENEMY_COUNT 2
#define ENEMY_X0 196
#define ENEMY_DX 96
#define ENEMY_Y 104
#define ENEMY_W 68
#define ENEMY_H 64

typedef struct {
    const char *name;
    C3D_Tex *tex;
    const StsGlyph *glyphs;
    Tex3DS_SubTexture *subs;
    int count;
    int line_height;
} Font;

static C2D_SpriteSheet s_sheets[3];
static Font s_fonts[3];
static Tex3DS_SubTexture s_subs12[STS_FONT_12_COUNT];
static Tex3DS_SubTexture s_subs16[STS_FONT_16_COUNT];
static Tex3DS_SubTexture s_subs20[STS_FONT_20_COUNT];

static const u32 COL_INK = 0xF0E8D6FF;
static const u32 COL_DIM = 0xA8A08EFF;
static const u32 COL_GOLD = 0xD8C46EFF;

static unsigned int utf8_next(const char **pp) {
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned int cp;
    int n;
    if (p[0] < 0x80) {
        cp = p[0];
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        n = 3;
    } else {
        cp = p[0] & 0x07;
        n = 4;
    }
    for (int i = 1; i < n; i++) cp = (cp << 6) | (p[i] & 0x3F);
    *pp += n;
    return cp;
}

static int glyph_index(const Font *f, unsigned int cp) {
    int lo = 0;
    int hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return mid;
        if (f->glyphs[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static void font_setup(Font *f, const char *name, C2D_SpriteSheet sheet,
                       const StsGlyph *glyphs, int count, int line_height,
                       Tex3DS_SubTexture *subs) {
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
    f->name = name;
    f->tex = img.tex;
    f->glyphs = glyphs;
    f->subs = subs;
    f->count = count;
    f->line_height = line_height;

    for (int i = 0; i < count; i++) {
        const StsGlyph *g = &glyphs[i];
        subs[i].width = g->w;
        subs[i].height = g->h;
        subs[i].left = g->x / ATLAS;
        subs[i].right = (g->x + g->w) / ATLAS;
        /* tex3ds stores v bottom-up and uses `top < bottom` to flag a rotated
         * region (Tex3DS_SubTextureRotated), so v has to be mirrored here.
         * Getting this wrong makes every glyph sample a rotated neighbour. */
        subs[i].top = 1.0f - g->y / ATLAS;
        subs[i].bottom = 1.0f - (g->y + g->h) / ATLAS;
    }
}

static float draw_text(const Font *f, const char *text, float x, float y, u32 color) {
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    float pen = x;
    while (*text) {
        unsigned int cp = utf8_next(&text);
        int index = glyph_index(f, cp);
        if (index < 0) {
            pen += f->line_height * 0.5f;
            continue;
        }
        const StsGlyph *g = &f->glyphs[index];
        if (cp != ' ') {
            C2D_Image img = {f->tex, &f->subs[index]};
            C2D_DrawImageAt(img, pen + g->bx, y + g->by, 0.5f, &tint, 1.0f, 1.0f);
        }
        pen += g->adv;
    }
    return pen - x;
}

static void draw_rect(float x, float y, float w, float h, u32 color) {
    C2D_DrawRectSolid(x, y, 0.4f, w, h, color);
}

static int nearest_enemy(int mapped_x) {
    int best = 0;
    int best_dist = 1 << 30;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        int cx = ENEMY_X0 + i * ENEMY_DX + ENEMY_W / 2;
        int dist = mapped_x > cx ? mapped_x - cx : cx - mapped_x;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    s_sheets[0] = C2D_SpriteSheetLoad("romfs:/font_12.t3x");
    s_sheets[1] = C2D_SpriteSheetLoad("romfs:/font_16.t3x");
    s_sheets[2] = C2D_SpriteSheetLoad("romfs:/font_20.t3x");
    if (!s_sheets[0] || !s_sheets[1] || !s_sheets[2]) {
        /* Without the romfs there is nothing to show; bail out loudly. */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(80, 0, 0, 255));
        C2D_SceneBegin(top);
        C3D_FrameEnd(0);
        svcSleepThread(3000000000LL);
        return 1;
    }

    font_setup(&s_fonts[0], "12", s_sheets[0], sts_font_12, STS_FONT_12_COUNT,
               STS_FONT_12_LINE_HEIGHT, s_subs12);
    font_setup(&s_fonts[1], "16", s_sheets[1], sts_font_16, STS_FONT_16_COUNT,
               STS_FONT_16_LINE_HEIGHT, s_subs16);
    font_setup(&s_fonts[2], "20", s_sheets[2], sts_font_20, STS_FONT_20_COUNT,
               STS_FONT_20_LINE_HEIGHT, s_subs20);

    const Font *f12 = &s_fonts[0];
    const Font *f16 = &s_fonts[1];
    const Font *f20 = &s_fonts[2];

    const char *sample = "示例文本 0123 排版测试。";
    const char *sample2 = "字形 图集 对齐 检查 一二三四五。";

    int mode = 0;        /* 0 = sample text, 1 = glyph grid */
    int page = 0;
    const int grid_cols = 33;
    const int grid_rows = 11;
    const int grid_per_page = grid_cols * grid_rows;
    const int grid_pages = (STS_FONT_12_COUNT + grid_per_page - 1) / grid_per_page;

    int touch_active = 0;
    int start_x = 0, start_y = 0;
    int lock_target = 0;
    int target = 0;
    int activated = 0;
    int commits = 0;
    touchPosition touch = {0};

    u64 last = osGetTime();
    int frames = 0;
    int fps = 0;
    char line[128];

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;
        if (kDown & KEY_A) mode = !mode;
        if (kDown & KEY_R && mode == 1) page = (page + 1) % grid_pages;
        if (kDown & KEY_L && mode == 1) page = (page + grid_pages - 1) % grid_pages;

        u32 held = hidKeysHeld();
        if (held & KEY_TOUCH) {
            hidTouchRead(&touch);
            if (!touch_active) {
                touch_active = 1;
                start_x = touch.px;
                start_y = touch.py;
                /* Lock the nearest enemy by mapping 320 -> 400 and comparing x. */
                lock_target = nearest_enemy(touch.px * (int)SCREEN_W / (int)BOTTOM_W);
                target = lock_target;
                activated = 0;
            }
            int rise = start_y - touch.py;
            activated = rise >= TOUCH_ACTIVATE_PX;
            if (activated) {
                /* Vertical motion never switches targets; only x steps do. */
                int steps = (touch.px - start_x) / TOUCH_SWITCH_PX;
                int picked = lock_target + steps;
                if (picked < 0) picked = 0;
                if (picked >= ENEMY_COUNT) picked = ENEMY_COUNT - 1;
                target = picked;
            }
        } else {
            if (touch_active && activated) {
                /* Release: commit only inside the screen and off the edge band. */
                int inside = touch.px > TOUCH_EDGE_PX && touch.px < (int)BOTTOM_W - TOUCH_EDGE_PX &&
                             touch.py > TOUCH_EDGE_PX && touch.py < 240 - TOUCH_EDGE_PX;
                if (inside) commits++;
            }
            touch_active = 0;
            activated = 0;
        }

        frames++;
        u64 now = osGetTime();
        if (now - last >= 1000) {
            fps = frames;
            frames = 0;
            last = now;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        /* ---- top screen ---- */
        C2D_TargetClear(top, C2D_Color32(16, 18, 20, 255));
        C2D_SceneBegin(top);

        if (mode == 0) {
            draw_text(f20, "字库实测", 8, 4, COL_GOLD);
            draw_text(f12, sample, 8, 40, COL_INK);
            draw_text(f16, sample, 8, 62, COL_INK);
            draw_text(f20, sample, 8, 90, COL_INK);
            draw_text(f12, sample2, 8, 124, COL_INK);
            draw_text(f16, sample2, 8, 148, COL_INK);
            draw_text(f12, "0123456789 +3 力量 8/80 第 1 层 99", 8, 182, COL_DIM);
            draw_text(f12, "A 网格  L/R 翻页  START 退出", 8, 206, COL_DIM);

            /* Battle slots, so the aim highlight can be eyeballed. */
            for (int i = 0; i < ENEMY_COUNT; i++) {
                float x = (float)(ENEMY_X0 + i * ENEMY_DX);
                int lit = touch_active && activated && target == i;
                draw_rect(x, ENEMY_Y, ENEMY_W, ENEMY_H, lit ? 0x503C1EFF : 0x3C3A32FF);
                if (lit) {
                    draw_rect(x, ENEMY_Y, ENEMY_W, 2, 0xF0BE5AFF);
                    draw_rect(x, ENEMY_Y + ENEMY_H - 2, ENEMY_W, 2, 0xF0BE5AFF);
                    draw_rect(x, ENEMY_Y, 2, ENEMY_H, 0xF0BE5AFF);
                    draw_rect(x + ENEMY_W - 2, ENEMY_Y, 2, ENEMY_H, 0xF0BE5AFF);
                }
            }
        } else {
            snprintf(line, sizeof(line), "grid %d/%d  33x11", page + 1, grid_pages);
            draw_text(f12, line, 8, 4, COL_DIM);
            int start = page * grid_per_page;
            for (int i = 0; i < grid_per_page; i++) {
                int index = start + i;
                if (index >= STS_FONT_12_COUNT) break;
                int col = i % grid_cols;
                int row = i / grid_cols;
                C2D_ImageTint tint;
                C2D_PlainImageTint(&tint, COL_INK, 1.0f);
                C2D_Image img = {f12->tex, &f12->subs[index]};
                C2D_DrawImageAt(img, 4.0f + col * 12.0f, 24.0f + row * 19.0f, 0.5f,
                                &tint, 1.0f, 1.0f);
            }
        }

        /* ---- bottom screen ---- */
        C2D_TargetClear(bottom, C2D_Color32(20, 20, 22, 255));
        C2D_SceneBegin(bottom);
        draw_text(f12, "STS 3DS font demo", 6, 4, COL_GOLD);
        snprintf(line, sizeof(line), "fonts 12/16/20  glyphs %d  fps %d", STS_FONT_12_COUNT, fps);
        draw_text(f12, line, 6, 24, COL_DIM);
        snprintf(line, sizeof(line), "touch %d,%d  rise %d  act %d  tgt %d",
                 touch.px, touch.py, touch_active ? start_y - touch.py : 0, activated, target);
        draw_text(f12, line, 6, 44, COL_INK);
        snprintf(line, sizeof(line), "mode %s  page %d/%d  commits %d", mode ? "grid" : "sample",
                 page + 1, grid_pages, commits);
        draw_text(f12, line, 6, 64, COL_DIM);
        draw_text(f12, "A 切换  L/R 翻页  START 退出", 6, 84, COL_DIM);

        if (touch_active) {
            draw_rect((float)touch.px - 1, (float)touch.py - 8, 3, 17, COL_GOLD);
            draw_rect((float)touch.px - 8, (float)touch.py - 1, 17, 3, COL_GOLD);
        }
        draw_text(f12, "8px 激活 / 20px 换敌 / 4px 边缘", 6, 220, COL_DIM);

        C3D_FrameEnd(0);
    }

    for (int i = 0; i < 3; i++) C2D_SpriteSheetFree(s_sheets[i]);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
