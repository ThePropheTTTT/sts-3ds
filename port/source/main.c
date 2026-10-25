/* v0.2 run prototype: five linear nodes, card rewards, one campfire.
 *
 * Screens are driven by g_run.screen; the fight screen keeps the v0.1 layout.
 * Key hints live on the top screen so they cannot overlap the hand.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include <3ds/allocator/linear.h>
#include "cards_gen.h"
#include "battle.h"
#include "run.h"
#include "l10n_gen.h"
#include "visuals_gen.h"
#include "combat_gen.h"
#include "ui_gen.h"
#include "map_paper_gen.h"
#include "hud_gen.h"
#include "save.h"
#include "audio.h"
#include "diag.h"
#if defined(CARD_TEST)
#include "cardtest.h"
#endif

#define TOUCH_ACTIVATE_PX 8
#define TOUCH_SWITCH_PX 20
#define TOUCH_EDGE_PX 4

#define ENEMY_X0 196
#define ENEMY_DX 96
#define ENEMY_Y 104
#define ENEMY_W 68
#define ENEMY_H 64

#define REWARD_W 78
#define REWARD_H 110
#define REWARD_GAP 12

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C2D_SpriteSheet s_art_sheets[4];
static C3D_Tex *s_art_tex[4];
static Tex3DS_SubTexture s_art_subs[STS_CARD_COUNT];
static C2D_SpriteSheet s_relic_sheet;
static C2D_SpriteSheet s_act_sheets[3];
static C2D_Image s_act_img[3];
static C2D_SpriteSheet s_mon_sheet;
static C2D_SpriteSheet s_mon_sheet_ref;
static C2D_Image s_mon_img[29];
static Tex3DS_SubTexture s_mon_subs[29];

/* 1024x512 page produced by tools/extract_monsters.py (8x4 cells of 96px).
 * NULL means the bake did not look right yet, so the placeholder box is used. */
static const char *MON_KEYS[29] = {
    "球体守卫", "史莱姆老大", "勇者", "酸液史莱姆",
    "尖刺史莱姆", "颚虫", "地精大块头", "红虱虫",
    "真菌怪", "邪教徒", "寄生兽", "异鸟",
    "天选者", "大书怪", "牧师", "坦克",
    "蓝奴隶贩子", "红奴隶贩子", "奴隶头子",
    "铜制机械人偶", "收藏家", "火炬头",
    "地精首领", "火大地精", "卑鄙地精", "胖地精", "持盾地精", "地精法师",
    "铜球",
};

static int monster_slot(const char *name) {
    for (int i = 0; i < 29; i++) {
        if (MON_KEYS[i] && strcmp(name, MON_KEYS[i]) == 0) return i;
    }
    return -1;
}

/* Up to four enemies share the top screen.  Narrower boxes keep all four
 * inside 400px; the Gremlin Leader fight and summoned enemies carry their own
 * positions from battle.c. */
static float enemy_w(int i) {
    if (i >= 0 && i < g_battle.enemy_count && g_battle.enemies[i].draw_w > 0)
        return (float)g_battle.enemies[i].draw_w;
    if (g_battle.enemy_count >= 4) return 54.0f;
    return g_battle.enemy_count >= 3 ? 64.0f : (float)ENEMY_W;
}
static float enemy_x(int i) {
    if (i >= 0 && i < g_battle.enemy_count && g_battle.enemies[i].draw_x > 0)
        return (float)g_battle.enemies[i].draw_x;
    if (g_battle.enemy_count >= 4) return 120.0f + (float)i * 68.0f;
    if (g_battle.enemy_count >= 3) return 152.0f + (float)i * 84.0f;
    return (float)(ENEMY_X0 + i * ENEMY_DX);
}
static float enemy_side(int i) {
    float w = enemy_w(i);
    return w >= 68.0f ? 74.0f : (w >= 60.0f ? 62.0f : 52.0f);
}
/* relic_icons.t3x is a 4x4 page of RELIC_ICON px icons (tools/extract_relics.py). */
#define RELIC_ICON 64.0f
#define RELIC_SHEET (RELIC_ICON * 4.0f)
static C3D_Tex *s_relic_tex;
static Tex3DS_SubTexture s_relic_subs[16];
static C2D_SpriteSheet s_vis_sheet;
static C3D_Tex *s_vis_tex;
static Tex3DS_SubTexture s_base[STS_VIS_BASE_COUNT];
static Tex3DS_SubTexture s_frame[STS_VIS_FRAME_COUNT];
static Tex3DS_SubTexture s_banner[STS_VIS_BANNER_COUNT];
static Tex3DS_SubTexture s_orb[STS_VIS_ORB_COUNT];
static Tex3DS_SubTexture s_intent[STS_VIS_INTENT_COUNT];
static C2D_SpriteSheet s_combat_sheet;
static C3D_Tex *s_combat_tex;
static Tex3DS_SubTexture s_cb_block;
static Tex3DS_SubTexture s_cb_player;
static Tex3DS_SubTexture s_cb_power[STS_POWER_COUNT];
static C2D_SpriteSheet s_ui_sheet;
static C3D_Tex *s_ui_tex;
static Tex3DS_SubTexture s_ui_icon[STS_MAP_ICON_COUNT];
static Tex3DS_SubTexture s_ui_icon_faded[STS_MAP_ICON_COUNT];
static Tex3DS_SubTexture s_ui_ring;
static Tex3DS_SubTexture s_ui_ring_sel;
static Tex3DS_SubTexture s_ui_dot;
static Tex3DS_SubTexture s_ui_dot_navy;
static Tex3DS_SubTexture s_ui_boss;
static Tex3DS_SubTexture s_ui_paper;
static C2D_SpriteSheet s_map_paper_sheet;
static C3D_Tex *s_map_paper_tex;
static Tex3DS_SubTexture s_map_paper_sub[3];
static C2D_SpriteSheet s_hud_sheet;
static C3D_Tex *s_hud_tex;
static Tex3DS_SubTexture s_hud_energy[6];
static Tex3DS_SubTexture s_hud_energy_dim[5];
static Tex3DS_SubTexture s_hud_end[STS_HUD_END_COUNT];
static Tex3DS_SubTexture s_hud_camp[STS_HUD_CAMP_COUNT];
static Tex3DS_SubTexture s_hud_vfx;
static float s_orb_angle[5];
static float s_hud_dt = 1.0f / 60.0f;
static u64 s_hud_tick;
static float s_camp_scale[2] = {1.0f, 0.9f};
static Tex3DS_SubTexture s_ev_panel;
static Tex3DS_SubTexture s_ev_frame;
static Tex3DS_SubTexture s_ev_ill[STS_EVENT_ILL_COUNT];
static Tex3DS_SubTexture s_ev_btn[2];
static Tex3DS_SubTexture s_shop_rug;
static Tex3DS_SubTexture s_shop_purge;
static Tex3DS_SubTexture s_shop_sold;
static Tex3DS_SubTexture s_shop_tag;
static Tex3DS_SubTexture s_shop_gold;

static int s_focus;
static int s_target_mode;
static int s_target;
static int s_reward_focus;
static int s_campfire_focus;
static int s_press_end;
static int s_debug_layers;   /* SELECT held: outline every card layer */
static int s_card_seq;       /* per-frame draw order of cards: a later card owns a nearer depth slice */
static int s_press_reward;
static int s_press_campfire;
static int s_press_restart;
static int s_press_advance;   /* the act transition waits for a touch release */
static int s_upgrade_focus;
static int s_map_sel;
static int s_shop_sel;
static int s_shop_page;
static int s_purge_focus;
static int s_event_sel;
static int s_detail;
static int s_pick_sel;
#if defined(CARD_TEST)
static int s_boss_demo = 7;   /* CARD_TEST report: B cycles the act 1/2 set pieces */
#endif
static int s_panel_tab;
static int s_panel_scroll;
static int s_potion_sel;
static int s_pending_potion = -1;
static int s_konami_pos;
static int s_last_screen = -1;
static int s_new_game_arm;   /* B twice on the map starts a fresh run */
static int s_swallow_touch;  /* eat the rest of a tap that closed the status panel */
static int s_press_skip;     /* reward screen: "skip" button is being pressed */
static int s_title_has_save;
static int s_title_sel;
static void reset_gestures(void);
static char s_toast[96];
static int s_toast_ttl;
static int s_diag_overlay;

static void play_card(int slot, int target);
static void draw_diag_overlay_top(void);
static void draw_diag_overlay_bottom(void);

static struct {
    int active;
    int card;
    int start_x, start_y;
    int lock;
    int target;
    int activated;
    int rise;
    int x, y;   /* current touch position, so the card can follow the finger */
} s_drag;

/* A short card-lift when a card is played: the card is resolved immediately,
 * but a snapshot flies up into the detail area so the input has a visible
 * response. Purely cosmetic; it never gates the battle state. */
#define PLAY_FX_FRAMES 16
static struct {
    int active;
    int card, up, ttl;
    float x0, y0, w0, h0;
} s_play_fx;

static void hand_metrics(int count, int *w, int *h, int *gap) {
    if (count <= 5) {
        *w = 62; *h = 88; *gap = -2;
    } else if (count <= 8) {
        *w = 56; *h = 79; *gap = -4;
    } else {
        /* A ten-card hand: 48 px is the smallest width at which the cost orb
         * and the top band of the art still read on the 320px screen. */
        *w = 48; *h = 68; *gap = -6;
    }
}

static void hand_origin(int *x0, int *y0, int *w, int *h, int *gap) {
    hand_metrics(g_battle.hand_count, w, h, gap);
    int total = g_battle.hand_count * (*w + *gap) - *gap;
    *x0 = (BOT_W - total) / 2;
    *y0 = BOT_H - *h - 4;
}

/* A hand fan is a fan of cards: the bottom edge of every card sits on the
 * same line around the middle of the screen, while the tops radiate outwards
 * and the outer cards lean away.  Rotating each card about its own centre
 * would make the bottoms splay and the whole group read as a pile, so the
 * card centre is derived from a fixed bottom-centre anchor instead. */
static float hand_spread(void) {
    int n = g_battle.hand_count;
    if (n < 2) return 0.0f;
    if (n <= 5) return 18.0f + 3.0f * (float)(n - 2);
    if (n <= 8) return 30.0f + 2.0f * (float)(n - 6);
    return 33.0f + (float)(n - 9);
}

/* C2D's positive angle turns the other way on a y-down screen, so the left
 * card (which must lean out towards the left) gets the negative angle. */
static float hand_angle(int i) {
    int n = g_battle.hand_count;
    if (n < 2 || i < 0 || i >= n) return 0.0f;
    float spread = hand_spread();
    float deg = spread * 0.5f - spread * (float)i / (float)(n - 1);
    return -deg * 0.01745329252f;
}

static void hand_slot(int i, float *x, float *y, float *w, float *h) {
    int n = g_battle.hand_count;
    int ww, hh, gap_unused;
    hand_metrics(n, &ww, &hh, &gap_unused);
    float spacing = n > 1 ? (float)(BOT_W - ww - 12) / (float)(n - 1) : 0.0f;
    /* More cards -> a tighter step.  The cap is what keeps the outer corners
     * inside the screen once the cards have been leaned outwards. */
    float capf = 0.58f - 0.015f * (float)(n - 5 > 0 ? n - 5 : 0);
    if (n >= 9) capf = 0.46f;
    float cap = (float)ww * capf;
    if (spacing > cap) spacing = cap;
    float a = hand_angle(i);
    /* Bottom-centre anchors lie on one circle, so the bottom edges are all
     * tangent to the same arc and the fan reads as one shape instead of five
     * independently placed cards. */
    float spread = hand_spread() * 0.01745329252f;
    float bottom_y = (float)BOT_H - 8.0f;
    float radius = 400.0f;
    if (n > 1 && spread > 0.0f) {
        radius = spacing * (float)(n - 1) / (2.0f * sinf(spread * 0.5f));
        if (radius < 80.0f) radius = 80.0f;
    }
    float pivot_y = bottom_y + radius;
    float cx = (float)BOT_W * 0.5f + sinf(a) * (radius + (float)hh * 0.5f);
    float cy = pivot_y - cosf(a) * (radius + (float)hh * 0.5f);
    *x = cx - (float)ww * 0.5f;
    *y = cy - (float)hh * 0.5f;
    *w = (float)ww;
    *h = (float)hh;
}

static void start_play_fx(int slot) {
    if (slot < 0 || slot >= g_battle.hand_count) return;
    hand_slot(slot, &s_play_fx.x0, &s_play_fx.y0, &s_play_fx.w0, &s_play_fx.h0);
    s_play_fx.card = battle_hand_card(slot);
    s_play_fx.up = battle_hand_up(slot);
    s_play_fx.ttl = PLAY_FX_FRAMES;
    s_play_fx.active = 1;
}

static void update_play_fx(void) {
    if (!s_play_fx.active) return;
    if (s_play_fx.ttl > 0) s_play_fx.ttl--;
    if (s_play_fx.ttl <= 0) s_play_fx.active = 0;
}

static int hand_hit(int px, int py, int *slot) {
    for (int i = 0; i < g_battle.hand_count; i++) {
        float x, y, w, h;
        hand_slot(i, &x, &y, &w, &h);
        if ((float)px >= x && (float)px < x + w && (float)py >= y - 16.0f && (float)py < (float)BOT_H) {
            *slot = i;
            return 1;
        }
    }
    return 0;
}

static int nearest_enemy(int mapped_x) {
    int best = -1;
    int best_dist = 1 << 30;
    for (int i = 0; i < g_battle.enemy_count; i++) {
        if (g_battle.enemies[i].hp <= 0) continue;
        int cx = (int)(enemy_x(i) + enemy_w(i) * 0.5f);
        int dist = mapped_x > cx ? mapped_x - cx : cx - mapped_x;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best < 0 ? 0 : best;
}

static int in_end_turn(int px, int py) {
    return px >= 226 && px < 314 && py >= 8 && py < 44;
}

static int in_status_button(int px, int py) {
    return px >= 150 && px < 214 && py >= 8 && py < 42;
}

static int reward_rect(int index, int *rx, int *ry) {
    int total = 3 * REWARD_W + 2 * REWARD_GAP;
    int x0 = (BOT_W - total) / 2;
    *rx = x0 + index * (REWARD_W + REWARD_GAP);
    *ry = 66;
    return 1;
}

static int in_skip_reward(int px, int py) {
    return px >= 246 && px < 314 && py >= 3 && py < 23;
}

static int reward_hit(int px, int py) {
    for (int i = 0; i < g_run.reward_count; i++) {
        int rx, ry;
        reward_rect(i, &rx, &ry);
        if (px >= rx && px < rx + REWARD_W && py >= ry && py < ry + REWARD_H) return i;
    }
    return -1;
}

/* CampfireUI puts the two options side by side; the 256px art is drawn at
 * 96px and the hitbox covers the visible card, not the transparent margin. */
static void campfire_rect(int index, int *rx, int *ry) {
    *rx = (index == 0 ? 96 : 224) - 48;
    *ry = 126 - 48;
}

static int campfire_hit(int px, int py) {
    for (int i = 0; i < 2; i++) {
        int rx, ry;
        campfire_rect(i, &rx, &ry);
        if (px >= rx && px < rx + 96 && py >= ry && py < ry + 96) return i;
    }
    return -1;
}

/* The upgrade list shows the actual card faces, ten to a page. */
static void upgrade_card_rect(int slot, int *rx, int *ry) {
    *rx = 9 + (slot % 5) * 62;
    *ry = 24 + (slot / 5) * 84;
}

static int in_detail_tab(int px, int py) {
    for (int i = 0; i < 2; i++) {
        int x = 112 + i * 56;
        if (px >= x && px < x + 52 && py >= 2 && py < 24) return i;
    }
    return -1;
}

static void title_rect(int index, int *rx, int *ry) {
    *rx = 50;
    *ry = index == 0 ? 104 : 148;
}

static int title_hit(int px, int py) {
    int count = s_title_has_save ? 2 : 1;
    for (int i = 0; i < count; i++) {
        int rx, ry;
        title_rect(i, &rx, &ry);
        if (px >= rx && px < rx + 220 && py >= ry && py < ry + 34) return i;
    }
    return -1;
}

static void set_sub_rect(Tex3DS_SubTexture *dst, int x, int y, int w, int h, float page) {
    dst->width = (u16)w;
    dst->height = (u16)h;
    dst->left = x / page;
    dst->right = (x + w) / page;
    dst->top = 1.0f - y / page;
    dst->bottom = 1.0f - (y + h) / page;
}

static void set_sub_page(Tex3DS_SubTexture *dst, const StsCombatRect *r, float page) {
    set_sub_rect(dst, r->x, r->y, r->w, r->h, page);
}

static void set_sub_ui(Tex3DS_SubTexture *dst, const StsUiRect *r) {
    set_sub_rect(dst, r->x, r->y, r->w, r->h, (float)STS_UI_PAGE);
}

/* hud.t3x is a square 1024x1024 page; PICA200 textures need power-of-two
 * dimensions, and the unused rows below the art are simply transparent. */
static void set_sub_hud(Tex3DS_SubTexture *dst, const StsHudRect *r) {
    dst->width = r->w;
    dst->height = r->h;
    dst->left = (float)r->x / (float)STS_HUD_PAGE_W;
    dst->right = (float)(r->x + r->w) / (float)STS_HUD_PAGE_W;
    dst->top = 1.0f - (float)r->y / (float)STS_HUD_PAGE_H;
    dst->bottom = 1.0f - (float)(r->y + r->h) / (float)STS_HUD_PAGE_H;
}

/* map_paper.t3x is not square (512x1024), so x and y need their own page size. */
static void set_sub_paper(Tex3DS_SubTexture *dst, const int r[4]) {
    dst->width = (u16)r[2];
    dst->height = (u16)r[3];
    dst->left = (float)r[0] / (float)STS_MAP_PAPER_PAGE_W;
    dst->right = (float)(r[0] + r[2]) / (float)STS_MAP_PAPER_PAGE_W;
    dst->top = 1.0f - (float)r[1] / (float)STS_MAP_PAPER_PAGE_H;
    dst->bottom = 1.0f - (float)(r[1] + r[3]) / (float)STS_MAP_PAPER_PAGE_H;
}

static void set_sub(Tex3DS_SubTexture *dst, const StsRegion *r) {
    dst->width = r->w;
    dst->height = r->h;
    dst->left = r->x / 1024.0f;
    dst->right = (r->x + r->w) / 1024.0f;
    dst->top = 1.0f - r->y / 1024.0f;
    dst->bottom = 1.0f - (r->y + r->h) / 1024.0f;
}

static void load_art(void) {
    int pages = STS_CARD_ART_PAGES;
    if (pages > 4) pages = 4;
    for (int p = 0; p < pages; p++) {
        char path[64];
        snprintf(path, sizeof(path), "cards_%03d.t3x", p);
        s_art_sheets[p] = sheet_load(path);
        if (s_art_sheets[p]) {
            C2D_Image img = C2D_SpriteSheetGetImage(s_art_sheets[p], 0);
            s_art_tex[p] = img.tex;
        }
    }
    static const char *act_files[3] = {"act1.t3x", "act2.t3x", "act3.t3x"};
    for (int i = 0; i < 3; i++) {
        s_act_sheets[i] = sheet_load(act_files[i]);
        if (s_act_sheets[i]) s_act_img[i] = C2D_SpriteSheetGetImage(s_act_sheets[i], 0);
    }

    s_mon_sheet = sheet_load("monsters.t3x");
    s_mon_sheet_ref = s_mon_sheet;
    if (s_mon_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_mon_sheet, 0);
        s_vis_tex = s_vis_tex ? s_vis_tex : img.tex;
        for (int i = 0; i < 29; i++) {
            C2D_Image one = {img.tex, NULL};
            Tex3DS_SubTexture *sub = &s_mon_subs[i];
            float x = (float)((i % 8) * 96);
            float y = (float)((i / 8) * 96);
            sub->width = 96;
            sub->height = 96;
            sub->left = x / 1024.0f;
            sub->right = (x + 96.0f) / 1024.0f;
            sub->top = 1.0f - y / 512.0f;
            sub->bottom = 1.0f - (y + 96.0f) / 512.0f;
            s_mon_img[i].tex = img.tex;
            s_mon_img[i].subtex = sub;
            (void)one;
        }
    }

    s_relic_sheet = sheet_load("relic_icons.t3x");
    if (s_relic_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_relic_sheet, 0);
        s_relic_tex = img.tex;
        for (int i = 0; i < 16; i++) {
            float x = (float)((i % 4) * RELIC_ICON);
            float y = (float)((i / 4) * RELIC_ICON);
            s_relic_subs[i].width = RELIC_ICON;
            s_relic_subs[i].height = RELIC_ICON;
            s_relic_subs[i].left = x / RELIC_SHEET;
            s_relic_subs[i].right = (x + RELIC_ICON) / RELIC_SHEET;
            s_relic_subs[i].top = 1.0f - y / RELIC_SHEET;
            s_relic_subs[i].bottom = 1.0f - (y + RELIC_ICON) / RELIC_SHEET;
        }
    }

    s_vis_sheet = sheet_load("visuals.t3x");
    if (s_vis_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_vis_sheet, 0);
        s_vis_tex = img.tex;
        for (int i = 0; i < STS_VIS_BASE_COUNT; i++) set_sub(&s_base[i], &sts_vis_base[i]);
        for (int i = 0; i < STS_VIS_FRAME_COUNT; i++) set_sub(&s_frame[i], &sts_vis_frame[i]);
        for (int i = 0; i < STS_VIS_BANNER_COUNT; i++) {
            const StsBanner *b = &sts_vis_banner_place[i];
            Tex3DS_SubTexture *dst = &s_banner[i];
            dst->width = b->w;
            dst->height = b->h;
            dst->left = b->px / 1024.0f;
            dst->right = (b->px + b->w) / 1024.0f;
            dst->top = 1.0f - b->py / 1024.0f;
            dst->bottom = 1.0f - (b->py + b->h) / 1024.0f;
        }
        for (int i = 0; i < STS_VIS_ORB_COUNT; i++) {
            const StsOrb *o = &sts_vis_orb[i];
            Tex3DS_SubTexture *dst = &s_orb[i];
            dst->width = o->w;
            dst->height = o->h;
            dst->left = o->px / 1024.0f;
            dst->right = (o->px + o->w) / 1024.0f;
            dst->top = 1.0f - o->py / 1024.0f;
            dst->bottom = 1.0f - (o->py + o->h) / 1024.0f;
        }
        for (int i = 0; i < STS_VIS_INTENT_COUNT; i++) set_sub(&s_intent[i], &sts_vis_intent[i]);
    }

    s_combat_sheet = sheet_load("combat.t3x");
    if (s_combat_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_combat_sheet, 0);
        s_combat_tex = img.tex;
        float page = (float)STS_COMBAT_PAGE;
        set_sub_page(&s_cb_block, &sts_combat_block, page);
        set_sub_page(&s_cb_player, &sts_combat_player, page);
        for (int i = 0; i < STS_POWER_COUNT; i++) {
            set_sub_page(&s_cb_power[i], &sts_combat_power[i], page);
        }
    }

    s_ui_sheet = sheet_load("ui.t3x");
    if (s_ui_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_ui_sheet, 0);
        s_ui_tex = img.tex;
        for (int i = 0; i < STS_MAP_ICON_COUNT; i++) {
            set_sub_ui(&s_ui_icon[i], &sts_ui_icon[i]);
            set_sub_ui(&s_ui_icon_faded[i], &sts_ui_icon_faded[i]);
        }
        set_sub_ui(&s_ui_ring, &sts_ui_ring);
        set_sub_ui(&s_ui_ring_sel, &sts_ui_ring_sel);
        set_sub_ui(&s_ui_dot, &sts_ui_dot);
        set_sub_ui(&s_ui_dot_navy, &sts_ui_dot_navy);
        set_sub_ui(&s_ui_boss, &sts_ui_boss);
        set_sub_ui(&s_ui_paper, &sts_ui_paper);
        set_sub_ui(&s_ev_panel, &sts_ui_event_panel);
        set_sub_ui(&s_ev_frame, &sts_ui_event_frame);
        for (int i = 0; i < STS_EVENT_ILL_COUNT; i++) set_sub_ui(&s_ev_ill[i], &sts_ui_event_ill[i]);
        set_sub_ui(&s_ev_btn[0], &sts_ui_event_btn[0]);
        set_sub_ui(&s_ev_btn[1], &sts_ui_event_btn[1]);
        set_sub_ui(&s_shop_rug, &sts_ui_shop_rug);
        set_sub_ui(&s_shop_purge, &sts_ui_shop_purge);
        set_sub_ui(&s_shop_sold, &sts_ui_shop_sold);
        set_sub_ui(&s_shop_tag, &sts_ui_shop_tag);
        set_sub_ui(&s_shop_gold, &sts_ui_shop_gold);
    }

    s_map_paper_sheet = sheet_load("map_paper.t3x");
    if (s_map_paper_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_map_paper_sheet, 0);
        s_map_paper_tex = img.tex;
        set_sub_paper(&s_map_paper_sub[0], sts_map_paper_top);
        set_sub_paper(&s_map_paper_sub[1], sts_map_paper_mid);
        set_sub_paper(&s_map_paper_sub[2], sts_map_paper_bot);
    }

    s_hud_sheet = sheet_load("hud.t3x");
    if (s_hud_sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(s_hud_sheet, 0);
        s_hud_tex = img.tex;
        for (int i = 0; i < 6; i++) set_sub_hud(&s_hud_energy[i], &sts_hud_energy[i]);
        for (int i = 0; i < 5; i++) set_sub_hud(&s_hud_energy_dim[i], &sts_hud_energy_dim[i]);
        for (int i = 0; i < STS_HUD_END_COUNT; i++) set_sub_hud(&s_hud_end[i], &sts_hud_end[i]);
        for (int i = 0; i < STS_HUD_CAMP_COUNT; i++) set_sub_hud(&s_hud_camp[i], &sts_hud_camp[i]);
        set_sub_hud(&s_hud_vfx, &sts_hud_vfx);
    }

    for (int i = 0; i < STS_CARD_COUNT; i++) {
        const struct CardDef *c = &sts_cards[i];
        s_art_subs[i].width = c->art_w;
        s_art_subs[i].height = c->art_h;
        s_art_subs[i].left = c->art_x / 1024.0f;
        s_art_subs[i].right = (c->art_x + c->art_w) / 1024.0f;
        s_art_subs[i].top = 1.0f - c->art_y / 1024.0f;
        s_art_subs[i].bottom = 1.0f - (c->art_y + c->art_h) / 1024.0f;
    }
}

static C2D_Image hud_image(Tex3DS_SubTexture *sub) {
    C2D_Image img = {s_hud_tex, sub};
    return img;
}

/* EnergyOrbRed/EnergyPanel: five rotating layers, a fixed rim, and the same
 * five layers in their dimmed empty-orb variant.  C2D has no text scaling, so
 * the original font pop is left out; the swirl is the animation that matters. */
static void update_hud_anim(void) {
    u64 now = svcGetSystemTick();
    float dt = s_hud_dt;
    if (s_hud_tick) {
        dt = (float)(now - s_hud_tick) / (float)SYSCLOCK_ARM11;
        if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
    }
    s_hud_dt = dt;
    s_hud_tick = now;

    static const float lit_speed[5] = {360.0f, 40.0f, -40.0f, 20.0f, -20.0f};
    static const float dim_speed[5] = {72.0f, 8.0f, -8.0f, 5.0f, -5.0f};
    int empty = g_battle.energy <= 0;
    for (int i = 0; i < 5; i++) {
        s_orb_angle[i] += (empty ? dim_speed[i] : lit_speed[i]) * dt;
        if (s_orb_angle[i] > 6.2831853f) s_orb_angle[i] -= 6.2831853f;
        if (s_orb_angle[i] < -6.2831853f) s_orb_angle[i] += 6.2831853f;
    }

    /* AbstractCampfireOption: hover grows 0.9 -> 1.0 and the other button
     * shrinks back.  Ease instead of snapping so the keyboard cursor reads. */
    float k = dt * 14.0f;
    if (k > 1.0f) k = 1.0f;
    for (int i = 0; i < 2; i++) {
        float target = i == s_campfire_focus ? 1.0f : 0.9f;
        s_camp_scale[i] += (target - s_camp_scale[i]) * k;
    }
}

static void draw_energy_orb(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", g_battle.energy, g_battle.max_energy);

    if (s_hud_tex) {
        const float size = 54.0f;
        const float scale = size / 128.0f;
        int empty = g_battle.energy <= 0;
        for (int i = 0; i < 5; i++) {
            C2D_Image layer = hud_image(empty ? &s_hud_energy_dim[i] : &s_hud_energy[i]);
            C2D_DrawImageAtRotated(layer, 28.0f, 30.0f, 0.30f + (float)i * 0.001f,
                                   s_orb_angle[i], NULL, scale, scale);
        }
        C2D_Image rim = hud_image(&s_hud_energy[5]);
        C2D_DrawImageAtRotated(rim, 28.0f, 30.0f, 0.306f, 0.0f, NULL, scale, scale);
    } else {
        C2D_DrawCircleSolid(28, 30, 0.3f, 23.0f, RGB(26, 22, 18));
        C2D_DrawCircleSolid(28, 30, 0.31f, 21.0f, RGB(60, 50, 26));
    }
    float tw = text_width(FS_MID, buf);
    float tx = 28.0f - tw * 0.5f;
    float ty = 21.0f;
    /* BitmapFont's border: a one-pixel dark rim keeps the white digits
     * readable where they cross the bright core. */
    text_draw_depth(FS_MID, buf, tx - 1.0f, ty, RGB(28, 14, 6), 0.319f);
    text_draw_depth(FS_MID, buf, tx + 1.0f, ty, RGB(28, 14, 6), 0.319f);
    text_draw_depth(FS_MID, buf, tx, ty - 1.0f, RGB(28, 14, 6), 0.319f);
    text_draw_depth(FS_MID, buf, tx, ty + 1.0f, RGB(28, 14, 6), 0.319f);
    text_draw_depth(FS_MID, buf, tx, ty, RGB(255, 255, 219), 0.32f);
}

/* EndTurnButton: the 256px hexagon is scaled over the old 88x36 hitbox.  The
 * hover plate is only press feedback for now (the 3DS has no pointer hover);
 * a press drops both by two pixels, matching tmpY. */
static void draw_end_turn_button(void) {
    char buf[32];
    int player_turn = g_battle.phase == PHASE_PLAYER;
    snprintf(buf, sizeof(buf), "%s", player_turn ? "结束回合" : "敌方回合");
    float cx = 270.0f, cy = 26.0f;
    float yoff = s_press_end ? 2.0f : 0.0f;
    if (s_hud_tex) {
        const float scale = 88.0f / 256.0f;
        if (s_press_end) {
            C2D_Image hover = hud_image(&s_hud_end[STS_HUD_END_HOVER]);
            C2D_DrawImageAt(hover, cx - 44.0f, cy + yoff - 44.0f, 0.30f, NULL, scale, scale);
        }
        C2D_Image base = hud_image(&s_hud_end[STS_HUD_END_TURN]);
        C2D_DrawImageAt(base, cx - 44.0f, cy + yoff - 44.0f, 0.31f, NULL, scale, scale);
    } else {
        rect_fill(226, 8 + yoff, 88, 36, s_press_end ? RGB(120, 62, 52) : RGB(74, 40, 36), 0.3f);
        rect_frame(226, 8 + yoff, 88, 36, 2.0f, s_press_end ? COL_LIT : RGB(170, 96, 84), 0.31f);
    }
    float tw = text_width(FS_SMALL, buf);
    text_draw_depth(FS_SMALL, buf, cx - tw * 0.5f, 19.0f + yoff,
                    player_turn ? COL_INK : COL_DIM, 0.32f);
}

static const char *type_label(int type) {
    switch (type) {
    case 0: return "攻击";
    case 1: return "技能";
    case 2: return "能力";
    case 3: return "状态";
    default: return "诅咒";
    }
}

static u32 type_color(int type) {
    switch (type) {
    case 0: return RGB(0x8B, 0x2F, 0x2F);
    case 1: return RGB(0x2F, 0x5D, 0x8B);
    case 2: return RGB(0x4A, 0x7A, 0x3A);
    case 3: return RGB(0x4A, 0x4A, 0x4A);
    default: return RGB(0x4A, 0x2F, 0x5D);
    }
}

static void draw_background(void) {
    if (!s_vis_tex) return;
    int act = g_run.act < 1 ? 1 : (g_run.act > 3 ? 3 : g_run.act);
    C2D_Image bg = s_act_img[act - 1];
    if (bg.tex) C2D_DrawImageAt(bg, 0.0f, 0.0f, 0.05f, NULL, 1.0f, 1.0f);
}

/* Depth slice reserved for one card. Cards have to cover the panels they are
 * drawn over (the hand detail panel and the buttons bottom out at 0.33, the
 * enemy rows at 0.46) while a card drawn later still covers every layer of the
 * card drawn before it, so each card gets the whole range instead of a fixed
 * depth per layer. */
#define CARD_Z_BASE 0.47f
/* The energy badge sits at +0.0205 inside its card. The next card's base must
 * clear that, or the badge of card N paints over card N+1 (seen in the hand
 * whenever two cards overlap). */
#define CARD_Z_STEP 0.024f
#define CARD_Z_MAX  12

/* CardGroup.refreshHandLayout rotates every hand card around its centre.  The
 * base card is the anchor: each layer keeps its own local centre, gets rotated
 * around the card centre, and is then drawn rotated around that new centre. */
static void rot_point(float cx, float cy, float dx, float dy, float angle,
                      float *ox, float *oy) {
    float c = cosf(angle), s = sinf(angle);
    *ox = cx + dx * c - dy * s;
    *oy = cy + dx * s + dy * c;
}

static void draw_card_layer(C2D_Image img, float ix, float iy, float iw, float ih,
                            float card_cx, float card_cy, float angle, float z) {
    float sx = iw / (float)img.subtex->width;
    float sy = ih / (float)img.subtex->height;
    if (angle == 0.0f) {
        C2D_DrawImageAt(img, ix, iy, z, NULL, sx, sy);
        return;
    }
    float cx = ix + iw * 0.5f;
    float cy = iy + ih * 0.5f;
    rot_point(card_cx, card_cy, cx - card_cx, cy - card_cy, angle, &cx, &cy);
    C2D_DrawImageAtRotated(img, cx, cy, z, angle, NULL, sx, sy);
}

static void rot_rect_frame(float cx, float cy, float w, float h, float angle,
                           float thick, u32 clr, float z) {
    float hw = w * 0.5f, hh = h * 0.5f;
    float px[4], py[4];
    rot_point(cx, cy, -hw, -hh, angle, &px[0], &py[0]);
    rot_point(cx, cy, hw, -hh, angle, &px[1], &py[1]);
    rot_point(cx, cy, hw, hh, angle, &px[2], &py[2]);
    rot_point(cx, cy, -hw, hh, angle, &px[3], &py[3]);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        C2D_DrawLine(px[i], py[i], clr, px[j], py[j], clr, thick, z);
    }
}

/* energy < 0 means "not in the player's hand": the game only reddens the cost
 * of a hand card the player cannot afford (AbstractCard.renderEnergy). */
static void draw_card_at(int card_index, int up, int level, float x, float y, float w, float h,
                         int focused, float zc, int energy, float angle) {
    const struct CardDef *c = &sts_cards[card_index];
    u32 accent = type_color(c->type);
    int ti = c->type == 0 ? 0 : (c->type == 2 ? 2 : 1);
    /* AbstractCard.renderAttackPortrait / renderBannerImage: BASIC, COMMON,
     * SPECIAL and CURSE all use the common frame and the common ribbon,
     * UNCOMMON the uncommon ones, RARE the rare ones.  extract_visuals.py
     * bakes exactly those three variants per type. */
    int ri3 = c->rarity == 2 ? 1 : (c->rarity == 3 ? 2 : 0);
    int black = c->color >= 5;             /* curses and statuses: black skill art */
    if (black) { ti = 1; ri3 = 0; }
    int base = black ? 6 : (c->color == 4 ? 3 : 0) + ti;
    int frame = ti * 3 + ri3;
    const StsBanner *ribbon = &sts_vis_banner_place[ri3];
    float ux = w / (float)STS_CARD_SRC_W;  /* card pixels per 1024-atlas pixel */
    float uy = h / (float)STS_CARD_SRC_H;
    /* AbstractCard.java:1509 draws the 250x190 portrait at (25,43) of a 300x420
     * card; the atlas crop is trimmed inside that canvas, hence art_f*. */
    float win_x = x + w * (25.0f / 300.0f);
    float win_y = y + h * (43.0f / 420.0f);
    float win_w = w * (250.0f / 300.0f);
    float win_h = h * (190.0f / 420.0f);
    float ribbon_x = x + (float)(ribbon->x - STS_CARD_SRC_X) * ux;
    float ribbon_y = y + (float)(ribbon->y - STS_CARD_SRC_Y) * uy;
    float ribbon_w = (float)ribbon->w * ux;
    float ribbon_h = (float)ribbon->h * uy;
    float card_cx = x + w * 0.5f;
    float card_cy = y + h * 0.5f;

    if (s_vis_tex) {
        /* Every card sprite is a crop of the same rectangle, measured on the
         * background layer alone, so drawing it at the card box puts the
         * visible edge exactly on the box: no getbbox() slack, and neither the
         * frame nor the ribbon overhang can shrink the face. */
        C2D_Image img = {s_vis_tex, &s_base[base]};
        draw_card_layer(img, x, y, w, h, card_cx, card_cy, angle, zc);
    } else if (angle == 0.0f) {
        rect_fill(x, y, w, h, COL_CARD_BG, zc);
        rect_frame(x, y, w, h, 2.0f, accent, zc + 0.008f);
    } else {
        rot_rect_frame(card_cx, card_cy, w, h, angle, 2.0f, accent, zc + 0.008f);
    }

    if (c->art_w && s_art_tex[c->page]) {
        float ax = win_x + win_w * (c->art_fx / 250.0f);
        float ay = win_y + win_h * (c->art_fy / 190.0f);
        float aw = win_w * (c->art_fw / 250.0f);
        float ah = win_h * (c->art_fh / 190.0f);
        C2D_Image img = {s_art_tex[c->page], &s_art_subs[card_index]};
        draw_card_layer(img, ax, ay, aw, ah, card_cx, card_cy, angle, zc + 0.004f);
    }

    if (s_vis_tex) {
        /* The frame and the ribbon sit on top of the portrait, in the game's
         * order (renderPortraitFrame then renderBannerImage), so they cannot
         * be baked into the face sprite. */
        C2D_Image img = {s_vis_tex, &s_frame[frame]};
        draw_card_layer(img, x, y, w, h, card_cx, card_cy, angle, zc + 0.008f);
        img.subtex = &s_banner[ri3];
        draw_card_layer(img, ribbon_x, ribbon_y, ribbon_w, ribbon_h,
                        card_cx, card_cy, angle, zc + 0.012f);
    }

    /* The highlight is a ring around the card, never a band inside it: a frame
     * drawn inside the box makes the visible face look a ring smaller than it
     * is, which is exactly what the card box is compared against. */
    if (focused) {
        if (angle == 0.0f) {
            rect_frame(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 2.0f, COL_LIT, zc + 0.016f);
        } else {
            rot_rect_frame(card_cx, card_cy, w + 4.0f, h + 4.0f, angle, 2.0f,
                           COL_LIT, zc + 0.016f);
        }
    }

    if (s_debug_layers && angle == 0.0f) {
        rect_frame(x, y, w, h, 1.0f, RGB(0, 255, 90), 0.90f);                     /* card box */
        rect_frame(win_x, win_y, win_w, win_h, 1.0f, RGB(255, 0, 255), 0.90f);    /* portrait */
        rect_frame(ribbon_x, ribbon_y, ribbon_w, ribbon_h,
                   1.0f, RGB(0, 200, 255), 0.90f);                                /* ribbon */
    }

    /* AbstractCard.renderEnergy (:1924) draws the badge last, on top of the card:
     * the orb for the CARD'S COLOUR (never the rarity), then the cost number.
     * Unplayable cards (cost <= -2) get no badge, and curses/statuses only show
     * a number when they are Slimed or Pride. */
    int card_cost = battle_cost_of(c, up);
    int show_cost = card_cost > -2;
    int show_number = show_cost
        && !(c->type == 3 && strcmp(c->id, "Slimed") != 0)
        && !(c->color == 5 && strcmp(c->id, "Pride") != 0);
    if (show_cost) {
        if (s_vis_tex) {
            int oi = c->color <= 3 ? (int)c->color : 4;   /* >= 4: colourless art */
            const StsOrb *o = &sts_vis_orb[oi];
            float ox = x + (float)(o->x - STS_CARD_SRC_X) * ux;
            float oy = y + (float)(o->y - STS_CARD_SRC_Y) * uy;
            C2D_Image img = {s_vis_tex, &s_orb[oi]};
            draw_card_layer(img, ox, oy, (float)o->w * ux, (float)o->h * uy,
                            card_cx, card_cy, angle, zc + 0.018f);
            if (s_debug_layers && angle == 0.0f) {
                rect_frame(ox, oy, (float)o->w * ux, (float)o->h * uy,
                           1.0f, RGB(255, 255, 0), 0.90f);                     /* orb */
            }
        } else {
            float dcx = x + 9.5f, dcy = y + 9.5f;
            if (angle != 0.0f) {
                rot_point(card_cx, card_cy, dcx - card_cx, dcy - card_cy, angle,
                          &dcx, &dcy);
            }
            C2D_DrawCircleSolid(dcx, dcy, zc + 0.018f, 8.0f, RGB(20, 16, 14));
            C2D_DrawCircleSolid(dcx, dcy, zc + 0.019f, 7.0f, accent);
        }
        if (show_number) {
            char cost[8];
            if (card_cost == -1) snprintf(cost, sizeof(cost), "X");
            else snprintf(cost, sizeof(cost), "%d", card_cost);
            int fs = h >= 120.0f ? FS_MID : FS_SMALL;
            float lh = fs == FS_MID ? (float)STS_FONT_16_LINE_HEIGHT : (float)STS_FONT_12_LINE_HEIGHT;
            u32 ink = COL_INK;
            if (energy >= 0 && card_cost > energy) ink = RGB(255, 77, 77);  /* (1.0, 0.3, 0.3) */
            float ccx = x + (float)(STS_COST_TXT_X - STS_CARD_SRC_X) * ux;
            float ccy = y + (float)(STS_COST_TXT_Y - STS_CARD_SRC_Y) * uy;
            float tw = text_width(fs, cost);
            float cx = ccx - tw / 2.0f;
            float cy = ccy - lh * 0.5f;
            if (angle != 0.0f) {
                float tcx = cx + tw * 0.5f, tcy = cy + lh * 0.5f;
                rot_point(card_cx, card_cy, tcx - card_cx, tcy - card_cy, angle,
                          &tcx, &tcy);
                cx = tcx - tw * 0.5f;
                cy = tcy - lh * 0.5f;
            }
            /* The game's cardEnergyFont_L is 38pt with a 4px dark grey border and a
             * shadow (FontHelper.java:283-285), which is what makes the number read
             * against the orb's bright core. Our atlas font has no outline, so lay a
             * dark copy around the ink - 1px each way at these card sizes. */
            for (int oy2 = -1; oy2 <= 1; oy2++) {
                for (int ox2 = -1; ox2 <= 1; ox2++) {
                    if (!ox2 && !oy2) continue;
                    text_draw_depth(fs, cost, cx + (float)ox2, cy + (float)oy2,
                                    RGB(38, 32, 28), zc + 0.020f);
                }
            }
            text_draw_depth(fs, cost, cx, cy, ink, zc + 0.0205f);
        }
    }

    /* Only the large detail cards keep the ribbon name and the type label: at
     * hand and shop sizes they were unreadable clutter on top of the artwork
     * (user call, 2026-10-21). */
    if (w >= 100.0f && angle == 0.0f) {
        /* AbstractCard.renderTitle draws the name 175*drawScale above the card
         * centre, i.e. 35 of 420 px below the top edge; that is the ribbon's upper
         * third, not its box centre (the banner region carries transparent trim).
         * The ribbon is silver, so the ink has to be dark. */
        char name[40];
        if (level > 1) snprintf(name, sizeof(name), "%s+%d", c->name, level);
        else if (level > 0) snprintf(name, sizeof(name), "%s+", c->name);
        else snprintf(name, sizeof(name), "%s", c->name);
        float name_w = text_width(FS_SMALL, name);
        const float name_cy = y + h * (35.0f / 420.0f);   /* 175/420 above centre */
        text_draw_depth(FS_SMALL, name, x + (w - name_w) / 2.0f,
                        name_cy - 9.5f,   /* glyph ink centre sits ~9.5px below the pen y */
                        RGB(30, 26, 22), zc + 0.014f);
        float label_w = text_width(FS_SMALL, type_label(c->type));
        text_draw_depth(FS_SMALL, type_label(c->type), x + (w - label_w) / 2.0f,
                        y + h * 0.545f, COL_DIM, zc + 0.010f);
    }
}

static void draw_card(int card_index, int up, int level, float x, float y, float w, float h, int focused) {
    int seq = s_card_seq < CARD_Z_MAX ? s_card_seq : CARD_Z_MAX;
    s_card_seq++;
    draw_card_at(card_index, up, level, x, y, w, h, focused,
                 CARD_Z_BASE + (float)seq * CARD_Z_STEP, -1, 0.0f);
}

/* Hand cards are the only ones where the game reddens an unaffordable cost. */
static void draw_card_in_hand(int card_index, int up, int level, float x, float y, float w, float h,
                              int focused, float angle) {
    int seq = s_card_seq < CARD_Z_MAX ? s_card_seq : CARD_Z_MAX;
    s_card_seq++;
    draw_card_at(card_index, up, level, x, y, w, h, focused,
                 CARD_Z_BASE + (float)seq * CARD_Z_STEP, g_battle.energy, angle);
}

static void draw_top_bar(void) {
    rect_fill(0, 0, TOP_W, 20, RGBA(14, 14, 14, 220), 0.3f);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d/%d", g_battle.hp, g_battle.max_hp);
    text_draw(FS_SMALL, buf, 6, 1, RGB(232, 120, 110));
    snprintf(buf, sizeof(buf), "第%d幕 %d层", g_run.act < 1 ? 1 : g_run.act,
             g_map.row < 0 ? 1 : g_map.row + 1);
    text_draw(FS_SMALL, buf, 62, 1, COL_DIM);
    snprintf(buf, sizeof(buf), "%d", g_map.gold);
    text_draw(FS_SMALL, buf, 150, 1, COL_GOLD);
    if (g_cheat_triple) text_draw(FS_SMALL, "伤害×3", 196, 1, RGB(255, 120, 200));
    for (int i = 0; i < g_run.relic_count && i < 7; i++) {
        float x = TOP_W - 24 - i * 22;
        rect_fill(x, 2, 18, 18, RGBA(26, 22, 16, 220), 0.34f);
        rect_frame(x, 2, 18, 18, 1.0f, RGBA(150, 128, 80, 255), 0.35f);
        int id = g_run.relics[i];
        if (s_relic_tex && id < 16) {
            C2D_Image icon = {s_relic_tex, &s_relic_subs[id]};
            C2D_DrawImageAt(icon, x + 1.0f, 3.0f, 0.36f, NULL,
                                16.0f / RELIC_ICON, 16.0f / RELIC_ICON);
        }
    }
}

static void draw_entity_box(float x, float w, int hp, int max_hp, int block, int lit, u32 fill) {
    rect_fill(x, ENEMY_Y, w, ENEMY_H, fill, 0.4f);
    rect_frame(x, ENEMY_Y, w, ENEMY_H, lit ? 3.0f : 1.0f, lit ? COL_LIT : RGBA(100, 96, 84, 255), 0.45f);
    rect_fill(x, ENEMY_Y + ENEMY_H + 2, w, 6, COL_HP_BACK, 0.45f);
    float frac = max_hp > 0 ? (float)hp / (float)max_hp : 0.0f;
    rect_fill(x + 1, ENEMY_Y + ENEMY_H + 3, (w - 2) * frac, 4, COL_HP, 0.46f);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", hp, max_hp);
    text_draw(FS_SMALL, buf, x, ENEMY_Y + ENEMY_H + 9, lit ? RGB(250, 220, 150) : COL_INK);
    if (block > 0) {
        /* AbstractCreature.renderBlockIconAndValue (:869): the 64px shield is
         * centred 14px left of and 14px below the hitbox's bottom-left corner,
         * and the value is centred on (x + BLOCK_ICON_X, y - 16).
         * BLOCK_ICON_X/Y = -14 (AbstractCreature.java:108-109). */
        float cx = x - 14.0f;
        float cy = ENEMY_Y + ENEMY_H - 14.0f;
        if (s_combat_tex) {
            C2D_Image img = {s_combat_tex, &s_cb_block};
            C2D_DrawImageAt(img, cx - 11.0f, cy - 11.0f, 0.47f, NULL, 22.0f / 64.0f, 22.0f / 64.0f);
        } else {
            C2D_DrawCircleSolid(cx, cy, 0.47f, 9.0f, COL_BLOCK);
        }
        snprintf(buf, sizeof(buf), "%d", block);
        float bw = text_width(FS_SMALL, buf);
        float bx = cx - bw * 0.5f;
        float by = (ENEMY_Y + ENEMY_H - 16.0f) - (float)STS_FONT_12_LINE_HEIGHT * 0.5f;
        /* blockInfoFont has a 2.7px dark green border; one dark copy ring stands
         * in for it (our atlas font has no outline). */
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
                text_draw_depth(FS_SMALL, buf, bx + (float)ox, by + (float)oy, RGB(10, 30, 20), 0.471f);
            }
        }
        text_draw_depth(FS_SMALL, buf, bx, by, RGB(230, 230, 230), 0.472f);
    }
}

static u32 node_color(int type) {
    switch (type) {
    case NODE_MONSTER: return RGB(150, 60, 55);
    case NODE_ELITE: return RGB(190, 110, 40);
    case NODE_REST: return RGB(70, 120, 70);
    case NODE_SHOP: return RGB(190, 160, 60);
    case NODE_EVENT: return RGB(110, 80, 160);
    case NODE_TREASURE: return RGB(70, 120, 180);
    case NODE_BOSS: return RGB(210, 70, 60);
    default: return RGB(60, 60, 60);
    }
}

static const char *node_name(int type) {
    switch (type) {
    case NODE_MONSTER: return "敌人";
    case NODE_ELITE: return "精英";
    case NODE_REST: return "篝火";
    case NODE_SHOP: return "商店";
    case NODE_EVENT: return "事件";
    case NODE_TREASURE: return "宝箱";
    case NODE_BOSS: return "首领";
    default: return "";
    }
}

/* Nodes the player may enter next, left to right. */
static int map_candidates(int *cols, int max) {
    int count = 0;
    for (int col = 0; col < MAP_COLS && count < max; col++) {
        if (map_can_move(g_map.row + 1, col)) cols[count++] = col;
    }
    return count;
}

/* Map screen, after DungeonMap/MapRoomNode: the parchment (mapTop/mapMid/
 * mapBot, 1281 of the game's 1920x1080 design px wide) under one 128x128 icon
 * per room, columns 128px apart and rows 150px apart.  Both screens use ONE
 * 320px-wide map at the same scale: the top screen centres it (40px of dimmed
 * scene on either side) instead of zooming it to 400px and the bottom screen
 * crops the middle out.  MAP_ZOOM maps the shared 1443px source crop (parchment
 * plus 18px of torn-edge scene on either side) onto those 320px. */
#define MAP_ZOOM ((float)BOT_W / (float)STS_MAP_PAPER_CROP_W)
#define MAP_ROW_DY (150.0f * MAP_ZOOM)
#define MAP_ICON_SZ (128.0f * MAP_ZOOM)
#define MAP_BOSS_SZ (512.0f * MAP_ZOOM * 0.6f)
#define MAP_PAPER_LEFT ((float)STS_MAP_PAPER_PAPER_LEFT)
#define MAP_PAPER_W ((float)STS_MAP_PAPER_PAPER_W)
#define MAP_PAPER_H ((float)(sts_map_paper_top[3] + sts_map_paper_mid[3] + \
                             sts_map_paper_bot[3]))

/* Both screens are one tall scroll: the top screen shows the upper half of the
 * viewport and the bottom screen the lower half, at the same scale.  The current
 * row sits at MAP_DUAL_ANCHOR_Y in the combined 320x480 viewport until the
 * player pans away. */
#define MAP_DUAL_ANCHOR_Y 380.0f
#define MAP_VIEW_H ((float)TOP_H + (float)BOT_H)
#define MAP_TOP_XOFF ((float)(TOP_W - BOT_W) * 0.5f)

/* Fifteen rows do not fit on 240px, so the map is drawn in content
 * coordinates and the two screens are a window onto them, the way
 * DungeonMapScreen pans its offsetY.  Row 0 sits at the bottom and the boss
 * above row 15.  The extra bottom margin keeps the current node's ring above
 * the bottom-screen footer. */
#define MAP_BOSS_GAP (1.9f * MAP_ROW_DY)      /* room row 15 -> boss, content px */
#define MAP_TOP_MARGIN (MAP_BOSS_GAP + MAP_BOSS_SZ * 0.5f + 24.0f)
                                          /* 24px clears the map's top HUD bar */
/* Keep the current node's ring above the bottom footer, and end the content
 * where the parchment ends (729px at this scale). */
#define MAP_BOTTOM_MARGIN 110.0f
#define MAP_PAN_SPEED 0.05f                   /* content px per frame at tilt */
#define MAP_PAN_STEP 32.0f                    /* content px per D-pad press */

static float s_map_view;                      /* content y at the top screen's top edge */
static int s_map_last_row = -1;
static int s_map_last_act = -1;

static float map_content_y(int row) {
    return MAP_TOP_MARGIN + (float)(MAP_ROWS - 1 - row) * MAP_ROW_DY;
}

static float map_content_h(void) {
    return MAP_TOP_MARGIN + (float)(MAP_ROWS - 1) * MAP_ROW_DY + MAP_BOTTOM_MARGIN;
}

static float map_boss_y(void) {
    return map_content_y(MAP_ROWS - 1) - MAP_BOSS_GAP;
}

static float map_clamp_view(float view) {
    float max = map_content_h() - MAP_VIEW_H;
    if (max < 0.0f) max = 0.0f;
    if (view < 0.0f) return 0.0f;
    if (view > max) return max;
    return view;
}

/* The row being stood on stays at MAP_DUAL_ANCHOR_Y until the player pans
 * away; a new room (or the next act) brings it back here. */
static float map_auto_view(void) {
    int cur = g_map.row < 0 ? 0 : g_map.row;
    return map_clamp_view(map_content_y(cur) - MAP_DUAL_ANCHOR_Y);
}

/* Circle pad pans the map; up on the stick shows the rows still ahead.  The
 * D-pad also pans while held (its KEY_UP/KEY_DOWN press is handled in
 * handle_buttons, this is the repeat). */
static void update_map_scroll(void) {
    if (g_run.screen != SCREEN_MAP) return;
    int cur = g_map.row < 0 ? 0 : g_map.row;
    if (cur != s_map_last_row || g_run.act != s_map_last_act) {
        s_map_last_row = cur;
        s_map_last_act = g_run.act;
        s_map_view = map_auto_view();
    }
    circlePosition pad;
    hidCircleRead(&pad);
    if (pad.dy > 30 || pad.dy < -30) {
        s_map_view = map_clamp_view(s_map_view - (float)pad.dy * MAP_PAN_SPEED);
    }
    u32 held = hidKeysHeld();
    if (held & KEY_UP) s_map_view = map_clamp_view(s_map_view - 5.0f);
    if (held & KEY_DOWN) s_map_view = map_clamp_view(s_map_view + 5.0f);
}

/* MapRoomNode: centre = col*SPACING_X + OFFSET_X, with the parchment's left
 * edge (design x 320) landing on screen x 0. */
static float map_node_x(int col) {
    return MAP_PAPER_LEFT + (240.0f + (float)col * 128.0f) * MAP_ZOOM;
}

/* MapEdge: a MapDot every 17px, one icon radius away from either room. */
static void map_path_dots(float x1, float y1, float x2, float y2, Tex3DS_SubTexture *dot) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    float step = 17.0f * MAP_ZOOM;
    int n = (int)(len / step);
    if (n < 2) return;
    C2D_Image img = {s_ui_tex, dot};
    for (int i = 1; i < n; i++) {
        float t = (float)i / (float)n;
        C2D_DrawImageAt(img, x1 + dx * t - 2.0f, y1 + dy * t - 2.0f, 0.16f, NULL,
                        0.25f, 0.25f);
    }
}

/* One half of the tall map.  is_bottom selects the lower 240px of the viewport
 * and the centre crop of the 400px map. */
static void draw_map_screen(int is_bottom) {
    int cand[4];
    int cand_count = map_candidates(cand, 4);
    float xoff = is_bottom ? 0.0f : MAP_TOP_XOFF;
    float yoff = is_bottom ? (float)TOP_H : 0.0f;
    float screen_w = is_bottom ? (float)BOT_W : (float)TOP_W;
    float screen_h = is_bottom ? (float)BOT_H : (float)TOP_H;
    float boss_y = map_boss_y() - s_map_view - yoff;

    /* The blank margin around the parchment is the dimmed previous scene, the
     * way DungeonMapScreen keeps its background behind the map. */
    draw_background();
    rect_fill(0, 0, screen_w, screen_h, RGBA(8, 8, 10, 150), 0.06f);

    if (s_map_paper_tex) {
        /* Three natural-aspect slices stacked, never stretched.  Their source
         * crop is shared, so the parchment and its torn side edges line up
         * across the hinge. */
        float py = -s_map_view - yoff;
        C2D_Image paper = {s_map_paper_tex, &s_map_paper_sub[0]};
        C2D_DrawImageAt(paper, xoff, py, 0.12f, NULL, 1.0f, 1.0f);
        paper.subtex = &s_map_paper_sub[1];
        C2D_DrawImageAt(paper, xoff, py + (float)sts_map_paper_top[3], 0.12f, NULL,
                        1.0f, 1.0f);
        paper.subtex = &s_map_paper_sub[2];
        C2D_DrawImageAt(paper, xoff,
                        py + (float)(sts_map_paper_top[3] + sts_map_paper_mid[3]),
                        0.12f, NULL, 1.0f, 1.0f);
    } else {
        rect_fill(xoff, 0, (float)BOT_W, screen_h, RGB(48, 45, 39), 0.10f);
    }

    if (!s_ui_tex) {
        /* No ui.t3x: keep a legible schematic rather than an empty screen. */
        for (int row = 0; row < MAP_ROWS; row++) {
            float y = map_content_y(row) - s_map_view - yoff;
            if (y < -12.0f || y > screen_h + 12.0f) continue;
            for (int col = 0; col < MAP_COLS; col++) {
                int type = map_node(row, col);
                if (type == NODE_NONE) continue;
                C2D_DrawCircleSolid(map_node_x(col) + xoff, y, 0.30f, 4.0f,
                                    node_color(type));
            }
        }
        return;
    }

    /* Edges: the ones already walked, plus the ways out of the current room,
     * are AVAILABLE_COLOR; everything else stays NOT_TAKEN_COLOR.  The room
     * graph decides which edges exist -- a one-column step is not a path by
     * itself any more. */
    for (int row = 0; row + 1 < MAP_ROWS; row++) {
        float y1 = map_content_y(row) - s_map_view - yoff;
        float y2 = map_content_y(row + 1) - s_map_view - yoff;
        if (y1 < -64.0f || y2 > screen_h + 64.0f) continue;
        for (int col = 0; col < MAP_COLS; col++) {
            int dst[MAP_COLS];
            int n = map_edges(row, col, dst, MAP_COLS);
            for (int i = 0; i < n; i++) {
                int col2 = dst[i];
                int walked = g_map.visited[row][col] && g_map.visited[row + 1][col2];
                int leaving = row == g_map.row && col == g_map.col;
                map_path_dots(map_node_x(col) + xoff, y1, map_node_x(col2) + xoff, y2,
                              (walked || leaving) ? &s_ui_dot_navy : &s_ui_dot);
            }
        }
    }

    /* Rooms. */
    for (int row = 0; row < MAP_ROWS; row++) {
        float y = map_content_y(row) - s_map_view - yoff;
        if (y < -80.0f || y > screen_h + 80.0f) continue;
        for (int col = 0; col < MAP_COLS; col++) {
            int type = map_node(row, col);
            if (type == NODE_NONE || type == NODE_BOSS) continue;
            float x = map_node_x(col) + xoff;
            int walked = g_map.visited[row][col] != 0;
            int here = row == g_map.row && col == g_map.col;
            int reachable = row == g_map.row + 1 && map_can_move(row, col);
            int lit = walked || here || reachable;
            int idx = type - 1;
            if (idx < 0 || idx >= STS_MAP_ICON_COUNT) continue;

            C2D_Image icon = {s_ui_tex, lit ? &s_ui_icon[idx] : &s_ui_icon_faded[idx]};
            C2D_DrawImageAt(icon, x - MAP_ICON_SZ * 0.5f, y - MAP_ICON_SZ * 0.5f, 0.20f,
                            NULL, MAP_ICON_SZ / 128.0f, MAP_ICON_SZ / 128.0f);

            /* MAP_CIRCLE_5 marks the rooms already taken and the current one. */
            if (walked || here) {
                float ring = 192.0f * MAP_ZOOM * 1.15f;
                C2D_Image img = {s_ui_tex, &s_ui_ring};
                C2D_DrawImageAt(img, x - ring * 0.5f, y - ring * 0.5f, 0.22f, NULL,
                                ring / 192.0f, ring / 192.0f);
            }
        }
    }

    /* Act 1 boss (slime.png), above the last row of rooms.  It belongs to the
     * map from the start, like DungeonMap's boss node, so scrolling up early
     * simply reveals it sooner. */
    {
        float bx = MAP_PAPER_LEFT + MAP_PAPER_W * 0.5f + xoff;  /* parchment centre */
        float half = MAP_BOSS_SZ * 0.5f;
        float last_y = map_content_y(MAP_ROWS - 1) - s_map_view - yoff;
        for (int col = 0; col < MAP_COLS; col++) {
            if (map_node(MAP_ROWS - 1, col) == NODE_NONE) continue;
            map_path_dots(map_node_x(col) + xoff, last_y, bx, boss_y + half * 0.45f,
                          &s_ui_dot_navy);
        }
        C2D_Image boss = {s_ui_tex, &s_ui_boss};
        C2D_DrawImageAt(boss, bx - half, boss_y - half, 0.21f, NULL,
                        MAP_BOSS_SZ / 256.0f, MAP_BOSS_SZ / 256.0f);
    }

    /* Keyboard selection ring: the game uses a mouse hover instead. */
    if (cand_count > 0) {
        int col = cand[s_map_sel % cand_count];
        float ring = 192.0f * MAP_ZOOM * 1.15f;
        int next = g_map.row + 1;
        float ry = next >= MAP_ROWS ? boss_y : map_content_y(next) - s_map_view - yoff;
        C2D_Image img = {s_ui_tex, &s_ui_ring_sel};
        C2D_DrawImageAt(img, map_node_x(col) + xoff - ring * 0.5f, ry - ring * 0.5f,
                        0.24f, NULL, ring / 192.0f, ring / 192.0f);
    }

    if (!is_bottom) {
        draw_top_bar();
        return;
    }

    /* The bottom screen's own footer: a compact strip over the lower map.  The
     * current node stays above it because MAP_BOTTOM_MARGIN lifts the view. */
    char buf[96];
    rect_fill(0, 202, (float)BOT_W, 38, RGBA(10, 10, 12, 205), 0.25f);
    if (cand_count > 0) {
        int col = cand[s_map_sel % cand_count];
        int type = map_node(g_map.row + 1, col);
        snprintf(buf, sizeof(buf), "前往：%s", node_name(type));
        text_draw_depth(FS_SMALL, buf, 8, 205, node_color(type), 0.26f);
        text_draw_depth(FS_SMALL, "←→选择 A进入",
                        8 + text_width(FS_SMALL, buf) + 14, 205, COL_DIM, 0.26f);
    } else {
        text_draw_depth(FS_SMALL, "没有可前往的节点", 8, 205, COL_DIM, 0.26f);
    }
    const char *pan = "↑↓滚动";
    text_draw_depth(FS_SMALL, pan, (float)BOT_W - 8.0f - text_width(FS_SMALL, pan),
                    205, COL_DIM, 0.26f);

    if (g_map.message[0]) {
        text_draw_depth(FS_SMALL, g_map.message, 8, 222, RGB(228, 218, 198), 0.26f);
    } else {
        snprintf(buf, sizeof(buf), "牌组 %d 张", g_run.deck_count);
        text_draw_depth(FS_SMALL, buf, 8, 222, COL_DIM, 0.26f);
    }
    const char *quit = "START 退出";
    text_draw_depth(FS_SMALL, quit, (float)BOT_W - 8.0f - text_width(FS_SMALL, quit),
                    222, COL_DIM, 0.26f);
}

static int shop_item_card(int index) {
    if (index < 5) return g_map.shop_colored[index];
    if (index < 7) return g_map.shop_colorless[index - 5];
    return -1;
}

static int shop_item_price(int index) {
    if (index < 7) return g_map.shop_price[index];
    if (index < 10) return g_map.shop_relic_price[index - 7];
    if (index < 13) return g_map.shop_potion_price[index - 10];
    return g_map.purge_cost;
}

static int shop_item_sold(int index) {
    if (index < 7) return g_map.shop_sold[index];
    if (index < 10) return g_map.shop_relic_sold[index - 7];
    if (index < 13) return g_map.shop_potion_sold[index - 10];
    return g_map.purge_done;
}

static void draw_potion_icon_sized(float x, float y, int id, int lit, float size) {
    float scale = size / 30.0f;
    u32 color = RGB(120, 200, 160);
    if (id == POTION_HEAL) color = RGB(220, 90, 100);
    else if (id == POTION_FIRE) color = RGB(235, 130, 60);
    else if (id == POTION_BLOCK) color = RGB(110, 160, 220);
    else if (id == POTION_DRAW) color = RGB(200, 190, 90);
    else if (id == POTION_ENERGY) color = RGB(240, 210, 110);
    else if (id == POTION_STR) color = RGB(220, 110, 160);
    /* Bottle silhouette: cork, neck, round body, a highlight.  The port invents
     * its own potions, so this stays a shape standing in for the game's layered
     * potion art. */
    rect_fill(x + 9.5f * scale, y + 8 * scale, 11 * scale, 14 * scale, RGB(58, 52, 42), 0.40f);
    rect_fill(x + 8 * scale, y + 1 * scale, 14 * scale, 8 * scale, RGB(58, 52, 42), 0.40f);
    rect_fill(x + 7 * scale, y, 16 * scale, 4 * scale, RGB(146, 116, 74), 0.41f);
    C2D_DrawCircleSolid(x + 15 * scale, y + 21 * scale, 0.41f, 11.0f * scale, RGB(34, 30, 24));
    C2D_DrawCircleSolid(x + 15 * scale, y + 21 * scale, 0.42f, 9.0f * scale, color);
    C2D_DrawCircleSolid(x + 11.5f * scale, y + 17.5f * scale, 0.43f, 2.6f * scale,
                        RGB(255, 246, 226));
    if (lit) rect_frame(x, y - 2, 30 * scale, 34 * scale, 2.0f, COL_LIT, 0.45f);
}

/* Shop screen: ShopScreen.render (:1110-:1127) stretches the rug over the whole
 * screen and then draws the cards (setStartingCardPositions :415-:435), the
 * relics (StoreRelic.update :46), the potions (StorePotion.update :43) and the
 * card-removal service (updatePurgeCard :913-:951).  The port draws that scene on
 * the top screen with the game's own pixel geometry (400/1920, the same trick as
 * the map) and keeps the selected item's details on the screen below.
 * Known deviations: no merchant (his body is a Spine skeleton, not a static
 * image) and no potion art yet (the port's potions are its own six kinds, not
 * the game's).
 *
 * Selection is flat: 0..4 colored cards, 5..6 colorless, 7..9 relics,
 * 10..12 potions, 13 the card-removal service. */
#define SHOP_ITEM_COUNT 14
#define SHOP_S (TOP_W / 1920.0f)
#define SHOP_ROW_X0 (412.2f * SHOP_S)                /* DRAW_START_X + IMG_WIDTH_S/2 */
#define SHOP_COL_DX (283.0f * SHOP_S)                /* padX */
#define SHOP_COLORED_Y (TOP_H - 760.0f * SHOP_S)     /* TOP_ROW_Y */
#define SHOP_COLORLESS_Y (TOP_H - 337.0f * SHOP_S)   /* BOTTOM_ROW_Y */
#define SHOP_CARD_W (225.0f * SHOP_S)                /* IMG_WIDTH_S * drawScale */
#define SHOP_CARD_H (315.0f * SHOP_S)                /* IMG_HEIGHT_S * drawScale */
#define SHOP_RELIC_X0 (1000.0f * SHOP_S)             /* StoreRelic 150 per slot */
#define SHOP_RELIC_DX (150.0f * SHOP_S)
#define SHOP_RELIC_Y (TOP_H - 400.0f * SHOP_S)
#define SHOP_RELIC_SZ 27.0f                          /* 128 * SHOP_S */
#define SHOP_POTION_Y (TOP_H - 200.0f * SHOP_S)
#define SHOP_POTION_SZ 16.0f                         /* 64 * 1.2 * SHOP_S */
#define SHOP_PURGE_X (1553.0f * SHOP_S)              /* purgeCardX */
#define SHOP_PURGE_Y (TOP_H - 337.5f * SHOP_S)       /* purgeCardY + art centre */
#define SHOP_GOLD_W 13.0f
#define SHOP_GOLD_H 11.0f

static void shop_slot(int index, float *cx, float *cy, float *w, float *h) {
    *w = SHOP_CARD_W;
    *h = SHOP_CARD_H;
    if (index < 5) {
        *cx = SHOP_ROW_X0 + (float)index * SHOP_COL_DX;
        *cy = SHOP_COLORED_Y;
    } else if (index < 7) {
        *cx = SHOP_ROW_X0 + (float)(index - 5) * SHOP_COL_DX;
        *cy = SHOP_COLORLESS_Y;
    } else if (index < 10) {
        *cx = SHOP_RELIC_X0 + (float)(index - 7) * SHOP_RELIC_DX;
        *cy = SHOP_RELIC_Y;
        *w = *h = SHOP_RELIC_SZ;
    } else if (index < 13) {
        *cx = SHOP_RELIC_X0 + (float)(index - 10) * SHOP_RELIC_DX;
        *cy = SHOP_POTION_Y;
        *w = *h = SHOP_POTION_SZ;
    } else {
        *cx = SHOP_PURGE_X;
        *cy = SHOP_PURGE_Y;
    }
}

/* GOLD_IMG_OFFSET is (-50,-215) under a card (ShopScreen.java:1149) and
 * (-56,-100) under a relic or potion (StoreRelic.java:30, StorePotion.java:27).
 * Both are bottom-left corners of the 64 px gold box in the game's y-up space,
 * so the coin's bottom edge lands |dy| below the item centre and its top edge
 * is one baked coin above that. */
static float shop_price_y(int index, float cy) {
    return cy + ((index >= 7 && index < 13) ? 100.0f : 215.0f) * SHOP_S - SHOP_GOLD_H;
}

static void shop_price(float cx, float y, int amount) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", amount);
    float tw = text_width(FS_SMALL, buf);
    float gx = cx - (SHOP_GOLD_W + 1.0f + tw) * 0.5f;
    if (s_ui_tex) {
        C2D_Image img = {s_ui_tex, &s_shop_gold};
        C2D_DrawImageAt(img, gx, y, 0.42f, NULL,
                        SHOP_GOLD_W / (float)sts_ui_shop_gold.w,
                        SHOP_GOLD_H / (float)sts_ui_shop_gold.h);
    }
    text_draw(FS_SMALL, buf, gx + SHOP_GOLD_W + 1.0f, y - 1.0f,
              g_map.gold >= amount ? COL_GOLD : RGB(180, 90, 80));
}

/* The detail screen's price: the same coin at 20x17 in front of a 16 px font. */
static float shop_price_big(float x, float y, int amount) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", amount);
    if (s_ui_tex) {
        C2D_Image img = {s_ui_tex, &s_shop_gold};
        C2D_DrawImageAt(img, x, y, 0.42f, NULL, 20.0f / (float)sts_ui_shop_gold.w,
                        17.0f / (float)sts_ui_shop_gold.h);
    }
    text_draw(FS_MID, buf, x + 23.0f, y + 1.0f,
              g_map.gold >= amount ? COL_GOLD : RGB(180, 90, 80));
    return x + 23.0f + text_width(FS_MID, buf);
}

static void draw_shop_sold(float cx, float cy, float w, float h) {
    if (!s_ui_tex) return;
    C2D_Image img = {s_ui_tex, &s_shop_sold};
    C2D_DrawImageAt(img, cx - w * 0.5f, cy - h * 0.5f, 0.42f, NULL,
                    w / (float)sts_ui_shop_sold.w, h / (float)sts_ui_shop_sold.h);
}

static void draw_shop_top(void) {
    if (s_ui_tex) {
        C2D_Image rug = {s_ui_tex, &s_shop_rug};
        C2D_DrawImageAt(rug, 0, 0, 0.12f, NULL, 1.0f, 1.0f);
    } else {
        rect_fill(0, 0, TOP_W, TOP_H, RGB(52, 62, 38), 0.12f);
    }
    draw_top_bar();

    for (int i = 0; i < SHOP_ITEM_COUNT; i++) {
        float cx, cy, w, h;
        shop_slot(i, &cx, &cy, &w, &h);
        int sold = shop_item_sold(i);
        if (i < 7) {
            int card = shop_item_card(i);
            if (card < 0) continue;
            if (sold) {
                draw_shop_sold(cx, cy, w, h);
            } else {
                draw_card(card, 0, 0, cx - w * 0.5f, cy - h * 0.5f, w, h, 0);
            }
        } else if (i < 10) {
            int id = g_map.shop_relic[i - 7];
            if (id < 0) continue;
            if (s_relic_tex && id < 16) {
                C2D_Image icon = {s_relic_tex, &s_relic_subs[id]};
                C2D_DrawImageAt(icon, cx - w * 0.5f, cy - h * 0.5f, 0.42f, NULL,
                                w / RELIC_ICON, h / RELIC_ICON);
            }
        } else if (i < 13) {
            int id = g_map.shop_potion[i - 10];
            draw_potion_icon_sized(cx - 8.0f, cy - 9.0f, id, 0, SHOP_POTION_SZ);
        } else if (s_ui_tex) {
            C2D_Image img = {s_ui_tex, sold ? &s_shop_sold : &s_shop_purge};
            C2D_DrawImageAt(img, cx - w * 0.5f, cy - h * 0.5f, 0.42f, NULL,
                            w / (float)sts_ui_shop_purge.w,
                            h / (float)sts_ui_shop_purge.h);
        }

        if (sold) {
            if (i >= 7) {
                float tw = text_width(FS_SMALL, "已售");
                text_draw(FS_SMALL, "已售", cx - tw * 0.5f, cy + h * 0.5f - 4.0f, COL_DIM);
            }
        } else {
            shop_price(cx, shop_price_y(i, cy), shop_item_price(i));
        }
    }

    /* OnSaleTag.java:16/76 draws the 128 px tag at (card_x + 30, card_y + 70)
     * from the card's centre, unrotated -- the tilt is baked into the art. */
    int sale = g_map.shop_sale;
    if (s_ui_tex && sale >= 0 && sale < 5 && !shop_item_sold(sale)) {
        float cx, cy, w, h;
        shop_slot(sale, &cx, &cy, &w, &h);
        C2D_Image tag = {s_ui_tex, &s_shop_tag};
        C2D_DrawImageAt(tag, cx + 30.0f * SHOP_S, cy - (70.0f + 96.0f) * SHOP_S, 0.44f,
                        NULL, 1.0f, 1.0f);
    }

    /* The game marks the hovered item; the port frames the cursor. */
    float cx, cy, w, h;
    shop_slot(s_shop_sel, &cx, &cy, &w, &h);
    rect_frame(cx - w * 0.5f - 2.0f, cy - h * 0.5f - 2.0f, w + 4.0f, h + 4.0f, 2.0f,
               COL_LIT, 0.5f);
}

static void draw_purge_top(void) {
    int card = battle_deck_card(s_purge_focus);
    if (card >= 0) {
        draw_card(card, battle_deck_up(s_purge_focus), battle_deck_level(s_purge_focus),
                  16.0f, 10.0f, 128.0f, 200.0f, 1);
        text_draw_wrapped(FS_SMALL, battle_deck_up(s_purge_focus) ? sts_cards[card].text_up : sts_cards[card].text,
                          164, 20, 220, 7, RGB(228, 218, 198));
    }
    text_draw(FS_SMALL, g_map.purge_from_event ? "牧师 · 净化" : "商店 · 删牌", 170, 2, COL_DIM);
}

/* Row/column address of the fourteen shop items, in the same order as the flat
 * 0..13 index the run state uses: row 0 = the five tinted cards, row 1 = the two
 * colourless cards, three relics, three potions and the purge service. */
static int shop_row(int sel) { return sel < 5 ? 0 : 1; }
static int shop_col(int sel) { return sel < 5 ? sel : sel - 5; }
static int shop_index(int row, int col) {
    if (row == 0) return col < 0 ? 0 : (col > 4 ? 4 : col);
    return 5 + (col < 0 ? 0 : (col > 8 ? 8 : col));
}

static void draw_shop(void) {
    int sel = s_shop_sel;
    int sold = shop_item_sold(sel);

    text_draw(FS_SMALL, "商店", 6, 2, COL_GOLD);
    text_draw(FS_SMALL, "←→↑↓ 选择  A 买  B 离开", 150, 2, COL_DIM);

    if (sel < 7) {
        int card = shop_item_card(sel);
        if (card < 0) return;
        draw_card(card, 0, 0, 6.0f, 26.0f, 96.0f, 150.0f, 1);
        text_draw(FS_MID, sts_cards[card].name, 112, 24, COL_INK);
        if (sold) {
            text_draw(FS_MID, "已售出", 112, 52, COL_DIM);
        } else {
            float px = shop_price_big(112.0f, 50.0f, shop_item_price(sel));
            if (sel == g_map.shop_sale) {
                text_draw(FS_SMALL, "半价", px + 8.0f, 56, RGB(250, 160, 200));
            }
        }
        text_draw_wrapped(FS_SMALL, sts_cards[card].text, 112, 78, 200, 8,
                          RGB(228, 218, 198));
        return;
    }

    if (sel < 10) {
        int id = g_map.shop_relic[sel - 7];
        if (id < 0) return;
        if (s_relic_tex && id < 16) {
            C2D_Image icon = {s_relic_tex, &s_relic_subs[id]};
            C2D_DrawImageAt(icon, 16.0f, 30.0f, 0.42f, NULL,
                            72.0f / RELIC_ICON, 72.0f / RELIC_ICON);
        }
        text_draw(FS_BIG, battle_relic_name(id), 100, 30, COL_GOLD);
        if (sold) text_draw(FS_MID, "已售出", 100, 62, COL_DIM);
        else shop_price_big(100.0f, 62.0f, shop_item_price(sel));
        text_draw_wrapped(FS_SMALL, battle_relic_desc(id), 16, 116, 288, 6,
                          RGB(228, 218, 198));
        return;
    }

    if (sel < 13) {
        int id = g_map.shop_potion[sel - 10];
        draw_potion_icon_sized(20.0f, 34.0f, id, 0, 48.0f);
        text_draw(FS_BIG, battle_potion_name(id), 88, 40, COL_GOLD);
        if (sold) text_draw(FS_MID, "已售出", 88, 72, COL_DIM);
        else shop_price_big(88.0f, 72.0f, shop_item_price(sel));
        text_draw_wrapped(FS_SMALL, battle_potion_desc(id), 16, 124, 288, 5,
                          RGB(228, 218, 198));
        return;
    }

    if (s_ui_tex) {
        C2D_Image img = {s_ui_tex, sold ? &s_shop_sold : &s_shop_purge};
        C2D_DrawImageAt(img, 14.0f, 26.0f, 0.42f, NULL, 1.0f, 1.0f);
    }
    text_draw(FS_BIG, "删牌服务", 120, 30, COL_GOLD);
    if (sold) text_draw(FS_MID, "已使用", 120, 62, COL_DIM);
    else shop_price_big(120.0f, 62.0f, g_map.purge_cost);
    text_draw_wrapped(FS_SMALL, "从牌组中移除一张牌，本店限一次。", 120, 96, 190, 4,
                      RGB(228, 218, 198));
    if (g_map.message[0]) {
        text_draw(FS_SMALL, g_map.message, 8, 222, RGB(228, 218, 198));
    }
}

/* The game draws one 1763x806 plate (GenericEventDialog.render :355) with the
 * 600x600 picture inside its left half (:358) and the 890x77 option bars under
 * it (LargeDialogOptionButton.render :173).  On a 320x240 screen the plate
 * leaves 152 px for the body, which holds a 12 px CJK text but nothing else,
 * so the picture moves to the top screen and the bars sit below the plate.
 *
 * Bottom screen: plate 320x152 at (0,0), body from y 22, bars 300x26 at
 * y 156/184/212 (the plate's banner is the top ~18 px and its interior is
 * near black, so the 12 px body reads on it). */
#define EVENT_PLATE_H 152.0f
#define EVENT_TEXT_Y 22.0f
#define EVENT_TEXT_W 300.0f
#define EVENT_TEXT_PITCH 15.0f
#define EVENT_BTN_X 10.0f
#define EVENT_BTN_W 300.0f
#define EVENT_BTN_H 26.0f
#define EVENT_BTN_Y0 156.0f
#define EVENT_BTN_DY 28.0f

static int event_id_checked(void) {
    int id = g_map.event_id;
    return (id < 0 || id >= STS_EVENT_COUNT) ? 0 : id;
}

/* imgFrame.png is 610 px around a 600 px picture, i.e. 1 px wider on each side
 * (:358 and :360 both centre on x 460). */
static void draw_event_top(void) {
    int id = event_id_checked();
    const char *name = sts_events[id].name;
    float cx = TOP_W * 0.5f;
    float cy = 104.0f;
    float w = text_width(FS_BIG, name);

    draw_background();
    if (s_ui_tex) {
        C2D_Image frame = {s_ui_tex, &s_ev_frame};
        C2D_Image art = {s_ui_tex, &s_ev_ill[id % STS_EVENT_ILL_COUNT]};
        C2D_DrawImageAt(frame, cx - 77.0f, cy - 77.0f, 0.20f, NULL, 1.0f, 1.0f);
        C2D_DrawImageAt(art, cx - 76.0f, cy - 76.0f, 0.21f, NULL, 1.0f, 1.0f);
    }
    text_draw(FS_BIG, name, (TOP_W - w) * 0.5f, cy + 80.0f, COL_GOLD);
}

static void draw_event(void) {
    const char *labels[3];
    const char *hints[3];
    int id = event_id_checked();
    const char *body = sts_events[id].body;
    const char *note = NULL;
    if (g_map.event_done) {
        /* The result page is a second body text (BigFish.java:65 etc.); the
         * one-line g_map.message is the concrete effect, kept below it. */
        if (g_map.event_result[0]) {
            body = g_map.event_result;
            note = g_map.message;
        } else {
            body = g_map.message;
        }
    }

    if (s_ui_tex) {
        C2D_Image plate = {s_ui_tex, &s_ev_panel};
        C2D_DrawImageAt(plate, 0.0f, 0.0f, 0.12f, NULL, 1.0f, 1.0f);
    } else {
        rect_fill(0, 0, BOT_W, EVENT_PLATE_H, COL_PANEL, 0.12f);
    }

    /* A body that needs more than eight lines gets a tighter pitch rather than
     * being cut off (the old screen stopped at three). Reserve room for the
     * effect note when this is a result page. */
    float body_h = EVENT_PLATE_H - EVENT_TEXT_Y - 4.0f;
    if (note && note[0]) body_h -= 18.0f;
    int lines = text_wrap_lines(FS_SMALL, body, EVENT_TEXT_W);
    if (lines < 1) lines = 1;
    float pitch = EVENT_TEXT_PITCH;
    if ((float)lines * pitch > body_h) {
        pitch = body_h / (float)lines;
        if (pitch < 12.0f) pitch = 12.0f;
    }
    text_draw_wrapped_pitch(FS_SMALL, body, 10.0f, EVENT_TEXT_Y, EVENT_TEXT_W, lines, pitch,
                            RGB(228, 218, 198));

    if (g_map.event_done) {
        if (note && note[0]) {
            float ny = EVENT_TEXT_Y + (float)lines * pitch + 4.0f;
            if (ny > EVENT_PLATE_H - 16.0f) ny = EVENT_PLATE_H - 16.0f;
            rect_fill(10.0f, ny - 4.0f, EVENT_TEXT_W, 1.0f, RGB(92, 82, 64), 0.19f);
            char effect[128];
            snprintf(effect, sizeof(effect), "结果：%s", note);
            text_draw(FS_SMALL, effect, 10.0f, ny, RGB(214, 196, 154));
        }
        text_draw(FS_SMALL, "A 继续", 10.0f, 214.0f, COL_DIM);
        return;
    }

    int count = map_event_options(labels, hints, 3);
    for (int i = 0; i < count && i < 3; i++) {
        float y = EVENT_BTN_Y0 + (float)i * EVENT_BTN_DY;
        int lit = i == s_event_sel;
        if (s_ui_tex) {
            C2D_Image bar = {s_ui_tex, &s_ev_btn[lit ? 1 : 0]};
            C2D_DrawImageAt(bar, EVENT_BTN_X, y, 0.30f, NULL, 1.0f, 1.0f);
        } else {
            rect_fill(EVENT_BTN_X, y, EVENT_BTN_W, 24.0f, lit ? RGB(64, 54, 36) : COL_PANEL, 0.30f);
        }
        if (lit) rect_frame(EVENT_BTN_X, y, EVENT_BTN_W, EVENT_BTN_H, 1.0f, COL_LIT, 0.31f);

        char buf[96];
        if (hints[i][0] && strcmp(hints[i], labels[i]) != 0) {
            snprintf(buf, sizeof(buf), "%s   %s", labels[i], hints[i]);
        } else {
            snprintf(buf, sizeof(buf), "%s", labels[i]);
        }
        float tw = text_width(FS_SMALL, buf);
        if (tw > 284.0f) {
            snprintf(buf, sizeof(buf), "%s", labels[i]);
            tw = text_width(FS_SMALL, buf);
        }
        text_draw(FS_SMALL, buf, EVENT_BTN_X + (EVENT_BTN_W - tw) * 0.5f, y + 3.0f,
                  lit ? COL_INK : COL_DIM);
    }
}

static void draw_treasure(void) {
    text_draw(FS_MID, "宝箱", 10, 6, COL_GOLD);
    text_draw_wrapped(FS_SMALL, g_map.message, 10, 40, 300, 3, RGB(228, 218, 198));
    text_draw(FS_SMALL, "A 继续", 10, 220, COL_DIM);
}

/* Power chips, in the game's own layout: AbstractCreature.renderPowerIcons
 * (:944) puts the 48px icons in a row 48px below the hitbox (POWER_ICON_PADDING_X
 * = 48) and right/top-aligns the amount over each icon's corner. */
#define POWER_ICON_SZ 16.0f
#define POWER_PAD_SZ 18.0f

typedef struct {
    int icon;
    int amount;
    u32 color;
} PowerChip;

static int power_chips(int strength, int vulnerable, int weak, int frail,
                       int metallicize, int flight, int plated, int artifact,
                       int barricade, int hex, int intangible, PowerChip *out) {
    int n = 0;
    if (strength != 0) {
        out[n].icon = STS_POWER_STRENGTH;
        out[n].amount = strength;
        out[n].color = strength > 0 ? RGB(130, 235, 155) : RGB(255, 120, 110);
        n++;
    }
    if (vulnerable > 0) {
        out[n].icon = STS_POWER_VULNERABLE;
        out[n].amount = vulnerable;
        out[n].color = RGB(255, 120, 110);
        n++;
    }
    if (weak > 0) {
        out[n].icon = STS_POWER_WEAK;
        out[n].amount = weak;
        out[n].color = RGB(255, 120, 110);
        n++;
    }
    if (frail > 0) {
        out[n].icon = STS_POWER_FRAIL;
        out[n].amount = frail;
        out[n].color = RGB(255, 120, 110);
        n++;
    }
    if (metallicize > 0) {
        out[n].icon = STS_POWER_METALLICIZE;
        out[n].amount = metallicize;
        out[n].color = RGB(200, 205, 210);
        n++;
    }
    if (flight > 0) {
        out[n].icon = STS_POWER_FLIGHT;
        out[n].amount = flight;
        out[n].color = RGB(180, 230, 255);
        n++;
    }
    if (plated > 0) {
        out[n].icon = STS_POWER_PLATEDARMOR;
        out[n].amount = plated;
        out[n].color = RGB(200, 210, 220);
        n++;
    }
    if (artifact > 0) {
        out[n].icon = STS_POWER_ARTIFACT;
        out[n].amount = artifact;
        out[n].color = RGB(250, 210, 120);
        n++;
    }
    if (barricade > 0) {
        out[n].icon = STS_POWER_BARRICADE;
        out[n].amount = barricade;
        out[n].color = RGB(140, 190, 255);
        n++;
    }
    if (hex > 0) {
        out[n].icon = STS_POWER_HEX;
        out[n].amount = hex;
        out[n].color = RGB(190, 150, 240);
        n++;
    }
    if (intangible > 0) {
        out[n].icon = STS_POWER_INTANGIBLE;
        out[n].amount = intangible;
        out[n].color = RGB(220, 225, 235);
        n++;
    }
    return n;
}

#define POWER_ROW_Y (ENEMY_Y + ENEMY_H + 28.0f)

static void draw_power_row(float x, const PowerChip *chips, int n) {
    for (int i = 0; i < n; i++) {
        float ix = x + (float)i * POWER_PAD_SZ;
        if (s_combat_tex) {
            C2D_Image img = {s_combat_tex, &s_cb_power[chips[i].icon]};
            C2D_DrawImageAt(img, ix, POWER_ROW_Y, 0.66f, NULL,
                            POWER_ICON_SZ / 48.0f, POWER_ICON_SZ / 48.0f);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", chips[i].amount);
        float tw = text_width(FS_SMALL, buf);
        float tx = ix + POWER_ICON_SZ + 1.0f - tw;   /* right-aligned on the icon */
        float ty = POWER_ROW_Y - 7.0f;               /* over its top-right corner */
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
                text_draw_depth(FS_SMALL, buf, tx + (float)ox, ty + (float)oy, RGB(18, 16, 14), 0.667f);
            }
        }
        text_draw_depth(FS_SMALL, buf, tx, ty, chips[i].color, 0.668f);
    }
}

/* Floating damage / block numbers. The port has no effect system (the game's
 * DamageNumberEffect lives in the EffectManager), so the screen keeps a
 * one-frame snapshot of every hitbox and spawns a number whenever hp drops.
 * battle.c stays untouched. */
#define FLOATER_MAX 12
typedef struct {
    int active;
    float x, y, vy;
    int ttl;
    int kind;            /* 0 = damage (red), 1 = blocked (blue) */
    char text[8];
} Floater;

static Floater s_floaters[FLOATER_MAX];
static int s_fl_next;
static struct { int hp, block, valid; } s_prev_player;
static struct { int hp, block, valid; } s_prev_enemy[4];

#define PLAYER_BOX_X 36.0f

static void spawn_floater(float x, float y, int amount, int kind) {
    Floater *f = NULL;
    for (int i = 0; i < FLOATER_MAX; i++) {
        if (!s_floaters[i].active) { f = &s_floaters[i]; break; }
    }
    if (!f) {
        f = &s_floaters[s_fl_next];
        s_fl_next = (s_fl_next + 1) % FLOATER_MAX;
    }
    f->active = 1;
    f->x = x;
    f->y = y;
    f->vy = -1.55f;      /* DamageNumberEffect: vY 400..500 in 1920-space */
    f->ttl = 52;
    f->kind = kind;
    snprintf(f->text, sizeof(f->text), "%d", amount);
}

static void update_floaters(void) {
    for (int i = 0; i < FLOATER_MAX; i++) {
        Floater *f = &s_floaters[i];
        if (!f->active) continue;
        f->y += f->vy;
        f->vy += 0.05f;  /* gravity -2000 units/s^2 */
        if (--f->ttl <= 0) f->active = 0;
    }

    if (g_run.screen != SCREEN_FIGHT) {
        s_prev_player.valid = 0;
        for (int i = 0; i < 4; i++) s_prev_enemy[i].valid = 0;
        return;
    }
    /* Block that simply expires at the start of a turn is not a block hit: the
     * player's block vanishes when their turn begins, an enemy's when its own
     * turn begins. */
    int player_blocked_ok = g_battle.phase == PHASE_ENEMY;
    int enemy_blocked_ok = g_battle.phase == PHASE_PLAYER;

    if (!s_prev_player.valid) {
        s_prev_player.hp = g_battle.hp;
        s_prev_player.block = g_battle.block;
        s_prev_player.valid = 1;
    } else {
        int lost = s_prev_player.hp - g_battle.hp;
        int blk = s_prev_player.block - g_battle.block;
        if (lost > 0) {
            spawn_floater(PLAYER_BOX_X + ENEMY_W * 0.5f, ENEMY_Y + 18.0f, lost, 0);
        } else if (blk > 0 && player_blocked_ok) {
            spawn_floater(PLAYER_BOX_X + ENEMY_W * 0.5f, ENEMY_Y + 18.0f, blk, 1);
        }
        s_prev_player.hp = g_battle.hp;
        s_prev_player.block = g_battle.block;
    }

    for (int i = 0; i < 4; i++) {
        int alive = i < g_battle.enemy_count && g_battle.enemies[i].hp > 0;
        int hp = alive ? g_battle.enemies[i].hp : 0;
        int blk = alive ? g_battle.enemies[i].block : 0;
        if (!s_prev_enemy[i].valid || !alive) {
            s_prev_enemy[i].valid = alive;
            s_prev_enemy[i].hp = hp;
            s_prev_enemy[i].block = blk;
            continue;
        }
        int lost = s_prev_enemy[i].hp - hp;
        int lost_blk = s_prev_enemy[i].block - blk;
        float cx = enemy_x(i) + enemy_w(i) * 0.5f;
        if (lost > 0) spawn_floater(cx, ENEMY_Y + 18.0f, lost, 0);
        else if (lost_blk > 0 && enemy_blocked_ok) spawn_floater(cx, ENEMY_Y + 18.0f, lost_blk, 1);
        s_prev_enemy[i].hp = hp;
        s_prev_enemy[i].block = blk;
    }
}

static void draw_floaters(void) {
    for (int i = 0; i < FLOATER_MAX; i++) {
        Floater *f = &s_floaters[i];
        if (!f->active) continue;
        u32 ink = f->kind == 0 ? RGB(255, 90, 80) : RGB(102, 204, 230);
        float w = text_width(FS_MID, f->text);
        float tx = f->x - w * 0.5f;
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
                text_draw_depth(FS_MID, f->text, tx + (float)ox, f->y + (float)oy,
                                RGB(28, 12, 10), 0.78f);
            }
        }
        text_draw_depth(FS_MID, f->text, tx, f->y, ink, 0.781f);
    }
}

/* The status panel is a scrolling list because a fight can have ten active
 * powers at once (Frailty, Metallicize, Flight, Plated Armor, Artifact,
 * Barricade...). Each row is one line: name, stack, and what the power does. */
#define STATUS_ROW_MAX 40
#define STATUS_ROW_PITCH 14.0f
#define STATUS_VISIBLE 12

typedef struct {
    char text[112];
    u32 color;
} StatusRow;

static int status_add(StatusRow *rows, int n, int max, const char *name, int value,
                      const char *desc, u32 color) {
    if (value <= 0 || n >= max) return n;
    snprintf(rows[n].text, sizeof(rows[n].text), "%s %d  %s", name, value, desc);
    rows[n].color = color;
    return n + 1;
}

static int status_header(StatusRow *rows, int n, int max, const char *text, u32 color) {
    if (n >= max) return n;
    snprintf(rows[n].text, sizeof(rows[n].text), "%s", text);
    rows[n].color = color;
    return n + 1;
}

static int status_collect(StatusRow *rows, int max) {
    int n = 0;
    char buf[96];
    u32 debuff = RGB(232, 150, 140);
    u32 buff = RGB(206, 192, 150);
    u32 neutral = RGB(222, 214, 196);

    snprintf(buf, sizeof(buf), "铁甲战士  %d/%d  格挡 %d",
             g_battle.hp, g_battle.max_hp, g_battle.block);
    n = status_header(rows, n, max, buf, RGB(232, 120, 110));

    int player_start = n;
    n = status_add(rows, n, max, "易伤", g_battle.vulnerable,
                   "受到的攻击伤害 +50%", debuff);
    n = status_add(rows, n, max, "虚弱", g_battle.weak,
                   "造成的攻击伤害 -25%", debuff);
    n = status_add(rows, n, max, "脆弱", g_battle.frail,
                   "卡牌格挡 -25%", debuff);
    n = status_add(rows, n, max, "力量", g_battle.strength,
                   "攻击伤害提升", buff);
    n = status_add(rows, n, max, "双发", battle_power(POW_DOUBLE_TAP),
                   "下一张攻击牌打出两次", buff);
    n = status_add(rows, n, max, "金属化", battle_power(POW_METALLICIZE),
                   "回合结束获得格挡", buff);
    n = status_add(rows, n, max, "壁垒", battle_power(POW_BARRICADE),
                   "格挡在回合开始时不消失", buff);
    n = status_add(rows, n, max, "邪咒", g_battle.hex,
                   "非攻击牌会塞入晕眩", debuff);
    n = status_add(rows, n, max, "缠身", g_battle.entangled,
                   "本回合不能打出攻击牌", debuff);
    n = status_add(rows, n, max, "人工制品", g_battle.artifact,
                   "免疫负面效果", buff);
    n = status_add(rows, n, max, "无实体", g_battle.intangible,
                   "受到的伤害降为 1", buff);
    if (n == player_start) {
        n = status_header(rows, n, max, "  无状态", COL_DIM);
    }

    for (int i = 0; i < g_battle.enemy_count; i++) {
        Enemy *e = &g_battle.enemies[i];
        if (e->hp <= 0) continue;
        snprintf(buf, sizeof(buf), "%s  %d/%d  格挡 %d",
                 e->name, e->hp, e->max_hp, e->block);
        n = status_header(rows, n, max, buf, neutral);
        int enemy_start = n;
        n = status_add(rows, n, max, "易伤", e->vulnerable,
                       "受到的攻击伤害 +50%", debuff);
        n = status_add(rows, n, max, "虚弱", e->weak,
                       "造成的攻击伤害 -25%", debuff);
        n = status_add(rows, n, max, "力量", e->strength,
                       "攻击伤害提升", buff);
        n = status_add(rows, n, max, "金属化", e->metallicize,
                       "回合结束获得格挡", buff);
        n = status_add(rows, n, max, "飞行", e->flight,
                       "攻击伤害减半，被打落", buff);
        n = status_add(rows, n, max, "多层护甲", e->plated,
                       "回合结束获得格挡；掉血减 1 层", buff);
        n = status_add(rows, n, max, "人工制品", e->artifact,
                       "免疫负面效果", buff);
        n = status_add(rows, n, max, "枷锁", e->shackled,
                       "回合结束时恢复力量", debuff);
        n = status_add(rows, n, max, "壁垒", e->barricade,
                       "格挡在回合开始时不消失", buff);
        if (e->stasis_card >= 0) {
            snprintf(buf, sizeof(buf), "静滞  %s",
                     sts_cards[CARD_INDEX(e->stasis_card)].name);
            n = status_header(rows, n, max, buf, debuff);
        }
        if (n == enemy_start) {
            n = status_header(rows, n, max, "  无状态", COL_DIM);
        }
    }

    if (g_run.relic_count > 0) {
        char relics[96];
        int used = 0;
        used += snprintf(relics + used, sizeof(relics) - (size_t)used, "遗物：");
        for (int i = 0; i < g_run.relic_count && used < (int)sizeof(relics) - 8; i++) {
            used += snprintf(relics + used, sizeof(relics) - (size_t)used, "%s%s",
                             i ? " " : "", battle_relic_name(g_run.relics[i]));
        }
        n = status_header(rows, n, max, relics, RGB(214, 200, 170));
    }
    return n;
}

static void draw_detail_panel(void) {
    /* Everything in this panel has to sit above its own 0.9 backdrop; the
     * default text depth is lower and would be painted over by the panel. */
    rect_fill(0, 0, BOT_W, BOT_H, RGBA(10, 10, 12, 245), 0.9f);
    text_draw_depth(FS_MID, "状态一览", 8, 2, COL_GOLD, 0.93f);

    /* Tabs. */
    const char *tabs[2] = {"状态", "药水"};
    for (int i = 0; i < 2; i++) {
        float x = 112.0f + i * 56.0f;
        int active = i == s_panel_tab;
        rect_fill(x, 2, 52, 22, active ? RGB(74, 62, 36) : RGB(34, 32, 28), 0.91f);
        if (active) rect_frame(x, 2, 52, 22, 1.0f, COL_LIT, 0.92f);
        text_draw_depth(FS_SMALL, tabs[i], x + 14, 4, active ? COL_INK : COL_DIM, 0.93f);
    }

    if (s_panel_tab == 0) {
        StatusRow rows[STATUS_ROW_MAX];
        int count = status_collect(rows, STATUS_ROW_MAX);
        int max_scroll = count > STATUS_VISIBLE ? count - STATUS_VISIBLE : 0;
        if (s_panel_scroll > max_scroll) s_panel_scroll = max_scroll;
        float y = 30.0f;
        for (int i = s_panel_scroll; i < count && y < 210.0f; i++) {
            text_draw_depth(FS_SMALL, rows[i].text, 8, y, rows[i].color, 0.92f);
            y += STATUS_ROW_PITCH;
        }
        if (max_scroll > 0) {
            char page[32];
            snprintf(page, sizeof(page), "%d/%d", s_panel_scroll + 1, max_scroll + 1);
            text_draw_depth(FS_SMALL, page, 292, 210, COL_DIM, 0.92f);
        }
    } else {
        int count = battle_potion_count();
        if (count == 0) {
            text_draw_depth(FS_SMALL, "没有药水（战斗胜利、宝箱有概率掉落）", 12, 60, COL_DIM, 0.92f);
        }
        for (int i = 0; i < count; i++) {
            int id = battle_potion_at(i);
            float y = 40.0f + i * 62.0f;
            int lit = i == s_potion_sel;
            rect_fill(8, y, 304, 54, lit ? RGB(60, 52, 34) : COL_PANEL, 0.91f);
            if (lit) rect_frame(8, y, 304, 54, 1.5f, COL_LIT, 0.92f);
            text_draw_depth(FS_MID, battle_potion_name(id), 16, y + 2, COL_INK, 0.93f);
            text_draw_depth(FS_SMALL, battle_potion_desc(id), 16, y + 28, COL_DIM, 0.93f);
        }
    }

    text_draw_depth(FS_SMALL, s_panel_tab == 0
                 ? "L/R 切换  ↑↓ 滚动  X/B 关闭"
                 : "L/R 切换  ↑↓ 选择  A 使用  X/B 关闭",
             8, 222, COL_DIM, 0.92f);
}

static void draw_hint(void) {
    const char *hint = "←→/LR 选牌  A 出牌  Y 结束回合  松手出牌";
    float w = text_width(FS_SMALL, hint);
    float x = (TOP_W - w) / 2.0f;
    /* A compact backing pill for the key hints only. The old version filled
     * the whole bottom 40px of the top screen, which read as an unexplained
     * translucent band over the fight. */
    rect_fill(x - 6.0f, TOP_H - 17.0f, w + 12.0f, 16.0f, RGBA(10, 8, 6, 185), 0.18f);
    text_draw(FS_SMALL, hint, x, TOP_H - 14.0f, RGBA(150, 144, 130, 220));
}

static void debug_creature(float x, float py, float pw, float ph, float ew) {
    rect_frame(x, ENEMY_Y, ew, ENEMY_H, 1.0f, RGB(0, 255, 90), 0.90f);
    rect_frame(x - 3.0f, py, pw, ph, 1.0f, RGB(255, 0, 255), 0.90f);
    rect_frame(x - 25.0f, ENEMY_Y + ENEMY_H - 25.0f, 22.0f, 22.0f, 1.0f, RGB(255, 255, 0), 0.90f);
    rect_frame(x + 1.0f, POWER_ROW_Y - 2.0f, 3.0f * POWER_PAD_SZ, POWER_ICON_SZ + 4.0f,
               1.0f, RGB(0, 200, 255), 0.90f);
}

static void draw_fight_top(void) {
    draw_background();
    draw_top_bar();

    text_draw(FS_SMALL, "铁甲战士", 36, 84, COL_DIM);
    draw_entity_box(PLAYER_BOX_X, ENEMY_W, g_battle.hp, g_battle.max_hp, g_battle.block, 0,
                    RGB(52, 58, 66));
    if (s_combat_tex) {
        /* The Ironclad is a Spine rig; extract_combat.py bakes its Idle frame
         * (the setup pose is a crouch).  Sit it on the bottom of the HP box. */
        float pw = 96.0f;
        float ph = pw * (float)sts_combat_player.h / (float)sts_combat_player.w;
        C2D_Image img = {s_combat_tex, &s_cb_player};
        C2D_DrawImageAt(img, PLAYER_BOX_X - 14.0f, ENEMY_Y + ENEMY_H - ph - 2.0f, 0.45f, NULL,
                        pw / (float)sts_combat_player.w,
                        ph / (float)sts_combat_player.h);
    }
    {
        PowerChip chips[10];
        int n = power_chips(g_battle.strength, g_battle.vulnerable, g_battle.weak,
                            g_battle.frail, battle_power(POW_METALLICIZE),
                            0, 0, g_battle.artifact, battle_power(POW_BARRICADE),
                            g_battle.hex, g_battle.intangible, chips);
        draw_power_row(PLAYER_BOX_X + 2.0f, chips, n);
    }

    for (int i = 0; i < g_battle.enemy_count; i++) {
        Enemy *e = &g_battle.enemies[i];
        if (e->hp <= 0) continue;
        float x = enemy_x(i);
        float ew = enemy_w(i);
        int lit = (s_target_mode || s_drag.activated) && s_target == i;
        char intent[48];
        int kind = battle_intent_kind(i);
        int value = battle_intent_value(i);
        int value2 = e->moves ? e->moves[e->move_index].value2 : 0;
        int value3 = e->moves ? e->moves[e->move_index].value3 : 0;
        int block = battle_intent_block(i);
        u32 intent_color = RGB(238, 176, 146);
        switch (kind) {
        case MOVE_ATTACK:
            snprintf(intent, sizeof(intent), "攻击 %d", battle_enemy_intent_damage(i));
            break;
        case MOVE_ATTACK_BLOCK:
            snprintf(intent, sizeof(intent), "攻击 %d 防御 %d", battle_enemy_intent_damage(i), block);
            break;
        case MOVE_ATTACK_DEBUFF:
            /* Face Slap: 12 damage + 2 Vulnerable + 2 Frail; Fell: 18 + 2
             * Frail; Frail Attack: 10 + 5 Frail. */
            if (value2 > 0 && value3 > 0) {
                snprintf(intent, sizeof(intent), "攻击 %d 易伤 %d 脆弱 %d",
                         battle_enemy_intent_damage(i), value2, value3);
            } else if (value2 > 0) {
                snprintf(intent, sizeof(intent), "攻击 %d 易伤 %d",
                         battle_enemy_intent_damage(i), value2);
            } else if (value3 > 0) {
                snprintf(intent, sizeof(intent), "攻击 %d 脆弱 %d",
                         battle_enemy_intent_damage(i), value3);
            } else {
                snprintf(intent, sizeof(intent), "攻击 %d", battle_enemy_intent_damage(i));
            }
            break;
        case MOVE_DEFEND:
            /* Defensive Stance: block for itself + Metallicize stacks. */
            if (value2 > 0) {
                snprintf(intent, sizeof(intent), "防御 %d 金属化 %d", value, value2);
            } else {
                snprintf(intent, sizeof(intent), "防御 %d", value);
            }
            intent_color = RGB(200, 205, 210);
            break;
        case MOVE_BLOCK_ATTACK:
            /* Spheric Guardian: +15 block first, then 10 damage. */
            snprintf(intent, sizeof(intent), "防御 %d 攻击 %d",
                     value, battle_enemy_intent_damage(i));
            break;
        case MOVE_DRAIN:
            /* Chosen: player 3 Weak, itself +3 Strength. */
            snprintf(intent, sizeof(intent), "虚弱 %d 力量 +%d", value, value2);
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_HEX:
            snprintf(intent, sizeof(intent), "诅咒");
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_FLY:
            snprintf(intent, sizeof(intent), "飞起");
            intent_color = RGB(170, 220, 250);
            break;
        case MOVE_HEADBUTT:
            snprintf(intent, sizeof(intent), "攻击 %d", battle_enemy_intent_damage(i));
            break;
        case MOVE_STUN:
            snprintf(intent, sizeof(intent), "眩晕");
            intent_color = RGB(200, 200, 160);
            break;
        case MOVE_SUCK:
            snprintf(intent, sizeof(intent), "攻击 %d 吸血", battle_enemy_intent_damage(i));
            break;
        case MOVE_DEFEND_BUFF:
            /* BronzeAutomaton Boost: +9 block, +3 Strength. */
            snprintf(intent, sizeof(intent), "防御 %d 力量 +%d", value, value2);
            intent_color = RGB(200, 205, 210);
            break;
        case MOVE_SPAWN:
            snprintf(intent, sizeof(intent), "召唤");
            intent_color = RGB(200, 180, 120);
            break;
        case MOVE_STASIS:
            snprintf(intent, sizeof(intent), "静滞");
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_SUPPORT:
            snprintf(intent, sizeof(intent), "支援 %d", value);
            intent_color = RGB(200, 205, 210);
            break;
        case MOVE_RALLY:
            /* Collector Buff: block for itself and Strength for the room. */
            snprintf(intent, sizeof(intent), "防御 %d 力量 +%d", value, value2);
            intent_color = RGB(200, 205, 210);
            break;
        case MOVE_CURSE:
            /* Collector Mega Debuff: equal stacks of Weak/Vulnerable/Frail. */
            snprintf(intent, sizeof(intent), "虚弱 易伤 脆弱 %d", value);
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_REVIVE:
            snprintf(intent, sizeof(intent), "复活");
            intent_color = RGB(200, 180, 120);
            break;
        case MOVE_COMMAND:
            /* Gremlin Leader Encourage: the original intent is a plain BUFF
             * icon; the numbers land on the status rows after the move. */
            snprintf(intent, sizeof(intent), "强化");
            intent_color = RGB(200, 180, 120);
            break;
        case MOVE_PROTECT:
            snprintf(intent, sizeof(intent), "保护 %d", value);
            intent_color = RGB(200, 205, 210);
            break;
        case MOVE_CHARGE:
            snprintf(intent, sizeof(intent), "蓄能");
            intent_color = RGB(170, 200, 230);
            break;
        case MOVE_TAUNT:
            snprintf(intent, sizeof(intent), "虚弱 %d 易伤 %d", value, value2);
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_ANGER:
            snprintf(intent, sizeof(intent), "强化 +%d", value);
            intent_color = RGB(200, 180, 120);
            break;
        case MOVE_ATTACK_WEAK:
            snprintf(intent, sizeof(intent), "攻击 %d 虚弱 %d",
                     battle_enemy_intent_damage(i), value2);
            break;
        case MOVE_ENTANGLE:
            snprintf(intent, sizeof(intent), "缠绕");
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_WOUND:
            snprintf(intent, sizeof(intent), "攻击 %d 伤口 %d",
                     battle_enemy_intent_damage(i), value2);
            break;
        case MOVE_BUFF:
            snprintf(intent, sizeof(intent), "强化 +%d", value);
            intent_color = RGB(200, 180, 120);
            break;
        case MOVE_WEAK:
            snprintf(intent, sizeof(intent), "虚弱 %d", value);
            intent_color = RGB(170, 200, 230);
            break;
        case MOVE_VULN:
            snprintf(intent, sizeof(intent), "易伤 %d", value);
            intent_color = RGB(200, 150, 240);
            break;
        case MOVE_MULTI: {
            int hits = battle_intent_hits(i);
            snprintf(intent, sizeof(intent), hits > 1 ? "攻击 %d×%d" : "攻击 %d",
                     battle_enemy_intent_damage(i), hits);
            break;
        }
        case MOVE_SLIME:
            if (value > 0) {
                snprintf(intent, sizeof(intent), "攻击 %d 黏液 %d",
                         battle_enemy_intent_damage(i), value2 > 0 ? value2 : 1);
            } else {
                snprintf(intent, sizeof(intent), "黏液 %d", value2 > 0 ? value2 : 1);
            }
            intent_color = RGB(150, 200, 150);
            break;
        case MOVE_PREP:
            /* Slime Boss' Prep has no effect and shows the UNKNOWN intent. */
            snprintf(intent, sizeof(intent), "未知");
            intent_color = RGB(190, 190, 190);
            break;
        case MOVE_FRAIL:
            snprintf(intent, sizeof(intent), "脆弱 %d", value);
            intent_color = RGB(200, 150, 240);
            break;
        default:
            intent[0] = 0;
            break;
        }
        if (s_vis_tex && kind != MOVE_PREP) {
            int icon = 0;
            if (kind == MOVE_ATTACK || kind == MOVE_ATTACK_BLOCK || kind == MOVE_MULTI ||
                kind == MOVE_ATTACK_DEBUFF || (kind == MOVE_SLIME && value > 0) ||
                kind == MOVE_ATTACK_WEAK ||
                kind == MOVE_WOUND || kind == MOVE_BLOCK_ATTACK || kind == MOVE_HEADBUTT ||
                kind == MOVE_SUCK) {
                int dmg = battle_enemy_intent_damage(i);
                icon = dmg <= 4 ? 0 : (dmg <= 9 ? 1 : (dmg <= 14 ? 2 : 3));
            } else if (kind == MOVE_BUFF || kind == MOVE_ANGER || kind == MOVE_FLY ||
                       kind == MOVE_SPAWN || kind == MOVE_REVIVE ||
                       kind == MOVE_COMMAND || kind == MOVE_CHARGE) {
                icon = 5;
            } else if (kind == MOVE_DEFEND || kind == MOVE_DEFEND_BUFF ||
                       kind == MOVE_SUPPORT || kind == MOVE_RALLY ||
                       kind == MOVE_PROTECT) {
                icon = 4;
            } else {
                icon = 6;
            }
            C2D_Image ic = {s_vis_tex, &s_intent[icon]};
            C2D_DrawImageAt(ic, x + 2.0f, 60.0f, 0.66f, NULL, 20.0f / 24.0f, 20.0f / 24.0f);
        }
        /* Four-enemy fights put the leader on the right.  Its line gets a row
         * above the others so the third Gremlin's longer intent does not run
         * straight into it; right-align it to the box. */
        float ix = x + 24.0f, iy = 64.0f;
        if (g_battle.enemy_count >= 4 && x >= 300.0f && intent[0]) {
            float tw = text_width(FS_SMALL, intent);
            ix = x + ew - tw;
            iy = 46.0f;
            if (ix < 2.0f) ix = 2.0f;
        }
        text_draw(FS_SMALL, intent, intent[0] ? ix : x + 2, iy, intent_color);
        text_draw(FS_SMALL, e->name, x, 84, COL_DIM);
        draw_entity_box(x, ew, e->hp, e->max_hp, e->block, lit, RGB(60, 56, 46));
        int slot = monster_slot(e->art ? e->art : e->name);
        if (slot >= 0 && s_mon_img[slot].tex) {
            float side = enemy_side(i);
            C2D_DrawImageAt(s_mon_img[slot], x + (ew - side) / 2.0f, 104.0f + (64.0f - side) / 2.0f,
                            0.45f, NULL, side / 96.0f, side / 96.0f);
        }
        {
            PowerChip chips[10];
            int n = power_chips(e->strength, e->vulnerable, e->weak, 0, e->metallicize,
                                e->flight, e->plated, e->artifact, e->barricade, 0, 0, chips);
            draw_power_row(x + 2.0f, chips, n);
        }
    }

    if (s_debug_layers) {
        /* Creature boxes, in the same colours as the card debug overlay: green =
         * hitbox, magenta = portrait, yellow = block badge, cyan = power row. */
        float pph = 74.0f * (float)sts_combat_player.h / (float)sts_combat_player.w;
        debug_creature(PLAYER_BOX_X, ENEMY_Y + 2.0f, 74.0f, pph, 74.0f);
        for (int i = 0; i < g_battle.enemy_count; i++) {
            if (g_battle.enemies[i].hp <= 0) continue;
            float x = enemy_x(i);
            float side = enemy_side(i);
            debug_creature(x, 104.0f + (64.0f - side) / 2.0f, side, side, enemy_w(i));
        }
    }

    draw_floaters();

    if ((s_target_mode || s_drag.activated) && s_target >= 0 && s_target < g_battle.enemy_count) {
        float ex = enemy_x(s_target) + enemy_w(s_target) * 0.5f;
        float ey = (float)(ENEMY_Y + ENEMY_H / 2);
        float x0 = 200.0f, y0 = (float)TOP_H - 20.0f;
        float cx = (x0 + ex) / 2.0f, cy = ey - 40.0f;
        for (int i = 0; i <= 26; i += 2) {
            float t = (float)i / 26.0f;
            float px = (1 - t) * (1 - t) * x0 + 2 * (1 - t) * t * cx + t * t * ex;
            float py = (1 - t) * (1 - t) * y0 + 2 * (1 - t) * t * cy + t * t * ey;
            C2D_DrawCircleSolid(px, py, 0.7f, 1.6f, RGB(250, 200, 110));
        }
    }
    draw_hint();
    if (s_toast_ttl > 0) {
        float tw = text_width(FS_SMALL, s_toast);
        /* Sit below the HP row so a rejection message never covers it. */
        rect_fill((TOP_W - tw) / 2.0f - 5.0f, 198.0f, tw + 10.0f, 14.0f,
                  RGBA(10, 8, 6, 210), 0.19f);
        text_draw(FS_SMALL, s_toast, (TOP_W - tw) / 2.0f, 200.0f, RGB(246, 214, 140));
    }
}

static void draw_bottom_fight(void) {
    draw_energy_orb();
    char buf[32];

    snprintf(buf, sizeof(buf), "抽 %d", g_battle.draw_count);
    text_draw(FS_SMALL, buf, 62, 8, COL_DIM);
    snprintf(buf, sizeof(buf), "弃 %d", g_battle.discard_count);
    text_draw(FS_SMALL, buf, 62, 26, COL_DIM);

    rect_fill(150, 8, 64, 34, s_detail ? RGB(90, 74, 40) : RGB(46, 40, 32), 0.3f);
    rect_frame(150, 8, 64, 34, 1.5f, s_detail ? COL_LIT : RGB(120, 104, 78), 0.31f);
    text_draw(FS_SMALL, "状态", 162, 16, COL_INK);

    draw_end_turn_button();

    int shown = s_drag.active ? s_drag.card : s_focus;
    if (shown >= 0 && shown < g_battle.hand_count) {
        int card_index = battle_hand_card(shown);
        const struct CardDef *c = &sts_cards[card_index];
        int up = battle_hand_up(shown);
        int level = battle_hand_level(shown);
        rect_fill(6, 56, 308, 84, COL_PANEL, 0.32f);
        rect_frame(6, 56, 308, 84, 1.0f, RGB(86, 78, 66), 0.33f);
        char name[40];
        if (strcmp(c->id, "Searing Blow") == 0 && level > 0)
            snprintf(name, sizeof(name), "%s+%d", c->name, level);
        else
            snprintf(name, sizeof(name), "%s%s", c->name, up ? "+" : "");
        text_draw(FS_MID, name, 12, 58, COL_INK);
        char searing[96];
        const char *body = up ? c->text_up : c->text;
        if (strcmp(c->id, "Searing Blow") == 0 && level > 0) {
            snprintf(searing, sizeof(searing), "造成 %d 点伤害。\n能被多次升级。",
                     battle_searing_damage(level));
            body = searing;
        }
        text_draw_wrapped(FS_SMALL, body, 12, 82, 296, 3, RGB(228, 218, 198));
        if (s_drag.activated && s_target >= 0 && s_target < g_battle.enemy_count) {
            Enemy *e = &g_battle.enemies[s_target];
            snprintf(buf, sizeof(buf), "目标：%s %d/%d", e->name, e->hp, e->max_hp);
            float bw = text_width(FS_SMALL, buf);
            text_draw(FS_SMALL, buf, 306 - bw, 60, RGB(250, 206, 120));
        }
    }

    int n = g_battle.hand_count;
    /* CardGroup.renderHand walks hand.group left to right, so every later card
     * covers its left neighbour and each cost orb -- which sits in the card's
     * top-left corner -- stays visible.  The focused card stays in place: it is
     * marked by a chevron drawn after the loop, because lifting it would break
     * the shared bottom arc that makes the group read as a fan. */
    for (int i = 0; i < n; i++) {
        int is_drag = s_drag.active && i == s_drag.card;
        if (is_drag && s_drag.activated) continue;

        float x, y, w, h;
        hand_slot(i, &x, &y, &w, &h);
        float lift = 0.0f;
        if (is_drag) lift = (float)(s_drag.rise > 60 ? 60 : (s_drag.rise < 0 ? 0 : s_drag.rise));
        draw_card_in_hand(battle_hand_card(i), battle_hand_up(i), battle_hand_level(i),
                          x, y - lift, w, h, 0, hand_angle(i));
    }

    /* The fan outline stays clean: the focused card is marked by a small gold
     * chevron above its top edge instead of a frame that the next card would
     * clip. */
    if (!s_drag.active && s_focus >= 0 && s_focus < n) {
        float x, y, w, h;
        hand_slot(s_focus, &x, &y, &w, &h);
        float a = hand_angle(s_focus);
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float tx = cx + sinf(a) * h * 0.5f;
        float ty = cy - cosf(a) * h * 0.5f;
        u32 gold = RGB(250, 210, 130);
        C2D_DrawLine(tx - 5.0f, ty - 10.0f, gold, tx, ty - 4.0f, gold, 2.0f, 0.80f);
        C2D_DrawLine(tx, ty - 4.0f, gold, tx + 5.0f, ty - 10.0f, gold, 2.0f, 0.80f);
    }

    if (s_drag.active && s_drag.activated && s_drag.card < g_battle.hand_count) {
        /* The dragged card follows the finger.  Keep the finger in the lower part
         * of the card so the art stays visible, and clamp it to the screen. */
        int dx = s_drag.x - 42;
        int dy = s_drag.y - 88;
        if (dx < 0) dx = 0;
        if (dx > BOT_W - 84) dx = BOT_W - 84;
        if (dy < 0) dy = 0;
        if (dy > BOT_H - 116) dy = BOT_H - 116;
        draw_card_at(battle_hand_card(s_drag.card), battle_hand_up(s_drag.card),
                     battle_hand_level(s_drag.card), (float)dx, (float)dy, 84.0f, 116.0f, 1,
                     0.72f, g_battle.energy, 0.0f);
    }

    if (s_play_fx.active && s_play_fx.ttl > 0) {
        float t = 1.0f - (float)s_play_fx.ttl / (float)PLAY_FX_FRAMES;
        float e = 1.0f - (1.0f - t) * (1.0f - t);   /* ease-out */
        float x = s_play_fx.x0 + ((float)(BOT_W / 2 - 42) - s_play_fx.x0) * e;
        float y = s_play_fx.y0 + (58.0f - s_play_fx.y0) * e;
        float w = s_play_fx.w0 + (84.0f - s_play_fx.w0) * e;
        float h = s_play_fx.h0 + (116.0f - s_play_fx.h0) * e;
        if (t < 0.5f) {
            rect_frame(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 2.0f,
                       RGB(250, 210, 130), 0.79f);
        }
        draw_card_at(s_play_fx.card, s_play_fx.up, 0, x, y, w, h, 1, 0.80f, -1, 0.0f);
    }
}

static void draw_reward_top(void) {
    char buf[64];
    draw_background();
    rect_fill(0, 0, TOP_W, TOP_H, RGBA(8, 8, 10, 150), 0.2f);
    text_draw(FS_BIG, "战利品", 160, 54, COL_GOLD);
    snprintf(buf, sizeof(buf), "+%d 金币", g_run.gold_gain);
    text_draw(FS_MID, buf, 130, 104, RGB(230, 200, 120));
    if (g_run.potion_gain >= 0) {
        snprintf(buf, sizeof(buf), "药水：%s", battle_potion_name(g_run.potion_gain));
        text_draw(FS_SMALL, buf, 130, 130, RGB(150, 210, 220));
    } else if (g_run.potion_gain == -2) {
        text_draw(FS_SMALL, "药水：栏位已满", 130, 130, COL_DIM);
    } else {
        text_draw(FS_SMALL, "药水：无", 130, 130, COL_DIM);
    }
    text_draw(FS_SMALL, "选择一张牌加入牌组，或跳过", 96, 190, COL_DIM);
}

static void draw_reward_screen(void) {
    char buf[64];
    rect_fill(0, 0, BOT_W, BOT_H, RGB(22, 20, 18), 0.0f);
    rect_fill(0, 0, BOT_W, 24, RGBA(12, 12, 14, 235), 0.3f);
    text_draw(FS_MID, "战利品", 8, 1, COL_GOLD);
    snprintf(buf, sizeof(buf), "关 %d/%d", g_run.node + 1, run_node_count());
    text_draw(FS_SMALL, buf, 84, 4, COL_DIM);
    int skip_lit = s_press_skip > 0;
    rect_fill(246, 3, 68, 20, skip_lit ? RGB(86, 58, 34) : RGB(38, 34, 30), 0.32f);
    rect_frame(246, 3, 68, 20, 1.0f, skip_lit ? COL_LIT : RGB(100, 88, 72), 0.33f);
    text_draw(FS_SMALL, "跳过 B", 254, 6, skip_lit ? COL_INK : COL_DIM);

    snprintf(buf, sizeof(buf), "金币 +%d", g_run.gold_gain);
    text_draw(FS_SMALL, buf, 8, 27, RGB(230, 200, 120));
    if (g_run.potion_gain >= 0) {
        snprintf(buf, sizeof(buf), "药水：%s", battle_potion_name(g_run.potion_gain));
        text_draw(FS_SMALL, buf, 8, 45, RGB(150, 210, 220));
    } else if (g_run.potion_gain == -2) {
        text_draw(FS_SMALL, "药水：栏位已满", 8, 45, COL_DIM);
    } else {
        text_draw(FS_SMALL, "药水：无", 8, 45, COL_DIM);
    }
    text_draw(FS_SMALL, "←→ 选择  A 拿牌", 190, 27, COL_DIM);

    for (int i = 0; i < g_run.reward_count; i++) {
        int rx, ry;
        reward_rect(i, &rx, &ry);
        draw_card(g_run.reward[i], 0, 0, (float)rx, (float)ry, REWARD_W, REWARD_H, i == s_reward_focus);
    }

    if (s_reward_focus >= 0 && s_reward_focus < g_run.reward_count) {
        const struct CardDef *c = &sts_cards[g_run.reward[s_reward_focus]];
        rect_fill(6, 180, 308, 54, COL_PANEL, 0.32f);
        rect_frame(6, 180, 308, 54, 1.0f, RGB(86, 78, 66), 0.33f);
        text_draw(FS_MID, c->name, 12, 182, COL_INK);
        text_draw_wrapped(FS_SMALL, c->text, 12, 205, 296, 2, RGB(228, 218, 198));
    }
}

static void draw_campfire_top(void) {
    char buf[64];
    draw_background();
    rect_fill(0, 0, TOP_W, TOP_H, RGBA(8, 8, 10, 130), 0.2f);
    text_draw(FS_BIG, "篝火", 168, 50, RGB(246, 176, 120));
    snprintf(buf, sizeof(buf), "生命 %d/%d", g_battle.hp, g_battle.max_hp);
    text_draw(FS_MID, buf, 148, 96, COL_INK);
    text_draw(FS_SMALL, "选择休息或锻造，A 或点击确认", 92, 158, COL_DIM);
}

static void draw_campfire_screen(void) {
    char buf[48];
    rect_fill(0, 0, BOT_W, BOT_H, RGB(24, 21, 18), 0.0f);
    rect_fill(0, 0, BOT_W, 22, RGBA(12, 12, 14, 235), 0.3f);
    text_draw(FS_SMALL, "篝火", 8, 3, COL_GOLD);
    snprintf(buf, sizeof(buf), "关 %d/%d  生命 %d/%d", g_run.node + 1, run_node_count(),
             g_battle.hp, g_battle.max_hp);
    text_draw(FS_SMALL, buf, 62, 3, COL_DIM);

    static const char *labels[2] = {"休息", "锻造"};
    for (int i = 0; i < 2; i++) {
        float cx = i == 0 ? 96.0f : 224.0f;
        float cy = 126.0f;
        float sz = 96.0f * s_camp_scale[i];
        float hs = sz * 0.5f;
        int lit = i == s_campfire_focus;

        if (s_hud_tex) {
            /* AbstractCampfireOption: shadow -> gold outline -> art.  The outline
             * is a filled rounded rect; drawing it first leaves only the rim. */
            C2D_Image shadow = hud_image(&s_hud_camp[STS_HUD_CAMP_SHADOW]);
            C2D_DrawImageAt(shadow, cx - hs + 4.0f, cy - hs + 3.0f, 0.30f, NULL,
                            sz / 256.0f, sz / 256.0f);
            if (lit) {
                float os = sz * 1.075f;
                C2D_Image outline = hud_image(&s_hud_camp[STS_HUD_CAMP_OUTLINE]);
                C2D_DrawImageAt(outline, cx - os * 0.5f, cy - os * 0.5f, 0.31f, NULL,
                                os / 256.0f, os / 256.0f);
            }
            C2D_Image art = hud_image(&s_hud_camp[i == 0 ? STS_HUD_CAMP_SLEEP
                                                       : STS_HUD_CAMP_SMITH]);
            C2D_DrawImageAt(art, cx - hs, cy - hs, 0.32f, NULL, sz / 256.0f, sz / 256.0f);
        } else {
            rect_fill(cx - hs, cy - hs, sz, sz, lit ? RGB(70, 52, 36) : RGB(40, 36, 32), 0.4f);
            rect_frame(cx - hs, cy - hs, sz, sz, lit ? 3.0f : 1.0f,
                       lit ? COL_LIT : RGB(110, 100, 86), 0.45f);
        }

        float lw = text_width(FS_MID, labels[i]);
        text_draw(FS_MID, labels[i], cx - lw * 0.5f, 164.0f, lit ? COL_GOLD : COL_INK);
    }

    const char *desc = s_campfire_focus == 0
        ? "回复你最大生命的30%。"
        : (battle_can_upgrade() ? "升级你牌组中的一张牌。" : "没有可以升级的牌。");
    float dw = text_width(FS_SMALL, desc);
    text_draw(FS_SMALL, desc, (BOT_W - dw) * 0.5f, 190.0f, COL_DIM);
    text_draw(FS_SMALL, "←→ 选择  A 确认", 104, 220, COL_DIM);
}

static void draw_upgrade_top(void) {
    int count = battle_deck_count();
    int sel = s_upgrade_focus;
    char buf[96];
    draw_background();
    rect_fill(0, 0, TOP_W, TOP_H, RGBA(8, 8, 10, 170), 0.2f);
    text_draw(FS_MID, "锻造 · 升级一张牌", 10, 4, COL_GOLD);
    snprintf(buf, sizeof(buf), "牌组 %d 张", count);
    text_draw(FS_SMALL, buf, 330, 7, COL_DIM);

    if (sel < 0 || sel >= count) return;
    int card = battle_deck_card(sel);
    int up = battle_deck_up(sel);
    int level = battle_deck_level(sel);
    const struct CardDef *c = &sts_cards[card];

    int can_more = strcmp(c->id, "Searing Blow") == 0 || !up;
    text_draw(FS_SMALL, up ? "当前（已升级）" : "当前", 48, 30, COL_DIM);
    draw_card(card, up, level, 36, 50, 110, 154, 1);
    if (can_more && c->type <= 2) {
        text_draw(FS_SMALL, "升级后", 268, 30, COL_GOLD);
        draw_card(card, 1, level + 1, 254, 50, 110, 154, 1);
        text_draw(FS_MID, "→", 190, 108, COL_LIT);
    } else {
        text_draw(FS_SMALL, "这张牌已经没有升级", 244, 108, COL_DIM);
    }

    int cost_before = battle_cost_of(c, up);
    int cost_after = battle_cost_of(c, 1);
    int dmg_before = c->damage + (up ? c->up_damage : 0);
    int dmg_after = c->damage + c->up_damage;
    if (strcmp(c->id, "Searing Blow") == 0) {
        dmg_before = battle_searing_damage(level);
        dmg_after = battle_searing_damage(level + 1);
    }
    int blk_before = c->block + (up ? c->up_block : 0);
    int blk_after = c->block + c->up_block;
    int magic_before = c->magic + (up ? c->up_magic : 0);
    int magic_after = c->magic + c->up_magic;

    snprintf(buf, sizeof(buf), "费 %d → %d", cost_before, can_more ? cost_after : cost_before);
    text_draw(FS_SMALL, buf, 10, 212, COL_INK);
    int x = 96;
    if (c->damage) {
        snprintf(buf, sizeof(buf), "伤害 %d → %d", dmg_before, can_more ? dmg_after : dmg_before);
        text_draw(FS_SMALL, buf, (float)x, 212, COL_INK);
        x += 98;
    }
    if (c->block) {
        snprintf(buf, sizeof(buf), "格挡 %d → %d", blk_before, can_more ? blk_after : blk_before);
        text_draw(FS_SMALL, buf, (float)x, 212, COL_INK);
        x += 98;
    }
    if (c->magic && !c->damage && !c->block) {
        snprintf(buf, sizeof(buf), "数值 %d → %d", magic_before, can_more ? magic_after : magic_before);
        text_draw(FS_SMALL, buf, (float)x, 212, COL_INK);
    }
}

static void draw_upgrade_screen(void) {
    char buf[64];
    int count = battle_deck_count();
    int page = s_upgrade_focus / 10;
    int first = page * 10;
    rect_fill(0, 0, BOT_W, BOT_H, RGB(22, 20, 18), 0.0f);
    rect_fill(0, 0, BOT_W, 22, RGBA(12, 12, 14, 235), 0.3f);
    text_draw(FS_SMALL, "锻造", 8, 3, COL_GOLD);
    snprintf(buf, sizeof(buf), "升级一张牌  %d/%d 页", page + 1, (count + 9) / 10);
    text_draw(FS_SMALL, buf, 62, 3, COL_DIM);

    for (int i = first; i < first + 10 && i < count; i++) {
        int rx, ry;
        upgrade_card_rect(i - first, &rx, &ry);
        int card_index = battle_deck_card(i);
        int up = battle_deck_up(i);
        draw_card(card_index, up, battle_deck_level(i), (float)rx, (float)ry, 54, 76, i == s_upgrade_focus);
        if (up) {
            rect_fill((float)rx, (float)ry, 54, 12, RGBA(8, 8, 10, 170), 0.7f);
            text_draw_depth(FS_SMALL, "已升级", (float)rx + 6, (float)ry + 1, COL_DIM, 0.72f);
        }
    }

    text_draw(FS_SMALL, count > 10 ? "←→↑↓ 选择  L/R 翻页  A 升级  B 返回"
                                  : "←→↑↓ 选择  A 升级  B 返回",
              8, 218, COL_DIM);
}

static void draw_end_screen(void) {
    int win = g_run.screen == SCREEN_VICTORY;
    text_draw(FS_BIG, win ? "通关" : "战败", 168, 90, win ? COL_GOLD : RGB(220, 90, 80));
    char buf[64];
    snprintf(buf, sizeof(buf), "到达第 %d 关  ·  牌组 %d 张", g_run.node + 1, g_run.deck_count);
    text_draw(FS_SMALL, buf, 90, 130, COL_DIM);
    if (g_map.message[0]) {
        text_draw_wrapped(FS_SMALL, g_map.message, 60, 152, 300, 2, COL_GOLD);
    }
    text_draw(FS_SMALL, "A 或点屏重新开始", 110, 176, COL_DIM);
}

/* AbstractDungeon's act transition: the boss is dead, the next act's map is
 * about to be built.  The boss gold and relic were granted when it died. */
static void draw_act_clear_top(void) {
    char buf[64];
    draw_background();
    snprintf(buf, sizeof(buf), "第 %d 幕 通过", g_run.act);
    float w = text_width(FS_BIG, buf);
    text_draw(FS_BIG, buf, (TOP_W - w) * 0.5f, 84.0f, COL_GOLD);
    if (g_map.message[0]) {
        float mw = text_width(FS_SMALL, g_map.message);
        text_draw(FS_SMALL, g_map.message, (TOP_W - mw) * 0.5f, 122.0f, COL_DIM);
    }
    snprintf(buf, sizeof(buf), "第 %d 幕", g_run.act + 1);
    w = text_width(FS_MID, buf);
    text_draw(FS_MID, buf, (TOP_W - w) * 0.5f, 150.0f, COL_INK);
    w = text_width(FS_SMALL, "A 继续");
    text_draw(FS_SMALL, "A 继续", (TOP_W - w) * 0.5f, 212.0f, COL_DIM);
}

/* ---- card-choice screen ------------------------------------------------ */

#define PICK_PER_PAGE 10

static const char *pick_title(int mode) {
    switch (mode) {
    case PICK_UPGRADE_HAND: return "升级一张牌";
    case PICK_EXHAUST_HAND: return "消耗一张牌";
    case PICK_TOP_HAND:     return "选择一张牌放回抽牌堆顶";
    case PICK_COPY_HAND:    return "复制一张牌";
    case PICK_EXHUME:       return "从消耗堆取回一张牌";
    case PICK_DISCOVERY:    return "发现：选择一张牌加入手牌";
    case PICK_DRAW_SKILL:   return "从抽牌堆选择一张技能牌";
    case PICK_DRAW_ATTACK:  return "从抽牌堆选择一张攻击牌";
    case PICK_BOTTOM_HAND:  return battle_pick_extra() < 0
                                   ? "选择任意张牌放到抽牌堆底部"
                                   : "选择一张牌放到抽牌堆底";
    case PICK_EXHAUST_MANY: {
        static char buf[48];
        snprintf(buf, sizeof(buf), "消耗最多 %d 张牌", battle_pick_extra());
        return buf;
    }
    default:                return "选择一张牌";
    }
}

/* Discovery offers three large cards; every other pile can hold more than ten,
 * so those use a 5x2 grid and L/R turns the page. */
static void pick_card_box(int slot, int mode, float *x, float *y, float *w, float *h) {
    if (mode == PICK_DISCOVERY) {
        *w = 84.0f;
        *h = *w * 1.4f;
        *x = (TOP_W - (3.0f * *w + 28.0f)) * 0.5f + (float)slot * (*w + 14.0f);
        *y = 66.0f;
        return;
    }
    *w = 64.0f;
    *h = *w * 1.4f;
    *x = 28.0f + (float)(slot % 5) * (*w + 6.0f);
    *y = 36.0f + (float)(slot / 5) * (*h + 10.0f);
}

static void draw_pick_top(void) {
    int mode = battle_pick_mode();
    int count = battle_pick_count();
    int per = mode == PICK_DISCOVERY ? 3 : PICK_PER_PAGE;
    int first = (s_pick_sel / per) * per;
    char buf[32];

    draw_background();
    rect_fill(0, 0, TOP_W, TOP_H, RGBA(10, 10, 12, 210), 0.20f);
    text_draw(FS_MID, pick_title(mode), 8, 4, COL_GOLD);
    snprintf(buf, sizeof(buf), "%d/%d", s_pick_sel + 1, count);
    text_draw(FS_SMALL, buf, TOP_W - text_width(FS_SMALL, buf) - 8.0f, 7, COL_DIM);

    for (int i = first; i < first + per && i < count; i++) {
        float x, y, w, h;
        pick_card_box(i - first, mode, &x, &y, &w, &h);
        draw_card(battle_pick_card(i), battle_pick_up(i), 0, x, y, w, h, i == s_pick_sel);
        if (battle_pick_multi() && battle_pick_marked(i)) {
            rect_fill(x + w - 14.0f, y + 2.0f, 12.0f, 12.0f, RGB(110, 180, 110), 0.35f);
        }
    }
}

static void draw_pick_bottom(void) {
    int mode = battle_pick_mode();
    int count = battle_pick_count();
    int sel = (s_pick_sel >= 0 && s_pick_sel < count) ? s_pick_sel : 0;
    int up = battle_pick_up(sel);
    const struct CardDef *c = &sts_cards[battle_pick_card(sel)];
    int per = mode == PICK_DISCOVERY ? 3 : PICK_PER_PAGE;
    char buf[96];

    text_draw(FS_MID, pick_title(mode), 8, 2, COL_GOLD);
    snprintf(buf, sizeof(buf), "%s%s", c->name, up ? "+" : "");
    text_draw(FS_MID, buf, 8, 26, COL_INK);
    snprintf(buf, sizeof(buf), "费 %d   %s", battle_cost_of(c, up), type_label(c->type));
    text_draw(FS_SMALL, buf, 8, 50, COL_DIM);
    text_draw_wrapped(FS_SMALL, up ? c->text_up : c->text, 8, 70, 304, 7,
                      RGB(228, 218, 198));
    if (battle_pick_multi()) {
        if (battle_pick_extra() < 0) {
            snprintf(buf, sizeof(buf), "已选 %d  A 选择  Y 确认  B 取消",
                     battle_pick_marked_count());
        } else if (count > per) {
            snprintf(buf, sizeof(buf), "已选 %d/%d  L/R 翻页  A 选择  Y 确认  B 取消",
                     battle_pick_marked_count(), battle_pick_extra());
        } else {
            snprintf(buf, sizeof(buf), "已选 %d/%d  A 选择  Y 确认  B 取消",
                     battle_pick_marked_count(), battle_pick_extra());
        }
    } else if (count > per) {
        snprintf(buf, sizeof(buf), "%d/%d  ←→↑↓ 选择  L/R 翻页  A 确认  B 取消",
                 sel + 1, count);
    } else {
        snprintf(buf, sizeof(buf), "%d/%d  ←→↑↓ 选择  A 确认  B 取消", sel + 1, count);
    }
    text_draw(FS_SMALL, buf, 8, 222, COL_DIM);
}

/* ---- diagnostics screens ------------------------------------------------ */

static u32 diag_state_color(int state) {
    if (state == DIAG_FAIL) return COL_HP;
    if (state == DIAG_WARN) return COL_GOLD;
    return RGB(150, 190, 140);
}

/* The top screen is 400x240.  Names and details get separate columns so a
 * long Chinese name cannot push its detail off the edge. */
static void diag_draw_top_panel(const char *title, const char *sub, const char *footer) {
    rect_fill(0.0f, 0.0f, TOP_W, TOP_H, RGB(9, 10, 12), 0.94f);
    text_draw_depth(FS_MID, title, 8, 3, COL_GOLD, 0.96f);
    if (sub) text_draw_depth(FS_SMALL, sub, 8, 25, COL_DIM, 0.96f);

    int y = 41;
    int shown = 0;
    /* Failures first: the panel only has room for 13 rows, and the one row
     * that matters must never be the one hidden after "还有 N 项". */
    static const int order[3] = {DIAG_FAIL, DIAG_WARN, DIAG_OK};
    for (int pass = 0; pass < 3 && shown < 13; pass++) {
        for (int i = 0; i < diag_row_count() && shown < 13; i++) {
            if (diag_row_state(i) != order[pass]) continue;
            int st = diag_row_state(i);
            char mark[4];
            snprintf(mark, sizeof(mark), "%s",
                     st == DIAG_FAIL ? "X" : (st == DIAG_WARN ? "!" : "+"));
            text_draw_depth(FS_SMALL, mark, 8, (float)y, diag_state_color(st), 0.96f);
            text_draw_depth(FS_SMALL, diag_row_name(i), 22, (float)y,
                            st == DIAG_FAIL ? COL_INK : COL_DIM, 0.96f);
            text_draw_depth(FS_SMALL, diag_row_detail(i), 220, (float)y, COL_DIM, 0.96f);
            y += 12;
            shown++;
        }
    }
    if (diag_row_count() == 0) {
        text_draw_depth(FS_SMALL, "没有检查项", 8, (float)y, COL_DIM, 0.96f);
    } else if (diag_row_count() > shown) {
        char more[64];
        snprintf(more, sizeof(more), "还有 %d 项，见日志", diag_row_count() - shown);
        text_draw_depth(FS_SMALL, more, 8, (float)y, COL_DIM, 0.96f);
    }
    text_draw_depth(FS_SMALL, footer, 8, 224, COL_DIM, 0.96f);
}

/* The bottom screen has the latest log lines: no need to take the SD card out
 * just to find out which stage failed. */
static void diag_draw_bottom_panel(const char *footer) {
    rect_fill(0.0f, 0.0f, BOT_W, BOT_H, RGB(9, 10, 12), 0.94f);
    text_draw_depth(FS_MID, "日志尾", 8, 2, COL_GOLD, 0.96f);
    int n = diag_tail_count();
    int y = 24;
    for (int i = 0; i < n; i++) {
        const char *line = diag_tail_line(i);
        if (!line || !line[0]) continue;
        text_draw_depth(FS_SMALL, line, 8, (float)y, COL_INK, 0.96f);
        y += 17;
        if (y > 190) break;
    }
    text_draw_depth(FS_SMALL, footer, 8, 222, COL_DIM, 0.96f);
}

static void draw_diag_overlay_top(void) {
    char sub[128];
    snprintf(sub, sizeof(sub), "阶段 %s   失败 %d   警告 %d",
             diag_last_stage(), diag_fail_count(), diag_warn_count());
    diag_draw_top_panel("诊断 / 自检", sub, "L+R+SELECT 或 B 关闭");
}

static void draw_diag_overlay_bottom(void) {
    char footer[96];
    snprintf(footer, sizeof(footer), "日志 %s", diag_log_path());
    diag_draw_bottom_panel(footer);
}

#if !defined(CARD_TEST)
static void diag_boot_screen(void) {
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_A | KEY_START | KEY_B)) break;

        char sub[128];
        snprintf(sub, sizeof(sub), "阶段 %s   失败 %d   警告 %d",
                 diag_last_stage(), diag_fail_count(), diag_warn_count());
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(s_top, RGB(8, 9, 10));
        C2D_SceneBegin(s_top);
        diag_draw_top_panel("STS 3DS 启动自检", sub, "A / START 继续");
        C2D_TargetClear(s_bottom, RGB(8, 9, 10));
        C2D_SceneBegin(s_bottom);
        diag_draw_bottom_panel("日志在 sdmc:/3ds/sts3ds/log.txt");
        C3D_FrameEnd(0);
    }
}
#endif

/* ---- startup checks ------------------------------------------------------ */

static void diag_check_asset(const char *name) {
    char path[96];
    const char *src = "romfs";
    snprintf(path, sizeof(path), "romfs:/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "sdmc:/3ds/sts3ds/%s", name);
        f = fopen(path, "rb");
        src = "sdmc";
    }
    diag_check(name, f != NULL, f ? src : "缺失");
    if (f) fclose(f);
}

static int diag_audio_present(const char *relative, char *detail, size_t cap) {
    char path[96];
    snprintf(path, sizeof(path), "romfs:/audio/%s", relative);
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        snprintf(detail, cap, "romfs");
        return 1;
    }
    snprintf(path, sizeof(path), "sdmc:/3ds/sts3ds/audio/%s", relative);
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        snprintf(detail, cap, "sdmc");
        return 1;
    }
    snprintf(detail, cap, "缺失");
    return 0;
}

static void diag_boot_checks(void) {
    static const char *const assets[] = {
        "font_12.t3x", "font_16.t3x", "font_20.t3x",
        "act1.t3x", "act2.t3x", "act3.t3x",
        "monsters.t3x", "combat.t3x", "ui.t3x", "map_paper.t3x",
        "relic_icons.t3x", "visuals.t3x",
    };
    for (unsigned i = 0; i < sizeof(assets) / sizeof(assets[0]); i++)
        diag_check_asset(assets[i]);
    for (int p = 0; p < STS_CARD_ART_PAGES && p < 4; p++) {
        char name[32];
        snprintf(name, sizeof(name), "cards_%03d.t3x", p);
        diag_check_asset(name);
    }

    /* A file can exist and still fail to decode; sheet_load() already tried
     * every one of them, so its counter is the authoritative load result. */
    int load_failures = sheet_load_failures();
    char load_detail[64];
    snprintf(load_detail, sizeof(load_detail), "失败 %d 个", load_failures);
    diag_check("资源加载", load_failures == 0, load_detail);

    /* The game must be able to write its own directory (save + logs). */
    const char *tmp = "sdmc:/3ds/sts3ds/diag.tmp";
    FILE *f = fopen(tmp, "wb");
    int sd_ok = 0;
    if (f) {
        sd_ok = fputc('x', f) != EOF;
        fclose(f);
        remove(tmp);
    }
    diag_check("SD 可写", sd_ok, sd_ok ? "" : "无法写 sdmc:/3ds/sts3ds");

    u32 lin = linearSpaceFree();
    char detail[64];
    snprintf(detail, sizeof(detail), "剩余 %lu KB", (unsigned long)(lin / 1024u));
    diag_check("线性内存", lin > 64u * 1024u, detail);

    u32 app = osGetMemRegionFree(MEMREGION_APPLICATION);
    snprintf(detail, sizeof(detail), "剩余 %lu KB", (unsigned long)(app / 1024u));
    diag_check("应用内存", app > 2u * 1024u * 1024u, detail);

    u32 firm = osGetFirmVersion();
    u32 kern = osGetKernelVersion();
    diag_log("firmware %lu.%lu.%lu, kernel %lu.%lu.%lu",
             (unsigned long)GET_VERSION_MAJOR(firm),
             (unsigned long)GET_VERSION_MINOR(firm),
             (unsigned long)GET_VERSION_REVISION(firm),
             (unsigned long)GET_VERSION_MAJOR(kern),
             (unsigned long)GET_VERSION_MINOR(kern),
             (unsigned long)GET_VERSION_REVISION(kern));
    diag_log("memtype %lu", (unsigned long)osGetApplicationMemType());
    diag_log("app free %lu KB, linear free %lu KB",
             (unsigned long)(app / 1024u), (unsigned long)(lin / 1024u));

    /* Music and effects prefer the copy packed into the 3dsx romfs; the SD
     * card is only a fallback.  The game is playable without either, so a
     * missing file is a warning, not a failure. */
    char audio_src[16];
    if (diag_audio_present("act1.pcm", audio_src, sizeof(audio_src)))
        diag_log("audio act1: %s", audio_src);
    else
        diag_warn("音乐文件", "act1.pcm 缺失，静音");
    if (diag_audio_present("sfx/card_play.pcm", audio_src, sizeof(audio_src)))
        diag_log("audio sfx: %s", audio_src);
    else
        diag_warn("音效文件", "sfx/card_play.pcm 缺失");
}

static void draw_title_top(void) {
    draw_background();
    rect_fill(0, 0, TOP_W, TOP_H, RGBA(6, 6, 8, 150), 0.2f);
    text_draw(FS_BIG, "杀戮尖塔", 128, 64, COL_GOLD);
    text_draw(FS_MID, "3DS", 178, 102, COL_INK);
    if (s_title_has_save)
        text_draw(FS_SMALL, "存档已找到", 160, 158, COL_DIM);
    else
        text_draw(FS_SMALL, "没有存档", 164, 158, COL_DIM);
    text_draw(FS_SMALL, "非官方同人移植 · 仅供学习", 106, 214, RGB(150, 142, 126));
}

static void draw_title_bottom(void) {
    rect_fill(0, 0, BOT_W, BOT_H, RGB(18, 17, 15), 0.0f);
    text_draw(FS_MID, "杀戮尖塔 · 3DS", 92, 46, COL_GOLD);
    text_draw(FS_SMALL, "选择一项开始", 116, 74, COL_DIM);

    int count = s_title_has_save ? 2 : 1;
    for (int i = 0; i < count; i++) {
        int rx, ry;
        title_rect(i, &rx, &ry);
        int lit = i == s_title_sel;
        rect_fill((float)rx, (float)ry, 220, 34, lit ? RGB(70, 56, 34) : RGB(40, 36, 32), 0.4f);
        rect_frame((float)rx, (float)ry, 220, 34, lit ? 2.0f : 1.0f,
                   lit ? COL_LIT : RGB(110, 100, 86), 0.45f);
        const char *label = s_title_has_save ? (i == 0 ? "继续游戏" : "新游戏") : "新游戏";
        text_draw(FS_MID, label, (float)rx + 80, (float)ry + 7, COL_INK);
    }
    text_draw(FS_SMALL, "↑↓ 选择  A 确认", 104, 216, COL_DIM);
}

static void title_choose(int index) {
    if (s_title_has_save && index == 0) {
        g_run.screen = SCREEN_MAP;
    } else {
        save_clear();
        run_start();
        g_run.screen = SCREEN_MAP;
    }
    s_last_screen = -1;
    reset_gestures();
}

static void scene_top(void) {
    C2D_TargetClear(s_top, RGB(16, 18, 20));
    C2D_SceneBegin(s_top);
    switch (g_run.screen) {
    case SCREEN_FIGHT:
        draw_fight_top();
        break;
    case SCREEN_MAP:
        /* The parchment fills the whole top screen, so it needs no panel. */
        draw_map_screen(0);
        break;
    case SCREEN_EVENT:
        draw_event_top();
        break;
    case SCREEN_SHOP:
        /* The rug fills the whole top screen, so it needs no panel. */
        draw_shop_top();
        break;
    case SCREEN_ACT_CLEAR:
        draw_act_clear_top();
        break;
    case SCREEN_PICK:
        draw_pick_top();
        break;
    case SCREEN_TITLE:
        draw_title_top();
        break;
    case SCREEN_REWARD:
        draw_reward_top();
        break;
    case SCREEN_CAMPFIRE:
        draw_campfire_top();
        break;
    case SCREEN_UPGRADE:
        draw_upgrade_top();
        break;
    case SCREEN_PURGE:
    case SCREEN_TREASURE:
    case SCREEN_VICTORY:
    case SCREEN_DEFEAT:
        rect_fill(0, 200, TOP_W, TOP_H - 200, RGB(32, 30, 26), 0.2f);
        if (g_run.screen == SCREEN_VICTORY || g_run.screen == SCREEN_DEFEAT) {
            draw_end_screen();
        } else if (g_run.screen == SCREEN_PURGE) {
            draw_purge_top();
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "%d/%d", g_battle.hp, g_battle.max_hp);
            text_draw(FS_SMALL, buf, 6, 1, RGB(232, 120, 110));
            snprintf(buf, sizeof(buf), "关 %d/%d", g_run.node + 1, run_node_count());
            text_draw(FS_SMALL, buf, 62, 1, COL_DIM);
        }
        break;
    }
    if (s_diag_overlay) draw_diag_overlay_top();
}

static void scene_bottom(void) {
    C2D_TargetClear(s_bottom, RGB(24, 22, 20));
    C2D_SceneBegin(s_bottom);
    switch (g_run.screen) {
    case SCREEN_FIGHT:
        draw_bottom_fight();
        break;
    case SCREEN_TITLE:
        draw_title_bottom();
        break;
    case SCREEN_REWARD:
        draw_reward_screen();
        break;
    case SCREEN_CAMPFIRE:
        draw_campfire_screen();
        break;
    case SCREEN_UPGRADE:
        draw_upgrade_screen();
        break;
    case SCREEN_MAP:
        draw_map_screen(1);
        break;
    case SCREEN_SHOP:
        draw_shop();
        break;
    case SCREEN_PURGE: {
        char buf[96];
        snprintf(buf, sizeof(buf), "选择要移除的牌   %d 金", g_map.purge_cost);
        text_draw(FS_MID, buf, 8, 2, COL_GOLD);
        int count = battle_deck_count();
        int page = s_purge_focus / 9;
        int first = page * 9;
        for (int i = first; i < first + 9 && i < count; i++) {
            float y = 28.0f + (float)(i - first) * 20.0f;
            int lit = i == s_purge_focus;
            rect_fill(8, y - 1, 304, 19, lit ? RGB(64, 54, 36) : COL_PANEL, 0.32f);
            if (lit) rect_frame(8, y - 1, 304, 19, 1.0f, COL_LIT, 0.33f);
            char name[48];
            snprintf(name, sizeof(name), "%s%s", sts_cards[battle_deck_card(i)].name,
                     battle_deck_up(i) ? "+" : "");
            text_draw(FS_SMALL, name, 14, y, COL_INK);
            snprintf(buf, sizeof(buf), "费 %d", battle_cost_of(&sts_cards[battle_deck_card(i)], battle_deck_up(i)));
            text_draw(FS_SMALL, buf, 210, y, COL_DIM);
        }
        text_draw(FS_SMALL, "↑↓ 选择  A 移除  B 返回", 8, 216, COL_DIM);
        break;
    }
    case SCREEN_EVENT:
        draw_event();
        break;
    case SCREEN_TREASURE:
        draw_treasure();
        break;
    case SCREEN_PICK:
        draw_pick_bottom();
        break;
    case SCREEN_ACT_CLEAR: {
        char buf[64];
        text_draw(FS_MID, "A 继续", 8, 6, COL_GOLD);
        snprintf(buf, sizeof(buf), "生命 %d/%d", g_battle.hp, g_battle.max_hp);
        text_draw(FS_SMALL, buf, 8, 40, COL_INK);
        snprintf(buf, sizeof(buf), "金币 %d   牌组 %d 张", g_map.gold, g_run.deck_count);
        text_draw(FS_SMALL, buf, 8, 62, COL_INK);
        break;
    }
    default:
        break;
    }
    if (s_detail && g_run.screen == SCREEN_FIGHT) draw_detail_panel();
    if (s_diag_overlay) draw_diag_overlay_bottom();
}

/* Konami code: 上上下下左右左右 B A B A. */
static const u32 KONAMI[12] = {
    KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_LEFT, KEY_RIGHT, KEY_B, KEY_A, KEY_B, KEY_A,
};

static void konami_feed(u32 kDown) {
    if (!kDown) return;
    u32 pressed = 0;
    for (int i = 0; i < 12; i++) {
        if (kDown & KONAMI[i]) {
            pressed = KONAMI[i];
            break;
        }
    }
    if (!pressed) return;

    if (pressed == KONAMI[s_konami_pos]) {
        s_konami_pos++;
        if (s_konami_pos == 12) {
            s_konami_pos = 0;
            int on = battle_toggle_cheat();
            snprintf(s_toast, sizeof(s_toast), on ? "作弊开启：伤害 ×3" : "作弊关闭");
            s_toast_ttl = 180;
        }
    } else {
        s_konami_pos = (pressed == KONAMI[0]) ? 1 : 0;
    }
}

static void reset_gestures(void) {
    s_drag.active = 0;
    s_drag.activated = 0;
    s_press_end = 0;
    s_press_reward = -1;
    s_press_campfire = -1;
    s_press_restart = 0;
    s_press_advance = 0;
    s_press_skip = 0;
    s_swallow_touch = 0;
    s_play_fx.active = 0;
}

static void handle_touch_fight(touchPosition *touch, int down) {
    /* A tap that dismissed or switched the status panel must not leak into the
     * buttons underneath it (the old overlap closed "药水" and then ended the
     * turn on the same press). */
    if (!down && s_swallow_touch) {
        s_swallow_touch = 0;
        return;
    }
    if (s_swallow_touch) return;
    if (down) {
        if (s_drag.active) {
            int rise = s_drag.start_y - touch->py;
            s_drag.activated = rise >= TOUCH_ACTIVATE_PX;
            s_drag.rise = rise;
            s_drag.x = touch->px;
            s_drag.y = touch->py;
            if (s_drag.activated) {
                int steps = (touch->px - s_drag.start_x) / TOUCH_SWITCH_PX;
                int picked = s_drag.lock + steps;
                if (picked < 0) picked = 0;
                if (picked >= g_battle.enemy_count) picked = g_battle.enemy_count - 1;
                s_drag.target = picked;
                s_target = picked;
            }
            return;
        }
        if (s_detail) {
            s_swallow_touch = 1;
            int tab = in_detail_tab(touch->px, touch->py);
            if (tab >= 0) {
                s_panel_tab = tab;
                s_potion_sel = 0;
                s_panel_scroll = 0;
            } else {
                s_detail = 0;
            }
            return;
        }
        if (s_press_end) return;
        if (in_status_button(touch->px, touch->py)) {
            s_detail = 1;
            s_panel_tab = 0;
            s_panel_scroll = 0;
            return;
        }
        if (g_battle.phase != PHASE_PLAYER) return;
        if (in_end_turn(touch->px, touch->py)) {
            s_press_end = 1;
            return;
        }
        int slot;
        if (hand_hit(touch->px, touch->py, &slot)) {
            s_drag.active = 1;
            s_drag.card = slot;
            s_drag.start_x = touch->px;
            s_drag.start_y = touch->py;
            s_drag.lock = nearest_enemy(touch->px * TOP_W / BOT_W);
            s_drag.target = s_drag.lock;
            s_drag.activated = 0;
            s_drag.rise = 0;
            s_drag.x = touch->px;
            s_drag.y = touch->py;
            s_focus = slot;
        }
        return;
    }

    if (s_press_end) {
        s_press_end = 0;
        if (in_end_turn(touch->px, touch->py)) battle_end_turn();
        return;
    }
    if (s_drag.active) {
        int cancel = 0;
        if (s_drag.activated) {
            int slot;
            /* Released back over the fan: put the card down instead of playing it. */
            if (hand_hit(touch->px, touch->py, &slot)) cancel = 1;
        }
        if (s_drag.activated && !cancel) {
            int inside = touch->px > TOUCH_EDGE_PX && touch->px < BOT_W - TOUCH_EDGE_PX &&
                         touch->py > TOUCH_EDGE_PX && touch->py < BOT_H - TOUCH_EDGE_PX;
            if (inside && s_drag.card < g_battle.hand_count) {
                start_play_fx(s_drag.card);
                play_card(s_drag.card, s_drag.target);
                if (s_focus >= g_battle.hand_count) {
                    s_focus = g_battle.hand_count > 0 ? g_battle.hand_count - 1 : 0;
                }
            }
        }
        s_drag.active = 0;
        s_drag.activated = 0;
    }
}

#if defined(CARD_TEST)
/* Azahar ignores synthesised mouse input (GRenderWindow returns early for
 * Qt::MouseEventSynthesizedBySystem), and Windows refuses touch injection in
 * this session, so the touch controls cannot be driven from the host.  These
 * helpers feed the same touchPosition path the hardware uses; X starts a drag
 * from the third card, Y releases it on the first enemy, B releases it back
 * over the fan.  CARD_TEST only: the shipping build never compiles this. */
static void card_test_touch_begin(void) {
    if (g_run.screen != SCREEN_FIGHT || s_drag.active) return;
    if (g_battle.phase != PHASE_PLAYER || g_battle.hand_count < 3) return;
    float x, y, w, h;
    hand_slot(2, &x, &y, &w, &h);
    touchPosition t = {0};
    t.px = (u16)(x + w * 0.5f);
    t.py = (u16)(y + h * 0.5f);
    handle_touch_fight(&t, 1);
    if (!s_drag.active) return;
    /* Pull far enough left to switch the target to the first enemy. */
    t.px = 100;
    t.py = 56;
    handle_touch_fight(&t, 1);
    s_drag.target = 0;
    s_target = 0;
}

static void card_test_touch_release(int cancel) {
    if (!s_drag.active) return;
    touchPosition t = {0};
    if (cancel) {
        float x, y, w, h;
        hand_slot(2, &x, &y, &w, &h);
        t.px = (u16)(x + w * 0.5f);
        t.py = (u16)(y + h * 0.5f);
    } else {
        t.px = (u16)s_drag.x;
        t.py = (u16)s_drag.y;
    }
    handle_touch_fight(&t, 0);
}
#endif

static void handle_touch(void) {
    static touchPosition touch = {0};
    int down = (hidKeysHeld() & KEY_TOUCH) != 0;
    if (down) hidTouchRead(&touch);

    switch (g_run.screen) {
    case SCREEN_FIGHT:
        handle_touch_fight(&touch, down);
        break;
    case SCREEN_TITLE:
        if (down) {
            if (s_press_reward < 0) s_press_reward = title_hit(touch.px, touch.py);
        } else if (s_press_reward >= 0) {
            int hit = title_hit(touch.px, touch.py);
            if (hit == s_press_reward) title_choose(hit);
            s_press_reward = -1;
        }
        break;
    case SCREEN_REWARD:
        if (down) {
            if (s_press_skip == 0 && in_skip_reward(touch.px, touch.py)) {
                s_press_skip = 1;
            } else if (s_press_reward < 0) {
                s_press_reward = reward_hit(touch.px, touch.py);
            }
        } else {
            if (s_press_skip > 0) {
                if (in_skip_reward(touch.px, touch.py)) battle_skip_reward();
                s_press_skip = 0;
            } else if (s_press_reward >= 0) {
                int hit = reward_hit(touch.px, touch.py);
                if (hit == s_press_reward) battle_choose_reward(hit);
                s_press_reward = -1;
            }
        }
        break;
    case SCREEN_UPGRADE: {
        static int press_row = -1;
        if (down) {
            if (press_row < 0) {
                int page = s_upgrade_focus / 10;
                int first = page * 10;
                for (int i = first; i < first + 10 && i < battle_deck_count(); i++) {
                    int rx, ry;
                    upgrade_card_rect(i - first, &rx, &ry);
                    if (touch.px >= rx && touch.px < rx + 54 &&
                        touch.py >= ry && touch.py < ry + 76) {
                        press_row = i;
                        s_upgrade_focus = i;
                        break;
                    }
                }
            }
        } else if (press_row >= 0) {
            int rx, ry;
            upgrade_card_rect(press_row - (s_upgrade_focus / 10) * 10, &rx, &ry);
            if (touch.px >= rx && touch.px < rx + 54 &&
                touch.py >= ry && touch.py < ry + 76) {
                battle_confirm_upgrade(press_row);
            }
            press_row = -1;
        }
        break;
    }
    case SCREEN_CAMPFIRE:
        if (down) {
            if (s_press_campfire < 0) s_press_campfire = campfire_hit(touch.px, touch.py);
        } else if (s_press_campfire >= 0) {
            int hit = campfire_hit(touch.px, touch.py);
            if (hit == s_press_campfire) battle_campfire(hit);
            s_press_campfire = -1;
        }
        break;
    case SCREEN_ACT_CLEAR:
        if (down) {
            s_press_advance = 1;
        } else if (s_press_advance) {
            run_advance_act();
        }
        break;
    case SCREEN_PICK:
        /* Touch would otherwise fall through to the "tap to restart" default;
         * the picker is driven by the buttons. */
        break;
    default:
        if (down) {
            s_press_restart = 1;
        } else if (s_press_restart) {
            run_restart();
            reset_gestures();
            s_focus = 0;
        }
        break;
    }
}

/* Every player-side card play goes through here so a rejection gets shown
 * instead of silently doing nothing. */
static void play_card(int slot, int target) {
    int r = battle_play(slot, target);
    if (r >= 0) {
        audio_sfx(AUDIO_SFX_CARD_PLAY);
        return;
    }
    audio_sfx(AUDIO_SFX_CARD_REJECT);
    const char *msg = "这张牌现在不能打出";
    if (r == -3) msg = "能量不足";
    else if (r == -4) msg = "没有合法目标";
    else if (r == -5) msg = "手牌里还有非攻击牌";
    else if (r == -6) msg = "本回合已打出三张牌（异常）";
    else if (r == -7) msg = "被缠身：本回合不能打出攻击牌";
    snprintf(s_toast, sizeof(s_toast), "%s", msg);
    s_toast_ttl = 120;
}

static void handle_buttons(u32 kDown) {
    /* Diagnostics are always reachable, even on a screen whose own keys are
     * broken.  The combo is checked before the screen switch so L/R cannot
     * page a list while the overlay is being opened. */
    if ((hidKeysHeld() & (KEY_L | KEY_R | KEY_SELECT)) == (KEY_L | KEY_R | KEY_SELECT)) {
        if (kDown & KEY_SELECT) {
            s_diag_overlay = !s_diag_overlay;
            diag_log("diagnostic overlay %s", s_diag_overlay ? "on" : "off");
        }
        return;
    }
    if (s_diag_overlay) {
        if (kDown & (KEY_B | KEY_SELECT | KEY_START)) s_diag_overlay = 0;
        return;
    }

    switch (g_run.screen) {
    case SCREEN_TITLE: {
        int count = s_title_has_save ? 2 : 1;
        if (kDown & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
            s_title_sel = (s_title_sel + 1) % count;
        if (kDown & KEY_A) title_choose(s_title_sel);
        break;
    }
    case SCREEN_FIGHT:
        if (s_detail) {
            if (kDown & (KEY_L | KEY_R)) {
                s_panel_tab = 1 - s_panel_tab;
                s_potion_sel = 0;
                s_panel_scroll = 0;
            }
            if (s_panel_tab == 0) {
                StatusRow rows[STATUS_ROW_MAX];
                int count = status_collect(rows, STATUS_ROW_MAX);
                int max_scroll = count > STATUS_VISIBLE ? count - STATUS_VISIBLE : 0;
                if (kDown & KEY_UP) {
                    if (s_panel_scroll > 0) s_panel_scroll--;
                }
                if (kDown & KEY_DOWN) {
                    if (s_panel_scroll < max_scroll) s_panel_scroll++;
                }
                if (s_panel_scroll > max_scroll) s_panel_scroll = max_scroll;
            }
            if (s_panel_tab == 1) {
                int count = battle_potion_count();
                if (count > 0) {
                    if (kDown & KEY_UP) s_potion_sel = (s_potion_sel + count - 1) % count;
                    if (kDown & KEY_DOWN) s_potion_sel = (s_potion_sel + 1) % count;
                    if (s_potion_sel >= count) s_potion_sel = 0;
                    if (kDown & KEY_A) {
                        int id = battle_potion_at(s_potion_sel);
                        if (id == POTION_FIRE) {
                            s_pending_potion = s_potion_sel;
                            s_detail = 0;
                            s_target_mode = 1;
                            s_target = nearest_enemy((int)(enemy_x(0) + enemy_w(0) * 0.5f));
                        } else {
                            battle_use_potion(s_potion_sel, -1);
                            s_potion_sel = 0;
                        }
                    }
                }
            }
            if (kDown & (KEY_X | KEY_B)) s_detail = 0;
            return;
        }
        if (s_pending_potion >= 0) {
            if (kDown & (KEY_LEFT | KEY_L)) {
                if (s_target > 0) s_target--;
            }
            if (kDown & (KEY_RIGHT | KEY_R)) {
                if (s_target + 1 < g_battle.enemy_count) s_target++;
            }
            if (kDown & KEY_B) {
                s_pending_potion = -1;
                s_target_mode = 0;
            } else if (kDown & KEY_A) {
                battle_use_potion(s_pending_potion, s_target);
                s_pending_potion = -1;
                s_target_mode = 0;
            }
            return;
        }
        if (kDown & KEY_X) {
            s_detail = !s_detail;
            s_panel_scroll = 0;
            return;
        }
        if (kDown & KEY_Y) {
            battle_end_turn();
            s_focus = 0;
            s_target_mode = 0;
            return;
        }
        if (kDown & (KEY_LEFT | KEY_L)) {
            if (s_target_mode) {
                if (s_target > 0) s_target--;
            } else if (s_focus > 0) {
                s_focus--;
            }
        }
        if (kDown & (KEY_RIGHT | KEY_R)) {
            if (s_target_mode) {
                if (s_target + 1 < g_battle.enemy_count) s_target++;
            } else if (s_focus + 1 < g_battle.hand_count) {
                s_focus++;
            }
        }
        if (kDown & KEY_B) s_target_mode = 0;
        if (kDown & KEY_A) {
            if (g_battle.phase != PHASE_PLAYER || s_focus >= g_battle.hand_count) return;
            if (s_target_mode) {
                start_play_fx(s_focus);
                play_card(s_focus, s_target);
                s_target_mode = 0;
                if (s_focus >= g_battle.hand_count) s_focus = g_battle.hand_count > 0 ? g_battle.hand_count - 1 : 0;
            } else if (battle_card_targeted(s_focus)) {
                s_target_mode = 1;
                s_target = nearest_enemy((int)(enemy_x(0) + enemy_w(0) * 0.5f));
            } else {
                start_play_fx(s_focus);
                play_card(s_focus, -1);
                if (s_focus >= g_battle.hand_count) s_focus = g_battle.hand_count > 0 ? g_battle.hand_count - 1 : 0;
            }
        }
        break;

    case SCREEN_REWARD:
        if (kDown & (KEY_LEFT | KEY_L)) {
            if (s_reward_focus > 0) s_reward_focus--;
        }
        if (kDown & (KEY_RIGHT | KEY_R)) {
            if (s_reward_focus + 1 < g_run.reward_count) s_reward_focus++;
        }
        if (kDown & KEY_B) battle_skip_reward();
        if (kDown & KEY_A) battle_choose_reward(s_reward_focus);
        break;

    case SCREEN_CAMPFIRE:
        if (kDown & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))
            s_campfire_focus = 1 - s_campfire_focus;
        if (kDown & KEY_A) battle_campfire(s_campfire_focus);
        break;

    case SCREEN_UPGRADE: {
        int count = battle_deck_count();
        int step = 0;
        if (kDown & KEY_LEFT) step = -1;
        if (kDown & KEY_RIGHT) step = 1;
        if (kDown & KEY_UP) step = -5;
        if (kDown & KEY_DOWN) step = 5;
        if (kDown & KEY_L) step = -10;
        if (kDown & KEY_R) step = 10;
        if (step) {
            s_upgrade_focus += step;
            if (s_upgrade_focus < 0) s_upgrade_focus = 0;
            if (s_upgrade_focus >= count) s_upgrade_focus = count > 0 ? count - 1 : 0;
        }
        if (kDown & KEY_B) battle_cancel_upgrade();
        if (kDown & KEY_A) battle_confirm_upgrade(s_upgrade_focus);
        break;
    }

    case SCREEN_MAP: {
        /* Debug hooks: SELECT opens a shop, X cycles the events. */
        if (kDown & KEY_X) {
            static int debug_event;
            g_map.event_id = debug_event % STS_EVENT_COUNT;
            debug_event++;
            g_map.event_done = 0;
            g_run.screen = SCREEN_EVENT;
            s_event_sel = 0;
            break;
        }
        if (kDown & KEY_SELECT) {
            map_prepare_shop();
            g_run.screen = SCREEN_SHOP;
            s_shop_page = 0;
            s_shop_sel = 0;
            break;
        }
        if (kDown & KEY_Y) {
            g_run.act = (g_run.act >= 2) ? 1 : 2;
            g_run.node = -1;
            map_generate();
            break;
        }
        /* B twice starts over.  The first press only arms it, so a stray tap
         * on the map cannot throw a run away. */
        if (kDown & KEY_B) {
            if (s_new_game_arm > 0) {
                s_new_game_arm = 0;
                save_clear();   /* do not leave the abandoned run behind */
                run_start();
                s_last_screen = -1;   /* reset the per-screen cursors */
                s_map_sel = 0;
                break;
            }
            s_new_game_arm = 120;
            snprintf(g_map.message, sizeof(g_map.message),
                     "再按一次 B 放弃本局，重新开始");
            break;
        }
        if (s_new_game_arm > 0 && --s_new_game_arm == 0) g_map.message[0] = 0;
        /* Up/down pan the tall map; left/right pick the next room. */
        if (kDown & KEY_UP) s_map_view = map_clamp_view(s_map_view - MAP_PAN_STEP);
        if (kDown & KEY_DOWN) s_map_view = map_clamp_view(s_map_view + MAP_PAN_STEP);
        int cand[4];
        int count = map_candidates(cand, 4);
        if (count > 0) {
            if (kDown & (KEY_LEFT | KEY_L)) {
                s_map_sel = (s_map_sel + count - 1) % count;
            }
            if (kDown & (KEY_RIGHT | KEY_R)) {
                s_map_sel = (s_map_sel + 1) % count;
            }
            if (kDown & KEY_A) {
                map_enter(g_map.row + 1, cand[s_map_sel % count]);
                s_map_sel = 0;
            }
        }
        break;
    }

    case SCREEN_SHOP: {
        /* The shop's fourteen items live in two rows on the top screen: the five
         * tinted cards, then everything else (two colourless cards, three relics,
         * three potions, the purge service).  The game walks them with a spatial
         * cursor (ShopScreen.setCursorPosition); a row/column grid reads the same
         * on the small screen and keeps LEFT/RIGHT meaningful. */
        if (kDown & (KEY_LEFT | KEY_L)) {
            int col = shop_col(s_shop_sel);
            if (col > 0) s_shop_sel = shop_index(shop_row(s_shop_sel), col - 1);
        }
        if (kDown & (KEY_RIGHT | KEY_R)) {
            s_shop_sel = shop_index(shop_row(s_shop_sel), shop_col(s_shop_sel) + 1);
        }
        if (kDown & KEY_DOWN) {
            if (shop_row(s_shop_sel) == 0) s_shop_sel = shop_index(1, shop_col(s_shop_sel));
        }
        if (kDown & KEY_UP) {
            if (shop_row(s_shop_sel) == 1) s_shop_sel = shop_index(0, shop_col(s_shop_sel));
        }
        if (kDown & KEY_A) {
            if (s_shop_sel < 7) {
                map_buy(s_shop_sel);
            } else if (s_shop_sel < 10) {
                map_buy_relic(s_shop_sel - 7);
            } else if (s_shop_sel < 13) {
                map_buy_potion(s_shop_sel - 10);
            } else if (!g_map.purge_done && g_map.gold >= g_map.purge_cost) {
                s_purge_focus = 0;
                g_run.screen = SCREEN_PURGE;
            } else {
                snprintf(g_map.message, sizeof(g_map.message), "金币不足");
            }
        }
        if (kDown & KEY_B) map_after_node();
        break;
    }

    case SCREEN_PURGE: {
        int count = battle_deck_count();
        if (kDown & KEY_UP) {
            if (s_purge_focus > 0) s_purge_focus--;
        }
        if (kDown & KEY_DOWN) {
            if (s_purge_focus + 1 < count) s_purge_focus++;
        }
        if (kDown & KEY_A) {
            /* map_purge() routes back to the event or the shop itself. Do not
             * overwrite that here: the Cleric's purify must return to its
             * result page, not to a shop screen that is not in the stack. */
            map_purge(s_purge_focus);
            if (s_purge_focus >= battle_deck_count()) s_purge_focus = 0;
        }
        if (kDown & KEY_B) {
            g_run.screen = g_map.purge_from_event ? SCREEN_EVENT : SCREEN_SHOP;
        }
        break;
    }

    case SCREEN_EVENT:
        if (g_map.event_done) {
            if (kDown & KEY_A) map_after_node();
        } else {
            if (kDown & (KEY_UP | KEY_LEFT)) {
                if (s_event_sel > 0) s_event_sel--;
            }
            if (kDown & (KEY_DOWN | KEY_RIGHT)) {
                if (s_event_sel < 2) s_event_sel++;
            }
            if (kDown & KEY_A) map_event_choose(s_event_sel);
        }
        break;

    case SCREEN_TREASURE:
        if (kDown & KEY_A) map_after_node();
        break;

    case SCREEN_ACT_CLEAR:
        if (kDown & KEY_A) run_advance_act();
        break;

    case SCREEN_PICK: {
        int count = battle_pick_count();
        int per = battle_pick_mode() == PICK_DISCOVERY ? 3 : PICK_PER_PAGE;
        int per_row = battle_pick_mode() == PICK_DISCOVERY ? 3 : 5;
        if (count <= 0) break;
        if (s_pick_sel >= count) s_pick_sel = count - 1;
        if (kDown & KEY_LEFT) {
            if (s_pick_sel > 0) s_pick_sel--;
        }
        if (kDown & KEY_RIGHT) {
            if (s_pick_sel + 1 < count) s_pick_sel++;
        }
        if (kDown & KEY_UP) {
            s_pick_sel -= per_row;
            if (s_pick_sel < 0) s_pick_sel = 0;
        }
        if (kDown & KEY_DOWN) {
            s_pick_sel += per_row;
            if (s_pick_sel >= count) s_pick_sel = count - 1;
        }
        if (kDown & KEY_L) {
            int page = s_pick_sel / per;
            if (page > 0) s_pick_sel = (page - 1) * per;
        }
        if (kDown & KEY_R) {
            int page = s_pick_sel / per;
            if ((page + 1) * per < count) s_pick_sel = (page + 1) * per;
        }
        if (kDown & KEY_B) battle_pick_cancel();
        if (battle_pick_multi()) {
            if (kDown & KEY_A) battle_pick_toggle(s_pick_sel);
            if (kDown & KEY_Y) battle_pick_confirm_multi();
        } else if (kDown & KEY_A) {
            battle_pick_choose(s_pick_sel);
        }
        break;
    }

    default:
        if (kDown & KEY_A) {
            run_restart();
            reset_gestures();
            s_focus = 0;
        }
        break;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    diag_init();
    diag_stage("romfsInit");
    Result rc = romfsInit();
    diag_log("romfsInit result 0x%08lx", (unsigned long)rc);
    if (R_FAILED(rc)) diag_check("romfsInit", 0, "失败，改用 SD 资源");

    diag_stage("gfxInitDefault");
    gfxInitDefault();
    diag_stage("C3D_Init");
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    diag_stage("C2D_Init");
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    diag_stage("C2D_Prepare");
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    diag_stage("font_init");
    font_init();
    diag_stage("load_art");
    load_art();
    diag_stage("audio_init");
    audio_init();

    char adbg[160];
    audio_debug(adbg, sizeof(adbg));
    diag_log("music: %s", adbg);
    audio_sfx_debug(adbg, sizeof(adbg));
    diag_log("sfx: %s", adbg);

    diag_stage("save_load");
#if defined(CARD_TEST)
    run_start();   /* the self-test starts from a known state, never a save */
    diag_log("CARD_TEST: run_start");
#else
    int loaded = save_load();
    diag_log("save_load: %d", loaded);
    s_title_has_save = loaded;
    s_title_sel = 0;
    if (!loaded) {
        /* Do not create a run (and a save file) just to show the title. */
        memset(&g_run, 0, sizeof(g_run));
        memset(&g_battle, 0, sizeof(g_battle));
        g_run.act = 1;
        g_run.potion_gain = -1;
        g_battle.hp = 80;
        g_battle.max_hp = 80;
    }
    g_run.screen = SCREEN_TITLE;
#endif

    diag_stage("boot checks");
    diag_boot_checks();
#if !defined(CARD_TEST)
    hidScanInput();
    if (diag_fail_count() > 0 || (hidKeysHeld() & KEY_SELECT)) {
        diag_log("boot screen: failures=%d warnings=%d",
                 diag_fail_count(), diag_warn_count());
        diag_boot_screen();
    }
#endif

#if defined(CARD_TEST)
    /* build.sh CARD_TEST=1: run the card assertions and show the report
     * instead of the game.  The screenshot of this screen is the evidence. */
    card_test_run();
#if defined(UI_DEMO)
    card_test_demo_ui(UI_DEMO);
#endif
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            diag_log("CARD_TEST: START exit");
            break;
        }
        /* X/Y/SELECT pick one of the three picker layouts; inside the preview
         * the real picker input runs, so the cursor, paging and confirm keys
         * are exercised too. */
        /* Inside a preview the real battle/picker keys must win: Y ends the
         * turn there, so it cannot also mean "show the Discovery demo". */
        if (g_run.screen == SCREEN_FIGHT) {
            if (kDown & KEY_SELECT) card_test_demo_exit();
            else if (kDown & KEY_R) s_detail = !s_detail;  /* CARD_TEST: status list */
            else if (kDown & KEY_X) card_test_touch_begin();
            else if (s_drag.active && (kDown & KEY_Y)) card_test_touch_release(0);
            else if (s_drag.active && (kDown & KEY_B)) card_test_touch_release(1);
            else handle_buttons(kDown);
        } else if (g_run.screen == SCREEN_PICK || g_run.screen == SCREEN_EVENT ||
                   g_run.screen == SCREEN_UPGRADE || g_run.screen == SCREEN_REWARD ||
                   g_run.screen == SCREEN_CAMPFIRE) {
            if ((g_run.screen == SCREEN_EVENT || g_run.screen == SCREEN_UPGRADE ||
                 g_run.screen == SCREEN_REWARD || g_run.screen == SCREEN_CAMPFIRE) &&
                (kDown & KEY_SELECT)) card_test_demo_exit();
            else handle_buttons(kDown);
        } else {
            if (kDown & KEY_X) card_test_demo_pick(0);
            if (kDown & KEY_Y) card_test_demo_pick(1);
            if (kDown & KEY_SELECT) card_test_demo_pick(2);
            /* R/L show the act 2 boss/elite, the D-Pad the four normal
             * encounters; SELECT returns to this report. */
            if (kDown & KEY_R) card_test_demo_fight(0);
            if (kDown & KEY_L) card_test_demo_fight(1);
            if (kDown & KEY_UP) card_test_demo_fight(2);
            if (kDown & KEY_DOWN) card_test_demo_fight(3);
            if (kDown & KEY_LEFT) card_test_demo_fight(4);
            if (kDown & KEY_RIGHT) card_test_demo_fight(5);
            if (kDown & KEY_A) card_test_demo_fight(6);   /* three Slavers */
            /* B cycles the set pieces: Automaton (7), Collector (8), Gremlin
             * Leader (9), act 3 background (10), then Slime Boss (11), Nob
             * (12), Acid Slimes (13), Jaw Worm (14), the split slimes (15),
             * then the last four powers on Acid Slimes (18), Purity's
             * multi-select (19) and Forethought's bottom-pick (20).  The two
             * draw-pile pickers (16, 17) are skipped here: they end on
             * SCREEN_PICK, where B only cancels the picker, so B cannot cycle
             * past them. */
            if (kDown & KEY_B) {
                card_test_demo_fight(s_boss_demo);
                s_boss_demo++;
                if (s_boss_demo == 16 || s_boss_demo == 17) s_boss_demo = 18;
                if (s_boss_demo > 20) s_boss_demo = 7;
            }
        }
        update_play_fx();
        update_floaters();
        update_hud_anim();
        if (s_toast_ttl > 0) s_toast_ttl--;
        audio_set_act(g_run.act);
        audio_update();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        if (g_run.screen == SCREEN_PICK || g_run.screen == SCREEN_FIGHT ||
            g_run.screen == SCREEN_EVENT || g_run.screen == SCREEN_UPGRADE ||
            g_run.screen == SCREEN_REWARD || g_run.screen == SCREEN_CAMPFIRE) {
            s_card_seq = 0;
            scene_top();
            scene_bottom();
        } else {
            card_test_draw(s_top);
            C2D_TargetClear(s_bottom, RGB(10, 12, 14));
            C2D_SceneBegin(s_bottom);
        }
        C3D_FrameEnd(0);
    }
    audio_exit();
    for (int i = 0; i < 4; i++) {
        if (s_art_sheets[i]) C2D_SpriteSheetFree(s_art_sheets[i]);
    }
    if (s_relic_sheet) C2D_SpriteSheetFree(s_relic_sheet);
    if (s_hud_sheet) C2D_SpriteSheetFree(s_hud_sheet);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    diag_shutdown();
    return 0;
#endif

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            diag_log("exit: START on screen %d", g_run.screen);
            save_write();
            break;
        }

        if (g_run.screen != s_last_screen) {
            reset_gestures();
            s_last_screen = g_run.screen;
            s_focus = 0;
            s_target_mode = 0;
            s_reward_focus = 0;
            s_campfire_focus = 0;
            if (s_upgrade_focus >= battle_deck_count()) s_upgrade_focus = 0;
            s_map_sel = 0;
            s_map_last_row = -1;   /* centre the map on the current room again */
            s_shop_sel = 0;
            s_shop_page = 0;
            s_event_sel = 0;
            s_pick_sel = 0;
            s_new_game_arm = 0;
            if (g_run.screen == SCREEN_FIGHT && g_run.upgrade_name[0]) {
                snprintf(s_toast, sizeof(s_toast), "%s", g_run.upgrade_name);
                s_toast_ttl = 150;
                g_run.upgrade_name[0] = 0;
            }
        }
        update_map_scroll();
        if (s_toast_ttl > 0) s_toast_ttl--;
        if (s_focus >= g_battle.hand_count) {
            s_focus = g_battle.hand_count > 0 ? g_battle.hand_count - 1 : 0;
        }

        konami_feed(kDown);
        s_debug_layers = (hidKeysHeld() & KEY_SELECT) != 0;
        if (!s_diag_overlay) handle_touch();
        handle_buttons(kDown);
        update_floaters();
        update_play_fx();
        update_hud_anim();
        audio_set_act(g_run.act);
        audio_update();

        s_card_seq = 0;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        scene_top();
        scene_bottom();
        C3D_FrameEnd(0);
    }

    audio_exit();
    for (int i = 0; i < 4; i++) {
        if (s_art_sheets[i]) C2D_SpriteSheetFree(s_art_sheets[i]);
    }
    if (s_relic_sheet) C2D_SpriteSheetFree(s_relic_sheet);
    if (s_hud_sheet) C2D_SpriteSheetFree(s_hud_sheet);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    diag_shutdown();
    return 0;
}
