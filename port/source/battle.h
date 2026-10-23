/* Combat + run rules for v0.2.
 *
 * Run: five linear nodes (fight, fight, campfire, fight, fight). HP carries
 * between fights; a won fight offers a 3-card reward; the campfire either heals
 * 30% or upgrades one un-upgraded card.
 *
 * Upgrades are approximate (+3 damage / +3 block / +1 magic) because the real
 * per-card deltas live in the game bytecode and are not extracted yet.
 */
#pragma once

typedef struct CardDef CardDef;

enum { PHASE_PLAYER = 0, PHASE_ENEMY, PHASE_WIN, PHASE_LOSE };

enum { SCREEN_FIGHT = 0, SCREEN_REWARD, SCREEN_CAMPFIRE, SCREEN_UPGRADE, SCREEN_SHOP,
       SCREEN_PURGE, SCREEN_EVENT, SCREEN_TREASURE, SCREEN_MAP, SCREEN_VICTORY, SCREEN_DEFEAT,
       SCREEN_ACT_CLEAR,   /* boss beaten, another act waits behind the next key press */
       SCREEN_PICK };      /* a card wants the player to choose from a pile */

/* A pile entry packs (card index | upgraded flag). */
#define CARD_ENTRY(card, up) ((card) | ((up) ? 0x100 : 0))
#define CARD_INDEX(entry) ((entry) & 0xFF)
#define CARD_UPGRADED(entry) (((entry) >> 8) & 1)

/* One entry of an enemy's move cycle. */
enum {
    MOVE_ATTACK = 0,     /* value = damage */
    MOVE_ATTACK_BLOCK,   /* value = damage, value2 = block for itself */
    MOVE_BUFF,           /* value = strength, value2 = block */
    MOVE_WEAK,           /* value = stacks of Weak on the player */
    MOVE_VULN,           /* value = stacks of Vulnerable on the player */
    MOVE_SLIME,          /* value = damage, also shuffles a 黏液 into the discard */
    MOVE_MULTI,          /* value = damage per hit, value2 = hits */
    MOVE_ATTACK_DEBUFF,  /* value = damage, value2 = Vulnerable, value3 = Frail */
    MOVE_DEFEND,         /* value = block for itself, value2 = Metallicize gained */
    MOVE_TAUNT,          /* value = Weak, value2 = Vulnerable on the player */
    MOVE_ANGER,          /* value = Strength; also removes its own debuffs */
    MOVE_DRAIN,          /* value = Weak on the player, value2 = Strength for itself */
    MOVE_HEX,            /* Chosen's Hex: non-Attack cards put Dazed in the draw pile */
    MOVE_FLY,            /* Byrd: GO_AIRBORNE, restores Flight */
    MOVE_HEADBUTT,       /* Byrd: value = damage, then it takes off again */
    MOVE_STUN,           /* a turn spent stunned while grounded / armour broken */
    MOVE_SUCK,           /* value = damage; heals itself for the HP actually lost */
    MOVE_BLOCK_ATTACK,   /* value = block for itself first, value2 = damage */
    MOVE_ATTACK_WEAK,    /* value = damage, value2 = Weak on the player */
    MOVE_ENTANGLE,       /* SlaverRed: EntanglePower, no Attack cards this turn */
    MOVE_WOUND,          /* value = damage, value2 = Wounds shuffled into discard */
    MOVE_DEFEND_BUFF,    /* value = block for itself, value2 = Strength gained */
    MOVE_SPAWN,          /* BronzeAutomaton: summon its two BronzeOrbs */
    MOVE_STASIS,         /* BronzeOrb: steal a card until the orb dies */
    MOVE_SUPPORT,        /* BronzeOrb: value = block given to its owner */
    MOVE_RALLY,          /* Collector: value = own block, value2 = Strength to all */
    MOVE_CURSE,          /* Collector: value = Weak/Vulnerable/Frail on the player */
    MOVE_REVIVE,         /* Collector: bring its dead Torch Heads back */
    MOVE_COMMAND,        /* Gremlin Leader: value = Strength, value2 = ally block */
    MOVE_PROTECT,        /* Gremlin Tsundere: value = block to a random ally */
    MOVE_CHARGE,         /* Gremlin Wizard: charge; at currentCharge 3 it blasts 25 */
    MOVE_PREP,           /* Slime Boss: the no-op Prep turn (UNKNOWN intent) */
    MOVE_FRAIL,          /* value = Frail on the player, no damage */
};

/* Act 2 uses the same room chances as Act 1 (TheCity.java), so only the
 * encounters change. */
#define ACT_COUNT 2

typedef struct {
    unsigned char kind;
    signed char value;
    signed char value2;
    signed char value3;   /* third stack count, kind-specific (Frail for Slap) */
} EnemyMove;

typedef struct {
    const char *name;
    const char *art;          /* sprite key; display name stays localised */
    int hp;
    int max_hp;
    int block;
    int vulnerable;
    int weak;
    int strength;
    const EnemyMove *moves;
    unsigned char move_count;
    unsigned char move_index;
    unsigned char last_move;
    unsigned char last_before;
    unsigned char first_turn;
    unsigned char used_bellow;
    unsigned char split_done;
    unsigned char metallicize;   /* MetallicizePower: block gained at end of its turn */
    unsigned char stab_count;    /* Book of Stabbing: hits of the next Multi-Stab */
    unsigned char ai_turn;       /* Champ: numTurns in getMove() */
    unsigned char forge_times;   /* Champ: Defensive Stance uses left */
    unsigned char threshold;     /* Champ: half-HP Anger has fired */
    unsigned char painful_stabs; /* Book: unblocked hits put a Wound in the discard */
    unsigned char flight;        /* Byrd: FlightPower stacks (damage is halved) */
    unsigned char flight_max;    /* Byrd: FlightPower.storedAmount, restored each turn */
    unsigned char grounded;      /* Byrd: FlightPower.onRemove has grounded it */
    unsigned char plated;        /* Shelled Parasite: Plated Armor stacks */
    unsigned char artifact;      /* Spheric Guardian: Artifact stacks */
    unsigned char barricade;     /* Spheric Guardian: block is not cleared each turn */
    unsigned char used_hex;      /* Chosen: Hex is used exactly once */
    unsigned char second_move;   /* Spheric Guardian: the scripted second move is done */
    unsigned char used_entangle; /* SlaverRed: ENTANGLE is used exactly once */
    unsigned char used_stasis;   /* BronzeOrb: STASIS is used exactly once */
    unsigned char used_ult;      /* Collector: Mega Debuff is used exactly once */
    unsigned char shackled;      /* GainStrengthPower: Strength restored at end of turn */
    unsigned char death_done;    /* on_death has already run for this enemy */
    unsigned char draw_w;        /* custom hitbox width, 0 = use the old layout */
    signed char minion_owner;    /* BronzeOrb: index of its BronzeAutomaton */
    short stasis_card;           /* BronzeOrb: stolen pile entry, -1 = none */
    short draw_x;                /* custom hitbox x, 0 = use the old layout */
} Enemy;

/* Powers the port implements so far.  Vanilla keeps one AbstractPower object
 * per power on the player; a plain int per card is enough here. */
#define POW_BARRICADE 0
#define POW_BERSERK 1
#define POW_BRUTALITY 2
#define POW_COMBUST 3
#define POW_CORRUPTION 4
#define POW_DARK_EMBRACE 5
#define POW_DEMON_FORM 6
#define POW_EVOLVE 7
#define POW_FEEL_NO_PAIN 8
#define POW_FIRE_BREATHING 9
#define POW_JUGGERNAUT 10
#define POW_MAGNETISM 11
#define POW_MAYHEM 12
#define POW_METALLICIZE 13
#define POW_PANACHE 14
#define POW_RUPTURE 15
#define POW_SADISTIC 16
#define POW_COUNT 17

/* Which pile a card effect is asking the player to choose from. */
#define PICK_NONE 0
#define PICK_UPGRADE_HAND 1    /* Armaments: upgrade one card in hand */
#define PICK_EXHAUST_HAND 2    /* Burning Pact: exhaust one card, draw */
#define PICK_TOP_HAND 3        /* Warcry / Thinking Ahead: card to draw pile top */
#define PICK_COPY_HAND 4       /* Dual Wield: copy an Attack or Power */
#define PICK_EXHUME 5          /* Exhume: take a card back out of the exhaust pile */
#define PICK_DISCOVERY 6       /* Discovery: one of three random cards */
#define PICK_DRAW_SKILL 7      /* Secret Technique: take a Skill from the draw pile */
#define PICK_DRAW_ATTACK 8     /* Secret Weapon: take an Attack from the draw pile */

typedef struct {
    int hp;
    int max_hp;
    int block;
    int energy;
    int max_energy;
    int strength;
    int vulnerable;
    int weak;
    int frail;           /* FrailPower: card block is 25% lower */
    int frail_just;      /* FrailPower.justApplied: skip the first decrement */
    int hex;             /* HexPower: Dazed added per non-Attack card played */
    int entangled;       /* EntanglePower: Attack cards are locked until turn end */
    int artifact;        /* ArtifactPower: negates one debuff per stack */
    int intangible;      /* IntangiblePlayerPower: all damage taken is 1 */

    int draw[64];
    int draw_count;
    int hand[10];
    int hand_count;
    int discard[64];
    int discard_count;
    int exhaust[64];
    int exhaust_count;
    int no_draw;
    int no_block_turns;      /* NoBlockPower: cards cannot grant block */
    int rage_block;          /* RagePower: block per attack this turn */
    int flame_barrier;       /* FlameBarrierPower: thorns for this enemy turn */
    int bomb_turns;          /* TheBombPower: turns left, then it detonates */
    int bomb_damage;         /* TheBombPower: damage when the fuse runs out */
    int rampage_bonus;   /* Rampage's permanent per-combat damage growth */
    int powers[POW_COUNT];
    int panache_left;    /* Panache: cards still to play before it fires */
    int combust_hp;      /* Combust: HP it costs at the end of turn, one per copy */
    int free_play;       /* Mayhem: the card being played costs no energy */
    int damaged;         /* HP-loss events this combat (Blood for Blood) */
    int necro_pending;   /* Necronomicurse copies waiting for the hand */
    unsigned char zero_cost[160];   /* card indexes that cost 0 this combat */
    unsigned char zero_turn[160];   /* card indexes that cost 0 this turn */
    unsigned char cost_one_turn[160];   /* Enlightenment: cost 1 this turn */
    unsigned char cost_one_combat[160]; /* Enlightenment+: cost 1 for the combat */
    int force_exhaust;              /* the next card played is exhausted (Havoc) */
    int pick_mode;       /* PICK_* while SCREEN_PICK is up */
    int pick_extra;      /* draw count / copy count / chosen count */
    int pick_sel;
    int pick_count;
    int pick_source[64]; /* hand slots, exhaust indexes or card indexes */

    int turn;
    int phase;
    int enemy_count;
    Enemy enemies[4];
    unsigned int rng;
    int temp_strength;
    int pending_split;   /* enemy index waiting to split, -1 = none */   /* from Flex: removed at the end of the player turn */
} Battle;

typedef struct {
    int node;
    int kind;
    int act;
    int gold_gain;
    unsigned char relics[8];
    int relic_count;
    unsigned char potions[3];
    int potion_count;   /* NODE_* of the map node being played */
    int screen;
    int deck[64];
    int deck_count;
    int reward[3];
    int reward_count;
    char upgrade_name[32];
    int campfire_choice;   /* 0 = rest, 1 = upgrade */
    int card_blizz;        /* cardBlizzRandomizer: +5 back on every rare reward */
    int ritual_bonus;      /* Ritual Dagger's permanent growth (one shared value) */
} Run;

extern Battle g_battle;
extern Run g_run;

void run_start(void);
void run_restart(void);
/* Starts a fight for a node kind (see run.h for NODE_*). */
void battle_start(int kind);
unsigned int battle_rand(void);

int battle_hand_card(int slot);
int battle_hand_up(int slot);
int battle_find_card(const char *id);
const CardDef *battle_card(int slot);
int battle_card_cost(int slot);
int battle_cost_of(const CardDef *c);
int battle_card_targeted(int slot);
/* Card-choice screen (Armaments, Burning Pact, ...). */
int battle_pick_active(void);
int battle_pick_mode(void);
int battle_pick_count(void);
int battle_pick_card(int index);
int battle_pick_up(int index);
int battle_pick_sel(void);
void battle_pick_move(int delta);
int battle_pick_choose(int index);
void battle_pick_cancel(void);
int battle_play(int slot, int target);
void battle_end_turn(void);

/* rollRarity(base_rare, base_uncommon): 3 = rare, 2 = uncommon, 1 = common. */
int battle_roll_rarity(int base_rare, int base_uncommon);
int battle_relic_tier(int id);
int battle_relic_price(int id);
int battle_potion_price(int id);

void battle_choose_reward(int index);
void battle_campfire(int choice);
int run_node_is_fight(int node);
int run_node_count(void);

/* Effective damage an enemy will deal right now (Weak on the enemy, Vulnerable
 * on the player, block is NOT subtracted — that is what the HUD shows). */
int battle_enemy_intent_damage(int index);
void battle_draw_for_test(int count);
void battle_start_turn_for_test(void);
void battle_end_turn_powers_for_test(void);

const char *battle_relic_name(int id);
const char *battle_relic_desc(int id);
int battle_has_relic(int id);
int battle_relic_count(void);
int battle_add_relic(int id);
int battle_intent_kind(int index);
int battle_intent_hits(int index);
int battle_intent_value(int index);
int battle_intent_block(int index);
/* Power stacks on the player (Metallicize, Frail lives on Battle directly). */
int battle_power(int id);

#if defined(CARD_TEST)
/* Deterministic hooks so the card tests can reach the enemy turn and the AI's
 * roll without waiting for the real turn order. */
void battle_test_spawn_act2(int boss);
void battle_test_spawn_automaton(void);
void battle_test_spawn_collector(void);
void battle_test_spawn_gremlin_leader(void);
/* slot 1..3, which 0 warrior / 1 thief / 2 fat / 3 tsundere / 4 wizard. */
void battle_test_spawn_gremlin_minion(int slot, int which);
void battle_test_damage_enemy(int index, int amount);
void battle_test_spawn_slavers(void);
/* 0 two Byrds, 1 Chosen, 2 Shelled Parasite, 3 Spheric Guardian. */
void battle_test_spawn_act2_monster(int which);
/* 0 Slime Boss, 1 Gremlin Nob, 2 two Acid Slimes (M), 3 Jaw Worm. */
void battle_test_spawn_act1(int which);
/* Drops the Slime Boss to half HP and resolves the split. */
void battle_test_split_boss(void);
void battle_test_choose_roll(int index, unsigned int roll);
void battle_test_enemy_turn(int index);
void battle_test_apply_relics(void);
void battle_test_bag_of_marbles(void);
#endif

enum { POTION_HEAL = 0, POTION_FIRE, POTION_BLOCK, POTION_DRAW, POTION_ENERGY, POTION_STR, POTION_COUNT };
int battle_potion_count(void);
int battle_potion_at(int slot);
const char *battle_potion_name(int id);
const char *battle_potion_desc(int id);
int battle_add_random_potion(void);
int battle_add_potion(int id);
int battle_use_potion(int slot, int target);

/* Konami cheat: player card damage is tripled while enabled. */
extern int g_cheat_triple;
int battle_toggle_cheat(void);

int battle_deck_count(void);
int battle_deck_card(int index);
int battle_deck_up(int index);
int battle_can_upgrade(void);
void battle_open_upgrade(void);
void battle_cancel_upgrade(void);
int battle_confirm_upgrade(int deck_index);
