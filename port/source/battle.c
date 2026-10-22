/* Combat + run rules for v0.2/v0.3.
 *
 * Run: five linear nodes (fight, fight, campfire, fight, fight). HP carries
 * between fights; a won fight offers a 3-card reward; the campfire heals 30% or
 * upgrades a card the player picks from the deck.
 *
 * Upgrades are approximate (+3 damage / +3 block / +1 magic) because the real
 * per-card deltas live in the game bytecode and are not extracted yet.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "cards_gen.h"
#include "battle.h"
#include "run.h"
#include "l10n_gen.h"

/* Defined further down; potions use it before its definition. */
static void damage_enemy(int index, int amount);

Battle g_battle;
Run g_run;
int g_cheat_triple;

enum {
    RELIC_BURNING_BLOOD = 0,
    RELIC_VAJRA,
    RELIC_ANCHOR,
    RELIC_BAG_OF_PREP,
    RELIC_BLOOD_VIAL,
    RELIC_LANTERN,
    RELIC_BAG_OF_MARBLES,
    RELIC_STRAWBERRY,
    RELIC_ORICHALCUM,
    RELIC_RED_SKULL,
    RELIC_PEN_NIB,
    RELIC_NUNCHAKU,
    RELIC_ART_OF_WAR,
    RELIC_ORNITHOPTER,
    RELIC_GOLDEN_IDOL,
    RELIC_BRONZE_SCALES,
    RELIC_COUNT,
};

/* Bookkeeping for the relics that count things. */
static int s_attacks_played;
static int s_attack_this_turn;
static int s_cards_played_turn;   /* Normality: no play once three are out */
static int s_art_of_war_bonus;
static int s_red_skull_on;


static const char *STARTER[] = {
    "Strike_R", "Strike_R", "Strike_R", "Strike_R", "Strike_R",
    "Defend_R", "Defend_R", "Defend_R", "Defend_R",
    "Bash",
};

/* Reward pool: red cards that exist in the extracted table. */
static const char *REWARDS[] = {
    "Anger", "Cleave", "Body Slam", "Uppercut", "Iron Wave", "Thunderclap",
    "Clothesline", "Heavy Blade", "Pommel Strike", "Shrug It Off", "True Grit",
    "Twin Strike", "Perfected Strike", "Headbutt", "Sword Boomerang", "Flex",
};

int run_node_count(void) { return MAP_ROWS; }
int run_node_is_fight(int node) { (void)node; return 1; }

static unsigned int rnd(void) {
    unsigned int x = g_battle.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_battle.rng = x;
    return x;
}

/* ---- powers -------------------------------------------------------------
 * One int per power (battle.h POW_*).  The hooks below mirror the vanilla
 * AbstractPower callbacks: atStartOfTurn / atStartOfTurnPostDraw /
 * atEndOfTurn, onCardDraw, onExhaust, onGainedBlock, wasHPLost. */
static int pw(int id) {
    return (id >= 0 && id < POW_COUNT) ? g_battle.powers[id] : 0;
}

static void add_pw(int id, int amount) {
    if (id >= 0 && id < POW_COUNT && amount != 0) g_battle.powers[id] += amount;
}

/* Lose HP in a way RupturePower.wasHPLost() can see (cards, not enemy hits). */
static void lose_hp_from_card(int amount) {
    if (amount <= 0) return;
    g_battle.hp -= amount;
    if (g_battle.hp < 0) g_battle.hp = 0;
    g_battle.damaged++;                 /* Blood for Blood's cost discount */
    if (pw(POW_RUPTURE)) g_battle.strength += pw(POW_RUPTURE);
}

static void hit_all(int amount);
static void on_card_drawn(int entry);
static void mayhem_play_top(void);
static void add_random_colorless_to_hand(void);
static void gain_block(int amount);
static void debuff_enemy(int index, int weak, int vulnerable);

int battle_find_card(const char *id) {
    for (int i = 0; i < STS_CARD_COUNT; i++) {
        if (strcmp(sts_cards[i].id, id) == 0) return i;
    }
    return -1;
}

int battle_hand_card(int slot) {
    if (slot < 0 || slot >= g_battle.hand_count) return -1;
    return CARD_INDEX(g_battle.hand[slot]);
}

int battle_hand_up(int slot) {
    if (slot < 0 || slot >= g_battle.hand_count) return 0;
    return CARD_UPGRADED(g_battle.hand[slot]);
}

const CardDef *battle_card(int slot) {
    int index = battle_hand_card(slot);
    return index < 0 ? NULL : &sts_cards[index];
}

/* Cards whose text carries the Exhaust keyword.  Kept as a list instead of a
 * text scan because several cards merely talk about exhausting other cards
 * (Fiend Fire, Corruption) without exhausting themselves. */
static const char *EXHAUSTS[] = {
    "Bandage Up", "Master of Strategy", "Disarm", "Jack Of All Trades",
    "Intimidate", "Shockwave", "Pummel", "Impervious", "Seeing Red", "Limit Break",
    "Reaper", "Feed", "Offering", "Exhume", "Slimed", "Wound", "Burn", "Dazed",
    "Dramatic Entrance", "RitualDagger", "PanicButton",
    "Burning Pact", "Warcry", "Discovery", "Thinking Ahead",
};

static int card_exhausts(const CardDef *c) {
    for (size_t i = 0; i < sizeof(EXHAUSTS) / sizeof(EXHAUSTS[0]); i++) {
        if (strcmp(c->id, EXHAUSTS[i]) == 0) return 1;
    }
    return 0;
}

static int card_damage(const CardDef *c, int up) { return c->damage + (up ? c->up_damage : 0); }
static int card_block(const CardDef *c, int up) { return c->block + (up ? c->up_block : 0); }
static int card_magic(const CardDef *c, int up) { return c->magic + (up ? c->up_magic : 0); }

/* The cost the card shows right now.  BloodForBlood.java drops its cost by 1
 * for every HP-loss event this combat (tookDamage -> updateCost(-1), floor 0);
 * the extracted table only has the base 4. */
int battle_cost_of(const CardDef *c) {
    if (!c) return 0;
    /* Cost made free for the rest of the combat by Discovery, Madness,
     * Chrysalis, Metamorphosis or Infernal Blade.  Vanilla scopes some of
     * these to "this turn"; the port scopes all of them to the combat. */
    {
        int index = (int)(c - sts_cards);
        if (index >= 0 && index < 160 && g_battle.zero_cost[index]) return 0;
    }
    if (strcmp(c->id, "Blood for Blood") == 0) {
        int cost = c->cost - g_battle.damaged;
        return cost < 0 ? 0 : cost;
    }
    return c->cost;
}

int battle_card_cost(int slot) {
    const CardDef *c = battle_card(slot);
    if (!c || c->cost < 0) return -1;
    return battle_cost_of(c);
}

int battle_card_targeted(int slot) {
    const CardDef *c = battle_card(slot);
    if (!c || c->target != 0) return 0;
    /* Blind and Trip switch to "all enemies" when upgraded, so the upgraded
     * copy is not aimed at one monster any more. */
    if ((strcmp(c->id, "Blind") == 0 || strcmp(c->id, "Trip") == 0) &&
        battle_hand_up(slot)) {
        return 0;
    }
    return 1;
}

static void shuffle(int *pile, int count) {
    for (int i = count - 1; i > 0; i--) {
        int j = (int)(rnd() % (unsigned int)(i + 1));
        int tmp = pile[i];
        pile[i] = pile[j];
        pile[j] = tmp;
    }
}

static void draw_cards(int count) {
    if (g_battle.no_draw) return;
    while (count-- > 0) {
        if (g_battle.draw_count == 0) {
            if (g_battle.discard_count == 0) return;
            memcpy(g_battle.draw, g_battle.discard, sizeof(int) * (size_t)g_battle.discard_count);
            g_battle.draw_count = g_battle.discard_count;
            g_battle.discard_count = 0;
            shuffle(g_battle.draw, g_battle.draw_count);
        }
        if (g_battle.hand_count >= 10) return;
        int entry = g_battle.draw[--g_battle.draw_count];
        g_battle.hand[g_battle.hand_count++] = entry;
        on_card_drawn(entry);
    }
}

void battle_draw_for_test(int count) { draw_cards(count); }

static void start_player_turn(void) {
    g_battle.turn++;
    /* Barricade: block survives the start of the turn. */
    if (!pw(POW_BARRICADE)) g_battle.block = 0;
    g_battle.no_draw = 0;
    g_battle.energy = g_battle.max_energy;
    /* BerserkPower.atStartOfTurn: one extra energy every turn. */
    if (pw(POW_BERSERK)) g_battle.energy += 1;
    /* NoBlockPower.atEndOfRound: expires after the enemy turn it guarded. */
    if (g_battle.no_block_turns > 0) g_battle.no_block_turns--;
    /* FlameBarrierPower.atStartOfTurn: only covers the turn it was played. */
    g_battle.flame_barrier = 0;
    s_attack_this_turn = 0;
    s_cards_played_turn = 0;
    if (s_art_of_war_bonus) {
        g_battle.energy += 1;   /* Art of War: no attack last turn */
        s_art_of_war_bonus = 0;
    }
    if (battle_has_relic(RELIC_RED_SKULL)) {
        int low = g_battle.hp * 2 <= g_battle.max_hp;
        if (low && !s_red_skull_on) {
            g_battle.strength += 3;
            s_red_skull_on = 1;
        } else if (!low && s_red_skull_on) {
            g_battle.strength -= 3;
            s_red_skull_on = 0;
        }
    }
    if (g_battle.vulnerable > 0) g_battle.vulnerable--;
    if (g_battle.weak > 0) g_battle.weak--;
    /* FrailPower.atEndOfRound: skips the round it was applied in. */
    if (g_battle.frail > 0) {
        if (g_battle.frail_just) g_battle.frail_just = 0;
        else g_battle.frail--;
    }
    /* atStartOfTurn powers that hand out cards or play them run before the
     * draw phase, exactly like the vanilla power order. */
    for (int i = 0; i < pw(POW_MAGNETISM); i++) add_random_colorless_to_hand();
    for (int i = 0; i < pw(POW_MAYHEM) && g_battle.phase == PHASE_PLAYER; i++) mayhem_play_top();
    draw_cards(5);
    /* atStartOfTurnPostDraw powers. */
    if (pw(POW_BRUTALITY)) {
        draw_cards(1);
        lose_hp_from_card(1);
    }
    if (pw(POW_DEMON_FORM)) g_battle.strength += pw(POW_DEMON_FORM);
    /* PanachePower.atStartOfTurn: the five-card count starts over. */
    g_battle.panache_left = 5;
}

void battle_start_turn_for_test(void) { start_player_turn(); }

static int enemy_alive(int index) {
    return index >= 0 && index < g_battle.enemy_count && g_battle.enemies[index].hp > 0;
}

static int alive_count(void) {
    int n = 0;
    for (int i = 0; i < g_battle.enemy_count; i++) n += enemy_alive(i);
    return n;
}

static void add_enemy(int slot, const char *name, int hp) {
    Enemy *e = &g_battle.enemies[slot];
    memset(e, 0, sizeof(*e));
    e->name = name;
    e->art = name;
    e->hp = hp;
    e->max_hp = hp;
    e->first_turn = 1;
    e->minion_owner = -1;
    e->stasis_card = -1;
}

/* Vanilla Act 1 move sets (numbers from the game's own data). */
static const EnemyMove SLIME_MOVES[] = {
    {MOVE_ATTACK, 10, 0, 0},   /* Tackle */
    {MOVE_SLIME, 7, 0, 0},     /* Corrosive Spit: 7 damage + a 黏液 */
    {MOVE_WEAK, 1, 0, 0},      /* Lick */
};
static const EnemyMove JAW_WORM_MOVES[] = {
    {MOVE_ATTACK, 11, 0, 0},       /* Chomp */
    {MOVE_ATTACK_BLOCK, 7, 5, 0},  /* Thrash */
    {MOVE_BUFF, 3, 6, 0},          /* Bellow: +3 Strength, 6 Block */
};
static const EnemyMove NOB_MOVES[] = {
    {MOVE_ATTACK, 14, 0, 0},       /* Rush */
    {MOVE_VULN, 2, 0, 0},          /* Skull Bash (damage omitted for now) */
    {MOVE_BUFF, 2, 0, 0},          /* Bellow */
};
static const EnemyMove SPIKE_L_MOVES[] = {
    {MOVE_ATTACK, 16, 0, 0},       /* Tackle */
    {MOVE_SLIME, 16, 0, 0},        /* Flame Tackle: 16 damage + 2 黏液 */
};
static const EnemyMove ACID_L_MOVES[] = {
    {MOVE_ATTACK, 16, 0, 0},       /* Tackle */
    {MOVE_SLIME, 11, 0, 0},        /* Corrosive Spit */
    {MOVE_WEAK, 2, 0, 0},          /* Lick */
};
/* Act 2 normal monsters.  The move ids are the game's own ids shifted to
 * table indexes; the comments name the vanilla id. */
static const EnemyMove BYRD_MOVES[] = {
    {MOVE_MULTI, 1, 5, 0},         /* 1 Peck: five hits of 1 */
    {MOVE_FLY, 0, 0, 0},           /* 2 Go Airborne */
    {MOVE_ATTACK, 12, 0, 0},       /* 3 Swoop */
    {MOVE_STUN, 0, 0, 0},          /* 4 Stunned (FlightPower.onRemove) */
    {MOVE_HEADBUTT, 3, 0, 0},      /* 5 Headbutt: 3, then Go Airborne */
    {MOVE_BUFF, 1, 0, 0},          /* 6 Caw: +1 Strength */
};
static const EnemyMove CHOSEN_MOVES[] = {
    {MOVE_ATTACK, 18, 0, 0},       /* 1 Zap */
    {MOVE_DRAIN, 3, 3, 0},         /* 2 Drain: player 3 Weak, self +3 Strength */
    {MOVE_ATTACK_DEBUFF, 10, 2, 0},/* 3 Debilitate: 10 + 2 Vulnerable */
    {MOVE_HEX, 0, 0, 0},           /* 4 Hex */
    {MOVE_MULTI, 5, 2, 0},         /* 5 Poke: two hits of 5 */
};
static const EnemyMove PARASITE_MOVES[] = {
    {MOVE_ATTACK_DEBUFF, 18, 0, 2},/* 1 Fell: 18 + 2 Frail */
    {MOVE_MULTI, 6, 2, 0},         /* 2 Double Strike */
    {MOVE_SUCK, 10, 0, 0},         /* 3 Life Suck: 10, heals by HP lost */
    {MOVE_STUN, 0, 0, 0},          /* 4 Stunned (Plated Armor break) */
};
static const EnemyMove ORB_MOVES[] = {
    {MOVE_MULTI, 10, 2, 0},        /* 1 Big Attack: two hits of 10 */
    {MOVE_DEFEND, 25, 0, 0},       /* 2 Initial Block: +25 */
    {MOVE_BLOCK_ATTACK, 15, 10, 0},/* 3 Block Attack: +15 then 10 */
    {MOVE_ATTACK_DEBUFF, 10, 0, 5},/* 4 Frail Attack: 10 + 5 Frail */
};
/* Book of Stabbing (BookOfStabbing.java): STAB hits 1.stabCount times, so the
 * hit count lives on the enemy, not in the move. */
static const EnemyMove STAB_MOVES[] = {
    {MOVE_MULTI, 6, 0, 0},         /* Stab: hits = e->stab_count */
    {MOVE_ATTACK, 21, 0, 0},       /* Big Stab */
};
/* Champ (Champ.java:143-295), ids shifted to indexes: 0 Slash 16, 1 Defensive
 * Stance (15 block + Metallicize 5), 2 Execute 10x2, 3 Face Slap 12 + 2
 * Vulnerable + 2 Frail, 4 Gloat +2 Strength, 5 Taunt (2 Weak + 2 Vulnerable),
 * 6 Anger +6 Strength and drops its own debuffs. */
static const EnemyMove CHAMP_MOVES[] = {
    {MOVE_ATTACK, 16, 0, 0},            /* Slash */
    {MOVE_DEFEND, 15, 5, 0},            /* Defensive Stance */
    {MOVE_MULTI, 10, 2, 0},             /* Execute */
    {MOVE_ATTACK_DEBUFF, 12, 2, 2},  /* Face Slap */
    {MOVE_BUFF, 2, 0, 0},               /* Gloat */
    {MOVE_TAUNT, 2, 2, 0},              /* Taunt */
    {MOVE_ANGER, 6, 0, 0},              /* Anger */
};
/* Slavers (MonsterHelper.java:504-506).  Blue and Red share the localised
 * name "奴隶贩子", so Enemy.art carries the sprite key. */
static const EnemyMove SLAVER_BLUE_MOVES[] = {
    {MOVE_ATTACK, 12, 0, 0},       /* Stab */
    {MOVE_ATTACK_WEAK, 7, 1, 0},   /* Rake: 7 + 1 Weak */
};
static const EnemyMove SLAVER_RED_MOVES[] = {
    {MOVE_ATTACK, 13, 0, 0},       /* Stab (also the scripted first turn) */
    {MOVE_ENTANGLE, 0, 0, 0},      /* Entangle */
    {MOVE_ATTACK_DEBUFF, 8, 1, 0}, /* Scrape: 8 + 1 Vulnerable */
};
static const EnemyMove TASKMASTER_MOVES[] = {
    {MOVE_WOUND, 7, 1, 0},         /* Scouring Whip: 7 + a Wound */
};
/* BronzeAutomaton.java: ids shifted to indexes.  0 Flail, 1 Hyper Beam,
 * 2 Stunned, 3 Spawn Orbs, 4 Boost. */
static const EnemyMove AUTOMATON_MOVES[] = {
    {MOVE_MULTI, 7, 2, 0},         /* Flail: two hits of 7 */
    {MOVE_ATTACK, 45, 0, 0},       /* Hyper Beam */
    {MOVE_STUN, 0, 0, 0},          /* Stunned */
    {MOVE_SPAWN, 0, 0, 0},         /* Spawn Orbs */
    {MOVE_DEFEND_BUFF, 9, 3, 0},   /* Boost: +9 block, +3 Strength */
};
/* BronzeOrb.java: 0 Beam, 1 Support Beam, 2 Stasis. */
static const EnemyMove BRONZE_ORB_MOVES[] = {
    {MOVE_ATTACK, 8, 0, 0},        /* Beam: 8 damage */
    {MOVE_SUPPORT, 12, 0, 0},      /* Support Beam: owner +12 block */
    {MOVE_STASIS, 0, 0, 0},        /* Stasis: steal a card */
};
/* TheCollector.java: ids shifted to indexes.  0 Spawn, 1 Fireball, 2 Buff,
 * 3 Mega Debuff, 4 Revive. */
static const EnemyMove COLLECTOR_MOVES[] = {
    {MOVE_SPAWN, 0, 0, 0},         /* Spawn: two Torch Heads */
    {MOVE_ATTACK, 18, 0, 0},       /* Fireball */
    {MOVE_RALLY, 15, 3, 0},        /* Buff: own +15 block, all +3 Strength */
    {MOVE_CURSE, 3, 0, 0},         /* Mega Debuff: 3 Weak/Vulnerable/Frail */
    {MOVE_REVIVE, 0, 0, 0},        /* Revive: one Torch Head per dead slot */
};
/* TorchHead.java: one move, 7 damage. */
static const EnemyMove TORCH_HEAD_MOVES[] = {
    {MOVE_ATTACK, 7, 0, 0},        /* Tackle */
};
/* The five Gremlins that the Gremlin Leader can summon.  Their pools and
 * numbers come from the exordium/*.java sources; the port keeps each one's
 * one signature move so the mini-fight still reads correctly. */
static const EnemyMove GREMLIN_WARRIOR_MOVES[] = {
    {MOVE_ATTACK, 4, 0, 0},        /* Scratch */
};
static const EnemyMove GREMLIN_THIEF_MOVES[] = {
    {MOVE_ATTACK, 9, 0, 0},        /* Puncture */
};
static const EnemyMove GREMLIN_FAT_MOVES[] = {
    {MOVE_ATTACK_WEAK, 4, 1, 0},   /* Blunt: 4 damage + 1 Weak */
};
static const EnemyMove GREMLIN_TSUNDERE_MOVES[] = {
    {MOVE_PROTECT, 7, 0, 0},       /* Protect: 7 block to a random ally */
    {MOVE_ATTACK, 6, 0, 0},        /* Bash */
};
static const EnemyMove GREMLIN_WIZARD_MOVES[] = {
    {MOVE_CHARGE, 0, 0, 0},        /* Charge */
    {MOVE_ATTACK, 25, 0, 0},       /* Ultimate Blast */
};
/* GremlinLeader.java: 2 Rally, 3 Encourage, 4 Stab; shifted to 0..2. */
static const EnemyMove GREMLIN_LEADER_MOVES[] = {
    {MOVE_SPAWN, 0, 0, 0},         /* Rally: summon up to two Gremlins */
    {MOVE_COMMAND, 3, 6, 0},       /* Encourage: +3 Str, +6 block to allies */
    {MOVE_MULTI, 6, 3, 0},         /* Stab: 6 damage three times */
};
static const EnemyMove BOSS_MOVES[] = {
    {MOVE_ATTACK, 35, 0, 0},       /* Slam */
    {MOVE_SLIME, 0, 0, 0},         /* Goop Spray */
    {MOVE_ATTACK, 10, 0, 0},
};


static const char *RELIC_NAMES[RELIC_COUNT] = {
    "燃烧之血", "金刚杵", "锚", "准备背包", "小血瓶", "灯笼", "弹珠袋",
    "草莓", "奥利哈刚", "红骷髅", "钢笔尖", "双节棍", "孙子兵法",
    "玩具扑翼飞机", "金色神像", "青铜鳞片",
};

int battle_relic_count(void) { return RELIC_COUNT; }

/* AbstractRelic.RelicTier per relic: 0 STARTER, 1 COMMON, 2 SPECIAL.  The port
 * only ships starter / common / one special relic, so the other tiers of
 * AbstractRelic.getPrice (:161-183) do not appear here. */
static const unsigned char RELIC_TIERS[RELIC_COUNT] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1,
};

/* 0 STARTER, 1 COMMON, 2 SPECIAL — the tiers this port actually has. */
int battle_relic_tier(int id) {
    return (id >= 0 && id < RELIC_COUNT) ? RELIC_TIERS[id] : 0;
}

int battle_relic_price(int id) {
    if (id < 0 || id >= RELIC_COUNT) return 0;
    if (RELIC_TIERS[id] == 0) return 300;   /* STARTER */
    if (RELIC_TIERS[id] == 2) return 400;   /* SPECIAL */
    return 150;                             /* COMMON */
}

/* AbstractPotion.getPrice: COMMON 50, UNCOMMON 75, RARE 100.  Every potion in
 * POTION_NAMES is COMMON (BloodPotion .. StrengthPotion are all COMMON). */
int battle_potion_price(int id) {
    (void)id;
    return 50;
}

static const char *POTION_NAMES[POTION_COUNT] = {
    "血液药水", "火焰药水", "格挡药水", "迅捷药水", "能量药水", "力量药水",
};
static const char *POTION_DESC[POTION_COUNT] = {
    "回复 20% 最大生命", "造成 20 点伤害", "获得 12 点格挡",
    "抽 3 张牌", "获得 2 点能量", "获得 2 点力量",
};

int battle_toggle_cheat(void) {
    g_cheat_triple = !g_cheat_triple;
    return g_cheat_triple;
}

int battle_potion_count(void) { return g_run.potion_count; }

int battle_potion_at(int slot) {
    if (slot < 0 || slot >= g_run.potion_count) return -1;
    return g_run.potions[slot];
}

const char *battle_potion_name(int id) {
    return id >= 0 && id < POTION_COUNT ? POTION_NAMES[id] : "";
}

const char *battle_potion_desc(int id) {
    return id >= 0 && id < POTION_COUNT ? POTION_DESC[id] : "";
}

int battle_add_potion(int id) {
    if (g_run.potion_count >= 3) return 0;
    if (id < 0 || id >= POTION_COUNT) return 0;
    g_run.potions[g_run.potion_count++] = (unsigned char)id;
    return 1;
}

int battle_add_random_potion(void) {
    if (g_run.potion_count >= 3) return 0;
    int id = (int)(rnd() % POTION_COUNT);
    g_run.potions[g_run.potion_count++] = (unsigned char)id;
    return 1;
}

int battle_use_potion(int slot, int target) {
    if (g_run.screen != SCREEN_FIGHT || g_battle.phase != PHASE_PLAYER) return 0;
    int id = battle_potion_at(slot);
    if (id < 0) return 0;

    switch (id) {
    case POTION_HEAL: {
        int amount = g_battle.max_hp * 20 / 100;
        g_battle.hp += amount;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
        break;
    }
    case POTION_FIRE:
        if (!enemy_alive(target)) return 0;
        damage_enemy(target, 20);
        break;
    case POTION_BLOCK:
        gain_block(12);
        break;
    case POTION_DRAW:
        draw_cards(3);
        break;
    case POTION_ENERGY:
        g_battle.energy += 2;
        break;
    case POTION_STR:
        g_battle.strength += 2;
        break;
    default:
        return 0;
    }

    if (battle_has_relic(RELIC_ORNITHOPTER)) {
        g_battle.hp += 5;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }

    for (int i = slot; i < g_run.potion_count - 1; i++) g_run.potions[i] = g_run.potions[i + 1];
    g_run.potion_count--;
    return 1;
}

const char *battle_relic_name(int id) {
    return id >= 0 && id < RELIC_COUNT ? RELIC_NAMES[id] : "";
}

const char *battle_relic_desc(int id) {
    return id >= 0 && id < RELIC_COUNT ? sts_relic_desc[id] : "";
}

int battle_has_relic(int id) {
    for (int i = 0; i < g_run.relic_count; i++) {
        if (g_run.relics[i] == id) return 1;
    }
    return 0;
}

int battle_add_relic(int id) {
    if (id < 0 || id >= RELIC_COUNT) return 0;
    if (battle_has_relic(id)) return 0;
    if (g_run.relic_count >= 8) return 0;
    g_run.relics[g_run.relic_count++] = (unsigned char)id;
    if (id == RELIC_STRAWBERRY) {
        g_battle.max_hp += 7;
        g_battle.hp += 7;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    return 1;
}

static void apply_relics_on_start(void);
static void debuff_enemy(int index, int weak, int vulnerable);

/* Move selection, transcribed from the decompiled getMove() methods.
 * Ids are indexes into each enemy's move table. */
static int last_two(const Enemy *e, int id) {
    return e->last_move == id && e->last_before == id;
}

/* TheCollector.getMove calls isMinionDead() before it considers Revive. */
static int collector_has_dead_minion(const Enemy *e) {
    int boss = (int)(e - g_battle.enemies);
    if (boss < 0 || boss >= g_battle.enemy_count) return 0;
    for (int i = 0; i < g_battle.enemy_count; i++) {
        if (i != boss && g_battle.enemies[i].minion_owner == boss &&
            g_battle.enemies[i].hp <= 0) return 1;
    }
    return 0;
}

static int gremlin_minions_alive(const Enemy *e) {
    int leader = (int)(e - g_battle.enemies);
    int n = 0;
    if (leader < 0 || leader >= g_battle.enemy_count) return 0;
    for (int i = 0; i < g_battle.enemy_count; i++) {
        if (i != leader && g_battle.enemies[i].minion_owner == leader &&
            g_battle.enemies[i].hp > 0) n++;
    }
    return n;
}

/* GremlinLeader.getMove (GremlinLeader.java:138-182), with the three minion
 * slot state deciding which table is used.  The two recursive calls are the
 * game's re-rolls (random(50,99) and random(0,80)). */
static void choose_gremlin_leader(Enemy *e, unsigned int roll) {
    int n = gremlin_minions_alive(e);
    if (n == 0) {
        if (roll < 75) e->move_index = e->last_move != 0 ? 0 : 2;
        else e->move_index = e->last_move != 2 ? 2 : 0;
    } else if (n < 2) {
        if (roll < 50) {
            if (e->last_move != 0) e->move_index = 0;
            else choose_gremlin_leader(e, 50 + rnd() % 50);
        } else if (roll < 80) {
            e->move_index = e->last_move != 1 ? 1 : 2;
        } else if (e->last_move != 2) {
            e->move_index = 2;
        } else {
            choose_gremlin_leader(e, rnd() % 81);
        }
    } else {
        if (roll < 66) e->move_index = e->last_move != 1 ? 1 : 2;
        else e->move_index = e->last_move != 2 ? 2 : 1;
    }
}

static void choose_move_roll(Enemy *e, unsigned int roll) {
    if (e->moves == SLIME_MOVES) {
        /* 0 Tackle, 1 Corrosive Spit, 2 Lick */
        if (roll < 30) {
            if (last_two(e, 1)) e->move_index = (rnd() & 1) ? 0 : 2;
            else e->move_index = 1;
        } else if (roll < 70) {
            if (e->last_move == 0) e->move_index = (rnd() % 100) < 40 ? 1 : 2;
            else e->move_index = 0;
        } else {
            if (last_two(e, 2)) e->move_index = (rnd() % 100) < 40 ? 1 : 0;
            else e->move_index = 2;
        }
        return;
    }

    if (e->moves == JAW_WORM_MOVES) {
        /* 0 Chomp, 1 Thrash, 2 Bellow */
        if (e->first_turn) {
            e->move_index = 0;
            return;
        }
        if (roll < 25) {
            if (e->last_move == 0) e->move_index = (rnd() % 10000) < 5625 ? 2 : 1;
            else e->move_index = 0;
        } else if (roll < 55) {
            if (last_two(e, 1)) e->move_index = (rnd() % 1000) < 357 ? 0 : 2;
            else e->move_index = 1;
        } else {
            if (e->last_move == 2) e->move_index = (rnd() % 1000) < 416 ? 0 : 1;
            else e->move_index = 2;
        }
        return;
    }

    if (e->moves == NOB_MOVES) {
        /* 0 Rush, 1 Skull Bash, 2 Bellow; Bellow always happens first */
        if (!e->used_bellow) {
            e->used_bellow = 1;
            e->move_index = 2;
            return;
        }
        e->move_index = last_two(e, 0) ? 1 : 0;
        return;
    }

    /* Slime Boss: opens with Goop Spray, then Slam / tackle pattern. */
    if (e->moves == BOSS_MOVES) {
        if (e->first_turn) {
            e->move_index = 1;
            return;
        }
        e->move_index = (unsigned char)(e->last_move == 0 ? 2 : 0);
        return;
    }

    /* Champ.getMove (Champ.java:253-295). numTurns counts the turn the move
     * will be used on; the half-HP Anger interrupts everything once. */
    if (e->moves == CHAMP_MOVES) {
        e->ai_turn++;
        if (e->hp < e->max_hp / 2 && !e->threshold) {
            e->threshold = 1;
            e->move_index = 6;
            return;
        }
        if (e->last_move != 2 && e->last_before != 2 && e->threshold) {
            e->move_index = 2;
            return;
        }
        if (e->ai_turn == 4 && !e->threshold) {
            e->move_index = 5;
            e->ai_turn = 0;
            return;
        }
        if (e->last_move != 1 && e->forge_times < 2 && roll <= 15) {
            e->forge_times++;
            e->move_index = 1;
            return;
        }
        if (e->last_move != 4 && e->last_move != 1 && roll <= 30) {
            e->move_index = 4;
            return;
        }
        if (e->last_move != 3 && roll <= 55) {
            e->move_index = 3;
            return;
        }
        e->move_index = (unsigned char)(e->last_move != 0 ? 0 : 3);
        return;
    }

    /* BookOfStabbing.getMove (BookOfStabbing.java:122-143). */
    if (e->moves == STAB_MOVES) {
        if (roll < 15) {
            if (e->last_move == 1) {
                e->stab_count++;
                e->move_index = 0;
            } else {
                e->move_index = 1;
            }
        } else if (last_two(e, 0)) {
            e->move_index = 1;
        } else {
            e->stab_count++;
            e->move_index = 0;
        }
        return;
    }

    /* Byrd.getMove (Byrd.java:98-126).  It opens in the air: 37.5% Caw,
     * otherwise Peck.  Grounded birds can only Headbutt. */
    if (e->moves == BYRD_MOVES) {
        if (e->grounded) {
            e->move_index = 4;
            return;
        }
        if (e->first_turn) {
            e->move_index = (rnd() % 1000) < 375 ? 5 : 0;
            return;
        }
        if (roll < 50) {
            if (last_two(e, 0)) e->move_index = (rnd() % 1000) < 400 ? 2 : 5;
            else e->move_index = 0;
        } else if (roll < 70) {
            if (e->last_move == 2) e->move_index = (rnd() % 1000) < 375 ? 5 : 0;
            else e->move_index = 2;
        } else {
            if (e->last_move == 5) e->move_index = (rnd() % 10000) < 2857 ? 2 : 0;
            else e->move_index = 5;
        }
        return;
    }

    /* Chosen.getMove (Chosen.java:153-190).  Poke on turn one, Hex exactly
     * once, then Debilitate/Drain until one has been used. */
    if (e->moves == CHOSEN_MOVES) {
        if (e->first_turn) {
            e->move_index = 4;
            return;
        }
        if (!e->used_hex) {
            e->used_hex = 1;
            e->move_index = 3;
            return;
        }
        if (e->last_move != 2 && e->last_move != 1) {
            e->move_index = roll < 50 ? 2 : 1;
            return;
        }
        e->move_index = roll < 40 ? 0 : 4;
        return;
    }

    /* ShelledParasite.getMove (ShelledParasite.java:132-177). */
    if (e->moves == PARASITE_MOVES) {
        if (e->first_turn) {
            e->move_index = (rnd() & 1) ? 1 : 2;
            return;
        }
        if (roll < 20) {
            if (e->last_move != 0) {
                e->move_index = 0;
            } else {
                /* Vanilla re-rolls 20..99 when Fell was just used. */
                choose_move_roll(e, 20 + rnd() % 80);
            }
            return;
        }
        if (roll < 60) {
            e->move_index = last_two(e, 1) ? 2 : 1;
            return;
        }
        e->move_index = last_two(e, 2) ? 1 : 2;
        return;
    }

    /* SphericGuardian.getMove (SphericGuardian.java:114-141). */
    if (e->moves == ORB_MOVES) {
        if (e->first_turn) {
            e->move_index = 1;
            return;
        }
        if (e->second_move) {
            e->second_move = 0;
            e->move_index = 3;
            return;
        }
        e->move_index = e->last_move == 0 ? 2 : 0;
        return;
    }

    /* SlaverBlue.getMove: Stab only after a high roll, and never three times
     * in a row; otherwise Rake, unless Rake also just happened twice. */
    if (e->moves == SLAVER_BLUE_MOVES) {
        if (roll >= 40 && !last_two(e, 0)) e->move_index = 0;
        else if (!last_two(e, 1)) e->move_index = 1;
        else e->move_index = 0;
        return;
    }

    /* TheCollector.getMove (TheCollector.java:160-205).  Initial Spawn, then
     * Mega Debuff once turnsTaken reaches 3, Revive while a head lies dead,
     * otherwise Fireball / Buff with their no-repeat guards. */
    if (e->moves == COLLECTOR_MOVES) {
        if (e->first_turn) {
            e->first_turn = 0;
            e->move_index = 0;             /* Spawn */
            return;
        }
        if (e->ai_turn >= 3 && !e->used_ult) {
            e->move_index = 3;             /* Mega Debuff */
            return;
        }
        if (roll <= 25 && collector_has_dead_minion(e) && e->last_move != 4) {
            e->move_index = 4;             /* Revive */
            return;
        }
        if (roll <= 70 && !last_two(e, 1)) {
            e->move_index = 1;             /* Fireball */
            return;
        }
        e->move_index = (unsigned char)(e->last_move != 2 ? 2 : 1);
        return;
    }

    /* GremlinLeader.getMove.  The two recursive re-rolls are already in
     * choose_gremlin_leader(), so the roll passed here is the first 0..99. */
    if (e->moves == GREMLIN_LEADER_MOVES) {
        choose_gremlin_leader(e, roll);
        return;
    }

    /* GremlinTsundere.getMove is fixed to Protect; after its own turn it stays
     * on Protect while another monster is alive, otherwise it switches to Bash
     * (and Bash is repeated forever). */
    if (e->moves == GREMLIN_TSUNDERE_MOVES) {
        if (e->last_move == 1) e->move_index = 1;
        else e->move_index = alive_count() > 1 ? 0 : 1;
        return;
    }

    /* GremlinWizard: currentCharge starts at 1, so two Charges, then the
     * 25-damage Ultimate Blast, then Charge again. */
    if (e->moves == GREMLIN_WIZARD_MOVES) {
        if (e->last_move == 1) e->move_index = 0;
        else e->move_index = e->ai_turn >= 3 ? 1 : 0;
        return;
    }

    /* BronzeAutomaton.getMove (BronzeAutomaton.java:177-203).  numTurns is
     * e->ai_turn; Hyper Beam fires when it reaches 4, then Stunned, then the
     * Flail/Boost loop begins again. */
    if (e->moves == AUTOMATON_MOVES) {
        if (e->first_turn) {
            e->first_turn = 0;
            e->move_index = 3;             /* Spawn Orbs */
            return;
        }
        if (e->ai_turn == 4) {
            e->ai_turn = 0;
            e->move_index = 1;             /* Hyper Beam */
            return;
        }
        if (e->last_move == 1) {
            e->move_index = 2;             /* Stunned after Hyper Beam */
            return;
        }
        if (e->last_move == 2 || e->last_move == 3 || e->last_move == 4 ||
            e->last_move == 255) {
            e->ai_turn++;
            e->move_index = 0;             /* Flail */
            return;
        }
        e->ai_turn++;
        e->move_index = 4;                 /* Boost */
        return;
    }

    /* BronzeOrb.getMove (BronzeOrb.java:104-130).  Stasis once, Support on a
     * high roll unless it just supported twice, otherwise Beam. */
    if (e->moves == BRONZE_ORB_MOVES) {
        if (!e->used_stasis && roll >= 25) {
            e->used_stasis = 1;
            e->move_index = 2;
            return;
        }
        if (roll >= 70 && !last_two(e, 1)) {
            e->move_index = 1;
            return;
        }
        e->move_index = last_two(e, 0) ? 1 : 0;
        return;
    }

    /* SlaverRed.getMove: scripted Stab on turn one, one Entangle, then Scrape
     * whenever it has not just been used twice. */
    if (e->moves == SLAVER_RED_MOVES) {
        if (e->first_turn) {
            e->move_index = 0;
            return;
        }
        if (roll >= 75 && !e->used_entangle) {
            e->move_index = 1;
            return;
        }
        if (roll >= 55 && e->used_entangle && !last_two(e, 0)) {
            e->move_index = 0;
            return;
        }
        e->move_index = last_two(e, 2) ? 0 : 2;
        return;
    }
}

static void choose_move(Enemy *e) { choose_move_roll(e, rnd() % 100); }

/* One act 2 normal encounter.  which: 0 two Byrds, 1 Chosen, 2 Shelled
 * Parasite, 3 Spheric Guardian.  Pre-battle powers and the opening move are
 * applied here, the way usePreBattleAction()/init() do in the game. */
static void setup_act2_byrd(int slot) {
    add_enemy(slot, "异鸟", 25 + (int)(battle_rand() % 7));
    Enemy *e = &g_battle.enemies[slot];
    e->moves = BYRD_MOVES;
    e->move_count = 6;
    e->flight = e->flight_max = 3;   /* FlightPower(3), starts in the air */
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);
}

static void setup_act2_monster(int which) {
    g_battle.enemy_count = 0;
    if (which == 0) {
        setup_act2_byrd(0);
        setup_act2_byrd(1);
        g_battle.enemy_count = 2;
        return;
    }
    if (which == 1) {
        add_enemy(0, "天选者", 95 + (int)(battle_rand() % 5));
        Enemy *e = &g_battle.enemies[0];
        e->moves = CHOSEN_MOVES;
        e->move_count = 5;
        e->last_move = 255;
        e->last_before = 255;
        choose_move(e);              /* first turn: Poke */
        g_battle.enemy_count = 1;
        return;
    }
    if (which == 2) {
        add_enemy(0, "寄生兽", 68 + (int)(battle_rand() % 5));
        Enemy *e = &g_battle.enemies[0];
        e->moves = PARASITE_MOVES;
        e->move_count = 4;
        e->plated = 14;              /* PlatedArmorPower(14) + 14 block */
        e->block = 14;
        e->last_move = 255;
        e->last_before = 255;
        choose_move(e);
        g_battle.enemy_count = 1;
        return;
    }
    add_enemy(0, "球体守卫", 20);
    {
        Enemy *e = &g_battle.enemies[0];
        e->moves = ORB_MOVES;
        e->move_count = 4;
        e->block = 40;               /* initial Block */
        e->barricade = 1;            /* BarricadePower */
        e->artifact = 3;             /* ArtifactPower(3) */
        e->second_move = 1;
        e->last_move = 255;
        e->last_before = 255;
        choose_move(e);              /* first turn: Initial Block */
    }
    g_battle.enemy_count = 1;
}

/* The three Slavers, left to right: blue, taskmaster, red. */
static void setup_act2_slavers(void) {
    add_enemy(0, "奴隶贩子", 46 + (int)(battle_rand() % 5));
    Enemy *e = &g_battle.enemies[0];
    e->art = "蓝奴隶贩子";
    e->moves = SLAVER_BLUE_MOVES;
    e->move_count = 2;
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);

    add_enemy(1, "奴隶头子", 54 + (int)(battle_rand() % 7));
    e = &g_battle.enemies[1];
    e->art = "奴隶头子";
    e->moves = TASKMASTER_MOVES;
    e->move_count = 1;
    e->move_index = 0;             /* getMove always sets Scouring Whip */

    add_enemy(2, "奴隶贩子", 46 + (int)(battle_rand() % 5));
    e = &g_battle.enemies[2];
    e->art = "红奴隶贩子";
    e->moves = SLAVER_RED_MOVES;
    e->move_count = 3;
    e->first_turn = 1;
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);                /* scripted Stab */
    g_battle.enemy_count = 3;
}

/* The Champ (Champ.java).  Kept separate so the boss pool can choose it. */
static void setup_act2_champ(void) {
    add_enemy(0, "勇者", 420);
    Enemy *e = &g_battle.enemies[0];
    e->moves = CHAMP_MOVES;
    e->move_count = 7;
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);
    g_battle.enemy_count = 1;
}

/* BronzeAutomaton.java.  It opens with Spawn Orbs and fights alone until the
 * two BronzeOrbs are summoned. */
static void setup_act2_automaton(void) {
    add_enemy(0, "铜制机械人偶", 300);
    Enemy *e = &g_battle.enemies[0];
    e->moves = AUTOMATON_MOVES;
    e->move_count = 5;
    e->artifact = 3;               /* ArtifactPower(3) */
    e->draw_x = 160;
    e->draw_w = 80;
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);                /* first turn: Spawn Orbs */
    g_battle.enemy_count = 1;
}

/* BronzeOrb.java.  Summoned by Spawn Orbs; the left/right positions are the
 * game's own (-300, 200) and (200, 130) offsets scaled onto the top screen. */
static void spawn_automaton_orbs(int boss_index) {
    if (g_battle.enemy_count >= 3) return;
    for (int k = 0; k < 2; k++) {
        int slot = 1 + k;
        add_enemy(slot, "铜球", 52 + (int)(battle_rand() % 7));
        Enemy *orb = &g_battle.enemies[slot];
        orb->art = "铜球";
        orb->moves = BRONZE_ORB_MOVES;
        orb->move_count = 3;
        orb->minion_owner = (signed char)boss_index;
        orb->draw_x = k == 0 ? 100 : 288;
        orb->draw_w = 52;
        orb->first_turn = 0;
        orb->last_move = 255;
        orb->last_before = 255;
        choose_move(orb);
    }
    g_battle.enemy_count = 3;
}

/* TheCollector.java: it opens with Spawn and fights alone until the two
 * Torch Heads arrive.  The game places them to the left of the Collector. */
static void spawn_collector_heads(int boss_index) {
    if (g_battle.enemy_count >= 3) return;
    for (int k = 0; k < 2; k++) {
        int slot = 1 + k;
        add_enemy(slot, "火炬头", 38 + (int)(battle_rand() % 3));
        Enemy *head = &g_battle.enemies[slot];
        head->art = "火炬头";
        head->moves = TORCH_HEAD_MOVES;
        head->move_count = 1;
        head->minion_owner = (signed char)boss_index;
        head->draw_x = (short)(k == 0 ? 160 : 216);
        head->draw_w = 48;
        head->first_turn = 0;
        head->last_move = 255;
        head->last_before = 255;
        choose_move(head);              /* Tackle */
    }
    g_battle.enemy_count = 3;
}

/* TheCollector.java Revive: every dying minion slot gets a fresh Torch Head. */
static void revive_collector_heads(int boss_index) {
    for (int i = 0; i < g_battle.enemy_count; i++) {
        Enemy *head = &g_battle.enemies[i];
        if (i == boss_index || head->minion_owner != boss_index || head->hp > 0) continue;
        head->hp = head->max_hp;
        head->block = 0;
        head->vulnerable = 0;
        head->weak = 0;
        head->strength = 0;
        head->death_done = 0;
        head->stasis_card = -1;
        head->move_index = 0;
        head->last_move = 255;
        head->last_before = 255;
        head->first_turn = 0;
        choose_move(head);
    }
}

/* TheCollector.java.  HP 282; it starts alone and summons its Torch Heads. */
static void setup_act2_collector(void) {
    add_enemy(0, "收藏家", 282);
    Enemy *e = &g_battle.enemies[0];
    e->moves = COLLECTOR_MOVES;
    e->move_count = 5;
    e->draw_x = 300;
    e->draw_w = 80;
    e->last_move = 255;
    e->last_before = 255;
    choose_move(e);                    /* first turn: Spawn */
    g_battle.enemy_count = 1;
}

/* One Gremlin from SummonGremlinAction's weighted pool.  which == -1 rolls
 * the pool (Warrior/Thief/Fat twice, Tsundere and Wizard once each). */
static void spawn_gremlin_minion(int leader, int slot, int which) {
    if (slot < 1 || slot > 3) return;
    if (which < 0) {
        static const signed char pool[8] = {0, 0, 1, 1, 2, 2, 3, 4};
        which = pool[rnd() % 8];
    }
    const char *name;
    int hp;
    const EnemyMove *moves;
    int move_count;
    switch (which) {
    case 0: name = "火大地精"; hp = 20 + (int)(rnd() % 5); moves = GREMLIN_WARRIOR_MOVES; move_count = 1; break;
    case 1: name = "卑鄙地精"; hp = 10 + (int)(rnd() % 5); moves = GREMLIN_THIEF_MOVES; move_count = 1; break;
    case 2: name = "胖地精";   hp = 13 + (int)(rnd() % 5); moves = GREMLIN_FAT_MOVES; move_count = 1; break;
    case 3: name = "持盾地精"; hp = 12 + (int)(rnd() % 4); moves = GREMLIN_TSUNDERE_MOVES; move_count = 2; break;
    default: name = "地精法师"; hp = 21 + (int)(rnd() % 5); moves = GREMLIN_WIZARD_MOVES; move_count = 2; break;
    }
    add_enemy(slot, name, hp);
    Enemy *e = &g_battle.enemies[slot];
    e->art = name;
    e->moves = moves;
    e->move_count = move_count;
    e->minion_owner = (signed char)leader;
    e->draw_x = (short)(120 + (slot - 1) * 68);
    e->draw_w = 54;
    e->first_turn = 0;
    e->last_move = 255;
    e->last_before = 255;
    if (which == 4) e->ai_turn = 1;   /* GremlinWizard.currentCharge starts at 1 */
    if (slot + 1 > g_battle.enemy_count) g_battle.enemy_count = slot + 1;
    choose_move(e);
}

/* GremlinLeader Rally (SummonGremlinAction twice): fill the first two empty
 * minion slots.  A full room simply ignores the extra summon, like the game. */
static void spawn_gremlin_minions(int leader) {
    for (int k = 0; k < 2; k++) {
        int slot = -1;
        for (int s = 1; s <= 3; s++) {
            if (s >= g_battle.enemy_count || g_battle.enemies[s].hp <= 0) {
                slot = s;
                break;
            }
        }
        if (slot < 0) break;
        spawn_gremlin_minion(leader, slot, -1);
    }
}

/* GremlinLeader.java: 140-148 HP, two Gremlins at battle start, leader on the
 * right.  Its minions carry MinionPower, so they leave when the leader dies. */
static void setup_act2_gremlin_leader(void) {
    add_enemy(0, "地精首领", 140 + (int)(battle_rand() % 9));
    Enemy *e = &g_battle.enemies[0];
    e->moves = GREMLIN_LEADER_MOVES;
    e->move_count = 3;
    e->draw_x = 316;
    e->draw_w = 60;
    e->last_move = 255;
    e->last_before = 255;
    g_battle.enemy_count = 1;
    /* MonsterHelper: two spawnGremlin() calls, then the leader. */
    spawn_gremlin_minion(0, 1, -1);
    spawn_gremlin_minion(0, 2, -1);
    choose_move(e);
}

/* Act 2 (The City). HP and move values come from the city monster sources. */
static void setup_act2(int kind) {
    g_battle.enemy_count = 0;
    if (kind == NODE_ELITE) {
        /* The city's elite pool: Book of Stabbing, Gremlin Leader, Slavers. */
        int elite = (int)(battle_rand() % 100);
        if (elite >= 67) {
            setup_act2_slavers();
            return;
        }
        if (elite >= 34) {
            setup_act2_gremlin_leader();
            return;
        }
        add_enemy(0, "大书怪", 160 + (int)(battle_rand() % 5));
        Enemy *e = &g_battle.enemies[0];
        e->moves = STAB_MOVES;
        e->move_count = 2;
        e->stab_count = 1;
        e->painful_stabs = 1;
        /* AbstractMonster.init() rolls the first move before combat starts. */
        e->last_move = 255;
        e->last_before = 255;
        choose_move(e);
        g_battle.enemy_count = 1;
        return;
    }
    if (kind == NODE_BOSS) {
        /* The city boss pool is the Champ, the Automaton or the Collector. */
        int roll = (int)(battle_rand() % 100);
        if (roll < 34) setup_act2_champ();
        else if (roll < 67) setup_act2_automaton();
        else setup_act2_collector();
        return;
    }
    if (kind == NODE_MONSTER) {
        int roll = (int)(battle_rand() % 100);
        setup_act2_monster(roll < 34 ? 0 : roll < 67 ? 1 : roll < 85 ? 2 : 3);
        return;
    }
    setup_act2_monster(0);
}

/* Line-ups depend on the node kind and on how deep the run is. */
static void setup_encounter(int kind) {
    int depth = g_map.row < 0 ? 0 : g_map.row;
    g_battle.enemy_count = 0;

    if (g_run.act >= 2) {
        setup_act2(kind);
        /* setup_act2 already placed the act 2 boss. */
        return;
    }

    if (kind == NODE_BOSS) {
        add_enemy(0, "史莱姆老大", 140);
        g_battle.enemies[0].moves = BOSS_MOVES;
        g_battle.enemies[0].move_count = 3;
        g_battle.enemy_count = 1;
        return;
    }
    if (kind == NODE_ELITE) {
        add_enemy(0, "地精大块头", 82);
        g_battle.enemies[0].moves = NOB_MOVES;
        g_battle.enemies[0].move_count = 3;
        g_battle.enemy_count = 1;
        return;
    }
    if (depth <= 2 || depth > 7) {
        add_enemy(0, "酸液史莱姆", 28 + (int)(battle_rand() % 5));
        add_enemy(1, "酸液史莱姆", 28 + (int)(battle_rand() % 5));
        for (int i = 0; i < 2; i++) {
            g_battle.enemies[i].moves = SLIME_MOVES;
            g_battle.enemies[i].move_count = 3;
            g_battle.enemies[i].move_index = 0;
        }
        g_battle.enemy_count = 2;
    } else {
        add_enemy(0, "颚虫", 40 + (int)(battle_rand() % 5));
        g_battle.enemies[0].moves = JAW_WORM_MOVES;
        g_battle.enemies[0].move_count = 3;
        g_battle.enemy_count = 1;
    }
}

void battle_start(int kind) {
    int hp = g_battle.hp;
    int max_hp = g_battle.max_hp;
    unsigned int rng_state = g_battle.rng;

    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = hp > 0 ? hp : max_hp;
    g_battle.max_hp = max_hp > 0 ? max_hp : 80;
    g_battle.max_energy = 3;
    g_battle.rng = rng_state ? rng_state : 0x5EED1234u;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    s_attacks_played = 0;
    s_attack_this_turn = 0;
    s_art_of_war_bonus = 0;
    s_red_skull_on = 0;
    g_run.kind = kind;

    for (int i = 0; i < g_run.deck_count; i++) g_battle.draw[g_battle.draw_count++] = g_run.deck[i];
    shuffle(g_battle.draw, g_battle.draw_count);
    setup_encounter(kind);
    g_run.screen = SCREEN_FIGHT;
    start_player_turn();
    apply_relics_on_start();
}

unsigned int battle_rand(void) { return rnd(); }

int battle_intent_kind(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return 0;
    const Enemy *e = &g_battle.enemies[index];
    return e->moves ? e->moves[e->move_index].kind : MOVE_ATTACK;
}

int battle_intent_value(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return 0;
    const Enemy *e = &g_battle.enemies[index];
    return e->moves ? e->moves[e->move_index].value : 0;
}

int battle_intent_block(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return 0;
    const Enemy *e = &g_battle.enemies[index];
    return e->moves ? e->moves[e->move_index].value2 : 0;
}

int battle_power(int id) { return pw(id); }

/* Relic effects that fire when combat starts. */
static void apply_relics_on_start(void) {
    if (battle_has_relic(RELIC_VAJRA)) g_battle.strength += 1;
    if (battle_has_relic(RELIC_ANCHOR)) gain_block(10);
    if (battle_has_relic(RELIC_BLOOD_VIAL)) {
        g_battle.hp += 2;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    if (battle_has_relic(RELIC_LANTERN)) g_battle.energy += 1;
    if (battle_has_relic(RELIC_BAG_OF_PREP)) draw_cards(2);
    if (battle_has_relic(RELIC_BAG_OF_MARBLES)) {
        /* ApplyPowerAction goes through ArtifactPower like any other debuff, so
         * a Spheric Guardian spends one stack instead of gaining Vulnerable. */
        for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, 0, 1);
    }
}

/* AbstractDungeon.rollRarity (:1577-1599): the 0..99 roll carries the card
 * blizzard offset, so a run of common rewards makes rares likelier. */
int battle_roll_rarity(int base_rare, int base_uncommon) {
    int roll = (int)(battle_rand() % 100u) + g_run.card_blizz;
    if (roll < base_rare) return 3;
    if (roll < base_rare + base_uncommon) return 2;
    return 1;
}

/* getRewardCards(): a rare puts the blizzard back to 5, a common lowers it. */
static void card_blizz_after_reward(int rarity) {
    if (rarity == 3) {
        g_run.card_blizz = 5;
    } else if (rarity == 1) {
        g_run.card_blizz--;
        if (g_run.card_blizz < -40) g_run.card_blizz = -40;
    }
}

static void roll_rewards(void) {
    g_run.reward_count = 0;
    while (g_run.reward_count < 3) {
        /* AbstractRoom: 3% rare, 37% uncommon, 60% common. */
        int want = battle_roll_rarity(3, 37);
        card_blizz_after_reward(want);
        int candidates[64];
        int count = 0;
        for (int pass = 0; pass < 2 && count == 0; pass++) {
            for (int i = 0; i < STS_CARD_COUNT; i++) {
                if (pass == 0 && sts_cards[i].rarity != want) continue;
                /* pass 1: any rarity, used only as a fallback */
                int in_pool = 0;
                for (size_t k = 0; k < sizeof(REWARDS) / sizeof(REWARDS[0]); k++) {
                    if (strcmp(sts_cards[i].id, REWARDS[k]) == 0) in_pool = 1;
                }
                if (!in_pool) continue;
                int dup = 0;
                for (int r = 0; r < g_run.reward_count; r++) {
                    if (g_run.reward[r] == i) dup = 1;
                }
                if (!dup && count < 64) candidates[count++] = i;
            }
        }
        if (count == 0) break;
        g_run.reward[g_run.reward_count++] = candidates[rnd() % (unsigned int)count];
    }
}

void run_start(void) {
    memset(&g_run, 0, sizeof(g_run));
    g_run.act = 1;
    g_run.card_blizz = 5;   /* AbstractDungeon.cardBlizzStartOffset */
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.rng = 0x5EED1234u;
    for (size_t i = 0; i < sizeof(STARTER) / sizeof(STARTER[0]); i++) {
        int index = battle_find_card(STARTER[i]);
        if (index >= 0 && g_run.deck_count < 64) g_run.deck[g_run.deck_count++] = CARD_ENTRY(index, 0);
    }
    battle_add_relic(RELIC_BURNING_BLOOD);
    map_generate();
    g_run.screen = SCREEN_MAP;
}

void run_restart(void) { run_start(); }

/* Damage numbers, all in one place. Weak = -25% dealt, Vulnerable = +50% taken. */
static int apply_weak(int amount, int weak) { return weak > 0 ? amount * 3 / 4 : amount; }
static int apply_vulnerable(int amount, int vulnerable) { return vulnerable > 0 ? amount * 3 / 2 : amount; }

/* StasisPower.onDeath: the stolen card comes back when the orb dies.  A full
 * hand sends it to the discard pile instead. */
static void on_enemy_death(int index) {
    Enemy *e = &g_battle.enemies[index];
    if (e->death_done) return;
    e->death_done = 1;
    if (e->stasis_card >= 0) {
        if (g_battle.hand_count < 10) {
            g_battle.hand[g_battle.hand_count++] = e->stasis_card;
        } else if (g_battle.discard_count < 64) {
            g_battle.discard[g_battle.discard_count++] = e->stasis_card;
        }
        e->stasis_card = -1;
    }
    /* BronzeAutomaton.die(): every other living monster in the room dies with
     * it. */
    if (e->moves == AUTOMATON_MOVES || e->moves == COLLECTOR_MOVES ||
        e->moves == GREMLIN_LEADER_MOVES) {
        for (int j = 0; j < g_battle.enemy_count; j++) {
            if (j == index) continue;
            if (g_battle.enemies[j].hp > 0) {
                g_battle.enemies[j].hp = 0;
                on_enemy_death(j);
            }
        }
    }
}

/* ApplyStasisAction: remove a card from the draw pile (or the discard pile when
 * the draw pile is empty), trying Rare, then Uncommon, then Common, then any
 * remaining card. */
static int stasis_take_card(void) {
    int *pile = g_battle.draw_count > 0 ? g_battle.draw : g_battle.discard;
    int *count = g_battle.draw_count > 0 ? &g_battle.draw_count : &g_battle.discard_count;
    int chosen = -1;
    for (int rarity = 3; rarity >= 1 && chosen < 0; rarity--) {
        int seen = 0;
        for (int i = 0; i < *count; i++) {
            if (sts_cards[CARD_INDEX(pile[i])].rarity == rarity) {
                seen++;
                if (rnd() % (unsigned int)seen == 0) chosen = i;
            }
        }
    }
    if (chosen < 0 && *count > 0) chosen = (int)(rnd() % (unsigned int)*count);
    if (chosen < 0) return -1;
    int entry = pile[chosen];
    for (int i = chosen; i < *count - 1; i++) pile[i] = pile[i + 1];
    (*count)--;
    return entry;
}

/* Damage to an enemy.  thorns = the hit came from Flame Barrier / Bronze
 * Scales: THORNS damage is not halved by Flight and does not spend Flight or
 * Plated Armor stacks (FlightPower/PlatedArmorPower both filter it out).
 * Returns the HP actually lost. */
static int damage_enemy_typed(int index, int amount, int thorns) {
    if (!enemy_alive(index) || amount <= 0) return 0;
    Enemy *e = &g_battle.enemies[index];
    amount = apply_vulnerable(amount, e->vulnerable);
    /* FlightPower.atDamageFinalReceive halves everything except HP_LOSS and
     * THORNS while the bird is in the air. */
    if (!thorns && e->moves == BYRD_MOVES && e->flight > 0) amount /= 2;
    int absorbed = e->block < amount ? e->block : amount;
    e->block -= absorbed;
    int lost = amount - absorbed;
    e->hp -= lost;
    if (e->hp < 0) e->hp = 0;
    if (e->hp <= 0) on_enemy_death(index);
    if (lost > 0 && e->hp > 0) {
        /* FlightPower.onAttacked: lose a stack on every hit that got through,
         * and land (Stunned) when the last one is spent. */
        if (!thorns && e->moves == BYRD_MOVES && e->flight > 0) {
            e->flight--;
            if (e->flight == 0) {
                e->grounded = 1;
                e->move_index = 3;   /* STUN */
            }
        }
        /* PlatedArmorPower.wasHPLost, then ARMOR_BREAK -> Stunned. */
        if (!thorns && e->plated > 0) {
            e->plated--;
            if (e->plated == 0) e->move_index = 3;
        }
        /* AngryPower.onAttacked: +1 Strength whenever the Gremlin Warrior takes
         * attack damage (blocked hits and THORNS do not count). */
        if (!thorns && e->moves == GREMLIN_WARRIOR_MOVES) e->strength++;
    }
    if (e->moves == BOSS_MOVES && e->hp > 0 && !e->split_done &&
        e->hp * 2 <= e->max_hp) {
        g_battle.pending_split = index;
    }
    return lost;
}

static void damage_enemy(int index, int amount) {
    (void)damage_enemy_typed(index, amount, 0);
}

static void damage_enemy_thorns(int index, int amount) {
    (void)damage_enemy_typed(index, amount, 1);
}

/* Returns the HP the player actually lost (0 while the hit was absorbed). */
static int damage_player_raw(int amount) {
    if (amount <= 0) return 0;
    int absorbed = g_battle.block < amount ? g_battle.block : amount;
    g_battle.block -= absorbed;
    if (amount - absorbed <= 0) return 0;
    g_battle.hp -= amount - absorbed;
    if (g_battle.hp < 0) g_battle.hp = 0;
    g_battle.damaged++;                 /* Blood for Blood counts HP actually lost */
    return amount - absorbed;
}

int battle_enemy_intent_damage(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return 0;
    Enemy *e = &g_battle.enemies[index];
    if (e->hp <= 0 || !e->moves) return 0;
    const EnemyMove *move = &e->moves[e->move_index];
    if (move->kind != MOVE_ATTACK && move->kind != MOVE_ATTACK_BLOCK && move->kind != MOVE_SLIME &&
        move->kind != MOVE_MULTI && move->kind != MOVE_ATTACK_DEBUFF &&
        move->kind != MOVE_HEADBUTT && move->kind != MOVE_SUCK &&
        move->kind != MOVE_ATTACK_WEAK && move->kind != MOVE_WOUND &&
        move->kind != MOVE_BLOCK_ATTACK) {
        return 0;
    }
    int base = move->kind == MOVE_BLOCK_ATTACK ? move->value2 : move->value;
    int amount = base + e->strength;
    if (amount < 0) amount = 0;
    amount = apply_weak(amount, e->weak);
    return apply_vulnerable(amount, g_battle.vulnerable);
}

int battle_intent_hits(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return 1;
    Enemy *e = &g_battle.enemies[index];
    if (!e->moves) return 1;
    const EnemyMove *move = &e->moves[e->move_index];
    if (move->kind != MOVE_MULTI) return 1;
    if (e->moves == STAB_MOVES) return e->stab_count > 0 ? e->stab_count : 1;
    return move->value2 > 0 ? move->value2 : 1;
}

static void hit_all(int amount) {
    for (int i = 0; i < g_battle.enemy_count; i++) damage_enemy(i, amount);
}

static int random_alive_enemy(void) {
    int alive[4];
    int count = 0;
    for (int i = 0; i < g_battle.enemy_count; i++) {
        if (enemy_alive(i)) alive[count++] = i;
    }
    return count == 0 ? -1 : alive[rnd() % (unsigned int)count];
}

/* Slime Boss splits at half HP into a Spike Slime (L) and an Acid Slime (L),
 * each with as much HP as the boss had left (SlimeBoss.java line 143-145). */
static void resolve_pending_split(void) {
    int index = g_battle.pending_split;
    g_battle.pending_split = -1;
    if (index < 0 || index >= g_battle.enemy_count) return;

    Enemy *boss = &g_battle.enemies[index];
    if (boss->hp <= 0 || boss->moves != BOSS_MOVES || boss->split_done) return;
    boss->split_done = 1;

    int hp_now = boss->hp;
    add_enemy(0, "尖刺史莱姆", hp_now);
    add_enemy(1, "酸液史莱姆", hp_now);
    g_battle.enemies[0].moves = SPIKE_L_MOVES;
    g_battle.enemies[0].move_count = 2;
    g_battle.enemies[1].moves = ACID_L_MOVES;
    g_battle.enemies[1].move_count = 3;
    g_battle.enemy_count = 2;
}

/* MagnetismPower.atStartOfTurn: MakeTempCardInHandAction with a random
 * colourless card (returnTrulyRandomColorlessCardInCombat). */
static void add_random_colorless_to_hand(void) {
    if (g_battle.hand_count >= 10) return;
    int pool[STS_CARD_COUNT];
    int n = 0;
    for (int i = 0; i < STS_CARD_COUNT; i++) {
        const CardDef *d = &sts_cards[i];
        if (d->color == 4 && d->rarity >= 1 && d->rarity <= 3) pool[n++] = i;
    }
    if (n == 0) return;
    int pick = pool[rnd() % (unsigned int)n];
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(pick, 0);
}

/* MayhemPower.atStartOfTurn -> PlayTopCardAction: the top card of the draw
 * pile is played for free against a random enemy, then discarded/exhausted. */
static void mayhem_play_top(void) {
    if (g_battle.draw_count <= 0 || g_battle.hand_count >= 10) return;
    int entry = g_battle.draw[--g_battle.draw_count];
    g_battle.hand[g_battle.hand_count++] = entry;
    int slot = g_battle.hand_count - 1;
    int target = sts_cards[CARD_INDEX(entry)].target == 0 ? random_alive_enemy() : -1;
    g_battle.free_play = 1;
    battle_play(slot, target);
    g_battle.free_play = 0;
}

static void gain_block(int amount) {
    if (amount <= 0) return;
    /* NoBlockPower.modifyBlockLast returns 0: while it is up, nothing grants
     * block.  Panic Button pays its own block before the power is applied. */
    if (g_battle.no_block_turns > 0) return;
    g_battle.block += amount;
    /* JuggernautPower.onGainedBlock: damage a random enemy on every block gain. */
    if (pw(POW_JUGGERNAUT)) {
        int target = random_alive_enemy();
        if (target >= 0) damage_enemy(target, pw(POW_JUGGERNAUT));
    }
}

/* SadisticPower.onApplyPower: every debuff the player applies to an enemy fires
 * one damage instance (Shockwave's weak+vulnerable therefore fires twice). */
static void debuff_enemy(int index, int weak, int vulnerable) {
    if (!enemy_alive(index)) return;
    Enemy *e = &g_battle.enemies[index];
    /* ArtifactPower.onSpecificTrigger consumes one stack per ApplyPowerAction,
     * so a source that applies both Weak and Vulnerable loses two stacks. */
    if (weak > 0) {
        if (e->artifact > 0) {
            e->artifact--;
        } else {
            e->weak += weak;
            if (pw(POW_SADISTIC)) damage_enemy(index, pw(POW_SADISTIC));
        }
    }
    if (vulnerable > 0) {
        if (e->artifact > 0) {
            e->artifact--;
        } else {
            e->vulnerable += vulnerable;
            if (pw(POW_SADISTIC)) damage_enemy(index, pw(POW_SADISTIC));
        }
    }
}

/* onCardDraw: Evolve draws on Statuses, Fire Breathing burns on Statuses and
 * Curses.  Called from draw_cards() for every card that enters the hand. */
static void on_card_drawn(int entry) {
    int index = CARD_INDEX(entry);
    if (index < 0 || index >= STS_CARD_COUNT) return;
    int type = sts_cards[index].type;
    if (pw(POW_FIRE_BREATHING) && (type == 3 || type == 4)) hit_all(pw(POW_FIRE_BREATHING));
    if (pw(POW_EVOLVE) && type == 3) draw_cards(pw(POW_EVOLVE));
    if (strcmp(sts_cards[index].id, "Void") == 0 && g_battle.energy > 0) {
        g_battle.energy -= 1;      /* Void: when drawn, lose 1 Energy */
    }
    if (strcmp(sts_cards[index].id, "Pain") == 0) {
        lose_hp_from_card(1);      /* Pain: when drawn, lose 1 HP */
    }
}

/* A card goes to the exhaust pile, and the onExhaust powers hear about it
 * (FeelNoPainPower.onExhaust, DarkEmbracePower.onExhaust). */
static void exhaust_card(int entry) {
    if (g_battle.exhaust_count < 64) g_battle.exhaust[g_battle.exhaust_count++] = entry;
    if (pw(POW_FEEL_NO_PAIN)) gain_block(pw(POW_FEEL_NO_PAIN));
    if (pw(POW_DARK_EMBRACE)) draw_cards(pw(POW_DARK_EMBRACE));
    {
        const CardDef *card = &sts_cards[CARD_INDEX(entry)];
        /* Sentinel.triggerOnExhaust: GainEnergyAction(2), or 3 upgraded. */
        if (strcmp(card->id, "Sentinel") == 0) {
            g_battle.energy += CARD_UPGRADED(entry) ? 3 : 2;
        }
        /* Necronomicurse.triggerOnExhaust: put a fresh copy back in hand.  It
         * is queued instead of inserted, because mass-exhaust effects iterate
         * the hand while they are still shrinking it. */
        if (strcmp(card->id, "Necronomicurse") == 0) g_battle.necro_pending++;
    }
}

/* Necronomicurse copies come back once the current effect is done with the
 * hand (Second Wind, Fiend Fire, ...) instead of mid-iteration. */
static void flush_necro(void) {
    int index = battle_find_card("Necronomicurse");
    while (g_battle.necro_pending > 0 && index >= 0 && g_battle.hand_count < 10) {
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(index, 0);
        g_battle.necro_pending--;
    }
    if (g_battle.hand_count >= 10) g_battle.necro_pending = 0;
}

/* The combat is over: potion drop, boss relic or reward roll, screen change. */
static void win_battle(void) {
    g_battle.phase = PHASE_WIN;
    if ((rnd() % 100) < 35) battle_add_random_potion();
    if (battle_has_relic(RELIC_BURNING_BLOOD)) {
        g_battle.hp += 6;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    if (g_run.kind == NODE_BOSS) {
        g_map.gold += 95 + (int)(battle_rand() % 11);
        for (int attempt = 0; attempt < 10; attempt++) {
            int id = (int)(battle_rand() % (unsigned int)RELIC_COUNT);
            if (battle_add_relic(id)) {
                snprintf(g_map.message, sizeof(g_map.message), "首领遗物：%s", battle_relic_name(id));
                break;
            }
        }
        /* AbstractDungeon.endAct: the run continues into the next act until
         * the last one is beaten.  The boss relic choice screen is still a
         * deviation: one random relic is handed over instead of three. */
        g_run.screen = g_run.act < ACT_COUNT ? SCREEN_ACT_CLEAR : SCREEN_VICTORY;
    } else {
        if (g_run.kind == NODE_ELITE) {
            g_run.gold_gain = 25 + (int)(battle_rand() % 11);
        } else {
            g_run.gold_gain = 10 + (int)(battle_rand() % 11);
        }
        if (battle_has_relic(RELIC_GOLDEN_IDOL)) g_run.gold_gain = g_run.gold_gain * 125 / 100;
        g_map.gold += g_run.gold_gain;
        roll_rewards();
        g_run.screen = SCREEN_REWARD;
    }
}

/* ---- card-choice screen ------------------------------------------------ */

static void pick_clear(void) {
    g_battle.pick_mode = PICK_NONE;
    g_battle.pick_extra = 0;
    g_battle.pick_sel = 0;
    g_battle.pick_count = 0;
    memset(g_battle.pick_source, 0, sizeof(g_battle.pick_source));
}

static void pick_push(int value) {
    if (g_battle.pick_count < 64) g_battle.pick_source[g_battle.pick_count++] = value;
}

/* Discovery's pool: any non-Basic, non-Special card in any colour. */
static int pick_discovery_ok(int index) {
    if (index < 0 || index >= STS_CARD_COUNT) return 0;
    const CardDef *c = &sts_cards[index];
    return c->rarity >= 1 && c->rarity <= 3 && c->color <= 4 && c->type <= 2;
}

/* Builds the list the player chooses from and switches to SCREEN_PICK.
 * Returns 0 (and stays in the fight) when there is nothing to choose. */
static int open_pick(int mode, int extra) {
    pick_clear();
    g_battle.pick_mode = mode;
    g_battle.pick_extra = extra;
    if (mode == PICK_UPGRADE_HAND || mode == PICK_EXHAUST_HAND || mode == PICK_TOP_HAND) {
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (mode == PICK_UPGRADE_HAND && CARD_UPGRADED(g_battle.hand[i])) continue;
            pick_push(i);
        }
    } else if (mode == PICK_COPY_HAND) {
        for (int i = 0; i < g_battle.hand_count; i++) {
            int type = sts_cards[CARD_INDEX(g_battle.hand[i])].type;
            if (type == 0 || type == 2) pick_push(i);   /* Attacks and Powers */
        }
    } else if (mode == PICK_EXHUME) {
        for (int i = 0; i < g_battle.exhaust_count; i++) pick_push(i);
    } else if (mode == PICK_DISCOVERY) {
        for (int attempt = 0; attempt < 200 && g_battle.pick_count < 3; attempt++) {
            int index = (int)(rnd() % (unsigned int)STS_CARD_COUNT);
            if (!pick_discovery_ok(index)) continue;
            int seen = 0;
            for (int i = 0; i < g_battle.pick_count; i++) {
                if (g_battle.pick_source[i] == index) seen = 1;
            }
            if (!seen) pick_push(index);
        }
    }
    if (g_battle.pick_count == 0) {
        pick_clear();
        return 0;
    }
    g_battle.pick_sel = 0;
    g_run.screen = SCREEN_PICK;
    return 1;
}

int battle_pick_active(void) { return g_battle.pick_mode != PICK_NONE; }
int battle_pick_mode(void) { return g_battle.pick_mode; }
int battle_pick_count(void) { return g_battle.pick_count; }
int battle_pick_sel(void) { return g_battle.pick_sel; }

int battle_pick_card(int index) {
    if (index < 0 || index >= g_battle.pick_count) return 0;
    int source = g_battle.pick_source[index];
    switch (g_battle.pick_mode) {
    case PICK_UPGRADE_HAND:
    case PICK_EXHAUST_HAND:
    case PICK_TOP_HAND:
    case PICK_COPY_HAND:
        return CARD_INDEX(g_battle.hand[source]);
    case PICK_EXHUME:
        return CARD_INDEX(g_battle.exhaust[source]);
    default:
        return source;
    }
}

int battle_pick_up(int index) {
    if (index < 0 || index >= g_battle.pick_count) return 0;
    if (g_battle.pick_mode == PICK_UPGRADE_HAND || g_battle.pick_mode == PICK_EXHAUST_HAND ||
        g_battle.pick_mode == PICK_TOP_HAND || g_battle.pick_mode == PICK_COPY_HAND) {
        return CARD_UPGRADED(g_battle.hand[g_battle.pick_source[index]]);
    }
    if (g_battle.pick_mode == PICK_EXHUME) {
        return CARD_UPGRADED(g_battle.exhaust[g_battle.pick_source[index]]);
    }
    return 0;
}

void battle_pick_move(int delta) {
    if (g_battle.pick_count <= 0) return;
    g_battle.pick_sel = (g_battle.pick_sel + delta) % g_battle.pick_count;
    if (g_battle.pick_sel < 0) g_battle.pick_sel += g_battle.pick_count;
}

static void hand_remove(int slot) {
    for (int i = slot; i < g_battle.hand_count - 1; i++) g_battle.hand[i] = g_battle.hand[i + 1];
    if (g_battle.hand_count > 0) g_battle.hand_count--;
}

static void exhaust_remove(int slot) {
    for (int i = slot; i < g_battle.exhaust_count - 1; i++) g_battle.exhaust[i] = g_battle.exhaust[i + 1];
    if (g_battle.exhaust_count > 0) g_battle.exhaust_count--;
}

int battle_pick_choose(int index) {
    if (g_run.screen != SCREEN_PICK || index < 0 || index >= g_battle.pick_count) return 0;
    int source = g_battle.pick_source[index];
    switch (g_battle.pick_mode) {
    case PICK_UPGRADE_HAND:
        g_battle.hand[source] = CARD_ENTRY(CARD_INDEX(g_battle.hand[source]), 1);
        break;
    case PICK_EXHAUST_HAND: {
        int entry = g_battle.hand[source];
        hand_remove(source);
        exhaust_card(entry);
        draw_cards(g_battle.pick_extra);
        flush_necro();
        break;
    }
    case PICK_TOP_HAND: {
        /* The caller already drew (Warcry, Thinking Ahead): putting the card
         * on top must not immediately draw it again. */
        int entry = g_battle.hand[source];
        hand_remove(source);
        if (g_battle.draw_count < 64) g_battle.draw[g_battle.draw_count++] = entry;
        break;
    }
    case PICK_COPY_HAND: {
        int entry = g_battle.hand[source];
        for (int i = 0; i < g_battle.pick_extra && g_battle.hand_count < 10; i++) {
            g_battle.hand[g_battle.hand_count++] = entry;
        }
        break;
    }
    case PICK_EXHUME: {
        int entry = g_battle.exhaust[source];
        exhaust_remove(source);
        if (g_battle.hand_count < 10) g_battle.hand[g_battle.hand_count++] = entry;
        break;
    }
    case PICK_DISCOVERY: {
        if (g_battle.hand_count < 10) {
            g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(source, 0);
            g_battle.zero_cost[source] = 1;   /* costs 0 for this combat */
        }
        break;
    }
    default:
        break;
    }
    pick_clear();
    g_run.screen = SCREEN_FIGHT;
    return 1;
}

/* Cancelling skips the effect.  Vanilla would not let the player cancel, but
 * the port has no other way back out of the screen. */
void battle_pick_cancel(void) {
    if (g_run.screen != SCREEN_PICK) return;
    pick_clear();
    g_run.screen = SCREEN_FIGHT;
}

/* Every card that needs more than "damage and/or block" is handled here. */
int battle_play(int slot, int target) {
    if (g_run.screen != SCREEN_FIGHT || g_battle.phase != PHASE_PLAYER) return -1;
    const CardDef *c = battle_card(slot);
    if (!c) return -1;
    /* EntanglePower: no Attacks until the end of the turn. */
    if (c->type == 0 && g_battle.entangled) return -7;
    /* Normality.canPlay: while it sits in hand, the fourth card of the turn
     * cannot be played. */
    if (s_cards_played_turn >= 3) {
        int normality = battle_find_card("Normality");
        if (normality >= 0) {
            for (int i = 0; i < g_battle.hand_count; i++) {
                if (CARD_INDEX(g_battle.hand[i]) == normality) return -6;
            }
        }
    }
    /* Clash is only playable when every other card in hand is an Attack. */
    if (strcmp(c->id, "Clash") == 0) {
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (i == slot) continue;
            if (sts_cards[CARD_INDEX(g_battle.hand[i])].type != 0) return -5;
        }
    }
    int up = battle_hand_up(slot);
    /* An upgraded Blind/Trip is card.target == 0 in the table but hits every
     * enemy, so -1 is a legal target for it. */
    int aoe_skill = (strcmp(c->id, "Blind") == 0 || strcmp(c->id, "Trip") == 0) && up;
    if (c->target == 0 && !enemy_alive(target) && !aoe_skill) return -4;

    int magic = card_magic(c, up);
    int cost = c->cost;
    /* BloodForBlood.tookDamage(): one energy off per HP-loss event, floor 0. */
    if (strcmp(c->id, "Blood for Blood") == 0) {
        cost -= g_battle.damaged;
        if (cost < 0) cost = 0;
    }
    /* CorruptionPower: every Skill costs 0 (they also exhaust, below). */
    if (pw(POW_CORRUPTION) && c->type == 1 && cost >= 0) cost = 0;
    int x_cost = 0;
    if (g_battle.free_play) {
        g_battle.free_play = 0;        /* Mayhem: PlayTopCardAction pays nothing */
    } else if (cost == -1) {
        x_cost = g_battle.energy;      /* X cost: spend everything (Whirlwind) */
        g_battle.energy = 0;
    } else if (cost < 0) {
        return -2;
    } else if (cost > g_battle.energy) {
        return -3;
    } else {
        g_battle.energy -= cost;
    }

    /* Gremlin Nob's Anger: +2 Strength for every Skill played. */
    if (c->type == 1) {
        for (int i = 0; i < g_battle.enemy_count; i++) {
            if (g_battle.enemies[i].moves == NOB_MOVES && g_battle.enemies[i].hp > 0) {
                g_battle.enemies[i].strength += 2;
            }
        }
    }

    int damage = card_damage(c, up);
    /* MindBlast.applyPowers(): damage is the draw pile size.  RitualDagger's
     * damage lives in misc (15), not the extracted damage column, so it is
     * built from the run-wide growth value here. */
    if (strcmp(c->id, "Mind Blast") == 0) damage = g_battle.draw_count;
    if (strcmp(c->id, "RitualDagger") == 0) damage = 15 + g_run.ritual_bonus;
    int strength_scale = 1;
    if (strcmp(c->id, "Heavy Blade") == 0) strength_scale = magic > 0 ? magic : 3;
    damage += g_battle.strength * strength_scale;
    if (strcmp(c->id, "Body Slam") == 0) damage = g_battle.block;
    if (strcmp(c->id, "Perfected Strike") == 0) {
        int strike_cards = 0;
        for (int i = 0; i < g_battle.draw_count; i++) {
            if (strstr(sts_cards[CARD_INDEX(g_battle.draw[i])].name, "打击")) strike_cards++;
        }
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (strstr(sts_cards[CARD_INDEX(g_battle.hand[i])].name, "打击")) strike_cards++;
        }
        damage += strike_cards * 2;
    }
    if (strcmp(c->id, "Rampage") == 0) {
        damage += g_battle.rampage_bonus;
        g_battle.rampage_bonus += magic;   /* one shared counter, not per copy */
    }
    damage = apply_weak(damage, g_battle.weak);
    if (c->type == 0) {
        s_attacks_played++;
        s_attack_this_turn = 1;
        /* RagePower.onUseCard: every Attack played grants its block. */
        if (g_battle.rage_block > 0) gain_block(g_battle.rage_block);
        if (s_attacks_played % 10 == 0) {
            if (battle_has_relic(RELIC_PEN_NIB)) damage *= 2;
            if (battle_has_relic(RELIC_NUNCHAKU)) g_battle.energy += 1;
        }
    }
    if (g_cheat_triple) damage *= 3;

    int hits = 1;
    if (strcmp(c->id, "Twin Strike") == 0) hits = 2;
    if (strcmp(c->id, "Sword Boomerang") == 0) hits = magic > 0 ? magic : 3;
    if (strcmp(c->id, "Pummel") == 0) hits = magic > 0 ? magic : 4;
    if (strcmp(c->id, "Whirlwind") == 0) hits = x_cost;
    if (strcmp(c->id, "Fiend Fire") == 0) {
        /* Exhaust the rest of the hand first: one hit per card exhausted. */
        int burned = 0;
        int keep = g_battle.hand[slot];
        int rest[10];
        int rest_count = 0;
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (i == slot) continue;
            rest[rest_count++] = g_battle.hand[i];
        }
        g_battle.hand_count = 0;
        g_battle.hand[g_battle.hand_count++] = keep;
        slot = 0;
        for (int i = 0; i < rest_count; i++) {
            exhaust_card(rest[i]);
            burned++;
        }
        hits = burned;
    }

    int hp_before[4];
    for (int i = 0; i < g_battle.enemy_count; i++) hp_before[i] = g_battle.enemies[i].hp;

    if (damage > 0) {
        for (int h = 0; h < hits; h++) {
            if (c->target == 1) {
                hit_all(damage);
            } else if (strcmp(c->id, "Sword Boomerang") == 0) {
                int who = random_alive_enemy();
                if (who >= 0) damage_enemy(who, damage);
            } else {
                damage_enemy(target, damage);
            }
        }
    }

    int unblocked = 0;
    for (int i = 0; i < g_battle.enemy_count; i++) {
        int lost = hp_before[i] - g_battle.enemies[i].hp;
        if (lost > 0) unblocked += lost;
    }
    int killed_target = target >= 0 && target < g_battle.enemy_count && hp_before[target] > 0 &&
                        g_battle.enemies[target].hp <= 0;

    int block = card_block(c, up);
    /* FrailPower.modifyBlock: 25% less block, from cards only. */
    if (g_battle.frail > 0) block = block * 3 / 4;
    if (strcmp(c->id, "Entrench") == 0) {
        g_battle.block *= 2;                 /* double the block you already have */
    } else if (strcmp(c->id, "Power Through") == 0) {
        gain_block(block);
        for (int i = 0; i < 2; i++) {
            int wound = battle_find_card("Wound");
            if (wound >= 0 && g_battle.hand_count < 10) {
                g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(wound, 0);
            }
        }
    } else if (strcmp(c->id, "Second Wind") == 0) {
        /* SecondWind.java: BlockPerNonAttackAction(this.block) pays 5 per card,
         * so the card gets no flat block of its own (below). */
    } else if (block > 0) {
        gain_block(block);
    }

    /* Skills that turn the rest of the hand into fuel. */
    if (strcmp(c->id, "Second Wind") == 0 || strcmp(c->id, "Sever Soul") == 0) {
        int kept[10];
        int kept_count = 0;
        int burned = 0;
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (i == slot) {
                kept[kept_count++] = g_battle.hand[i];
                continue;
            }
            if (sts_cards[CARD_INDEX(g_battle.hand[i])].type != 0) {
                exhaust_card(g_battle.hand[i]);
                burned++;
            } else {
                kept[kept_count++] = g_battle.hand[i];
            }
        }
        memcpy(g_battle.hand, kept, sizeof(int) * (size_t)kept_count);
        g_battle.hand_count = kept_count;
        if (strcmp(c->id, "Second Wind") == 0) gain_block(block * burned);
    }

    if (strcmp(c->id, "Bite") == 0) {
        g_battle.hp += magic;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    if (strcmp(c->id, "Flash of Steel") == 0) draw_cards(1);
    if (strcmp(c->id, "HandOfGreed") == 0 && killed_target) g_map.gold += magic;
    if (strcmp(c->id, "RitualDagger") == 0 && killed_target) g_run.ritual_bonus += magic;
    if (strcmp(c->id, "Reaper") == 0 && unblocked > 0) {
        g_battle.hp += unblocked;
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    if (strcmp(c->id, "Feed") == 0 && killed_target) {
        g_battle.max_hp += magic;
        g_battle.hp += magic;
    }
    if (strcmp(c->id, "Bandage Up") == 0) {
        g_battle.hp += magic;                 /* HealAction */
        if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    }
    if (strcmp(c->id, "Deep Breath") == 0) {
        /* EmptyDeckShuffleAction then ShuffleAction, then one more draw. */
        int room = 64 - g_battle.draw_count;
        int n = g_battle.discard_count < room ? g_battle.discard_count : room;
        if (n > 0) {
            memcpy(&g_battle.draw[g_battle.draw_count], g_battle.discard, sizeof(int) * (size_t)n);
            for (int i = 0; i < n; i++) g_battle.discard[i] = g_battle.discard[i + n];
            g_battle.draw_count += n;
            g_battle.discard_count -= n;
            shuffle(g_battle.draw, g_battle.draw_count);
        }
        draw_cards(magic);
    }
    if (strcmp(c->id, "Finesse") == 0) draw_cards(1);          /* block is generic */
    if (strcmp(c->id, "Master of Strategy") == 0) draw_cards(magic);
    if (strcmp(c->id, "Offering") == 0) {
        lose_hp_from_card(6);   /* LoseHPAction(p, p, 6) is a literal */
        g_battle.energy += 2;   /* GainEnergyAction(2) is a literal */
        draw_cards(magic);
    }
    if (strcmp(c->id, "J.A.X.") == 0) {
        lose_hp_from_card(3);   /* LoseHPAction(p, p, 3) is a literal */
        g_battle.strength += magic;
    }
    if (strcmp(c->id, "Jack Of All Trades") == 0) {
        for (int i = 0; i < magic; i++) add_random_colorless_to_hand();
    }
    /* Blind and Trip only change from one target to all of them when upgraded
     * (the table's up_magic is 0); the stacks stay the same. */
    if (strcmp(c->id, "Blind") == 0) {
        if (up) {
            for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, magic, 0);
        } else if (enemy_alive(target)) {
            debuff_enemy(target, magic, 0);
        }
    }
    if (strcmp(c->id, "Trip") == 0) {
        if (up) {
            for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, 0, magic);
        } else if (enemy_alive(target)) {
            debuff_enemy(target, 0, magic);
        }
    }
    if (strcmp(c->id, "Disarm") == 0 && enemy_alive(target)) {
        /* StrengthPower(m, -magic), or Artifact eats it first. */
        if (g_battle.enemies[target].artifact > 0) g_battle.enemies[target].artifact--;
        else g_battle.enemies[target].strength -= magic;
    }
    /* RagePower: the power grants block for each Attack, starting next card. */
    if (strcmp(c->id, "Rage") == 0) g_battle.rage_block += magic;
    /* FlameBarrierPower: thorns against whoever hits us this enemy turn. */
    if (strcmp(c->id, "Flame Barrier") == 0) g_battle.flame_barrier = magic;
    /* NoBlockPower: no block from any source for its duration. */
    if (strcmp(c->id, "PanicButton") == 0) g_battle.no_block_turns = magic;
    /* TheBombPower: three turns of fuse, then one big hit on everything. */
    if (strcmp(c->id, "The Bomb") == 0) {
        g_battle.bomb_turns = 3;
        g_battle.bomb_damage = magic;
    }
    if (strcmp(c->id, "Hemokinesis") == 0) lose_hp_from_card(magic);
    if (strcmp(c->id, "Bloodletting") == 0) {
        g_battle.energy += magic;
        lose_hp_from_card(3);
    }
    /* SeeingRed.java: GainEnergyAction(2) is a literal in use(), with no
     * magicNumber field, so the extracted table has magic 0 for it. */
    if (strcmp(c->id, "Seeing Red") == 0) g_battle.energy += 2;
    if (strcmp(c->id, "Inflame") == 0) g_battle.strength += magic;
    if (strcmp(c->id, "Limit Break") == 0) g_battle.strength *= 2;
    if (strcmp(c->id, "Battle Trance") == 0) {
        draw_cards(magic);
        g_battle.no_draw = 1;
    }
    if (strcmp(c->id, "Wild Strike") == 0) {
        int wound = battle_find_card("Wound");
        if (wound >= 0 && g_battle.discard_count < 64) {
            g_battle.discard[g_battle.discard_count++] = CARD_ENTRY(wound, 0);
        }
    }
    /* Immolate: MakeTempCardInDiscardAction(new Burn(), 1). */
    if (strcmp(c->id, "Immolate") == 0) {
        int burn = battle_find_card("Burn");
        if (burn >= 0 && g_battle.discard_count < 64) {
            g_battle.discard[g_battle.discard_count++] = CARD_ENTRY(burn, 0);
        }
    }
    if (strcmp(c->id, "Reckless Charge") == 0) {
        int dazed = battle_find_card("Dazed");
        if (dazed >= 0 && g_battle.draw_count < 64) {
            int at = (int)(rnd() % (unsigned int)(g_battle.draw_count + 1));
            for (int i = g_battle.draw_count; i > at; i--) g_battle.draw[i] = g_battle.draw[i - 1];
            g_battle.draw[at] = CARD_ENTRY(dazed, 0);
            g_battle.draw_count++;
        }
    }
    if (strcmp(c->id, "Intimidate") == 0) {
        for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, magic, 0);
    }
    if (strcmp(c->id, "Shockwave") == 0) {
        for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, magic, magic);
    }
    if (strcmp(c->id, "Spot Weakness") == 0 && enemy_alive(target)) {
        int kind = battle_intent_kind(target);
        if (kind == MOVE_ATTACK || kind == MOVE_ATTACK_BLOCK || kind == MOVE_MULTI ||
            kind == MOVE_ATTACK_DEBUFF || kind == MOVE_SLIME) {
            g_battle.strength += magic;
        }
    }
    if (strcmp(c->id, "Dropkick") == 0 && enemy_alive(target) &&
        g_battle.enemies[target].vulnerable > 0) {
        g_battle.energy += 1;
        draw_cards(1);
    }

    if (enemy_alive(target)) {
        if (strcmp(c->id, "Bash") == 0) debuff_enemy(target, 0, magic);
        if (strcmp(c->id, "Clothesline") == 0) debuff_enemy(target, magic, 0);
        if (strcmp(c->id, "Uppercut") == 0) debuff_enemy(target, magic, magic);
    }
    if (c->target == 1 && strcmp(c->id, "Thunderclap") == 0) {
        for (int i = 0; i < g_battle.enemy_count; i++) debuff_enemy(i, 0, 1);
    }
    if (strcmp(c->id, "Anger") == 0 && g_battle.discard_count < 64) {
        g_battle.discard[g_battle.discard_count++] = g_battle.hand[slot];
    }
    if (strcmp(c->id, "Pommel Strike") == 0 || strcmp(c->id, "Shrug It Off") == 0) {
        draw_cards(strcmp(c->id, "Pommel Strike") == 0 ? (magic > 0 ? magic : 1) : 1);
    }
    if (strcmp(c->id, "Flex") == 0) {
        g_battle.strength += magic;
        g_battle.temp_strength += magic;
    }
    if (strcmp(c->id, "Headbutt") == 0 && g_battle.discard_count > 0) {
        /* Put the most recently discarded card back on top of the draw pile. */
        int card = g_battle.discard[--g_battle.discard_count];
        if (g_battle.draw_count < 64) {
            g_battle.draw[g_battle.draw_count++] = card;
        }
    }
    if (strcmp(c->id, "True Grit") == 0 && g_battle.hand_count > 1) {
        int victim = (int)(rnd() % (unsigned int)g_battle.hand_count);
        if (victim != slot) {
            for (int i = victim; i < g_battle.hand_count - 1; i++) g_battle.hand[i] = g_battle.hand[i + 1];
            g_battle.hand_count--;
        }
    }

    /* PanachePower.onUseCard counts every card played; the card that puts
     * Panache in play does not count itself, so this runs first. */
    if (pw(POW_PANACHE) && --g_battle.panache_left <= 0) {
        hit_all(pw(POW_PANACHE));
        g_battle.panache_left = 5;
    }

    /* Power cards: the effect lives on the player for the rest of the combat. */
    if (strcmp(c->id, "Barricade") == 0) add_pw(POW_BARRICADE, 1);
    if (strcmp(c->id, "Berserk") == 0) {
        g_battle.vulnerable += magic;   /* Berserk also makes you Vulnerable */
        add_pw(POW_BERSERK, 1);
    }
    if (strcmp(c->id, "Brutality") == 0) add_pw(POW_BRUTALITY, 1);
    if (strcmp(c->id, "Combust") == 0) {
        add_pw(POW_COMBUST, magic);
        g_battle.combust_hp += 1;   /* CombustPower.stackPower: +1 HP per copy */
    }
    if (strcmp(c->id, "Corruption") == 0) add_pw(POW_CORRUPTION, 1);
    if (strcmp(c->id, "Dark Embrace") == 0) add_pw(POW_DARK_EMBRACE, 1);
    if (strcmp(c->id, "Demon Form") == 0) add_pw(POW_DEMON_FORM, magic);
    if (strcmp(c->id, "Evolve") == 0) add_pw(POW_EVOLVE, magic);
    if (strcmp(c->id, "Feel No Pain") == 0) add_pw(POW_FEEL_NO_PAIN, magic);
    if (strcmp(c->id, "Fire Breathing") == 0) add_pw(POW_FIRE_BREATHING, magic);
    if (strcmp(c->id, "Juggernaut") == 0) add_pw(POW_JUGGERNAUT, magic);
    if (strcmp(c->id, "Magnetism") == 0) add_pw(POW_MAGNETISM, magic);
    if (strcmp(c->id, "Mayhem") == 0) add_pw(POW_MAYHEM, magic);
    if (strcmp(c->id, "Metallicize") == 0) add_pw(POW_METALLICIZE, magic);
    if (strcmp(c->id, "Panache") == 0) {
        add_pw(POW_PANACHE, magic);   /* PanachePower.stackPower adds to damage */
        g_battle.panache_left = 5;
    }
    if (strcmp(c->id, "Rupture") == 0) add_pw(POW_RUPTURE, magic);
    if (strcmp(c->id, "Sadistic Nature") == 0) add_pw(POW_SADISTIC, magic);

    int entry = g_battle.hand[slot];
    for (int i = slot; i < g_battle.hand_count - 1; i++) g_battle.hand[i] = g_battle.hand[i + 1];
    g_battle.hand_count--;

    /* Status cards are exhausted when played, not discarded (vanilla).
     * Everything else in EXHAUSTS carries the Exhaust keyword, and Corruption
     * exhausts every Skill.  Power cards stay in play and never come back. */
    if (card_exhausts(c) || (pw(POW_CORRUPTION) && c->type == 1)) {
        exhaust_card(entry);
    } else if (c->type == 2) {
        /* power: removed from the deck for this combat */
    } else if (g_battle.discard_count < 64) {
        g_battle.discard[g_battle.discard_count++] = entry;
    }

    /* Cards that make the player pick from a pile open the picker after the
     * played card has already left the hand (so it cannot pick itself). */
    if (strcmp(c->id, "Armaments") == 0) {
        if (up) {
            for (int i = 0; i < g_battle.hand_count; i++) {
                g_battle.hand[i] = CARD_ENTRY(CARD_INDEX(g_battle.hand[i]), 1);
            }
        } else {
            open_pick(PICK_UPGRADE_HAND, 0);
        }
    } else if (strcmp(c->id, "Burning Pact") == 0) {
        open_pick(PICK_EXHAUST_HAND, magic);
    } else if (strcmp(c->id, "Warcry") == 0) {
        /* Warcry.java:29-32: DrawCardAction runs before PutOnDeckAction, so
         * the card the player puts back cannot be the one just drawn. */
        draw_cards(magic);
        open_pick(PICK_TOP_HAND, 0);
    } else if (strcmp(c->id, "Dual Wield") == 0) {
        open_pick(PICK_COPY_HAND, magic);
    } else if (strcmp(c->id, "Exhume") == 0) {
        open_pick(PICK_EXHUME, 0);
    } else if (strcmp(c->id, "Thinking Ahead") == 0) {
        draw_cards(2);                      /* draw first, then place one back */
        open_pick(PICK_TOP_HAND, 0);
    } else if (strcmp(c->id, "Discovery") == 0) {
        open_pick(PICK_DISCOVERY, 0);
    }

    /* HexPower.onUseCard fires from UseCardAction, after the card's own
     * actions: every non-Attack card puts one Dazed per Hex stack in the draw
     * pile at a random position. */
    if (c->type != 0 && g_battle.hex > 0) {
        int dazed = battle_find_card("Dazed");
        for (int n = 0; n < g_battle.hex && dazed >= 0 && g_battle.draw_count < 64; n++) {
            int at = (int)(rnd() % (unsigned int)(g_battle.draw_count + 1));
            for (int i = g_battle.draw_count; i > at; i--) g_battle.draw[i] = g_battle.draw[i - 1];
            g_battle.draw[at] = CARD_ENTRY(dazed, 0);
            g_battle.draw_count++;
        }
    }

    flush_necro();
    resolve_pending_split();
    s_cards_played_turn++;

    if (alive_count() == 0) win_battle();
    return 0;
}

/* Everything the player side does at the end of the turn, before the enemies
 * act: atEndOfTurnPreEndTurnCards (Metallicize), atEndOfTurn (Combust), the
 * curses that bite at the end of the turn and the discard pile.  Returns 0
 * when the combat has already ended (all enemies dead or the player died). */
static int end_turn_powers(void) {
    /* RagePower.atEndOfTurn: the block-per-attack expires with the turn. */
    g_battle.rage_block = 0;
    if (g_battle.temp_strength > 0) {
        g_battle.strength -= g_battle.temp_strength;
        g_battle.temp_strength = 0;
    }

    if (battle_has_relic(RELIC_ORICHALCUM) && g_battle.block == 0) {
        gain_block(6);
    }
    if (battle_has_relic(RELIC_ART_OF_WAR) && !s_attack_this_turn) {
        s_art_of_war_bonus = 1;
    }
    /* atEndOfTurnPreEndTurnCards: Metallicize grants block before the enemies
     * swing, so it covers this enemy turn. */
    if (pw(POW_METALLICIZE)) gain_block(pw(POW_METALLICIZE));
    /* CombustPower.atEndOfTurn: pay HP, then burn every enemy. */
    if (pw(POW_COMBUST)) {
        lose_hp_from_card(g_battle.combust_hp > 0 ? g_battle.combust_hp : 1);
        if (g_battle.hp <= 0) {
            g_battle.phase = PHASE_LOSE;
            g_run.screen = SCREEN_DEFEAT;
            return 0;
        }
        hit_all(pw(POW_COMBUST));
        if (alive_count() == 0) {
            win_battle();
            return 0;
        }
    }
    /* TheBombPower.atEndOfTurn: ReducePower 1 first, then detonate when the
     * last tick is spent. */
    if (g_battle.bomb_turns > 0) {
        g_battle.bomb_turns--;
        if (g_battle.bomb_turns == 0) {
            hit_all(g_battle.bomb_damage);
            if (alive_count() == 0) {
                win_battle();
                return 0;
            }
        }
    }
    {
        int regret = battle_find_card("Regret");
        int decay = battle_find_card("Decay");
        int doubt = battle_find_card("Doubt");
        int clumsy = battle_find_card("Clumsy");
        int carnage = battle_find_card("Carnage");
        int ghostly = battle_find_card("Ghostly Armor");
        int burn = battle_find_card("Burn");
        int pride = battle_find_card("Pride");
        for (int i = 0; i < g_battle.hand_count; i++) {
            int card = CARD_INDEX(g_battle.hand[i]);
            if (card == burn) {
                /* Burn: at the end of the turn, take its magic as damage. */
                damage_player_raw(sts_cards[burn].magic);
            } else if (card == pride) {
                /* Pride: at the end of the turn, grow a copy in the draw pile. */
                if (g_battle.draw_count < 64) {
                    g_battle.draw[g_battle.draw_count++] = CARD_ENTRY(pride, 0);
                }
            } else if (card == regret) {
                lose_hp_from_card(g_battle.hand_count);
            } else if (card == decay) {
                lose_hp_from_card(2);
            } else if (card == doubt) {
                g_battle.weak += 1;
            } else if (card == clumsy || card == carnage || card == ghostly) {
                /* Ethereal: exhausts at the end of the turn. */
                exhaust_card(g_battle.hand[i]);
                g_battle.hand[i] = -1;
            }
        }
        if (g_battle.hp <= 0) {
            g_battle.phase = PHASE_LOSE;
            g_run.screen = SCREEN_DEFEAT;
            return 0;
        }
    }

    while (g_battle.hand_count > 0) {
        int entry = g_battle.hand[g_battle.hand_count - 1];
        if (entry >= 0 && g_battle.discard_count < 64) {
            g_battle.discard[g_battle.discard_count++] = entry;
        }
        g_battle.hand_count--;
    }
    return 1;
}

void battle_end_turn_powers_for_test(void) { (void)end_turn_powers(); }

/* FlameBarrierPower.onAttacked: the attacker takes the thorns for every hit,
 * including each hit of a multi-attack. */
static void flame_hit(int enemy) {
    if (g_battle.flame_barrier > 0 && enemy_alive(enemy)) {
        /* THORNS damage: Flight does not halve it and it spends no stacks. */
        damage_enemy_thorns(enemy, g_battle.flame_barrier);
    }
}

/* PainfulStabsPower.onAttacked: every unblocked hit puts a Wound in the
 * discard pile (BookOfStabbing.java:66-75). */
static void painful_stab(void) {
    int wound = battle_find_card("Wound");
    if (wound >= 0 && g_battle.discard_count < 64) {
        g_battle.discard[g_battle.discard_count++] = CARD_ENTRY(wound, 0);
    }
}

/* One enemy attack, with everything the game hooks onto it: thorns, Bronze
 * Scales, and the Wound from Painful Stabs only when HP was actually lost. */
static int enemy_hit(Enemy *e, int index, int amount) {
    int lost = damage_player_raw(amount);
    if (lost > 0 && e->painful_stabs) painful_stab();
    flame_hit(index);
    if (battle_has_relic(RELIC_BRONZE_SCALES)) damage_enemy_thorns(index, 3);
    return lost;
}

/* One enemy's whole turn. Extracted from battle_end_turn so the card tests can
 * drive a turn directly and inspect the result. */
static void enemy_take_turn(int i) {
    Enemy *e = &g_battle.enemies[i];
    if (e->hp <= 0) return;
    /* BarricadePower keeps the block; otherwise it is gone at turn start. */
    if (!e->barricade) e->block = 0;
    /* FlightPower.atStartOfTurn restores the stored stacks while airborne. */
    if (e->flight_max > 0 && !e->grounded) e->flight = e->flight_max;
    /* PlatedArmorPower and MetallicizePower both sit in
     * atEndOfTurnPreEndTurnCards and grant block before the move. */
    if (e->plated > 0) e->block += e->plated;
    if (e->metallicize > 0) e->block += e->metallicize;
    if (e->moves) {
        const EnemyMove *move = &e->moves[e->move_index];
        int custom_roll = 0;
        switch (move->kind) {
        case MOVE_MULTI: {
            int hits = e->moves == STAB_MOVES ? e->stab_count : move->value2;
            if (hits <= 0) hits = 1;
            for (int h = 0; h < hits && g_battle.hp > 0; h++) {
                enemy_hit(e, i, battle_enemy_intent_damage(i));
            }
            break;
        }
        case MOVE_ATTACK:
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            /* GremlinWizard Ultimate Blast resets currentCharge to 0. */
            if (e->moves == GREMLIN_WIZARD_MOVES) e->ai_turn = 0;
            break;
        case MOVE_ATTACK_DEBUFF:
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            /* Face Slap's two debuffs; Fell only applies Frail.  A zero is a
             * "not used by this move", not one stack. */
            if (move->value2 > 0) g_battle.vulnerable += move->value2;
            if (move->value3 > 0) {
                g_battle.frail += move->value3;
                g_battle.frail_just = 1;
            }
            break;
        case MOVE_ATTACK_BLOCK:
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            e->block += move->value2;
            break;
        case MOVE_BLOCK_ATTACK:
            /* Spheric Guardian: GainBlockAction(15) then the 10 damage. */
            e->block += move->value;
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            break;
        case MOVE_BUFF:
            e->strength += move->value;
            e->block += move->value2;
            break;
        case MOVE_WEAK:
            g_battle.weak += move->value;
            break;
        case MOVE_VULN:
            g_battle.vulnerable += move->value;
            break;
        case MOVE_DEFEND:
            e->block += move->value;
            e->metallicize += move->value2;
            break;
        case MOVE_TAUNT:
            g_battle.weak += move->value;
            g_battle.vulnerable += move->value2;
            break;
        case MOVE_ANGER:
            /* Anger: +6 Strength and RemoveDebuffsAction on itself. */
            e->strength += move->value;
            e->vulnerable = 0;
            e->weak = 0;
            break;
        case MOVE_ATTACK_WEAK:
            /* SlaverBlue Rake: 7 damage, then 1 Weak. */
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            g_battle.weak += move->value2;
            break;
        case MOVE_ENTANGLE:
            /* EntanglePower: Attack cards are locked until the turn ends. */
            g_battle.entangled = 1;
            e->used_entangle = 1;
            break;
        case MOVE_WOUND: {
            /* Scouring Whip: 7 damage, then a Wound into the discard pile. */
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            int wound = battle_find_card("Wound");
            for (int k = 0; k < move->value2; k++) {
                if (wound >= 0 && g_battle.discard_count < 64) {
                    g_battle.discard[g_battle.discard_count++] = CARD_ENTRY(wound, 0);
                }
            }
            break;
        }
        case MOVE_DEFEND_BUFF:
            /* BronzeAutomaton Boost: +9 block, +3 Strength. */
            e->block += move->value;
            e->strength += move->value2;
            break;
        case MOVE_SPAWN:
            /* BronzeAutomaton Orbs, the Collector's Torch Heads, or the Gremlin
             * Leader's Rally (two empty minion slots). */
            if (e->moves == GREMLIN_LEADER_MOVES) spawn_gremlin_minions(i);
            else if (e->moves == COLLECTOR_MOVES) spawn_collector_heads(i);
            else spawn_automaton_orbs(i);
            break;
        case MOVE_COMMAND:
            /* GremlinLeader Encourage: itself +3 Strength, every living minion
             * +3 Strength and +6 block. */
            e->strength += move->value;
            for (int j = 0; j < g_battle.enemy_count; j++) {
                if (j != i && g_battle.enemies[j].hp > 0) {
                    g_battle.enemies[j].strength += move->value;
                    g_battle.enemies[j].block += move->value2;
                }
            }
            break;
        case MOVE_PROTECT: {
            /* GremlinTsundere Protect: 7 block to a random other monster; if
             * there is none, it protects itself. */
            int candidates[4];
            int n = 0;
            for (int j = 0; j < g_battle.enemy_count; j++) {
                if (j != i && g_battle.enemies[j].hp > 0) candidates[n++] = j;
            }
            int who = n > 0 ? candidates[rnd() % (unsigned int)n] : i;
            g_battle.enemies[who].block += move->value;
            break;
        }
        case MOVE_CHARGE:
            /* GremlinWizard charge counter; the next getMove decides whether
             * the third charge has become the 25-damage blast. */
            e->ai_turn++;
            break;
        case MOVE_STASIS:
            /* BronzeOrb Stasis: steal one card until the orb dies. */
            if (e->stasis_card < 0) e->stasis_card = (short)stasis_take_card();
            e->used_stasis = 1;
            break;
        case MOVE_SUPPORT: {
            /* BronzeOrb Support Beam: give its owner 12 block. */
            int owner = e->minion_owner;
            if (owner >= 0 && owner < g_battle.enemy_count &&
                g_battle.enemies[owner].hp > 0) {
                g_battle.enemies[owner].block += move->value;
            }
            break;
        }
        case MOVE_RALLY:
            /* Collector Buff: +15 block for itself, +3 Strength to every living
             * monster in the room (ApplyPowerAction over monster group). */
            e->block += move->value;
            for (int j = 0; j < g_battle.enemy_count; j++) {
                if (g_battle.enemies[j].hp > 0)
                    g_battle.enemies[j].strength += move->value2;
            }
            break;
        case MOVE_CURSE:
            /* Collector Mega Debuff: 3 Weak, 3 Vulnerable and 3 Frail.  Frail
             * was just applied, so its first end-of-round decrement is skipped. */
            g_battle.weak += move->value;
            g_battle.vulnerable += move->value;
            g_battle.frail += move->value;
            g_battle.frail_just = 1;
            e->used_ult = 1;
            break;
        case MOVE_REVIVE:
            revive_collector_heads(i);
            break;
        case MOVE_DRAIN:
            /* Chosen Drain: player 3 Weak, itself +3 Strength. */
            g_battle.weak += move->value;
            e->strength += move->value2;
            break;
        case MOVE_HEX:
            g_battle.hex += 1;
            break;
        case MOVE_FLY:
            e->grounded = 0;
            e->flight = e->flight_max;
            break;
        case MOVE_HEADBUTT:
            /* Byrd: 3 damage, then setMove(GO_AIRBORNE) and return - no
             * RollMoveAction this turn. */
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            e->last_before = e->last_move;
            e->last_move = 1;
            e->move_index = 1;
            e->first_turn = 0;
            custom_roll = 1;
            break;
        case MOVE_STUN:
            /* Once the stun is over, ShelledParasite sets Fell and still rolls
             * (getMove sees Fell in history and re-rolls 20..99). */
            if (e->moves == PARASITE_MOVES) {
                e->last_before = e->last_move;
                e->last_move = 0;
                e->first_turn = 0;
                choose_move_roll(e, 20 + rnd() % 80);
                custom_roll = 1;
            }
            break;
        case MOVE_SUCK: {
            /* VampireDamageAction heals by the HP the target actually lost. */
            int lost = enemy_hit(e, i, battle_enemy_intent_damage(i));
            if (lost > 0 && e->hp > 0) {
                e->hp += lost;
                if (e->hp > e->max_hp) e->hp = e->max_hp;
            }
            break;
        }
        case MOVE_SLIME: {
            enemy_hit(e, i, battle_enemy_intent_damage(i));
            int slimed = battle_find_card("Slimed");
            if (slimed >= 0 && g_battle.discard_count < 64) {
                g_battle.discard[g_battle.discard_count++] = CARD_ENTRY(slimed, 0);
            }
            break;
        }
        default:
            break;
        }
        /* TheCollector.takeTurn increments turnsTaken after the action; the
         * following getMove sees the new value. */
        if (e->moves == COLLECTOR_MOVES) e->ai_turn++;
        if (!custom_roll) {
            e->last_before = e->last_move;
            e->last_move = e->move_index;
            e->first_turn = 0;
            choose_move(e);
        }
    }
    if (e->vulnerable > 0) e->vulnerable--;
    if (e->weak > 0) e->weak--;
}

void battle_end_turn(void) {
    if (g_run.screen != SCREEN_FIGHT || g_battle.phase != PHASE_PLAYER) return;
    if (!end_turn_powers()) return;

    /* EntanglePower.atEndOfTurn: the lock is lifted as the turn ends. */
    g_battle.entangled = 0;
    g_battle.phase = PHASE_ENEMY;
    /* Enemies summoned during this enemy phase (the Automaton's orbs) do not
     * act until the next round, so the count is fixed up front. */
    int enemies_at_start = g_battle.enemy_count;
    for (int i = 0; i < enemies_at_start; i++) {
        enemy_take_turn(i);
    }

    if (g_battle.hp <= 0) {
        g_battle.phase = PHASE_LOSE;
        g_run.screen = SCREEN_DEFEAT;
        return;
    }
    g_battle.phase = PHASE_PLAYER;
    start_player_turn();
}

#if defined(CARD_TEST)
/* Spawns the act 2 elite/boss with a clean battle so the card tests start from
 * the same state the real encounter does (setup_act2 picks the opening move). */
void battle_test_spawn_act2(int boss) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2(boss ? NODE_BOSS : NODE_ELITE);
}

void battle_test_spawn_automaton(void) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2_automaton();
}

void battle_test_spawn_collector(void) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2_collector();
}

void battle_test_spawn_gremlin_leader(void) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2_gremlin_leader();
}

void battle_test_spawn_gremlin_minion(int slot, int which) {
    if (g_battle.enemy_count < 1) return;
    if (which < 0 || which > 4) return;
    spawn_gremlin_minion(0, slot, which);
}

/* Card tests need to kill a minion (or the boss) without constructing a hand. */
void battle_test_damage_enemy(int index, int amount) {
    if (index < 0 || index >= g_battle.enemy_count) return;
    (void)damage_enemy_typed(index, amount, 0);
}

void battle_test_spawn_slavers(void) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2_slavers();
}

void battle_test_spawn_act2_monster(int which) {
    memset(&g_battle, 0, sizeof(g_battle));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    setup_act2_monster(which);
}

void battle_test_apply_relics(void) { apply_relics_on_start(); }

void battle_test_bag_of_marbles(void) {
    battle_add_relic(RELIC_BAG_OF_MARBLES);
    apply_relics_on_start();
}

void battle_test_choose_roll(int index, unsigned int roll) {
    if (index < 0 || index >= g_battle.enemy_count) return;
    choose_move_roll(&g_battle.enemies[index], roll);
}

void battle_test_enemy_turn(int index) {
    if (index < 0 || index >= g_battle.enemy_count) return;
    enemy_take_turn(index);
}
#endif

void battle_choose_reward(int index) {
    if (g_run.screen != SCREEN_REWARD) return;
    if (index < 0 || index >= g_run.reward_count) return;
    if (g_run.deck_count < 64) g_run.deck[g_run.deck_count++] = CARD_ENTRY(g_run.reward[index], 0);
    map_after_node();
}

int battle_deck_count(void) { return g_run.deck_count; }

int battle_deck_card(int index) {
    return index < 0 || index >= g_run.deck_count ? -1 : CARD_INDEX(g_run.deck[index]);
}

int battle_deck_up(int index) {
    return index < 0 || index >= g_run.deck_count ? 0 : CARD_UPGRADED(g_run.deck[index]);
}

int battle_can_upgrade(void) {
    for (int i = 0; i < g_run.deck_count; i++) {
        if (!CARD_UPGRADED(g_run.deck[i])) return 1;
    }
    return 0;
}

void battle_open_upgrade(void) {
    if (g_run.screen != SCREEN_CAMPFIRE || !battle_can_upgrade()) return;
    g_run.screen = SCREEN_UPGRADE;
}

void battle_cancel_upgrade(void) {
    if (g_run.screen == SCREEN_UPGRADE) g_run.screen = SCREEN_CAMPFIRE;
}

int battle_confirm_upgrade(int deck_index) {
    if (g_run.screen != SCREEN_UPGRADE) return 0;
    if (deck_index < 0 || deck_index >= g_run.deck_count) return 0;
    if (CARD_UPGRADED(g_run.deck[deck_index])) return 0;
    g_run.deck[deck_index] = CARD_ENTRY(CARD_INDEX(g_run.deck[deck_index]), 1);
    snprintf(g_run.upgrade_name, sizeof(g_run.upgrade_name), "升级 %s",
             sts_cards[CARD_INDEX(g_run.deck[deck_index])].name);
    map_after_node();
    return 1;
}

void battle_campfire(int choice) {
    if (g_run.screen != SCREEN_CAMPFIRE) return;
    if (choice == 1) {
        battle_open_upgrade();
        return;
    }
    int heal = g_battle.max_hp * 30 / 100;
    g_battle.hp += heal;
    if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
    snprintf(g_run.upgrade_name, sizeof(g_run.upgrade_name), "回复 %d 生命", heal);
    map_after_node();
}
