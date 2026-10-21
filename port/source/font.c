/* Text rendering on top of the baked glyph atlas.
 *
 * The atlas is loaded from romfs as a .t3x; every glyph gets a Tex3DS_SubTexture
 * built from the generated table. Remember the two tex3ds conventions (they cost
 * a hardware round-trip once): v is measured from the bottom of the image, and
 * `top < bottom` marks a rotated region.
 */

#include <string.h>

#include "common.h"

typedef struct {
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

C2D_SpriteSheet sheet_load(const char *filename) {
    char path[96];
    snprintf(path, sizeof(path), "romfs:/%s", filename);
    C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path);
    if (sheet) return sheet;
    snprintf(path, sizeof(path), "sdmc:/3ds/sts3ds/%s", filename);
    return C2D_SpriteSheetLoad(path);
}

static void build_subs(Tex3DS_SubTexture *subs, const StsGlyph *glyphs, int count) {
    for (int i = 0; i < count; i++) {
        const StsGlyph *g = &glyphs[i];
        subs[i].width = g->w;
        subs[i].height = g->h;
        subs[i].left = g->x / ATLAS_PX;
        subs[i].right = (g->x + g->w) / ATLAS_PX;
        subs[i].top = 1.0f - g->y / ATLAS_PX;
        subs[i].bottom = 1.0f - (g->y + g->h) / ATLAS_PX;
    }
}

static void load_font(int slot, const char *path, const StsGlyph *glyphs, int count, int line_height,
                      Tex3DS_SubTexture *subs) {
    C2D_SpriteSheet sheet = sheet_load(path);
    s_sheets[slot] = sheet;
    if (!sheet) {
        memset(&s_fonts[slot], 0, sizeof(Font));
        return;
    }
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
    build_subs(subs, glyphs, count);
    s_fonts[slot].tex = img.tex;
    s_fonts[slot].glyphs = glyphs;
    s_fonts[slot].subs = subs;
    s_fonts[slot].count = count;
    s_fonts[slot].line_height = line_height;
}

void font_init(void) {
    load_font(FS_SMALL, "font_12.t3x", sts_font_12, STS_FONT_12_COUNT,
              STS_FONT_12_LINE_HEIGHT, s_subs12);
    load_font(FS_MID, "font_16.t3x", sts_font_16, STS_FONT_16_COUNT,
              STS_FONT_16_LINE_HEIGHT, s_subs16);
    load_font(FS_BIG, "font_20.t3x", sts_font_20, STS_FONT_20_COUNT,
              STS_FONT_20_LINE_HEIGHT, s_subs20);
}

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

float text_width(int size, const char *text) {
    const Font *f = &s_fonts[size];
    float pen = 0.0f;
    while (*text) {
        unsigned int cp = utf8_next(&text);
        int index = glyph_index(f, cp);
        pen += index < 0 ? f->line_height * 0.5f : (float)f->glyphs[index].adv;
    }
    return pen;
}

float text_draw_depth(int size, const char *text, float x, float y, u32 color, float depth) {
    const Font *f = &s_fonts[size];
    if (!f->tex) return 0.0f;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    float pen = x;
    while (*text) {
        unsigned int cp = utf8_next(&text);
        if (cp == '\n') {
            y += f->line_height;
            pen = x;
            continue;
        }
        int index = glyph_index(f, cp);
        if (index < 0) {
            pen += f->line_height * 0.5f;
            continue;
        }
        const StsGlyph *g = &f->glyphs[index];
        if (cp != ' ') {
            C2D_Image img = {f->tex, &f->subs[index]};
            C2D_DrawImageAt(img, pen + g->bx, y + g->by, depth, &tint, 1.0f, 1.0f);
        }
        pen += g->adv;
    }
    return pen - x;
}

float text_draw(int size, const char *text, float x, float y, u32 color) {
    return text_draw_depth(size, text, x, y, color, 0.7f);
}

/* Byte length of one UTF-8 character, for re-slicing wrapped lines. */
static int utf8_len(unsigned char first) {
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) return 2;
    if ((first & 0xF0) == 0xE0) return 3;
    return 4;
}

/* Closing punctuation may not start a line.  The game's own text breaks as
 * "你回头一看，发现..." — letting the ，fall to the next line reads as broken
 * CJK, so an overflowing ，。、 etc. hangs past the right margin instead and
 * the following character starts the new line. */
static int closing_punct(unsigned int cp) {
    switch (cp) {
    case 0x3001: case 0x3002: case 0x3009: case 0x300B: case 0x300D: case 0x300F:
    case 0x3011: case 0x3015: case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E:
    case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF3D: case 0xFF05: case 0x2026:
    case 0x201D: case 0x2019: case 0x00BB: /* ” ’ » */
        return 1;
    case ',': case '.': case ';': case ':': case '!': case '?': case ')': case '%':
        return 1;
    }
    return 0;
}

/* Start of the character that ends just before `pos` (pos > line_start). */
static const char *prev_char(const char *line_start, const char *pos) {
    const char *p = line_start, *last = line_start;
    while (p < pos) {
        last = p;
        p += utf8_len((unsigned char)*p);
    }
    return last;
}

static unsigned int char_at(const char *s) {
    char buf[5] = {0};
    memcpy(buf, s, (size_t)utf8_len((unsigned char)*s));
    const char *scan = buf;
    return utf8_next(&scan);
}

/* Shared body of the wrapped-text routines.  draw == 0 only measures, and with
 * pitch 1.0 the value returned is then the number of lines the text needs. */
static float wrap_text(int size, const char *text, float x, float y, float max_width,
                       int max_lines, float pitch, u32 color, int draw) {
    const Font *f = &s_fonts[size];
    if (!f->tex || max_lines <= 0) return 0.0f;
    if (pitch <= 0.0f) pitch = (float)f->line_height;

    float pen = 0.0f;
    float line_top = y;
    int lines = 1;
    const char *line_start = text;

    while (*text) {
        unsigned char first = (unsigned char)*text;
        int bytes = utf8_len(first);
        char ch[5] = {0};
        memcpy(ch, text, (size_t)bytes);
        const char *scan = ch;
        unsigned int cp = utf8_next(&scan);

        int index = glyph_index(f, cp);
        float adv = index < 0 ? (float)f->line_height * 0.5f : (float)f->glyphs[index].adv;

        if (cp == '\n' || (pen + adv > max_width && pen > 0.0f)) {
            /* Break here: everything up to this character is one line.  A line
             * may not open on closing punctuation, so when that is what would
             * land there, hand the character before it down as well and let the
             * new line start on real text (kinsoku shori). */
            const char *brk = text;
            int moved = 0;
            /* Walk back over the whole run of closing punctuation: a line may
             * not open on it, so the character before the run joins the run on
             * the next line. */
            while (cp != '\n' && brk > line_start && closing_punct(char_at(brk))) {
                const char *back = prev_char(line_start, brk);
                if (back == brk) break;
                brk = back;
                moved = 1;
            }
            if (brk == line_start) { brk = text; moved = 0; }  /* all punctuation: leave it */
            if (draw && brk > line_start) {
                char line[256];
                size_t len = (size_t)(brk - line_start);
                if (len > sizeof(line) - 1) len = sizeof(line) - 1;
                memcpy(line, line_start, len);
                line[len] = 0;
                text_draw(size, line, x, line_top, color);
            }
            if (cp == '\n') {
                text += bytes;
                line_start = text;
            } else {
                line_start = brk;
            }
            line_top += pitch;
            lines++;
            pen = 0.0f;
            if (lines > max_lines) return line_top - y;
            if (moved) text = brk;   /* re-scan the character handed down */
            continue;
        }

        pen += adv;
        text += bytes;
    }

    if (text > line_start) {
        if (draw) {
            char line[256];
            size_t len = (size_t)(text - line_start);
            if (len > sizeof(line) - 1) len = sizeof(line) - 1;
            memcpy(line, line_start, len);
            line[len] = 0;
            text_draw(size, line, x, line_top, color);
        }
        line_top += pitch;
    }
    return line_top - y;
}

/* How many lines `text` wraps to at `max_width` (0 for an empty string). */
int text_wrap_lines(int size, const char *text, float max_width) {
    if (!text || !*text) return 0;
    return (int)(wrap_text(size, text, 0.0f, 0.0f, max_width, 9999, 1.0f, 0, 0) + 0.5f);
}

/* Like text_draw_wrapped, but the caller picks the line pitch. */
float text_draw_wrapped_pitch(int size, const char *text, float x, float y, float max_width,
                              int max_lines, float pitch, u32 color) {
    return wrap_text(size, text, x, y, max_width, max_lines, pitch, color, 1);
}

float text_draw_wrapped(int size, const char *text, float x, float y, float max_width, int max_lines, u32 color) {
    return wrap_text(size, text, x, y, max_width, max_lines, 0.0f, color, 1);
}

void rect_fill(float x, float y, float w, float h, u32 color, float depth) {
    C2D_DrawRectSolid(x, y, depth, w, h, color);
}

void rect_frame(float x, float y, float w, float h, float thickness, u32 color, float depth) {
    C2D_DrawRectSolid(x, y, depth, w, thickness, color);
    C2D_DrawRectSolid(x, y + h - thickness, depth, w, thickness, color);
    C2D_DrawRectSolid(x, y, depth, thickness, h, color);
    C2D_DrawRectSolid(x + w - thickness, y, depth, thickness, h, color);
}
