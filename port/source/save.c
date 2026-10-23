#include "save.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cards_gen.h"
#include "battle.h"
#include "run.h"

#define SAVE_PATH "sdmc:/3ds/sts3ds/save.dat"
#define SAVE_TMP  "sdmc:/3ds/sts3ds/save.tmp"
#define SAVE_VERSION 1u

typedef struct {
    char magic[4];
    unsigned int version;
    Run run;
    MapState map;
    int hp;
    int max_hp;
    unsigned int battle_rng;
    unsigned int checksum;
} SaveBlob;

static unsigned int save_checksum(const SaveBlob *blob) {
    const unsigned char *p = (const unsigned char *)blob;
    unsigned int hash = 2166136261u;
    for (size_t i = 0; i < offsetof(SaveBlob, checksum); i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static int save_valid(const SaveBlob *b) {
    if (memcmp(b->magic, "STS3", 4) != 0) return 0;
    if (b->version != SAVE_VERSION) return 0;
    if (b->checksum != save_checksum(b)) return 0;
    if (b->run.act < 1 || b->run.act > 3) return 0;
    if (b->run.screen != SCREEN_MAP) return 0;
    if (b->run.deck_count < 0 || b->run.deck_count > 64) return 0;
    if (b->run.relic_count < 0 || b->run.relic_count > 8) return 0;
    if (b->run.potion_count < 0 || b->run.potion_count > 3) return 0;
    for (int i = 0; i < b->run.deck_count; i++) {
        int entry = b->run.deck[i];
        if (entry & ~0x1FF) return 0;
        if (CARD_INDEX(entry) >= STS_CARD_COUNT) return 0;
    }
    if (b->hp <= 0 || b->max_hp <= 0 || b->hp > b->max_hp) return 0;
    if (b->map.row < -1 || b->map.row >= MAP_ROWS) return 0;
    if (b->map.col < -1 || b->map.col >= MAP_COLS) return 0;
    if (b->map.gold < 0) return 0;
    if (b->map.map_seed == 0 || b->battle_rng == 0) return 0;
    return 1;
}

int save_load(void) {
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return 0;
    SaveBlob blob;
    size_t got = fread(&blob, 1, sizeof(blob), f);
    fclose(f);
    if (got != sizeof(blob) || !save_valid(&blob)) return 0;

    /* The edge table is static in run.c, so regenerate it from the same seed,
     * then put the saved MapState back on top.  g_run.act must be in place
     * first: the room shares are per act. */
    g_run = blob.run;
    g_battle.rng = blob.map.map_seed;
    map_generate();
    g_map = blob.map;
    g_map.message[0] = 0;

    g_battle.hp = blob.hp;
    g_battle.max_hp = blob.max_hp;
    g_battle.rng = blob.battle_rng;
    g_run.screen = SCREEN_MAP;
    return 1;
}

void save_write(void) {
#if defined(CARD_TEST)
    return;   /* the self-test run must not overwrite a real save */
#else
    if (g_run.screen != SCREEN_MAP) return;

    SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    memcpy(blob.magic, "STS3", 4);
    blob.version = SAVE_VERSION;
    blob.run = g_run;
    blob.map = g_map;
    blob.hp = g_battle.hp;
    blob.max_hp = g_battle.max_hp;
    blob.battle_rng = g_battle.rng;
    blob.checksum = save_checksum(&blob);

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/sts3ds", 0777);

    FILE *f = fopen(SAVE_TMP, "wb");
    if (!f) return;
    size_t put = fwrite(&blob, 1, sizeof(blob), f);
    fclose(f);
    if (put != sizeof(blob)) {
        remove(SAVE_TMP);
        return;
    }
    remove(SAVE_PATH);
    if (rename(SAVE_TMP, SAVE_PATH) != 0) remove(SAVE_TMP);
#endif
}

void save_clear(void) {
    remove(SAVE_PATH);
    remove(SAVE_TMP);
}
#if defined(CARD_TEST)
int save_self_test(void) {
    SaveBlob b;
    memset(&b, 0, sizeof(b));
    memcpy(b.magic, "STS3", 4);
    b.version = SAVE_VERSION;
    b.run.act = 1;
    b.run.screen = SCREEN_MAP;
    b.run.deck_count = 1;
    b.hp = 80;
    b.max_hp = 80;
    b.map.row = -1;
    b.map.col = 0;
    b.map.gold = 99;
    b.map.map_seed = 0x12345678u;
    b.battle_rng = 0x9ABCDEF0u;
    b.checksum = save_checksum(&b);
    if (!save_valid(&b)) return 0;

    b.checksum ^= 1u;
    if (save_valid(&b)) return 0;
    b.checksum ^= 1u;

    b.version = SAVE_VERSION + 1;
    b.checksum = save_checksum(&b);
    if (save_valid(&b)) return 0;
    b.version = SAVE_VERSION;

    b.hp = 81;   /* above max_hp */
    b.checksum = save_checksum(&b);
    if (save_valid(&b)) return 0;
    return 1;
}
#endif
