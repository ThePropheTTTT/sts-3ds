/* Headless assertions for battle_play(): card effects are checked against
 * g_battle / g_battle.enemies after playing one card from a controlled state.
 * Built only with -DCARD_TEST; the normal build has none of this. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "cards_gen.h"
#include "battle.h"
#include "run.h"
#include "l10n_gen.h"
#include "save.h"
#include "audio.h"

#define MAX_FAILS 12

static const EnemyMove HIT10[] = {{MOVE_ATTACK, 10, 0, 0}};

static int s_cases;
static int s_fails;
static char s_fail_text[MAX_FAILS][56];

/* ---- harness ------------------------------------------------------------ */

static const CardDef *D(const char *id) {
    int i = battle_find_card(id);
    return i < 0 ? NULL : &sts_cards[i];
}

static void fresh(int enemy_hp) {
    memset(&g_battle, 0, sizeof(g_battle));
    memset(&g_run, 0, sizeof(g_run));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    g_battle.enemy_count = 1;
    g_battle.enemies[0].name = "Dummy";
    g_battle.enemies[0].hp = enemy_hp;
    g_battle.enemies[0].max_hp = enemy_hp;
    g_battle.rng = 0x1234567u;
    g_run.screen = SCREEN_FIGHT;
}

static void check(const char *label, int got, int want) {
    s_cases++;
    if (got == want) return;
    s_fails++;
    if (s_fails <= MAX_FAILS) {
        snprintf(s_fail_text[s_fails - 1], sizeof(s_fail_text[0]), "%s %d!=%d", label, got, want);
    }
}

static void hand(const char *id, int up) {
    int idx = battle_find_card(id);
    g_battle.hand_count = 1;
    g_battle.hand[0] = idx < 0 ? 0 : CARD_ENTRY(idx, up);
}

static void hand_add(const char *id) {
    int idx = battle_find_card(id);
    if (idx < 0 || g_battle.hand_count >= 10) return;
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(idx, 0);
}

static void pile_add(int *pile, int *count, const char *id) {
    int idx = battle_find_card(id);
    if (idx < 0 || *count >= 64) return;
    pile[(*count)++] = CARD_ENTRY(idx, 0);
}

static int pile_has(const int *pile, int count, const char *id) {
    int idx = battle_find_card(id);
    for (int i = 0; i < count; i++) if (CARD_INDEX(pile[i]) == idx) return 1;
    return 0;
}

/* ---- the cases ---------------------------------------------------------- */

static void test_batch1(void) {
    const CardDef *c;

    /* Pummel: one hit per magic point, then exhausted. */
    c = D("Pummel");
    fresh(200);
    hand("Pummel", 0);
    battle_play(0, 0);
    check("pummel dmg", 200 - g_battle.enemies[0].hp, c->damage * c->magic);
    check("pummel exhaust", g_battle.exhaust_count, 1);

    /* Twin Strike: two hits. */
    c = D("Twin Strike");
    fresh(200);
    hand("Twin Strike", 0);
    battle_play(0, 0);
    check("twin dmg", 200 - g_battle.enemies[0].hp, c->damage * 2);

    /* Whirlwind: X cost, one hit per energy, energy emptied. */
    c = D("Whirlwind");
    fresh(400);
    g_battle.energy = 3;
    hand("Whirlwind", 0);
    battle_play(0, 0);
    check("whirlwind dmg", 400 - g_battle.enemies[0].hp, c->damage * 3);
    check("whirlwind energy", g_battle.energy, 0);

    fresh(400);
    g_battle.energy = 0;
    hand("Whirlwind", 0);
    battle_play(0, 0);
    check("whirlwind x0", 400 - g_battle.enemies[0].hp, 0);

    /* Fiend Fire: one hit per other card, and they are exhausted. */
    c = D("Fiend Fire");
    fresh(400);
    hand("Fiend Fire", 0);
    hand_add("Strike_R");
    hand_add("Strike_R");
    hand_add("Strike_R");
    battle_play(0, 0);
    check("fiendfire dmg", 400 - g_battle.enemies[0].hp, c->damage * 3);
    check("fiendfire exhaust", g_battle.exhaust_count, 3);

    /* Body Slam: damage equals current block. */
    fresh(200);
    g_battle.block = 13;
    hand("Body Slam", 0);
    battle_play(0, 0);
    check("bodyslam dmg", 200 - g_battle.enemies[0].hp, 13);

    /* Heavy Blade: Strength counts 3x. */
    c = D("Heavy Blade");
    fresh(200);
    g_battle.strength = 2;
    hand("Heavy Blade", 0);
    battle_play(0, 0);
    check("heavyblade dmg", 200 - g_battle.enemies[0].hp, c->damage + 6);

    /* Entrench: doubles the block you already have. */
    fresh(100);
    g_battle.block = 7;
    hand("Entrench", 0);
    battle_play(0, 0);
    check("entrench block", g_battle.block, 14);

    /* Power Through: block plus two Wounds in hand. */
    c = D("Power Through");
    fresh(100);
    hand("Power Through", 0);
    battle_play(0, 0);
    check("powerthrough block", g_battle.block, c->block);
    check("powerthrough wounds", g_battle.hand_count, 2);

    /* Second Wind: exhausts non-attacks, block per card exhausted. */
    c = D("Second Wind");
    fresh(100);
    hand("Second Wind", 0);
    hand_add("Defend_R");
    hand_add("Defend_R");
    hand_add("Strike_R");
    battle_play(0, 0);
    check("secondwind block", g_battle.block, c->block * 2);
    check("secondwind exhaust", g_battle.exhaust_count, 2);

    /* Reaper: heals for unblocked damage, exhausted. */
    c = D("Reaper");
    fresh(200);
    g_battle.hp = 40;
    hand("Reaper", 0);
    battle_play(0, 0);
    check("reaper dmg", 200 - g_battle.enemies[0].hp, c->damage);
    check("reaper heal", g_battle.hp, 40 + c->damage);
    check("reaper exhaust", g_battle.exhaust_count, 1);

    /* Feed: raises max HP only on a kill. */
    c = D("Feed");
    fresh(5);
    hand("Feed", 0);
    battle_play(0, 0);
    check("feed maxhp", g_battle.max_hp, 80 + c->magic);
    check("feed hp", g_battle.hp, 80 + c->magic);

    /* Hemokinesis: costs HP. */
    c = D("Hemokinesis");
    fresh(200);
    hand("Hemokinesis", 0);
    battle_play(0, 0);
    check("hemokinesis hp", g_battle.hp, 80 - c->magic);

    /* Bloodletting: energy for HP (fixed 3 HP). */
    c = D("Bloodletting");
    fresh(100);
    g_battle.energy = 0;
    hand("Bloodletting", 0);
    battle_play(0, 0);
    check("bloodletting energy", g_battle.energy, c->magic);
    check("bloodletting hp", g_battle.hp, 77);

    /* Seeing Red: 2 energy for 1, and the card is exhausted.  The value is a
     * literal in SeeingRed.use(), so the table cannot supply it. */
    fresh(100);
    g_battle.energy = 3;
    hand("Seeing Red", 0);
    check("seeingred play", battle_play(0, 0), 0);
    check("seeingred energy", g_battle.energy, 4);
    check("seeingred exhaust", g_battle.exhaust_count, 1);

    /* Battle Trance: draws, then stops all drawing this turn. */
    c = D("Battle Trance");
    fresh(100);
    for (int i = 0; i < 5; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Battle Trance", 0);
    battle_play(0, 0);
    check("trance hand", g_battle.hand_count, c->magic);   /* the card itself is gone */
    check("trance no_draw", g_battle.no_draw, 1);
    {
        int before = g_battle.hand_count;
        battle_draw_for_test(1);
        check("trance blocks draw", g_battle.hand_count, before);
    }

    /* Intimidate: Weak on everything, exhausted. */
    c = D("Intimidate");
    fresh(100);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].hp = 100;
    g_battle.enemies[1].max_hp = 100;
    hand("Intimidate", 0);
    battle_play(0, 0);
    check("intimidate weak0", g_battle.enemies[0].weak, c->magic);
    check("intimidate weak1", g_battle.enemies[1].weak, c->magic);
    check("intimidate exhaust", g_battle.exhaust_count, 1);

    /* Shockwave: Weak and Vulnerable on everything. */
    c = D("Shockwave");
    fresh(100);
    hand("Shockwave", 0);
    battle_play(0, 0);
    check("shockwave weak", g_battle.enemies[0].weak, c->magic);
    check("shockwave vuln", g_battle.enemies[0].vulnerable, c->magic);

    /* Wild Strike: a Wound goes to the discard pile. */
    fresh(200);
    hand("Wild Strike", 0);
    battle_play(0, 0);
    check("wildstrike wound", pile_has(g_battle.discard, g_battle.discard_count, "Wound"), 1);

    /* Reckless Charge: a Dazed goes into the draw pile. */
    fresh(200);
    for (int i = 0; i < 3; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Reckless Charge", 0);
    battle_play(0, 0);
    check("reckless dazed", pile_has(g_battle.draw, g_battle.draw_count, "Dazed"), 1);
    check("reckless draw size", g_battle.draw_count, 4);

    /* Dropkick: refunds and draws against a Vulnerable target. */
    fresh(200);
    g_battle.enemies[0].vulnerable = 1;
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Dropkick", 0);
    battle_play(0, 0);
    check("dropkick energy", g_battle.energy, 3);
    check("dropkick hand", g_battle.hand_count, 1);   /* the drawn card, Dropkick itself is gone */

    fresh(200);
    hand("Dropkick", 0);
    battle_play(0, 0);
    check("dropkick no vuln energy", g_battle.energy, 2);

    /* Spot Weakness: Strength only against an attacking intent. */
    c = D("Spot Weakness");
    {
        static const EnemyMove ATTACK[] = {{MOVE_ATTACK, 10, 0, 0}};
        fresh(200);
        g_battle.enemies[0].moves = ATTACK;
        g_battle.enemies[0].move_count = 1;
        hand("Spot Weakness", 0);
        battle_play(0, 0);
        check("spotweak str", g_battle.strength, c->magic);
    }
    {
        static const EnemyMove BUFF[] = {{MOVE_BUFF, 3, 0, 0}};
        fresh(200);
        g_battle.enemies[0].moves = BUFF;
        g_battle.enemies[0].move_count = 1;
        hand("Spot Weakness", 0);
        battle_play(0, 0);
        check("spotweak no str", g_battle.strength, 0);
    }

    /* Clash: refuses to be played while a Skill is in hand. */
    fresh(200);
    hand("Clash", 0);
    hand_add("Defend_R");
    check("clash rejected", battle_play(0, 0), -5);
    check("clash no dmg", 200 - g_battle.enemies[0].hp, 0);

    /* Inflame and Limit Break both move Strength. */
    c = D("Inflame");
    fresh(200);
    hand("Inflame", 0);
    battle_play(0, 0);
    check("inflame str", g_battle.strength, c->magic);

    fresh(200);
    g_battle.strength = 3;
    hand("Limit Break", 0);
    battle_play(0, 0);
    check("limitbreak str", g_battle.strength, 6);
    check("limitbreak exhaust", g_battle.exhaust_count, 1);
}

static void test_powers(void) {
    const CardDef *c;

    /* Barricade: block survives the start of the turn. */
    fresh(200);
    g_battle.block = 10;
    hand("Barricade", 0);
    battle_play(0, 0);
    check("barricade block", g_battle.block, 10);
    battle_start_turn_for_test();
    check("barricade kept", g_battle.block, 10);

    /* Without it, the start of the turn clears block. */
    fresh(200);
    g_battle.block = 10;
    battle_start_turn_for_test();
    check("block cleared", g_battle.block, 0);

    /* Berserk: Vulnerable on yourself, +1 energy every turn. */
    c = D("Berserk");
    fresh(200);
    hand("Berserk", 0);
    battle_play(0, 0);
    check("berserk vuln", g_battle.vulnerable, c->magic);
    battle_start_turn_for_test();
    check("berserk energy", g_battle.energy, g_battle.max_energy + 1);

    /* Brutality: draw one and lose one HP at the start of the turn. */
    fresh(200);
    hand("Brutality", 0);
    battle_play(0, 0);
    for (int i = 0; i < 10; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    battle_start_turn_for_test();
    check("brutality hp", g_battle.hp, 79);
    check("brutality draw", g_battle.hand_count, 6);

    /* Combust: end of turn costs 1 HP and hits every enemy. */
    c = D("Combust");
    fresh(200);
    hand("Combust", 0);
    battle_play(0, 0);
    battle_end_turn();
    check("combust hp", g_battle.hp, 79);
    check("combust dmg", 200 - g_battle.enemies[0].hp, c->magic);

    /* Corruption: Skills cost 0 and exhaust. */
    fresh(200);
    hand("Corruption", 0);
    battle_play(0, 0);
    check("corruption energy", g_battle.energy, 0);
    hand("Defend_R", 0);
    check("corruption cost", battle_play(0, 0), 0);
    check("corruption block", g_battle.block, D("Defend_R")->block);
    check("corruption exhaust", g_battle.exhaust_count, 1);

    /* Dark Embrace: every exhaust draws a card. */
    fresh(200);
    hand("Dark Embrace", 0);
    battle_play(0, 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Seeing Red", 0);
    battle_play(0, 0);
    check("darkembrace draw", g_battle.hand_count, 1);

    /* Demon Form: Strength every turn. */
    c = D("Demon Form");
    fresh(200);
    hand("Demon Form", 0);
    battle_play(0, 0);
    battle_start_turn_for_test();
    check("demonform str", g_battle.strength, c->magic);

    /* Evolve: drawing a Status draws another card. */
    fresh(200);
    hand("Evolve", 0);
    battle_play(0, 0);
    g_battle.hand_count = 0;
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Wound");
    battle_draw_for_test(1);
    check("evolve hand", g_battle.hand_count, 2);

    /* Feel No Pain: exhausting a card grants block. */
    c = D("Feel No Pain");
    fresh(200);
    hand("Feel No Pain", 0);
    battle_play(0, 0);
    hand("Seeing Red", 0);
    battle_play(0, 0);
    check("feelnopain block", g_battle.block, c->magic);

    /* Fire Breathing: drawing a Status burns every enemy. */
    c = D("Fire Breathing");
    fresh(200);
    hand("Fire Breathing", 0);
    battle_play(0, 0);
    g_battle.hand_count = 0;
    pile_add(g_battle.draw, &g_battle.draw_count, "Wound");
    battle_draw_for_test(1);
    check("firebreathing dmg", 200 - g_battle.enemies[0].hp, c->magic);

    /* Juggernaut: every block gain hits a random enemy. */
    c = D("Juggernaut");
    fresh(200);
    hand("Juggernaut", 0);
    battle_play(0, 0);
    hand("Defend_R", 0);
    battle_play(0, 0);
    check("juggernaut dmg", 200 - g_battle.enemies[0].hp, c->magic);

    /* Magnetism: one colourless card at the start of the turn. */
    fresh(200);
    hand("Magnetism", 0);
    battle_play(0, 0);
    battle_start_turn_for_test();
    check("magnetism hand", g_battle.hand_count, 1);

    /* Mayhem: the top card of the draw pile is played for free. */
    fresh(200);
    hand("Mayhem", 0);
    battle_play(0, 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_start_turn_for_test();
    check("mayhem block", g_battle.block, D("Defend_R")->block);
    check("mayhem energy", g_battle.energy, g_battle.max_energy);

    /* Metallicize: block before the enemies act. */
    c = D("Metallicize");
    fresh(200);
    hand("Metallicize", 0);
    battle_play(0, 0);
    battle_end_turn_powers_for_test();
    check("metallicize block", g_battle.block, c->magic);

    /* Panache: every fifth card burns all enemies. */
    c = D("Panache");
    fresh(500);
    hand("Panache", 0);
    battle_play(0, 0);
    for (int i = 0; i < 5; i++) {
        hand("Defend_R", 0);
        g_battle.energy = 3;
        battle_play(0, 0);
    }
    check("panache dmg", 500 - g_battle.enemies[0].hp, c->magic);

    /* Rupture: losing HP from a card grants Strength. */
    c = D("Rupture");
    fresh(200);
    hand("Rupture", 0);
    battle_play(0, 0);
    hand("Hemokinesis", 0);
    battle_play(0, 0);
    check("rupture str", g_battle.strength, c->magic);

    /* Sadistic Nature: applying a debuff deals damage. */
    c = D("Sadistic Nature");
    fresh(200);
    hand("Sadistic Nature", 0);
    battle_play(0, 0);
    hand("Intimidate", 0);
    battle_play(0, 0);
    check("sadistic dmg", 200 - g_battle.enemies[0].hp, c->magic);
    check("sadistic weak", g_battle.enemies[0].weak, D("Intimidate")->magic);

    /* Power cards stay in play: neither discarded nor exhausted. */
    fresh(200);
    hand("Barricade", 0);
    battle_play(0, 0);
    check("power stays", g_battle.discard_count + g_battle.exhaust_count, 0);
}

static void test_batch3(void) {
    const CardDef *c;

    /* Bite: damage plus HealAction(magicNumber), clamped to max HP. */
    c = D("Bite");
    fresh(200);
    g_battle.hp = 78;
    hand("Bite", 0);
    battle_play(0, 0);
    check("bite dmg", 200 - g_battle.enemies[0].hp, c->damage);
    check("bite heal", g_battle.hp, 80);

    /* Flash of Steel: damage, then draw one. */
    c = D("Flash of Steel");
    fresh(200);
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Flash of Steel", 0);
    battle_play(0, 0);
    check("flash dmg", 200 - g_battle.enemies[0].hp, c->damage);
    check("flash draw", g_battle.hand_count, 1);

    /* Hand of Greed: gold only when the blow kills. */
    c = D("HandOfGreed");
    fresh(10);
    g_map.gold = 0;
    hand("HandOfGreed", 0);
    battle_play(0, 0);
    /* The killing blow also ends the fight, so the reward gold has to be
     * subtracted; Hand of Greed's own share is what this asserts. */
    check("greed gold", g_map.gold - g_run.gold_gain, c->magic);
    check("greed dead", g_battle.enemies[0].hp, 0);

    fresh(200);
    g_map.gold = 0;
    hand("HandOfGreed", 0);
    battle_play(0, 0);
    check("greed no gold", g_map.gold, 0);

    /* Immolate: hits every enemy and puts a Burn in the discard pile. */
    c = D("Immolate");
    fresh(200);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].name = "Dummy2";
    g_battle.enemies[1].hp = 200;
    g_battle.enemies[1].max_hp = 200;
    hand("Immolate", 0);
    battle_play(0, -1);
    check("immolate both", (200 - g_battle.enemies[0].hp) + (200 - g_battle.enemies[1].hp),
          c->damage * 2);
    check("immolate burn", pile_has(g_battle.discard, g_battle.discard_count, "Burn"), 1);

    /* Dramatic Entrance exhausts (and hits all). */
    fresh(200);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].name = "Dummy2";
    g_battle.enemies[1].hp = 200;
    g_battle.enemies[1].max_hp = 200;
    hand("Dramatic Entrance", 0);
    battle_play(0, -1);
    check("entrance dmg", 200 - g_battle.enemies[1].hp, D("Dramatic Entrance")->damage);
    check("entrance exhaust", g_battle.exhaust_count, 1);

    /* Ritual Dagger: 15 damage from misc, grows on a killing blow. */
    c = D("RitualDagger");
    fresh(10);
    g_run.ritual_bonus = 0;
    hand("RitualDagger", 0);
    battle_play(0, 0);
    check("ritual base", g_run.ritual_bonus, c->magic);
    /* Revive the dummy for a second blow: the first one ended the fight. */
    g_battle.phase = PHASE_PLAYER;
    g_run.screen = SCREEN_FIGHT;
    g_battle.enemies[0].hp = 200;
    g_battle.enemies[0].max_hp = 200;
    hand("RitualDagger", 0);
    g_battle.energy = 3;
    battle_play(0, 0);
    check("ritual grown", 200 - g_battle.enemies[0].hp, 15 + c->magic);

    /* Mind Blast: damage equals the draw pile size. */
    fresh(200);
    hand("Mind Blast", 0);
    for (int i = 0; i < 3; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    battle_play(0, 0);
    check("mindblast dmg", 200 - g_battle.enemies[0].hp, 3);

    /* Blood for Blood: one energy off per HP-loss event, floor 0. */
    c = D("Blood for Blood");
    fresh(200);
    g_battle.damaged = 2;
    hand("Blood for Blood", 0);
    check("bfb cost", battle_play(0, 0), 0);
    check("bfb energy", g_battle.energy, 1);
    check("bfb dmg", 200 - g_battle.enemies[0].hp, c->damage);

    fresh(200);
    g_battle.damaged = 9;
    g_battle.energy = 3;
    hand("Blood for Blood", 0);
    battle_play(0, 0);
    check("bfb floor", g_battle.energy, 3);

    /* Carnage is Ethereal: it exhausts at the end of the turn. */
    fresh(200);
    hand("Carnage", 0);
    battle_end_turn_powers_for_test();
    check("carnage exhaust", g_battle.exhaust_count, 1);

    /* Cleave hits every enemy; Iron Wave pays damage and block. */
    fresh(200);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].name = "Dummy2";
    g_battle.enemies[1].hp = 200;
    g_battle.enemies[1].max_hp = 200;
    hand("Cleave", 0);
    battle_play(0, -1);
    check("cleave both", (200 - g_battle.enemies[0].hp) + (200 - g_battle.enemies[1].hp),
          D("Cleave")->damage * 2);

    fresh(200);
    hand("Iron Wave", 0);
    battle_play(0, 0);
    check("ironwave dmg", 200 - g_battle.enemies[0].hp, D("Iron Wave")->damage);
    check("ironwave block", g_battle.block, D("Iron Wave")->block);

    /* Swift Strike and Bludgeon are plain damage through the generic path. */
    fresh(200);
    hand("Swift Strike", 0);
    battle_play(0, 0);
    check("swift dmg", 200 - g_battle.enemies[0].hp, D("Swift Strike")->damage);

    fresh(200);
    hand("Bludgeon", 0);
    battle_play(0, 0);
    check("bludgeon dmg", 200 - g_battle.enemies[0].hp, D("Bludgeon")->damage);
}

static void test_batch4(void) {
    const CardDef *c;

    /* Bandage Up: HealAction(magic), exhausted. */
    c = D("Bandage Up");
    fresh(200);
    g_battle.hp = 40;
    hand("Bandage Up", 0);
    battle_play(0, -1);
    check("bandage heal", g_battle.hp, 40 + c->magic);
    check("bandage exhaust", g_battle.exhaust_count, 1);

    /* Deep Breath: discard pile becomes the draw pile, then one draw. */
    fresh(200);
    for (int i = 0; i < 3; i++) pile_add(g_battle.discard, &g_battle.discard_count, "Strike_R");
    hand("Deep Breath", 0);
    battle_play(0, -1);
    /* Deep Breath itself is not Exhaust, so it lands in the discard pile. */
    check("breath discard", g_battle.discard_count, 1);
    check("breath draw", g_battle.draw_count, 2);
    check("breath hand", g_battle.hand_count, 1);

    /* Finesse pays block through the generic path and draws one. */
    fresh(200);
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Finesse", 0);
    battle_play(0, -1);
    check("finesse block", g_battle.block, D("Finesse")->block);
    check("finesse draw", g_battle.hand_count, 1);

    /* Good Instincts and Impervious are plain block; the latter exhausts. */
    fresh(200);
    hand("Good Instincts", 0);
    battle_play(0, -1);
    check("instincts block", g_battle.block, D("Good Instincts")->block);

    fresh(200);
    hand("Impervious", 0);
    battle_play(0, -1);
    check("impervious block", g_battle.block, D("Impervious")->block);
    check("impervious exhaust", g_battle.exhaust_count, 1);

    /* Master of Strategy: draw three, exhausted. */
    fresh(200);
    for (int i = 0; i < 3; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Master of Strategy", 0);
    battle_play(0, -1);
    check("master draw", g_battle.hand_count, 3);
    check("master exhaust", g_battle.exhaust_count, 1);

    /* Offering: 6 HP, +2 energy, draw three, exhausted. */
    fresh(200);
    for (int i = 0; i < 3; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    hand("Offering", 0);
    battle_play(0, -1);
    check("offering hp", g_battle.hp, 74);
    check("offering energy", g_battle.energy, 5);
    check("offering draw", g_battle.hand_count, 3);
    check("offering exhaust", g_battle.exhaust_count, 1);

    /* J.A.X.: 3 HP for magic Strength. */
    c = D("J.A.X.");
    fresh(200);
    hand("J.A.X.", 0);
    battle_play(0, -1);
    check("jax hp", g_battle.hp, 77);
    check("jax str", g_battle.strength, c->magic);

    /* Jack Of All Trades: one random colourless card, exhausted. */
    fresh(200);
    hand("Jack Of All Trades", 0);
    battle_play(0, -1);
    check("jack hand", g_battle.hand_count, 1);
    check("jack exhaust", g_battle.exhaust_count, 1);

    /* Blind and Trip: single target when not upgraded, all when upgraded. */
    c = D("Blind");
    fresh(200);
    hand("Blind", 0);
    battle_play(0, 0);
    check("blind weak", g_battle.enemies[0].weak, c->magic);

    fresh(200);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].name = "Dummy2";
    g_battle.enemies[1].hp = 200;
    g_battle.enemies[1].max_hp = 200;
    hand("Blind", 1);
    battle_play(0, -1);
    check("blind up both", g_battle.enemies[0].weak + g_battle.enemies[1].weak, c->magic * 2);

    c = D("Trip");
    fresh(200);
    hand("Trip", 0);
    battle_play(0, 0);
    check("trip vuln", g_battle.enemies[0].vulnerable, c->magic);

    fresh(200);
    g_battle.enemy_count = 2;
    g_battle.enemies[1].name = "Dummy2";
    g_battle.enemies[1].hp = 200;
    g_battle.enemies[1].max_hp = 200;
    hand("Trip", 1);
    battle_play(0, -1);
    check("trip up both", g_battle.enemies[0].vulnerable + g_battle.enemies[1].vulnerable,
          c->magic * 2);

    /* Disarm: negative Strength, exhausted; upgraded takes one more. */
    c = D("Disarm");
    fresh(200);
    hand("Disarm", 0);
    battle_play(0, 0);
    check("disarm str", g_battle.enemies[0].strength, -c->magic);
    check("disarm exhaust", g_battle.exhaust_count, 1);

    fresh(200);
    hand("Disarm", 1);
    battle_play(0, 0);
    check("disarm up", g_battle.enemies[0].strength, -(c->magic + c->up_magic));

    /* Ghostly Armor: block when played, and Ethereal if it is still in hand
     * when the turn ends. */
    fresh(200);
    hand("Ghostly Armor", 0);
    battle_play(0, -1);
    check("ghostly block", g_battle.block, D("Ghostly Armor")->block);

    fresh(200);
    hand("Ghostly Armor", 0);
    battle_end_turn_powers_for_test();
    check("ghostly exhaust", g_battle.exhaust_count, 1);
}

static void test_batch5(void) {
    const CardDef *c;

    /* Rage: block for each Attack played after it, gone at the end of turn. */
    c = D("Rage");
    fresh(200);
    hand("Rage", 0);
    battle_play(0, -1);
    check("rage self block", g_battle.block, 0);
    check("rage power", g_battle.rage_block, c->magic);
    hand("Strike_R", 0);
    battle_play(0, 0);
    check("rage attack block", g_battle.block, c->magic);
    battle_end_turn_powers_for_test();
    check("rage cleared", g_battle.rage_block, 0);
    hand("Strike_R", 0);
    int block_before = g_battle.block;
    battle_play(0, 0);
    check("rage expired", g_battle.block, block_before);

    /* Flame Barrier: block now, thorns on every enemy hit, gone next turn. */
    c = D("Flame Barrier");
    fresh(200);
    g_battle.enemies[0].moves = HIT10;
    g_battle.enemies[0].move_count = 1;
    g_battle.enemies[0].move_index = 0;
    hand("Flame Barrier", 0);
    battle_play(0, -1);
    check("flame block", g_battle.block, c->block);
    check("flame power", g_battle.flame_barrier, c->magic);
    battle_end_turn();
    check("flame hp", g_battle.hp, 80);
    check("flame thorns", 200 - g_battle.enemies[0].hp, c->magic);
    check("flame expired", g_battle.flame_barrier, 0);

    /* Panic Button: block, then nothing else grants block for two turns. */
    c = D("PanicButton");
    fresh(200);
    hand("PanicButton", 0);
    battle_play(0, -1);
    check("panic block", g_battle.block, c->block);
    check("panic power", g_battle.no_block_turns, c->magic);
    battle_start_turn_for_test();
    hand("Defend_R", 0);
    battle_play(0, -1);
    check("panic gates block", g_battle.block, 0);
    battle_start_turn_for_test();
    hand("Defend_R", 0);
    battle_play(0, -1);
    check("panic over", g_battle.block, D("Defend_R")->block);

    /* The Bomb: three end-of-turn ticks, then one hit on everything. */
    c = D("The Bomb");
    fresh(200);
    hand("The Bomb", 0);
    battle_play(0, -1);
    check("bomb fuse", g_battle.bomb_turns, 3);
    battle_end_turn_powers_for_test();
    check("bomb tick1", g_battle.bomb_turns, 2);
    check("bomb hp1", g_battle.enemies[0].hp, 200);
    battle_end_turn_powers_for_test();
    check("bomb tick2", g_battle.bomb_turns, 1);
    battle_end_turn_powers_for_test();
    check("bomb tick3", g_battle.bomb_turns, 0);
    check("bomb damage", 200 - g_battle.enemies[0].hp, c->magic);

    /* Sentinel pays energy when something else exhausts it. */
    fresh(200);
    hand("Second Wind", 0);
    hand_add("Sentinel");
    battle_play(0, -1);
    check("sentinel energy", g_battle.energy, 4);
    check("sentinel exhausted", pile_has(g_battle.exhaust, g_battle.exhaust_count, "Sentinel"), 1);

    /* Necronomicurse comes back to hand when it is exhausted. */
    fresh(200);
    hand("Second Wind", 0);
    hand_add("Necronomicurse");
    battle_play(0, -1);
    check("necro back", pile_has(g_battle.hand, g_battle.hand_count, "Necronomicurse"), 1);

    /* Void drains energy as it is drawn. */
    fresh(200);
    pile_add(g_battle.draw, &g_battle.draw_count, "Void");
    battle_start_turn_for_test();
    check("void energy", g_battle.energy, 2);
    check("void hand", pile_has(g_battle.hand, g_battle.hand_count, "Void"), 1);

    /* Pain costs 1 HP as it is drawn. */
    fresh(200);
    pile_add(g_battle.draw, &g_battle.draw_count, "Pain");
    battle_start_turn_for_test();
    check("pain hp", g_battle.hp, 79);

    /* Burn: end-of-turn damage; Pride: copies itself into the draw pile. */
    fresh(200);
    hand("Burn", 0);
    battle_end_turn_powers_for_test();
    check("burn hp", g_battle.hp, 78);

    fresh(200);
    hand("Pride", 0);
    battle_end_turn_powers_for_test();
    check("pride copy", pile_has(g_battle.draw, g_battle.draw_count, "Pride"), 1);

    /* Normality: the fourth card of a turn cannot be played. */
    fresh(200);
    hand("Strike_R", 0);
    hand_add("Strike_R");
    hand_add("Strike_R");
    hand_add("Normality");
    battle_play(0, 0);
    battle_play(0, 0);
    battle_play(0, 0);
    check("normality blocked", battle_play(0, 0), -6);
}

static void test_batch6(void) {
    const CardDef *c;
    int strike = battle_find_card("Strike_R");
    int defend = battle_find_card("Defend_R");

    /* Armaments: block, then upgrade one card in hand (the played card has
     * already left it).  Armaments+ upgrades the whole hand with no picker. */
    fresh(200);
    hand("Armaments", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    c = D("Armaments");
    battle_play(0, -1);
    check("arm block", g_battle.block, c->block);
    check("arm screen", g_run.screen, SCREEN_PICK);
    check("arm count", battle_pick_count(), 2);
    check("arm first", battle_pick_card(0), strike);
    battle_pick_choose(0);
    check("arm upgraded", CARD_UPGRADED(g_battle.hand[0]), 1);
    check("arm back", g_run.screen, SCREEN_FIGHT);

    fresh(200);
    hand("Armaments", 1);
    hand_add("Strike_R");
    hand_add("Defend_R");
    battle_play(0, -1);
    check("arm+ no picker", g_run.screen, SCREEN_FIGHT);
    check("arm+ first", CARD_UPGRADED(g_battle.hand[0]), 1);
    check("arm+ second", CARD_UPGRADED(g_battle.hand[1]), 1);

    /* Burning Pact: exhaust one card, then draw its magic number. */
    fresh(200);
    c = D("Burning Pact");
    hand("Burning Pact", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, -1);
    check("pact screen", g_run.screen, SCREEN_PICK);
    check("pact count", battle_pick_count(), 2);
    battle_pick_choose(0);
    check("pact exhausted", g_battle.exhaust_count, 2);   /* itself + the pick */
    check("pact hand", g_battle.hand_count, 1 + c->magic);
    check("pact back", g_run.screen, SCREEN_FIGHT);

    /* Warcry draws first and only then puts a card back (Warcry.java:31-32),
     * so the card the player picks must stay on top of the draw pile. */
    fresh(200);
    c = D("Warcry");
    hand("Warcry", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    battle_play(0, -1);
    check("warcry screen", g_run.screen, SCREEN_PICK);
    check("warcry count", battle_pick_count(), 3);        /* 2 kept + 1 drawn */
    int warcry_entry = g_battle.hand[0];
    battle_pick_choose(0);
    check("warcry hand", g_battle.hand_count, 2);
    check("warcry top", g_battle.draw[g_battle.draw_count - 1], warcry_entry);
    check("warcry back", g_run.screen, SCREEN_FIGHT);

    /* Thinking Ahead: draw 2, then put one back. */
    fresh(200);
    hand("Thinking Ahead", 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, -1);
    check("ahead count", battle_pick_count(), 2);
    int ahead_entry = g_battle.hand[1];
    battle_pick_choose(1);
    check("ahead hand", g_battle.hand_count, 1);
    check("ahead top", g_battle.draw[g_battle.draw_count - 1], ahead_entry);

    /* Dual Wield copies an Attack or a Power; it cannot copy itself. */
    fresh(200);
    c = D("Dual Wield");
    hand("Dual Wield", 0);
    hand_add("Strike_R");
    battle_play(0, -1);
    check("wield count", battle_pick_count(), 1);
    battle_pick_choose(0);
    check("wield hand", g_battle.hand_count, 1 + c->magic);
    check("wield copy", CARD_INDEX(g_battle.hand[1]), strike);

    /* Exhume pulls a card out of the exhaust pile. */
    fresh(200);
    hand("Exhume", 0);
    g_battle.exhaust[g_battle.exhaust_count++] = CARD_ENTRY(strike, 0);
    g_battle.exhaust[g_battle.exhaust_count++] = CARD_ENTRY(defend, 0);
    battle_play(0, -1);
    check("exhume count", battle_pick_count(), 3);        /* including Exhume itself */
    battle_pick_choose(0);
    check("exhume hand", g_battle.hand_count, 1);
    check("exhume card", CARD_INDEX(g_battle.hand[0]), strike);

    /* Discovery: pick 1 of 3, free for the rest of the combat, then exhaust. */
    fresh(200);
    hand("Discovery", 0);
    battle_play(0, -1);
    check("discovery count", battle_pick_count(), 3);
    int found = battle_pick_card(1);
    battle_pick_choose(1);
    check("discovery hand", g_battle.hand_count, 1);
    check("discovery card", CARD_INDEX(g_battle.hand[0]), found);
    /* battle_card_cost indexes battle.c's copy of sts_cards internally; a
     * pointer into this file's static table would be subtracted there. */
    check("discovery free", battle_card_cost(0), 0);
    check("discovery exhaust", g_battle.exhaust_count, 1);
}

/* ---- batch 7: act 2 boss and elite -------------------------------------- */

/* Puts the Champ into a known getMove() state.  hp, numTurns, forge count,
 * threshold flag and the last two moves are everything its AI reads. */
static void champ_set(int hp, int ai, int forge, int thr, int last, int before) {
    Enemy *e = &g_battle.enemies[0];
    e->hp = hp;
    e->ai_turn = (unsigned char)ai;
    e->forge_times = (unsigned char)forge;
    e->threshold = (unsigned char)thr;
    e->last_move = (unsigned char)last;
    e->last_before = (unsigned char)before;
    e->move_index = 9;
}

static void test_batch7(void) {
    Enemy *e;

    /* The opener is rolled, not fixed: init() calls rollMove() before the
     * first turn, exactly like AbstractMonster.init(). */
    battle_test_spawn_act2(1);
    e = &g_battle.enemies[0];
    check("champ hp", e->max_hp, 420);
    check("champ opened once", e->ai_turn, 1);
    check("champ opening move", e->move_index <= 6, 1);

    /* Anger fires once when HP drops below half, before anything else. */
    champ_set(209, 0, 0, 0, 255, 255);
    battle_test_choose_roll(0, 50);
    check("champ anger move", g_battle.enemies[0].move_index, 6);
    check("champ anger flag", g_battle.enemies[0].threshold, 1);

    /* After the threshold, Execute always follows unless it just acted. */
    champ_set(420, 0, 0, 1, 0, 0);
    battle_test_choose_roll(0, 50);
    check("champ execute", g_battle.enemies[0].move_index, 2);

    /* Every fourth un-thresholded turn is Taunt, and numTurns resets. */
    champ_set(420, 3, 2, 0, 0, 0);
    battle_test_choose_roll(0, 50);
    check("champ taunt move", g_battle.enemies[0].move_index, 5);
    check("champ taunt resets", g_battle.enemies[0].ai_turn, 0);

    /* roll <= 15 forges, at most twice. */
    champ_set(420, 0, 0, 0, 0, 0);
    battle_test_choose_roll(0, 10);
    check("champ stance move", g_battle.enemies[0].move_index, 1);
    check("champ stance count", g_battle.enemies[0].forge_times, 1);
    champ_set(420, 0, 2, 0, 0, 0);
    battle_test_choose_roll(0, 10);
    check("champ no third stance", g_battle.enemies[0].move_index, 4);

    /* roll <= 30 gloats, <= 55 slaps, otherwise Slash (Slap if Slash was
     * just used). */
    champ_set(420, 0, 2, 0, 0, 0);
    battle_test_choose_roll(0, 40);
    check("champ slap", g_battle.enemies[0].move_index, 3);
    champ_set(420, 0, 2, 0, 3, 0);
    battle_test_choose_roll(0, 70);
    check("champ slash", g_battle.enemies[0].move_index, 0);
    champ_set(420, 0, 2, 0, 0, 0);
    battle_test_choose_roll(0, 70);
    check("champ repeat slash", g_battle.enemies[0].move_index, 3);

    /* Book of Stabbing: Multi-Stab grows by one hit each use, and two in a row
     * force Big Stab. */
    battle_test_spawn_act2(0);
    e = &g_battle.enemies[0];
    check("book hp range", e->max_hp >= 160 && e->max_hp <= 164, 1);
    e->stab_count = 1;
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(0, 50);
    check("book first stab", g_battle.enemies[0].move_index, 0);
    check("book first stab count", g_battle.enemies[0].stab_count, 2);
    g_battle.enemies[0].last_move = 0;
    g_battle.enemies[0].last_before = 0;
    g_battle.enemies[0].stab_count = 2;
    battle_test_choose_roll(0, 50);
    check("book big stab", g_battle.enemies[0].move_index, 1);
    check("book big stab count", g_battle.enemies[0].stab_count, 2);
    g_battle.enemies[0].last_move = 1;
    g_battle.enemies[0].last_before = 0;
    battle_test_choose_roll(0, 5);
    check("book after big", g_battle.enemies[0].move_index, 0);
    check("book after big count", g_battle.enemies[0].stab_count, 3);

    /* Stance banks 15 block and starts Metallicize at 5 (Champ.java:170-178). */
    battle_test_spawn_act2(1);
    e = &g_battle.enemies[0];
    e->move_index = 1;
    e->last_move = 255;
    e->metallicize = 0;
    e->block = 0;
    battle_test_enemy_turn(0);
    check("champ stance block", g_battle.enemies[0].block, 15);
    check("champ metallicize", g_battle.enemies[0].metallicize, 5);
    /* Next turn clears its block and Metallicize is up before it acts. */
    g_battle.enemies[0].move_index = 6;
    g_battle.enemies[0].last_move = 255;
    g_battle.enemies[0].block = 99;
    battle_test_enemy_turn(0);
    check("champ metallicize block", g_battle.enemies[0].block, 5);

    /* Anger: +6 Strength and its own debuffs are removed. */
    battle_test_spawn_act2(1);
    e = &g_battle.enemies[0];
    e->move_index = 6;
    e->strength = 2;
    e->vulnerable = 3;
    e->weak = 2;
    battle_test_enemy_turn(0);
    check("champ anger strength", g_battle.enemies[0].strength, 8);
    check("champ anger vuln", g_battle.enemies[0].vulnerable, 0);
    check("champ anger weak", g_battle.enemies[0].weak, 0);

    /* Taunt: 2 Weak + 2 Vulnerable on the player. */
    battle_test_spawn_act2(1);
    e = &g_battle.enemies[0];
    e->move_index = 5;
    g_battle.weak = 0;
    g_battle.vulnerable = 0;
    battle_test_enemy_turn(0);
    check("champ taunt weak", g_battle.weak, 2);
    check("champ taunt vuln", g_battle.vulnerable, 2);

    /* Face Slap: 12 damage, 2 Vulnerable, 2 Frail. */
    battle_test_spawn_act2(1);
    e = &g_battle.enemies[0];
    e->move_index = 3;
    e->strength = 0;
    g_battle.hp = 80;
    g_battle.block = 0;
    g_battle.vulnerable = 0;
    g_battle.weak = 0;
    g_battle.frail = 0;
    g_battle.frail_just = 0;
    battle_test_enemy_turn(0);
    check("champ slap damage", g_battle.hp, 68);
    check("champ slap frail", g_battle.frail, 2);
    check("champ slap just applied", g_battle.frail_just, 1);
    check("champ slap vuln", g_battle.vulnerable, 2);

    /* Painful Stabs: every unblocked hit seeds a Wound (BookOfStabbing:66). */
    battle_test_spawn_act2(0);
    e = &g_battle.enemies[0];
    e->move_index = 0;
    e->stab_count = 4;
    e->painful_stabs = 1;
    e->strength = 0;
    g_battle.hp = 80;
    g_battle.block = 0;
    g_battle.discard_count = 0;
    battle_test_enemy_turn(0);
    check("book stab damage", g_battle.hp, 56);
    check("book stab wounds", g_battle.discard_count, 4);
    check("book wound card", CARD_INDEX(g_battle.discard[0]), battle_find_card("Wound"));

    battle_test_spawn_act2(0);
    e = &g_battle.enemies[0];
    e->move_index = 1;
    e->painful_stabs = 1;
    e->strength = 0;
    g_battle.hp = 80;
    g_battle.block = 0;
    g_battle.discard_count = 0;
    battle_test_enemy_turn(0);
    check("book big stab damage", g_battle.hp, 59);
    check("book big stab wound", g_battle.discard_count, 1);

    /* FrailPower.modifyBlock: 5-block Defend gives 3 while Frail is up. */
    fresh(200);
    g_battle.frail = 1;
    hand("Defend_R", 0);
    battle_play(0, 0);
    check("frail defend", g_battle.block, 3);
    fresh(200);
    hand("Defend_R", 0);
    battle_play(0, 0);
    check("plain defend", g_battle.block, 5);
}

/* ---- batch 8: act 2 normal monsters ------------------------------------ */

/* A clean act 2 normal encounter plus the run state battle_play() insists on. */
static void act2_fresh(int which) {
    battle_test_spawn_act2_monster(which);
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.energy = g_battle.max_energy = 3;
}

/* Plays one fresh card from a full three energy.  target 0 is ignored by
 * self-target skills. */
static void act2_play(const char *id) {
    hand(id, 0);
    g_battle.energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_run.screen = SCREEN_FIGHT;
    battle_play(0, 0);
}

static int count_card(const int *pile, int count, const char *id) {
    int idx = battle_find_card(id);
    int n = 0;
    for (int i = 0; i < count; i++) if (CARD_INDEX(pile[i]) == idx) n++;
    return n;
}

static void test_batch8(void) {
    Enemy *e;

    /* Chosen: Poke opens, Hex fires exactly once, then Debilitate/Drain, then
     * Zap/Poke once either debuff has landed. */
    act2_fresh(1);
    e = &g_battle.enemies[0];
    check("chosen one", g_battle.enemy_count, 1);
    check("chosen opened poke", e->move_index, 4);
    e->first_turn = 0;
    e->used_hex = 0;
    battle_test_choose_roll(0, 50);
    check("chosen hex", e->move_index, 3);
    check("chosen hex once", e->used_hex, 1);
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(0, 30);
    check("chosen debilitate", e->move_index, 2);
    battle_test_choose_roll(0, 80);
    check("chosen drain", e->move_index, 1);
    e->last_move = 1;
    e->last_before = 0;
    battle_test_choose_roll(0, 10);
    check("chosen zap", e->move_index, 0);
    battle_test_choose_roll(0, 90);
    check("chosen poke again", e->move_index, 4);

    /* Drain: three Weak on the player and three Strength on itself. */
    act2_fresh(1);
    e = &g_battle.enemies[0];
    e->move_index = 1;
    e->first_turn = 0;
    battle_test_enemy_turn(0);
    check("drain weak", g_battle.weak, 3);
    check("drain strength", e->strength, 3);

    /* Byrd: opens with Caw or Peck, grounded means Headbutt, and each airborne
     * branch re-rolls within its own range. */
    act2_fresh(0);
    check("byrds two", g_battle.enemy_count, 2);
    e = &g_battle.enemies[0];
    check("byrd opening", e->move_index == 0 || e->move_index == 5, 1);
    check("byrd flight", e->flight, 3);
    e->first_turn = 0;
    e->grounded = 1;
    battle_test_choose_roll(0, 50);
    check("byrd grounded headbutt", e->move_index, 4);
    e->grounded = 0;
    e->last_move = 0;
    e->last_before = 5;
    battle_test_choose_roll(0, 10);
    check("byrd peck repeat", e->move_index, 0);
    e->last_move = 0;
    e->last_before = 0;
    battle_test_choose_roll(0, 40);
    check("byrd double peck swoop", e->move_index == 2 || e->move_index == 5, 1);
    e->last_move = 2;
    e->last_before = 0;
    battle_test_choose_roll(0, 60);
    check("byrd swoop branch", e->move_index == 2 || e->move_index == 5, 1);
    e->last_move = 0;
    battle_test_choose_roll(0, 60);
    check("byrd swoop", e->move_index, 2);
    e->last_move = 5;
    e->last_before = 0;
    battle_test_choose_roll(0, 80);
    check("byrd caw branch", e->move_index == 5 || e->move_index == 2, 1);
    e->last_move = 0;
    battle_test_choose_roll(0, 80);
    check("byrd caw", e->move_index, 5);

    /* FlightPower halves every non-thorns hit and loses a stack when HP is
     * actually lost; the third stack breaking grounds the bird (Stunned). */
    act2_fresh(0);
    e = &g_battle.enemies[0];
    e->hp = e->max_hp = 30;
    e->flight = e->flight_max = 3;
    e->grounded = 0;
    act2_play("Strike_R");
    check("byrd first hit", e->hp, 27);
    check("byrd flight one", e->flight, 2);
    act2_play("Strike_R");
    check("byrd second hit", e->hp, 24);
    act2_play("Strike_R");
    check("byrd third hit", e->hp, 21);
    check("byrd flight gone", e->flight, 0);
    check("byrd grounded", e->grounded, 1);
    check("byrd stun move", e->move_index, 3);

    /* Headbutt sets Go Airborne and skips the roll; the next turn flies. */
    act2_fresh(0);
    e = &g_battle.enemies[0];
    e->move_index = 4;
    e->grounded = 1;
    e->flight = 0;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("byrd headbutt damage", g_battle.hp, 77);
    check("byrd go airborne", e->move_index, 1);
    battle_test_enemy_turn(0);
    check("byrd flying again", e->grounded, 0);
    check("byrd flight restored", e->flight, 3);

    /* A grounded Stunned bird skips its attack and rolls Headbutt next. */
    act2_fresh(0);
    e = &g_battle.enemies[0];
    e->move_index = 3;
    e->grounded = 1;
    e->flight = 0;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("byrd stun no damage", g_battle.hp, 80);
    check("byrd headbutt queued", e->move_index, 4);

    /* Spheric Guardian: Initial Block, then Frail Attack; Barricade keeps the
     * block it starts with (40) and Block Attack adds 15 before striking. */
    act2_fresh(3);
    e = &g_battle.enemies[0];
    check("orb barricade", e->barricade, 1);
    check("orb artifact", e->artifact, 3);
    check("orb initial intent", e->move_index, 1);
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("orb initial block", e->block, 65);
    check("orb frail attack queued", e->move_index, 3);
    battle_test_enemy_turn(0);
    check("orb barricade keeps block", e->block, 65);
    check("orb frail attack damage", g_battle.hp, 70);
    check("orb frail stacks", g_battle.frail, 5);
    check("orb big attack queued", e->move_index, 0);

    /* Artifact eats one debuff per stack; Disarm is a debuff too. */
    act2_fresh(3);
    e = &g_battle.enemies[0];
    act2_play("Bash");
    check("artifact bash", e->artifact, 2);
    check("artifact bash vuln", e->vulnerable, 0);
    act2_fresh(3);
    e = &g_battle.enemies[0];
    e->artifact = 1;                 /* one stack eats one of the two debuffs */
    act2_play("Uppercut");
    check("artifact uppercut", e->artifact, 0);
    check("artifact uppercut weak eaten", e->weak, 0);
    check("artifact uppercut vuln", e->vulnerable > 0, 1);
    act2_fresh(3);
    e = &g_battle.enemies[0];
    act2_play("Disarm");
    check("artifact disarm", e->artifact, 2);
    check("artifact disarm str", e->strength, 0);
    e->move_index = 2;               /* Block Attack: 15 block then 10 */
    check("orb block attack intent", battle_enemy_intent_damage(0), 10);

    /* Shelled Parasite: Plated Armor grants block at the end of the turn, is
     * consumed only by lost HP, and Armor Break stuns at zero. */
    act2_fresh(2);
    e = &g_battle.enemies[0];
    e->move_index = 1;
    e->first_turn = 0;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("parasite double damage", g_battle.hp, 68);
    check("parasite plated block", e->block, 14);
    check("parasite plated", e->plated, 14);

    act2_fresh(2);
    e = &g_battle.enemies[0];
    e->block = 14;
    e->plated = 14;
    int para_hp = e->hp;
    act2_play("Strike_R");
    check("plated no chip", e->hp, para_hp);
    check("plated block eight", e->block, 8);
    check("plated kept", e->plated, 14);
    act2_play("Strike_R");
    check("plated block two", e->block, 2);
    act2_play("Strike_R");
    check("plated hp lost", e->hp, para_hp - 4);
    check("plated one less", e->plated, 13);

    act2_fresh(2);
    e = &g_battle.enemies[0];
    e->plated = 1;
    e->block = 0;
    act2_play("Strike_R");
    check("armor break", e->plated, 0);
    check("armor break stun", e->move_index, 3);

    /* Life Suck heals by the HP the player actually lost. */
    act2_fresh(2);
    e = &g_battle.enemies[0];
    e->hp = 50;
    e->max_hp = 72;
    e->move_index = 2;
    e->first_turn = 0;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("life suck player", g_battle.hp, 70);
    check("life suck heal", e->hp, 60);

    /* Its Stun sets Fell but still rolls: the next move is never Fell. */
    act2_fresh(2);
    e = &g_battle.enemies[0];
    e->move_index = 3;
    e->last_move = 1;
    e->first_turn = 0;
    battle_test_enemy_turn(0);
    check("parasite stun rolled", e->last_move, 0);
    check("parasite not fell", e->move_index == 1 || e->move_index == 2, 1);

    /* HexPower: a Skill adds Dazed to the draw pile, one per stack; an Attack
     * adds nothing. */
    act2_fresh(1);
    g_battle.hex = 1;
    g_battle.draw_count = 0;
    act2_play("Defend_R");
    check("hex draw count", g_battle.draw_count, 1);
    check("hex dazed", pile_has(g_battle.draw, g_battle.draw_count, "Dazed"), 1);
    g_battle.hex = 2;
    g_battle.draw_count = 0;
    act2_play("Defend_R");
    check("hex two stacks", count_card(g_battle.draw, g_battle.draw_count, "Dazed"), 2);
    g_battle.hex = 1;
    g_battle.draw_count = 0;
    act2_play("Strike_R");
    check("hex attack none", g_battle.draw_count, 0);

    /* Bag of Marbles is an ApplyPowerAction, so ArtifactPower eats it too. */
    act2_fresh(3);
    battle_test_bag_of_marbles();
    check("marbles artifact", g_battle.enemies[0].artifact, 2);
    check("marbles vuln eaten", g_battle.enemies[0].vulnerable, 0);

    /* Multi-hit intents: Peck is five hits, Poke two. */
    act2_fresh(0);
    g_battle.enemies[0].move_index = 0;
    check("peck hits", battle_intent_hits(0), 5);
    act2_fresh(1);
    g_battle.enemies[0].move_index = 4;
    check("poke hits", battle_intent_hits(0), 2);
}

/* ---- batch 9: the Slavers ---------------------------------------------- */

static void test_batch9(void) {
    Enemy *e;

    /* Three of them, left to right blue / taskmaster / red, with the localised
     * names and distinct sprite keys. */
    battle_test_spawn_slavers();
    check("slavers three", g_battle.enemy_count, 3);
    check("slaver blue name", strcmp(g_battle.enemies[0].name, "奴隶贩子"), 0);
    check("slaver taskmaster name", strcmp(g_battle.enemies[1].name, "奴隶头子"), 0);
    check("slaver red name", strcmp(g_battle.enemies[2].name, "奴隶贩子"), 0);
    check("slaver blue art", strcmp(g_battle.enemies[0].art, "蓝奴隶贩子"), 0);
    check("slaver red art", strcmp(g_battle.enemies[2].art, "红奴隶贩子"), 0);
    check("slaver blue hp", g_battle.enemies[0].max_hp >= 46 && g_battle.enemies[0].max_hp <= 50, 1);
    check("slaver red hp", g_battle.enemies[2].max_hp >= 46 && g_battle.enemies[2].max_hp <= 50, 1);
    check("slaver taskmaster hp",
          g_battle.enemies[1].max_hp >= 54 && g_battle.enemies[1].max_hp <= 60, 1);
    check("slaver red opens stab", g_battle.enemies[2].move_index, 0);
    check("slaver taskmaster intent", g_battle.enemies[1].move_index, 0);

    /* SlaverBlue.getMove: Stab on a high roll unless Stab just happened twice;
     * Rake unless Rake just happened twice. */
    battle_test_spawn_slavers();
    e = &g_battle.enemies[0];
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(0, 80);
    check("slaver blue stab", e->move_index, 0);
    e->last_move = 0;
    e->last_before = 0;
    battle_test_choose_roll(0, 80);
    check("slaver blue rake", e->move_index, 1);
    e->last_move = 1;
    e->last_before = 1;
    battle_test_choose_roll(0, 10);
    check("slaver blue stab after rakes", e->move_index, 0);

    /* Rake: 7 damage and 1 Weak. */
    battle_test_spawn_slavers();
    e = &g_battle.enemies[0];
    e->move_index = 1;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("slaver rake damage", g_battle.hp, 73);
    check("slaver rake weak", g_battle.weak, 1);

    /* SlaverRed.getMove: one scripted Stab, then Entangle once, then it may
     * Stab again; Scrape carries 1 Vulnerable. */
    battle_test_spawn_slavers();
    e = &g_battle.enemies[2];
    e->first_turn = 0;
    e->used_entangle = 0;
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(2, 80);
    check("slaver red entangle", e->move_index, 1);
    g_battle.entangled = 0;
    battle_test_enemy_turn(2);
    check("slaver entangle set", g_battle.entangled, 1);
    check("slaver entangle used", e->used_entangle, 1);
    battle_test_choose_roll(2, 80);
    check("slaver entangle once", e->move_index, 0);
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(2, 10);
    check("slaver scrape", e->move_index, 2);
    g_battle.vulnerable = 0;
    battle_test_enemy_turn(2);
    check("slaver scrape vuln", g_battle.vulnerable, 1);

    /* Scouring Whip: 7 damage and a Wound in the discard pile. */
    battle_test_spawn_slavers();
    e = &g_battle.enemies[1];
    g_battle.hp = 80;
    g_battle.discard_count = 0;
    battle_test_enemy_turn(1);
    check("slaver whip damage", g_battle.hp, 73);
    check("slaver whip wound", g_battle.discard_count, 1);
    check("slaver whip wound card", pile_has(g_battle.discard, g_battle.discard_count, "Wound"), 1);

    /* EntanglePower: Attacks are rejected, Skills still play, and the lock is
     * gone as the turn ends. */
    battle_test_spawn_slavers();
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.energy = 3;
    g_battle.entangled = 1;
    hand("Strike_R", 0);
    check("entangle blocks attack", battle_play(0, 0), -7);
    hand("Defend_R", 0);
    check("entangle allows skill", battle_play(0, 0), 0);
    check("entangle defend block", g_battle.block >= 5, 1);
    g_battle.entangled = 1;
    g_battle.hp = 80;
    battle_end_turn();
    check("entangle cleared at end", g_battle.entangled, 0);

    /* The intent reads as an attack for the new move kinds. */
    battle_test_spawn_slavers();
    g_battle.enemies[1].move_index = 0;
    check("whip intent damage", battle_enemy_intent_damage(1), 7);
    g_battle.enemies[0].move_index = 1;
    check("rake intent damage", battle_enemy_intent_damage(0), 7);
}

/* ---- batch 10: BronzeAutomaton and BronzeOrbs --------------------------- */

static void automaton_fresh(void) {
    battle_test_spawn_automaton();
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.energy = g_battle.max_energy = 3;
}

static void test_batch10(void) {
    Enemy *e;

    /* It opens alone, with Artifact 3 and Spawn Orbs queued. */
    automaton_fresh();
    e = &g_battle.enemies[0];
    check("automaton one", g_battle.enemy_count, 1);
    check("automaton hp", e->max_hp, 300);
    check("automaton artifact", e->artifact, 3);
    check("automaton move count", e->move_count, 5);
    check("automaton opens spawn", e->move_index, 3);
    check("automaton spawn intent", battle_intent_kind(0), MOVE_SPAWN);
    check("automaton draw x", e->draw_x, 160);
    check("automaton draw w", e->draw_w, 80);

    /* Spawn Orbs: two BronzeOrbs, 52-58 HP, positioned left and right. */
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("automaton spawned two", g_battle.enemy_count, 3);
    check("automaton spawn no damage", g_battle.hp, 80);
    check("orb one name", strcmp(g_battle.enemies[1].name, "铜球"), 0);
    check("orb two name", strcmp(g_battle.enemies[2].name, "铜球"), 0);
    check("orb one hp", g_battle.enemies[1].max_hp >= 52 && g_battle.enemies[1].max_hp <= 58, 1);
    check("orb two hp", g_battle.enemies[2].max_hp >= 52 && g_battle.enemies[2].max_hp <= 58, 1);
    check("orb one owner", g_battle.enemies[1].minion_owner, 0);
    check("orb one x", g_battle.enemies[1].draw_x, 100);
    check("orb two x", g_battle.enemies[2].draw_x, 288);
    check("orb one width", g_battle.enemies[1].draw_w, 52);
    check("orb no stolen card", g_battle.enemies[1].stasis_card, -1);
    check("automaton after spawn flail", e->move_index, 0);
    check("automaton turn one after spawn", e->ai_turn, 1);
    check("automaton flail hits", battle_intent_hits(0), 2);
    check("automaton flail damage", battle_enemy_intent_damage(0), 7);

    /* The orbs spawned this enemy phase do not also act in it. */
    automaton_fresh();
    g_battle.hp = 80;
    battle_end_turn();
    check("spawn turn no orb damage", g_battle.hp, 80);
    check("spawn turn still three", g_battle.enemy_count, 3);
    check("spawn turn boss flail", g_battle.enemies[0].move_index, 0);

    /* Flail is two hits of 7; the next move is Boost. */
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("automaton flail total", g_battle.hp, 66);
    check("automaton boost next", e->move_index, 4);
    check("automaton turn two", e->ai_turn, 2);

    /* Boost: +9 block, +3 Strength. */
    e = &g_battle.enemies[0];
    e->block = 0;
    e->strength = 0;
    battle_test_enemy_turn(0);
    check("automaton boost block", e->block, 9);
    check("automaton boost strength", e->strength, 3);
    check("automaton flail after boost", e->move_index, 0);

    /* The full loop: Spawn, Flail, Boost, Flail, Boost, Hyper Beam, Stunned,
     * then Flail again.  getMove increments numTurns on both Flail and Boost. */
    automaton_fresh();
    e = &g_battle.enemies[0];
    g_battle.hp = 300;
    battle_test_enemy_turn(0);           /* Spawn Orbs -> Flail */
    check("loop after spawn", e->move_index, 0);
    check("loop turn 1", e->ai_turn, 1);
    battle_test_enemy_turn(0);           /* Flail -> Boost */
    check("loop turn 2", e->move_index, 4);
    check("loop turn 2 count", e->ai_turn, 2);
    battle_test_enemy_turn(0);           /* Boost -> Flail */
    check("loop turn 3", e->move_index, 0);
    check("loop turn 3 count", e->ai_turn, 3);
    battle_test_enemy_turn(0);           /* Flail -> Boost */
    check("loop turn 4", e->move_index, 4);
    check("loop turn 4 count", e->ai_turn, 4);
    battle_test_enemy_turn(0);           /* Boost -> Hyper Beam */
    check("loop hyper", e->move_index, 1);
    check("loop hyper count", e->ai_turn, 0);
    battle_test_enemy_turn(0);           /* Hyper -> Stunned */
    check("loop stun", e->move_index, 2);
    battle_test_enemy_turn(0);           /* Stunned -> Flail */
    check("loop flail again", e->move_index, 0);
    check("loop flail count", e->ai_turn, 1);

    /* BronzeOrb.getMove: Stasis only once, then Beam/Support. */
    automaton_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[1];
    e->last_move = 255;
    e->last_before = 255;
    e->used_stasis = 0;
    battle_test_choose_roll(1, 50);
    check("orb stasis", e->move_index, 2);
    check("orb stasis used", e->used_stasis, 1);
    battle_test_choose_roll(1, 50);
    check("orb stasis once", e->move_index != 2, 1);

    /* Stasis removes a card from the draw pile and holds the full entry. */
    automaton_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[1];
    int bash = battle_find_card("Bash");
    g_battle.draw_count = 0;
    g_battle.discard_count = 0;
    g_battle.draw[g_battle.draw_count++] = CARD_ENTRY(bash, 0);
    e->move_index = 2;
    e->stasis_card = -1;
    e->used_stasis = 0;
    battle_test_enemy_turn(1);
    check("stasis draw empty", g_battle.draw_count, 0);
    check("stasis holds card", e->stasis_card, CARD_ENTRY(bash, 0));
    check("stasis card index", CARD_INDEX(e->stasis_card), bash);

    /* Support Beam gives the Automaton 12 block. */
    automaton_fresh();
    battle_test_enemy_turn(0);
    g_battle.enemies[0].block = 0;
    g_battle.enemies[1].move_index = 1;
    battle_test_enemy_turn(1);
    check("orb support block", g_battle.enemies[0].block, 12);

    /* An orb that dies returns its stolen card to the hand. */
    automaton_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[1];
    bash = battle_find_card("Bash");
    e->stasis_card = (short)CARD_ENTRY(bash, 0);
    e->hp = 1;
    g_battle.hand_count = 0;
    g_battle.energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_run.screen = SCREEN_FIGHT;
    hand("Strike_R", 0);
    battle_play(0, 1);
    check("stasis orb dead", e->hp, 0);
    check("stasis card came back", g_battle.hand_count, 1);
    check("stasis returned bash", CARD_INDEX(g_battle.hand[0]), bash);

    /* The Automaton's death takes both orbs with it. */
    automaton_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[0];
    e->hp = 1;
    g_battle.enemies[1].hp = 50;
    g_battle.enemies[2].hp = 50;
    g_battle.hand_count = 0;
    g_battle.energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_run.screen = SCREEN_FIGHT;
    hand("Strike_R", 0);
    battle_play(0, 0);
    check("automaton dead", e->hp, 0);
    check("orb one died with boss", g_battle.enemies[1].hp, 0);
    check("orb two died with boss", g_battle.enemies[2].hp, 0);
}

/* ---- batch 11: TheCollector and its Torch Heads ------------------------- */

static void collector_fresh(void) {
    battle_test_spawn_collector();
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.energy = g_battle.max_energy = 3;
}

static void test_batch11(void) {
    Enemy *e;

    /* It opens alone, 282 HP, with Spawn queued. */
    collector_fresh();
    e = &g_battle.enemies[0];
    check("collector one", g_battle.enemy_count, 1);
    check("collector hp", e->max_hp, 282);
    check("collector moves", e->move_count, 5);
    check("collector opens spawn", e->move_index, 0);
    check("collector spawn intent", battle_intent_kind(0), MOVE_SPAWN);
    check("collector draw x", e->draw_x, 300);
    check("collector draw w", e->draw_w, 80);

    /* Spawn: two Torch Heads at 38-40 HP, to the left of the Collector. */
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("collector spawned two", g_battle.enemy_count, 3);
    check("collector spawn no damage", g_battle.hp, 80);
    check("torch one name", strcmp(g_battle.enemies[1].name, "火炬头"), 0);
    check("torch two name", strcmp(g_battle.enemies[2].name, "火炬头"), 0);
    check("torch one hp", g_battle.enemies[1].max_hp >= 38 && g_battle.enemies[1].max_hp <= 40, 1);
    check("torch two hp", g_battle.enemies[2].max_hp >= 38 && g_battle.enemies[2].max_hp <= 40, 1);
    check("torch one owner", g_battle.enemies[1].minion_owner, 0);
    check("torch two owner", g_battle.enemies[2].minion_owner, 0);
    check("torch one x", g_battle.enemies[1].draw_x, 160);
    check("torch two x", g_battle.enemies[2].draw_x, 216);
    check("torch one width", g_battle.enemies[1].draw_w, 48);
    check("torch tackle intent", battle_intent_kind(1), MOVE_ATTACK);
    check("torch tackle damage", battle_enemy_intent_damage(1), 7);
    check("collector turn after spawn", e->ai_turn, 1);

    /* Fireball is 18. */
    collector_fresh();
    battle_test_enemy_turn(0);
    g_battle.hp = 80;
    e = &g_battle.enemies[0];
    e->move_index = 1;
    battle_test_enemy_turn(0);
    check("collector fireball", g_battle.hp, 62);

    /* Buff: +15 block for itself, +3 Strength to every living monster. */
    collector_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[0];
    e->block = 0;
    e->strength = 0;
    g_battle.enemies[1].strength = 0;
    g_battle.enemies[2].strength = 0;
    e->move_index = 2;
    battle_test_enemy_turn(0);
    check("collector buff block", e->block, 15);
    check("collector buff strength", e->strength, 3);
    check("torch buff strength", g_battle.enemies[1].strength, 3);
    check("torch two buff strength", g_battle.enemies[2].strength, 3);

    /* Mega Debuff fires once, as soon as turnsTaken is 3. */
    collector_fresh();
    battle_test_enemy_turn(0);
    e = &g_battle.enemies[0];
    e->ai_turn = 3;
    e->used_ult = 0;
    battle_test_choose_roll(0, 0);
    check("collector mega queued", e->move_index, 3);
    check("collector mega intent", battle_intent_kind(0), MOVE_CURSE);
    g_battle.weak = g_battle.vulnerable = g_battle.frail = 0;
    battle_test_enemy_turn(0);
    check("collector mega weak", g_battle.weak, 3);
    check("collector mega vulnerable", g_battle.vulnerable, 3);
    check("collector mega frail", g_battle.frail, 3);
    check("collector mega frail just", g_battle.frail_just, 1);
    check("collector mega used", e->used_ult, 1);
    battle_test_choose_roll(0, 0);
    check("collector mega once", e->move_index, 1);

    /* A Torch Head Tackles for 7. */
    collector_fresh();
    battle_test_enemy_turn(0);
    g_battle.hp = 80;
    g_battle.enemies[1].move_index = 0;
    battle_test_enemy_turn(1);
    check("torch tackle", g_battle.hp, 73);

    /* Revive brings a dead head back at full HP, and only once. */
    collector_fresh();
    battle_test_enemy_turn(0);
    battle_test_damage_enemy(1, 999);
    e = &g_battle.enemies[0];
    int head_hp = g_battle.enemies[1].max_hp;
    check("torch dead", g_battle.enemies[1].hp, 0);
    e->ai_turn = 1;
    e->last_move = 0;
    e->last_before = 255;
    e->move_index = 4;
    battle_test_enemy_turn(0);
    check("collector revived", g_battle.enemies[1].hp, head_hp);
    check("collector revived owner", g_battle.enemies[1].minion_owner, 0);
    check("collector revived death flag", g_battle.enemies[1].death_done, 0);

    /* Killing the Collector takes both heads with it. */
    collector_fresh();
    battle_test_enemy_turn(0);
    g_battle.enemies[1].hp = 30;
    g_battle.enemies[2].hp = 30;
    battle_test_damage_enemy(0, 999);
    check("collector dead", g_battle.enemies[0].hp, 0);
    check("torch one died with boss", g_battle.enemies[1].hp, 0);
    check("torch two died with boss", g_battle.enemies[2].hp, 0);
}


/* ---- batch 12: the Gremlin Leader and its five Gremlins ---------------- */

static void gremlin_fresh(void) {
    battle_test_spawn_gremlin_leader();
    /* Overwrite the two random starters and add the third slot, so the layout
     * is deterministic: three Warriors unless a test says otherwise. */
    battle_test_spawn_gremlin_minion(1, 0);
    battle_test_spawn_gremlin_minion(2, 0);
    battle_test_spawn_gremlin_minion(3, 0);
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.energy = g_battle.max_energy = 3;
    g_battle.hp = 80;
}

static void test_batch12(void) {
    Enemy *e;

    /* It opens with two random minions and sits on the right edge. */
    battle_test_spawn_gremlin_leader();
    e = &g_battle.enemies[0];
    check("leader minions two", g_battle.enemy_count, 3);
    check("leader hp low", e->max_hp >= 140, 1);
    check("leader hp high", e->max_hp <= 148, 1);
    check("leader moves", e->move_count, 3);
    check("leader x", e->draw_x, 316);
    check("leader width", e->draw_w, 60);
    check("leader minion one owner", g_battle.enemies[1].minion_owner, 0);
    check("leader minion two owner", g_battle.enemies[2].minion_owner, 0);

    /* A full room: Warrior / Thief / Fat at their real HP ranges and slots. */
    gremlin_fresh();
    check("gremlin four enemies", g_battle.enemy_count, 4);
    check("warrior name", strcmp(g_battle.enemies[1].name, "火大地精"), 0);
    check("warrior hp", g_battle.enemies[1].max_hp >= 20 && g_battle.enemies[1].max_hp <= 24, 1);
    check("warrior x", g_battle.enemies[1].draw_x, 120);
    check("thief x", g_battle.enemies[2].draw_x, 188);
    check("fat x", g_battle.enemies[3].draw_x, 256);
    check("minion width", g_battle.enemies[3].draw_w, 54);

    /* Warrior: Claw 4, and AngryPower adds Strength only when HP is lost. */
    gremlin_fresh();
    g_battle.enemies[1].move_index = 0;
    battle_test_enemy_turn(1);
    check("warrior claw", g_battle.hp, 76);
    g_battle.enemies[1].block = 0;
    battle_test_damage_enemy(1, 3);
    check("warrior angry", g_battle.enemies[1].strength, 1);
    g_battle.enemies[1].block = 10;
    battle_test_damage_enemy(1, 3);
    check("warrior angry blocked", g_battle.enemies[1].strength, 1);
    check("warrior block spent", g_battle.enemies[1].block, 7);

    /* Thief Puncture 9. */
    gremlin_fresh();
    battle_test_spawn_gremlin_minion(2, 1);
    g_battle.enemies[2].move_index = 0;
    battle_test_enemy_turn(2);
    check("thief puncture", g_battle.hp, 71);

    /* Fat Blunt 4 + 1 Weak. */
    gremlin_fresh();
    battle_test_spawn_gremlin_minion(3, 2);
    g_battle.weak = 0;
    g_battle.enemies[3].move_index = 0;
    battle_test_enemy_turn(3);
    check("fat blunt", g_battle.hp, 76);
    check("fat weak", g_battle.weak, 1);

    /* Tsundere Protect gives 7 block to another monster and keeps Protecting
     * while one lives; alone it switches to Bash and stays there. */
    gremlin_fresh();
    battle_test_spawn_gremlin_minion(1, 3);
    for (int i = 2; i < 4; i++) g_battle.enemies[i].hp = 0;
    g_battle.enemies[0].block = 0;
    g_battle.enemies[1].block = 0;
    g_battle.enemies[1].last_move = 255;
    g_battle.enemies[1].move_index = 0;
    battle_test_enemy_turn(1);
    check("tsundere protect owner", g_battle.enemies[0].block, 7);
    check("tsundere protect self", g_battle.enemies[1].block, 0);
    check("tsundere stays protect", g_battle.enemies[1].move_index, 0);
    g_battle.enemies[0].hp = 0;
    battle_test_choose_roll(1, 0);
    check("tsundere alone bash", g_battle.enemies[1].move_index, 1);
    g_battle.hp = 80;
    battle_test_enemy_turn(1);
    check("tsundere bash damage", g_battle.hp, 74);
    check("tsundere keeps bash", g_battle.enemies[1].move_index, 1);

    /* Wizard: currentCharge starts at 1, so two Charges then a 25 blast. */
    gremlin_fresh();
    battle_test_spawn_gremlin_minion(1, 4);
    for (int i = 2; i < 4; i++) g_battle.enemies[i].hp = 0;
    Enemy *wiz = &g_battle.enemies[1];
    check("wizard charge start", wiz->ai_turn, 1);
    check("wizard opens charge", wiz->move_index, 0);
    battle_test_enemy_turn(1);
    check("wizard charge two", wiz->ai_turn, 2);
    check("wizard still charge", wiz->move_index, 0);
    battle_test_enemy_turn(1);
    check("wizard charge three", wiz->ai_turn, 3);
    check("wizard blast queued", wiz->move_index, 1);
    g_battle.hp = 80;
    battle_test_enemy_turn(1);
    check("wizard blast", g_battle.hp, 55);
    check("wizard reset", wiz->ai_turn, 0);
    check("wizard charge again", wiz->move_index, 0);

    /* Leader, no minions: Rally unless the last move was Rally, otherwise
     * Stab; roll 75+ does the same for Stab. */
    gremlin_fresh();
    for (int i = 1; i < 4; i++) g_battle.enemies[i].hp = 0;
    e = &g_battle.enemies[0];
    e->last_move = 255;
    battle_test_choose_roll(0, 0);
    check("leader no minions rally", e->move_index, 0);
    e->last_move = 0;
    battle_test_choose_roll(0, 0);
    check("leader no minions repeat rally", e->move_index, 2);
    e->last_move = 2;
    battle_test_choose_roll(0, 0);
    check("leader no minions repeat stab", e->move_index, 0);
    e->last_move = 255;
    battle_test_choose_roll(0, 90);
    check("leader no minions stab", e->move_index, 2);

    /* One minion: no repeated Rally, Encourage guarded, Stab guarded. */
    gremlin_fresh();
    for (int i = 2; i < 4; i++) g_battle.enemies[i].hp = 0;
    e = &g_battle.enemies[0];
    e->last_move = 255;
    battle_test_choose_roll(0, 0);
    check("leader one rally", e->move_index, 0);
    e->last_move = 0;
    battle_test_choose_roll(0, 0);
    check("leader one repeat rally", e->move_index != 0, 1);
    e->last_move = 1;
    battle_test_choose_roll(0, 60);
    check("leader one repeat encourage", e->move_index, 2);
    e->last_move = 2;
    battle_test_choose_roll(0, 90);
    check("leader one repeat stab", e->move_index != 2, 1);

    /* Two minions: Encourage on a low roll, Stab on a high one, no repeats. */
    gremlin_fresh();
    e = &g_battle.enemies[0];
    e->last_move = 255;
    battle_test_choose_roll(0, 60);
    check("leader two encourage", e->move_index, 1);
    e->last_move = 1;
    battle_test_choose_roll(0, 60);
    check("leader two repeat encourage", e->move_index, 2);
    e->last_move = 2;
    battle_test_choose_roll(0, 90);
    check("leader two repeat stab", e->move_index, 1);

    /* Rally fills two empty slots, and minions do not act on the summon turn. */
    gremlin_fresh();
    for (int i = 1; i < 4; i++) g_battle.enemies[i].hp = 0;
    e = &g_battle.enemies[0];
    e->move_index = 0;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("leader rallied two", g_battle.enemy_count, 4);
    check("leader rally no damage", g_battle.hp, 80);
    check("leader rally owner", g_battle.enemies[1].minion_owner, 0);
    check("leader rally owner two", g_battle.enemies[2].minion_owner, 0);

    /* Encourage: leader +3 Strength, each living minion +3 and +6 block. */
    gremlin_fresh();
    for (int i = 0; i < 4; i++) {
        g_battle.enemies[i].strength = 0;
        g_battle.enemies[i].block = 0;
    }
    g_battle.enemies[0].move_index = 1;
    battle_test_enemy_turn(0);
    check("leader encourage strength", g_battle.enemies[0].strength, 3);
    check("warrior encouraged strength", g_battle.enemies[1].strength, 3);
    check("warrior encouraged block", g_battle.enemies[1].block, 6);
    check("thief encouraged block", g_battle.enemies[2].block, 6);
    check("fat encouraged block", g_battle.enemies[3].block, 6);

    /* Stab is three hits of 6. */
    gremlin_fresh();
    g_battle.enemies[0].move_index = 2;
    g_battle.hp = 80;
    battle_test_enemy_turn(0);
    check("leader stab", g_battle.hp, 62);

    /* Killing the leader takes the whole room with it. */
    gremlin_fresh();
    battle_test_damage_enemy(0, 999);
    check("leader dead", g_battle.enemies[0].hp, 0);
    check("leader death clears one", g_battle.enemies[1].hp, 0);
    check("leader death clears two", g_battle.enemies[2].hp, 0);
    check("leader death clears three", g_battle.enemies[3].hp, 0);
}

/* ---- batch 13: act 1 move sets and the Slime Boss split ---------------- */

static void test_batch13(void) {
    Enemy *e;

    /* Slime Boss: Sticky (3 Slimed, no damage) -> Prep -> Slam 35 -> Sticky. */
    battle_test_spawn_act1(0);
    e = &g_battle.enemies[0];
    check("slime boss one enemy", g_battle.enemy_count, 1);
    check("slime boss hp", e->max_hp, 140);
    check("slime boss opening", e->move_index, 2);
    check("slime boss opening kind", e->moves[e->move_index].kind, MOVE_SLIME);
    check("slime boss goop count", e->moves[e->move_index].value2, 3);
    battle_test_enemy_turn(0);
    check("slime goop no damage", g_battle.hp, 80);
    check("slime goop cards", g_battle.discard_count, 3);
    check("slime boss prep", e->move_index, 1);
    battle_test_enemy_turn(0);
    check("slime prep no damage", g_battle.hp, 80);
    check("slime boss slam", e->move_index, 0);
    battle_test_enemy_turn(0);
    check("slime slam damage", g_battle.hp, 45);
    check("slime boss sticky again", e->move_index, 2);

    /* Half HP split: the two L slimes inherit the boss' remaining HP. */
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    check("split two enemies", g_battle.enemy_count, 2);
    check("split spike hp", g_battle.enemies[0].max_hp, 69);
    check("split acid hp", g_battle.enemies[1].max_hp, 69);
    check("split spike name", strcmp(g_battle.enemies[0].name, "尖刺史莱姆"), 0);
    check("split acid name", strcmp(g_battle.enemies[1].name, "酸液史莱姆"), 0);
    check("split spike moves", g_battle.enemies[0].move_count, 2);
    check("split acid moves", g_battle.enemies[1].move_count, 3);
    check("split spike opening legal", g_battle.enemies[0].move_index < 2, 1);
    check("split acid opening legal", g_battle.enemies[1].move_index < 3, 1);
    check("split spike table", g_battle.enemies[0].moves[0].kind, MOVE_SLIME);
    check("split spike goop", g_battle.enemies[0].moves[0].value2, 2);
    check("split acid table", g_battle.enemies[1].moves[0].value2, 2);

    /* Gremlin Nob opens with Bellow, then 33% / two Rushes force Skull Bash. */
    battle_test_spawn_act1(1);
    e = &g_battle.enemies[0];
    check("nob hp", e->max_hp, 82);
    check("nob bellow first", e->move_index, 2);
    check("nob bellow used", e->used_bellow, 1);
    battle_test_choose_roll(0, 32);
    check("nob bash roll", e->move_index, 1);
    check("nob bash kind", e->moves[1].kind, MOVE_ATTACK_DEBUFF);
    check("nob bash damage", e->moves[1].value, 6);
    check("nob bash vuln", e->moves[1].value2, 2);
    e->last_move = 0;
    e->last_before = 0;
    battle_test_choose_roll(0, 80);
    check("nob two rushes bash", e->move_index, 1);
    e->last_move = 1;
    e->last_before = 1;
    battle_test_choose_roll(0, 80);
    check("nob rush otherwise", e->move_index, 0);
    battle_test_spawn_act1(1);
    g_battle.enemies[0].move_index = 1;
    battle_test_enemy_turn(0);
    check("nob bash hits", g_battle.hp, 74);
    check("nob bash vulnerable", g_battle.vulnerable, 2);

    /* Two Acid Slimes (M): opening move is one of Tackle/Wound/Lick. */
    battle_test_spawn_act1(2);
    check("acid pair count", g_battle.enemy_count, 2);
    check("acid pair hp low", g_battle.enemies[0].max_hp >= 28, 1);
    check("acid pair hp high", g_battle.enemies[1].max_hp <= 32, 1);
    check("acid pair opening one", g_battle.enemies[0].move_index < 3, 1);
    check("acid pair opening two", g_battle.enemies[1].move_index < 3, 1);
    check("acid pair moves", g_battle.enemies[0].move_count, 3);

    battle_test_spawn_act1(2);
    g_battle.enemies[0].move_index = 1;
    battle_test_enemy_turn(0);
    check("acid wound tackle", g_battle.hp, 73);
    check("acid wound slimed", g_battle.discard_count, 1);
    battle_test_spawn_act1(2);
    g_battle.enemies[0].move_index = 0;
    battle_test_enemy_turn(0);
    check("acid tackle", g_battle.hp, 70);
    battle_test_spawn_act1(2);
    g_battle.enemies[0].move_index = 2;
    battle_test_enemy_turn(0);
    check("acid lick", g_battle.weak, 1);

    /* Spike Slime (L): Flame Tackle 16 + 2 Slimed, Frail Lick 2. */
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    g_battle.enemies[0].move_index = 0;
    battle_test_enemy_turn(0);
    check("spike flame damage", g_battle.hp, 64);
    check("spike flame slimed", g_battle.discard_count, 2);
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    g_battle.enemies[0].move_index = 1;
    battle_test_enemy_turn(0);
    check("spike frail no damage", g_battle.hp, 80);
    check("spike frail stacks", g_battle.frail, 2);
    check("spike frail just", g_battle.frail_just, 1);

    /* Spike Slime (L) getMove guards. */
    e = &g_battle.enemies[0];
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(0, 20);
    check("spike flame roll", e->move_index, 0);
    e->last_move = 0;
    e->last_before = 0;
    battle_test_choose_roll(0, 20);
    check("spike two flames frail", e->move_index, 1);
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(0, 50);
    check("spike high roll frail", e->move_index, 1);
    e->last_move = 1;
    e->last_before = 1;
    battle_test_choose_roll(0, 50);
    check("spike two frails flame", e->move_index, 0);

    /* Acid Slime (L): 11 + 2, 16, 2 Weak; each getMove branch legal. */
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    e = &g_battle.enemies[1];
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(1, 10);
    check("acid L wound roll", e->move_index, 0);
    e->last_move = 0;
    e->last_before = 0;
    battle_test_choose_roll(1, 10);
    check("acid L two wounds", e->move_index == 1 || e->move_index == 2, 1);
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(1, 50);
    check("acid L normal middle", e->move_index, 1);
    e->last_move = 1;
    e->last_before = 1;
    battle_test_choose_roll(1, 50);
    check("acid L after normal", e->move_index == 0 || e->move_index == 2, 1);
    e->last_move = 255;
    e->last_before = 255;
    battle_test_choose_roll(1, 80);
    check("acid L high weak", e->move_index, 2);
    e->last_move = 2;
    e->last_before = 2;
    battle_test_choose_roll(1, 80);
    check("acid L two weaks", e->move_index == 0 || e->move_index == 1, 1);

    battle_test_spawn_act1(0);
    battle_test_split_boss();
    g_battle.enemies[1].move_index = 0;
    battle_test_enemy_turn(1);
    check("acid L wound damage", g_battle.hp, 69);
    check("acid L wound slimed", g_battle.discard_count, 2);
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    g_battle.enemies[1].move_index = 1;
    battle_test_enemy_turn(1);
    check("acid L tackle", g_battle.hp, 64);
    battle_test_spawn_act1(0);
    battle_test_split_boss();
    g_battle.enemies[1].move_index = 2;
    battle_test_enemy_turn(1);
    check("acid L lick", g_battle.weak, 2);

    /* Jaw Worm still opens with Chomp 11. */
    battle_test_spawn_act1(3);
    check("jaw one enemy", g_battle.enemy_count, 1);
    check("jaw opening", g_battle.enemies[0].move_index, 0);
    check("jaw chomp", g_battle.enemies[0].moves[0].value, 11);
    battle_test_enemy_turn(0);
    check("jaw chomp damage", g_battle.hp, 69);
}

static void test_batch14(void) {
    /* Madness: one random card in hand becomes free for the combat, and the
     * next play must actually pay zero (battle_play used to read c->cost). */
    fresh(200);
    g_battle.energy = 3;
    hand("Madness", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    battle_play(0, 0);
    check("madness energy", g_battle.energy, 2);
    check("madness exhaust", g_battle.exhaust_count, 1);
    {
        int free_cards = 0;
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (battle_card_cost(i) == 0) free_cards++;
        }
        check("madness one free", free_cards, 1);
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (battle_card_cost(i) == 0) {
                battle_play(i, 0);
                check("madness free play", g_battle.energy, 2);
                break;
            }
        }
    }

    /* Apotheosis: every pile is upgraded. */
    fresh(200);
    g_battle.energy = 3;
    hand("Apotheosis", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Bash");
    pile_add(g_battle.discard, &g_battle.discard_count, "Cleave");
    pile_add(g_battle.exhaust, &g_battle.exhaust_count, "Pummel");
    battle_play(0, 0);
    check("apotheosis energy", g_battle.energy, 1);
    check("apotheosis hand up", CARD_UPGRADED(g_battle.hand[0]) && CARD_UPGRADED(g_battle.hand[1]), 1);
    check("apotheosis draw up", CARD_UPGRADED(g_battle.draw[0]), 1);
    check("apotheosis discard up", CARD_UPGRADED(g_battle.discard[0]), 1);
    check("apotheosis exhaust up", CARD_UPGRADED(g_battle.exhaust[0]), 1);

    /* Chrysalis / Metamorphosis: three random Skills / Attacks into the draw
     * pile, free if they used to cost something. */
    fresh(200);
    hand("Chrysalis", 0);
    battle_play(0, 0);
    check("chrysalis count", g_battle.draw_count, 3);
    {
        int skills = 0, free_ok = 1;
        for (int i = 0; i < g_battle.draw_count; i++) {
            int index = CARD_INDEX(g_battle.draw[i]);
            if (sts_cards[index].type == 1) skills++;
            if (sts_cards[index].cost > 0 && !g_battle.zero_cost[index]) free_ok = 0;
        }
        check("chrysalis skills", skills, 3);
        check("chrysalis free", free_ok, 1);
    }
    fresh(200);
    hand("Metamorphosis", 0);
    battle_play(0, 0);
    check("metamorphosis count", g_battle.draw_count, 3);
    {
        int attacks = 0;
        for (int i = 0; i < g_battle.draw_count; i++) {
            if (sts_cards[CARD_INDEX(g_battle.draw[i])].type == 0) attacks++;
        }
        check("metamorphosis attacks", attacks, 3);
    }

    /* Secret Technique / Weapon: choose one matching card out of the draw pile. */
    fresh(200);
    hand("Secret Technique", 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Bash");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("secret technique picker", g_run.screen == SCREEN_PICK, 1);
    check("secret technique count", battle_pick_count(), 2);
    check("secret technique type", sts_cards[battle_pick_card(0)].type, 1);
    battle_pick_choose(0);
    check("secret technique hand", g_battle.hand_count, 1);
    check("secret technique hand type", sts_cards[CARD_INDEX(g_battle.hand[0])].type, 1);
    check("secret technique draw", g_battle.draw_count, 3);
    check("secret technique exhaust", g_battle.exhaust_count, 1);

    fresh(200);
    hand("Secret Technique", 1);
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("secret technique plus picker", g_run.screen == SCREEN_PICK, 1);
    battle_pick_choose(0);
    check("secret technique plus no exhaust", g_battle.exhaust_count, 0);

    fresh(200);
    hand("Secret Technique", 0);
    {
        int rc = battle_play(0, 0);
        check("secret technique canuse", rc, -2);
        check("secret technique canuse energy", g_battle.energy, 3);
        check("secret technique canuse hand", g_battle.hand_count, 1);
    }

    fresh(200);
    hand("Secret Weapon", 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Bash");
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("secret weapon count", battle_pick_count(), 2);
    check("secret weapon type", sts_cards[battle_pick_card(0)].type, 0);
    battle_pick_choose(0);
    check("secret weapon hand", g_battle.hand_count, 1);

    /* Infernal Blade: random Attack in hand, free this turn only. */
    fresh(200);
    hand("Infernal Blade", 0);
    battle_play(0, 0);
    check("infernal energy", g_battle.energy, 2);
    check("infernal hand", g_battle.hand_count, 1);
    check("infernal attack", sts_cards[CARD_INDEX(g_battle.hand[0])].type, 0);
    {
        int index = CARD_INDEX(g_battle.hand[0]);
        check("infernal zero turn", g_battle.zero_turn[index], 1);
        check("infernal cost", battle_card_cost(0), 0);
        battle_start_turn_for_test();
        check("infernal cleared", g_battle.zero_turn[index], 0);
    }

    /* Transmutation: one random colourless card per energy, all free. */
    fresh(200);
    g_battle.energy = 3;
    hand("Transmutation", 0);
    battle_play(0, 0);
    check("transmutation energy", g_battle.energy, 0);
    check("transmutation hand", g_battle.hand_count, 3);
    {
        int colourless = 0, free_ok = 1;
        for (int i = 0; i < g_battle.hand_count; i++) {
            int index = CARD_INDEX(g_battle.hand[i]);
            if (sts_cards[index].color == 4) colourless++;
            if (!g_battle.zero_turn[index]) free_ok = 0;
        }
        check("transmutation colourless", colourless, 3);
        check("transmutation free", free_ok, 1);
    }
    fresh(200);
    g_battle.energy = 2;
    hand("Transmutation", 1);
    battle_play(0, 0);
    check("transmutation plus hand", g_battle.hand_count, 2);
    check("transmutation plus up", CARD_UPGRADED(g_battle.hand[0]) && CARD_UPGRADED(g_battle.hand[1]), 1);

    /* Violence: up to three random Attacks out of the draw pile. */
    fresh(200);
    hand("Violence", 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Strike_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    pile_add(g_battle.draw, &g_battle.draw_count, "Bash");
    pile_add(g_battle.draw, &g_battle.draw_count, "Cleave");
    pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("violence hand", g_battle.hand_count, 3);
    check("violence draw", g_battle.draw_count, 2);
    {
        int attacks = 0;
        for (int i = 0; i < g_battle.hand_count; i++) {
            if (sts_cards[CARD_INDEX(g_battle.hand[i])].type == 0) attacks++;
        }
        check("violence attacks", attacks, 3);
    }

    /* Impatience: draw only when the hand holds no Attack. */
    fresh(200);
    hand("Impatience", 0);
    for (int i = 0; i < 5; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("impatience drew", g_battle.hand_count, 2);
    fresh(200);
    hand("Impatience", 0);
    hand_add("Strike_R");
    for (int i = 0; i < 5; i++) pile_add(g_battle.draw, &g_battle.draw_count, "Defend_R");
    battle_play(0, 0);
    check("impatience held", g_battle.hand_count, 1);

    /* Havoc: play the top card for free and exhaust it. */
    fresh(200);
    hand("Havoc", 0);
    pile_add(g_battle.draw, &g_battle.draw_count, "Bash");
    battle_play(0, 0);
    check("havoc energy", g_battle.energy, 2);
    check("havoc damage", 200 - g_battle.enemies[0].hp, 8);
    check("havoc vulnerable", g_battle.enemies[0].vulnerable, 2);
    check("havoc exhaust", g_battle.exhaust_count, 1);
    fresh(200);
    hand("Havoc", 0);
    pile_add(g_battle.discard, &g_battle.discard_count, "Strike_R");
    battle_play(0, 0);
    check("havoc shuffle damage", 200 - g_battle.enemies[0].hp, 6);
    check("havoc shuffle exhaust", g_battle.exhaust_count, 1);
}


/* ---- batch 15: the last four use() effects ----------------------------- */

static const EnemyMove WEAK2[] = {{MOVE_WEAK, 2, 0, 0}};
static const EnemyMove CURSE3[] = {{MOVE_CURSE, 3, 0, 0}};
static const EnemyMove PREP1[] = {{MOVE_PREP, 0, 0, 0}};

static void enemy_move_set(int index, const EnemyMove *moves, int count) {
    Enemy *e = &g_battle.enemies[index];
    e->moves = moves;
    e->move_count = (unsigned char)count;
    e->move_index = 0;
    e->last_move = 255;
    e->last_before = 255;
}

static void test_batch15(void) {
    Enemy *e;
    int bash = battle_find_card("Bash");
    int bludgeon = battle_find_card("Bludgeon");

    /* Panacea: ArtifactPower(p, magic), exhausted. */
    fresh(200);
    hand("Panacea", 0);
    battle_play(0, 0);
    check("panacea artifact", g_battle.artifact, 1);
    check("panacea exhaust", g_battle.exhaust_count, 1);
    fresh(200);
    hand("Panacea", 1);
    battle_play(0, 0);
    check("panacea+ artifact", g_battle.artifact, 2);

    /* ArtifactPower.onSpecificTrigger eats one debuff per stack. */
    fresh(200);
    enemy_move_set(0, WEAK2, 1);
    g_battle.artifact = 1;
    battle_test_enemy_turn(0);
    check("artifact eats weak", g_battle.weak, 0);
    check("artifact spent", g_battle.artifact, 0);
    g_battle.enemies[0].move_index = 0;
    g_battle.enemies[0].last_move = 255;
    battle_test_enemy_turn(0);
    check("weak lands", g_battle.weak, 2);

    /* Collector Mega Debuff spends all three stacks, one per debuff. */
    fresh(200);
    enemy_move_set(0, CURSE3, 1);
    g_battle.artifact = 3;
    battle_test_enemy_turn(0);
    check("curse weak blocked", g_battle.weak, 0);
    check("curse vuln blocked", g_battle.vulnerable, 0);
    check("curse frail blocked", g_battle.frail, 0);
    check("curse artifact all spent", g_battle.artifact, 0);

    /* Ghostly: IntangiblePlayerPower caps the final damage at 1, after block. */
    fresh(200);
    hand("Ghostly", 0);
    battle_play(0, 0);
    check("ghostly intangible", g_battle.intangible, 1);
    check("ghostly exhaust", g_battle.exhaust_count, 1);
    enemy_move_set(0, HIT10, 1);
    g_battle.hp = 80;
    g_battle.block = 0;
    battle_test_enemy_turn(0);
    check("ghostly caps hit", g_battle.hp, 79);
    g_battle.hp = 80;
    g_battle.block = 5;
    g_battle.enemies[0].move_index = 0;
    battle_test_enemy_turn(0);
    check("ghostly block absorbs", g_battle.hp, 80);
    check("ghostly block left", g_battle.block, 4);
    g_battle.enemies[0].moves = PREP1;
    g_battle.enemies[0].move_count = 1;
    g_battle.enemies[0].move_index = 0;
    battle_end_turn();
    check("ghostly expires", g_battle.intangible, 0);

    /* Dark Shackles: LoseStrength + GainStrengthPower restores at the end of
     * the monster's turn; enemy Artifact absorbs the whole debuff. */
    fresh(200);
    e = &g_battle.enemies[0];
    e->strength = 10;
    hand("Dark Shackles", 0);
    battle_play(0, 0);
    check("shackles strength", e->strength, 1);
    check("shackles bank", e->shackled, 9);
    check("shackles exhaust", g_battle.exhaust_count, 1);
    enemy_move_set(0, PREP1, 1);
    battle_test_enemy_turn(0);
    check("shackles returns", g_battle.enemies[0].strength, 10);
    check("shackles cleared", g_battle.enemies[0].shackled, 0);

    fresh(200);
    e = &g_battle.enemies[0];
    e->strength = 10;
    e->artifact = 1;
    hand("Dark Shackles", 0);
    battle_play(0, 0);
    check("enemy artifact blocks shackles", e->strength, 10);
    check("enemy artifact spent", e->artifact, 0);
    check("shackles not banked", e->shackled, 0);

    /* Enlightenment caps the cost of every card in hand to 1 for the turn. */
    fresh(200);
    hand("Bash", 0);
    hand_add("Bludgeon");
    hand_add("Strike_R");
    hand_add("Enlightenment");
    battle_play(3, 0);
    check("enlighten bash cost", battle_card_cost(0), 1);
    check("enlighten bludgeon cost", battle_card_cost(1), 1);
    check("enlighten turn flag", g_battle.cost_one_turn[bash], 1);
    check("enlighten bludgeon flag", g_battle.cost_one_turn[bludgeon], 1);
    enemy_move_set(0, PREP1, 1);
    battle_end_turn();
    check("enlighten expires", g_battle.cost_one_turn[bash], 0);

    /* Enlightenment+: the cost also stays at 1 for the rest of combat. */
    fresh(200);
    hand("Bash", 0);
    hand_add("Enlightenment");
    g_battle.hand[1] = CARD_ENTRY(battle_find_card("Enlightenment"), 1);
    battle_play(1, 0);
    check("enlighten+ turn flag", g_battle.cost_one_turn[bash], 1);
    check("enlighten+ combat flag", g_battle.cost_one_combat[bash], 1);
    enemy_move_set(0, PREP1, 1);
    battle_end_turn();
    check("enlighten+ turn expires", g_battle.cost_one_turn[bash], 0);
    check("enlighten+ combat stays", g_battle.cost_one_combat[bash], 1);
    /* cards_gen.h is a static table per translation unit; ask battle.c for the
     * cost through the hand slot instead of passing cardtest's own pointer. */
    hand("Bash", 0);
    check("enlighten+ cost stays", battle_card_cost(0), 1);
}

static void test_batch16(void) {
    int bash = battle_find_card("Bash");

    /* A Purity that is the only card in hand has nothing to choose from and
     * must not open an empty picker. */
    fresh(200);
    hand("Purity", 0);
    battle_play(0, 0);
    check("purity empty screen", g_run.screen, SCREEN_FIGHT);
    check("purity empty exhaust", g_battle.exhaust_count, 1);
    fresh(200);
    hand("Forethought", 0);
    battle_play(0, 0);
    check("forethought empty screen", g_run.screen, SCREEN_FIGHT);
    check("forethought empty hand", g_battle.hand_count, 0);

    /* Purity: exhaust up to 3 cards from hand, then the card itself. */
    fresh(200);
    hand("Purity", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    hand_add("Bash");
    battle_play(0, 0);
    check("purity picker", battle_pick_mode(), PICK_EXHAUST_MANY);
    check("purity pick count", battle_pick_count(), 3);
    check("purity max", battle_pick_extra(), 3);
    check("purity self exhaust", g_battle.exhaust_count, 1);
    battle_pick_toggle(0);
    battle_pick_toggle(1);
    check("purity marked", battle_pick_marked_count(), 2);
    battle_pick_confirm_multi();
    check("purity leaves one", g_battle.hand_count, 1);
    check("purity exhaust total", g_battle.exhaust_count, 3);

    /* Purity+: the cap rises to 5. */
    fresh(200);
    hand("Purity", 1);
    for (int i = 0; i < 6; i++) hand_add("Strike_R");
    battle_play(0, 0);
    check("purity+ pick count", battle_pick_count(), 6);
    check("purity+ max", battle_pick_extra(), 5);
    for (int i = 0; i < 6; i++) battle_pick_toggle(i);
    check("purity+ capped", battle_pick_marked_count(), 5);
    battle_pick_confirm_multi();
    check("purity+ hand left", g_battle.hand_count, 1);
    check("purity+ exhaust total", g_battle.exhaust_count, 6);

    /* Cancelling Purity skips the exhaust, but Purity is already spent. */
    fresh(200);
    hand("Purity", 0);
    hand_add("Strike_R");
    hand_add("Defend_R");
    battle_play(0, 0);
    battle_pick_cancel();
    check("purity cancel hand", g_battle.hand_count, 2);
    check("purity cancel exhaust", g_battle.exhaust_count, 1);

    /* Forethought: one card goes to the bottom of the draw pile and costs 0
     * until it is played. */
    fresh(200);
    hand("Forethought", 0);
    hand_add("Bash");
    battle_play(0, 0);
    check("forethought picker", battle_pick_mode(), PICK_BOTTOM_HAND);
    check("forethought pick count", battle_pick_count(), 1);
    battle_pick_choose(0);
    check("forethought hand", g_battle.hand_count, 0);
    check("forethought draw", g_battle.draw_count, 1);
    check("forethought bottom", CARD_INDEX(g_battle.draw[0]), bash);
    check("forethought zero once", g_battle.zero_once[bash], 1);
    g_battle.hand[0] = g_battle.draw[--g_battle.draw_count];
    g_battle.hand_count = 1;
    check("forethought cost", battle_card_cost(0), 0);
    battle_play(0, 0);
    check("forethought cleared", g_battle.zero_once[bash], 0);
    check("forethought played", 200 - g_battle.enemies[0].hp, 8);

    /* Double Tap: the next Attack is played twice, then the power is spent. */
    fresh(200);
    hand("Double Tap", 0);
    hand_add("Strike_R");
    battle_play(0, 0);
    check("double tap power", battle_power(POW_DOUBLE_TAP), 1);
    battle_play(0, 0);
    check("double tap strike", 200 - g_battle.enemies[0].hp, 12);
    check("double tap spent", battle_power(POW_DOUBLE_TAP), 0);
    hand("Strike_R", 0);
    battle_play(0, 0);
    check("double tap next only", 200 - g_battle.enemies[0].hp, 18);

    /* Double Tap+ holds two charges; the third Attack is normal again. */
    fresh(200);
    g_battle.energy = 10;
    hand("Double Tap", 1);
    hand_add("Strike_R");
    hand_add("Strike_R");
    hand_add("Strike_R");
    battle_play(0, 0);
    check("double tap+ charges", battle_power(POW_DOUBLE_TAP), 2);
    battle_play(0, 0);
    battle_play(0, 0);
    battle_play(0, 0);
    check("double tap+ third normal", 200 - g_battle.enemies[0].hp, 30);

    /* An Attack with a debuff applies that debuff on both plays. */
    fresh(200);
    hand("Double Tap", 0);
    hand_add("Bash");
    battle_play(0, 0);
    battle_play(0, 0);
    /* The first Bash applies Vulnerable before the replayed second Bash, so
     * the second hit is 8 * 1.5 = 12 and the total is 20. */
    check("double tap bash dmg", 200 - g_battle.enemies[0].hp, 20);
    check("double tap bash vuln", g_battle.enemies[0].vulnerable, 4);
}

/* ---- picker preview ---------------------------------------------------- */

/* The card-choice screen only appears mid-combat when one of seven cards is
 * played, so the card-test flavour grows a small demo built from the same
 * g_battle state the real screen reads.  A cycles the layouts, B returns. */
void card_test_demo_pick(int which) {
    static const char *const HAND_CARDS[] = {
        "Strike_R", "Defend_R", "Bash", "Warcry", "Discovery", "Pommel Strike",
    };
    static const char *const EXHAUST_CARDS[] = {
        "Strike_R", "Defend_R", "Bash", "Anger", "Cleave", "Iron Wave",
        "Twin Strike", "Pommel Strike", "Thunderclap", "Body Slam", "Clash", "Flex",
    };

    memset(&g_battle, 0, sizeof(g_battle));
    memset(&g_run, 0, sizeof(g_run));
    g_battle.hp = 80;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;

    if (which < 0 || which > 4) which = 0;
    g_battle.pick_sel = 0;
    if (which == 3 || which == 4) {
        static const char *const SKILL_DRAW[] = {
            "Defend_R", "Shrug It Off", "True Grit", "Warcry", "Armaments",
            "Battle Trance", "Entrench", "Ghostly Armor", "Second Wind",
            "Burning Pact", "Defend_R", "Defend_R",
        };
        static const char *const ATTACK_DRAW[] = {
            "Strike_R", "Bash", "Cleave", "Iron Wave", "Twin Strike",
            "Pommel Strike", "Thunderclap", "Body Slam", "Clash", "Anger",
            "Strike_R", "Strike_R",
        };
        const char *const *cards = which == 3 ? SKILL_DRAW : ATTACK_DRAW;
        g_battle.pick_mode = which == 3 ? PICK_DRAW_SKILL : PICK_DRAW_ATTACK;
        for (int i = 0; i < 12; i++) {
            g_battle.draw[i] = CARD_ENTRY(battle_find_card(cards[i]), 0);
            g_battle.pick_source[i] = i;
        }
        g_battle.draw_count = 12;
        g_battle.pick_count = 12;
        g_run.screen = SCREEN_PICK;
        return;
    }
    if (which == 0) {
        g_battle.pick_mode = PICK_UPGRADE_HAND;
        for (int i = 0; i < 6; i++) {
            g_battle.hand[i] = CARD_ENTRY(battle_find_card(HAND_CARDS[i]), i == 2);
            g_battle.pick_source[i] = i;
        }
        g_battle.hand_count = 6;
        g_battle.pick_count = 6;
    } else if (which == 1) {
        g_battle.pick_mode = PICK_DISCOVERY;
        g_battle.hand[0] = CARD_ENTRY(battle_find_card("Strike_R"), 0);
        g_battle.hand_count = 1;
        g_battle.pick_source[0] = battle_find_card("Bash");
        g_battle.pick_source[1] = battle_find_card("Warcry");
        g_battle.pick_source[2] = battle_find_card("Uppercut");
        g_battle.pick_count = 3;
    } else {
        g_battle.pick_mode = PICK_EXHUME;
        for (int i = 0; i < 12; i++) {
            g_battle.exhaust[i] = CARD_ENTRY(battle_find_card(EXHAUST_CARDS[i]), 0);
            g_battle.pick_source[i] = i;
        }
        g_battle.exhaust_count = 12;
        g_battle.pick_count = 12;
    }
    g_run.screen = SCREEN_PICK;
}

void card_test_demo_ui(int which) {
    static const char *const DECK[] = {
        "Body Slam", "Strike_R", "Defend_R", "Bash", "True Grit",
        "Pommel Strike", "Shrug It Off", "Iron Wave", "Cleave", "Anger",
    };
    memset(&g_battle, 0, sizeof(g_battle));
    memset(&g_run, 0, sizeof(g_run));
    g_battle.hp = 55;
    g_battle.max_hp = 80;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.phase = PHASE_PLAYER;
    g_battle.pending_split = -1;
    g_run.act = 1;
    g_run.node = 4;
    g_map.gold = 99;
    g_battle.rng = 0x12345678u;
    map_generate();

    if (which == 0) {
        /* Campfire upgrade: Body Slam is first so the top screen shows the
         * 1 -> 0 cost change without any navigation.  True Grit+ is already
         * upgraded, which draws the grey "已升级" strip. */
        g_run.deck_count = 0;
        for (int i = 0; i < 10; i++) {
            int idx = battle_find_card(DECK[i]);
            g_run.deck[g_run.deck_count++] = CARD_ENTRY(idx, i == 4);
        }
        g_run.screen = SCREEN_UPGRADE;
    } else if (which == 1) {
        /* Reward: gold, a potion and three cards, with the skip button. */
        g_run.gold_gain = 27;
        g_run.potion_gain = 0;
        g_run.potions[0] = 0;
        g_run.potion_count = 1;
        g_run.reward[0] = battle_find_card("Bash");
        g_run.reward[1] = battle_find_card("Pommel Strike");
        g_run.reward[2] = battle_find_card("Shrug It Off");
        g_run.reward_count = 3;
        g_run.screen = SCREEN_REWARD;
    } else if (which == 2) {
        g_run.deck_count = 0;
        for (int i = 0; i < 10; i++) {
            int idx = battle_find_card(DECK[i]);
            g_run.deck[g_run.deck_count++] = CARD_ENTRY(idx, 0);
        }
        g_run.screen = SCREEN_CAMPFIRE;
    } else if (which == 4) {
        /* Searing Blow +1: the upgrade screen must still offer another level
         * (16 -> 21) even though the card is already upgraded. */
        g_run.deck_count = 0;
        int sear = battle_find_card("Searing Blow");
        g_run.deck[g_run.deck_count++] = CARD_ENTRY_LEVEL(sear, 1);
        for (int i = 0; i < 9; i++) {
            int idx = battle_find_card(DECK[i]);
            g_run.deck[g_run.deck_count++] = CARD_ENTRY(idx, 0);
        }
        g_run.screen = SCREEN_UPGRADE;
    } else if (which == 5) {
        /* Five upgraded cards whose wording changes on upgrade; the focus
         * starts on Ghostly+ and RIGHT walks through the rest. */
        battle_test_spawn_act1(2);
        g_battle.hand_count = 0;
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(battle_find_card("Ghostly"), 1);
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(battle_find_card("Forethought"), 1);
        g_battle.hand[g_battle.hand_count++] =
            CARD_ENTRY_LEVEL(battle_find_card("Searing Blow"), 2);
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(battle_find_card("Discovery"), 1);
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(battle_find_card("Burn"), 1);
        g_battle.energy = 3;
        g_battle.phase = PHASE_PLAYER;
        g_battle.turn = 1;
        g_run.act = 1;
        g_run.screen = SCREEN_FIGHT;
    } else {
        /* True Grit+: play it and leave the real PICK_EXHAUST_HAND picker up. */
        battle_test_spawn_act1(2);
        g_battle.hand_count = 0;
        int grit = battle_find_card("True Grit");
        g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(grit, 1);
        for (int i = 1; i < 5; i++) {
            int idx = battle_find_card(DECK[i]);
            g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(idx, 0);
        }
        g_battle.energy = 3;
        g_battle.phase = PHASE_PLAYER;
        g_run.screen = SCREEN_FIGHT;
        battle_play(0, -1);
    }
}

void card_test_demo_exit(void) {
    g_run.screen = SCREEN_MAP;
}

void card_test_demo_fight(int which) {
    static const char *const HAND_CARDS[] = {
        "Strike_R", "Defend_R", "Bash", "Strike_R", "Defend_R",
    };
    if (which == 16 || which == 17) {
        card_test_demo_pick(which == 16 ? 3 : 4);
        return;
    }
    if (which == 19 || which == 20) {
        /* The two new pickers.  19 is Purity (multi-select, Y confirms),
         * 20 is Forethought (a single card to the bottom of the draw pile). */
        memset(&g_battle, 0, sizeof(g_battle));
        memset(&g_run, 0, sizeof(g_run));
        g_battle.hp = 80;
        g_battle.max_hp = 80;
        g_battle.energy = 3;
        g_battle.max_energy = 3;
        g_battle.phase = PHASE_PLAYER;
        g_battle.pending_split = -1;
        g_run.act = 1;
        g_map.gold = 99;
        static const char *const PICK_CARDS[] = {
            "Strike_R", "Defend_R", "Bash", "Pommel Strike", "Cleave",
        };
        for (int i = 0; i < 5; i++) {
            int idx = battle_find_card(PICK_CARDS[i]);
            g_battle.hand[i] = CARD_ENTRY(idx, 0);
            g_battle.pick_source[i] = i;
        }
        g_battle.hand_count = 5;
        g_battle.pick_count = 5;
        g_battle.pick_sel = 0;
        g_battle.pick_mode = which == 19 ? PICK_EXHAUST_MANY : PICK_BOTTOM_HAND;
        g_battle.pick_extra = which == 19 ? 3 : 0;
        if (which == 19) g_battle.pick_marked[0] = 1;
        g_run.screen = SCREEN_PICK;
        return;
    }
    /* 0 Champ, 1 Book of Stabbing, 2 Byrds, 3 Chosen, 4 Parasite,
     * 5 Spheric Guardian, 6 the three Slavers, 7 BronzeAutomaton with its
     * two summoned orbs, 8 the Collector with its Torch Heads, 9 the Gremlin
     * Leader with three different minions, 10 the same Champ fight with the
     * act 3 background (a preview of beyondScene's floor stack), 11 Slime
     * Boss, 12 Gremlin Nob, 13 two Acid Slimes (M), 14 Jaw Worm, 15 the
     * Slime Boss' two split slimes, 16/17 the two draw-pile pickers, 18 the
     * last four powers (Panacea/Ghostly/Dark Shackles/Enlightenment) on a pair
     * of Acid Slimes, 19 Purity's multi-select, 20 Forethought's bottom-pick.
     * Same spawn path the real encounter uses, then a hand so the bottom
     * screen has cards to draw. */
    if (which < 0 || which > 20) which = 0;
    if (which == 10) battle_test_spawn_act2(1);
    else if (which == 11) battle_test_spawn_act1(0);
    else if (which == 12) battle_test_spawn_act1(1);
    else if (which == 13) battle_test_spawn_act1(2);
    else if (which == 14) battle_test_spawn_act1(3);
    else if (which == 15) {
        battle_test_spawn_act1(0);
        battle_test_split_boss();
    }
    else if (which == 18) battle_test_spawn_act1(2);
    else if (which == 6) battle_test_spawn_slavers();
    else if (which == 7) {
        battle_test_spawn_automaton();
        battle_test_enemy_turn(0);   /* Spawn Orbs, then Flail */
    } else if (which == 8) {
        battle_test_spawn_collector();
        battle_test_enemy_turn(0);   /* Spawn the two Torch Heads */
    } else if (which == 9) {
        battle_test_spawn_gremlin_leader();
        /* Force one of each shape: Warrior, Thief, Fat. */
        battle_test_spawn_gremlin_minion(1, 0);
        battle_test_spawn_gremlin_minion(2, 1);
        battle_test_spawn_gremlin_minion(3, 2);
    } else if (which <= 1) battle_test_spawn_act2(which == 0 ? 1 : 0);
    else battle_test_spawn_act2_monster(which - 2);
    g_run.act = (which == 10) ? 3 : (which >= 11 ? 1 : 2); /* display is 1-based */
    g_map.gold = 99;
    g_run.screen = SCREEN_FIGHT;
    g_battle.phase = PHASE_PLAYER;
    g_battle.turn = 1;
    g_battle.energy = 3;
    g_battle.max_energy = 3;
    g_battle.hand_count = 0;
    for (int i = 0; i < 5; i++) {
        int idx = battle_find_card(HAND_CARDS[i]);
        if (idx >= 0) g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(idx, 0);
    }
    if (which == 18) {
        static const char *const POWER_CARDS[] = {
            "Panacea", "Ghostly", "Dark Shackles", "Enlightenment", "Strike_R",
        };
        g_battle.hand_count = 0;
        for (int i = 0; i < 5; i++) {
            int idx = battle_find_card(POWER_CARDS[i]);
            if (idx >= 0) g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(idx, 0);
        }
        /* Two Artifact stacks and one Intangible for the icon rows; the
         * status panel lists both by name. */
        g_battle.artifact = 2;
        g_battle.intangible = 1;
        g_battle.enemies[0].move_index = 0;
        g_battle.enemies[1].move_index = 0;
    }
    Enemy *e = &g_battle.enemies[0];
    if (which == 0) {
        /* Defensive Stance already forged once: intent + both icon rows. */
        e->move_index = 1;
        e->metallicize = 5;
        g_battle.frail = 2;
        g_battle.frail_just = 1;
        g_battle.weak = 1;
    } else if (which == 1) {
        /* A four-hit Multi-Stab, the grown stab_count's intent. */
        e->move_index = 0;
        e->stab_count = 4;
    } else if (which == 2) {
        /* Both birds airborne on a Peck, Flight 3 on each. */
        for (int i = 0; i < g_battle.enemy_count; i++) {
            g_battle.enemies[i].move_index = 0;
            g_battle.enemies[i].flight = 3;
            g_battle.enemies[i].grounded = 0;
        }
    } else if (which == 3) {
        /* Poke (two hits of 5); the player has Hex 1 for the icon row. */
        e->move_index = 4;
        g_battle.hex = 1;
    } else if (which == 4) {
        /* Double Strike with Plated Armor 14 and its 14 block. */
        e->move_index = 1;
        e->plated = 14;
        e->block = 14;
    } else if (which == 5) {
        /* Block Attack (15 block then 10) with Barricade 40 and Artifact 3. */
        e->move_index = 2;
        e->block = 40;
    } else if (which == 6) {
        /* All three Slavers, with one of each shape of intent: Rake, Scouring
         * Whip and Entangle.  The player is already entangled so the status
         * list and the rejected-Attack toast can both be seen. */
        g_battle.enemies[0].move_index = 1;
        g_battle.enemies[1].move_index = 0;
        g_battle.enemies[2].move_index = 1;
        g_battle.enemies[2].used_entangle = 0;
        g_battle.entangled = 1;
    } else if (which == 9) {
        /* Leader Encourage (own +3 Strength, each Gremlin +3 and +6 block)
         * with a Warrior, Thief and Fat Gremlin in the three minion slots. */
        e->move_index = 1;
        for (int i = 1; i < 4; i++) g_battle.enemies[i].move_index = 0;
    } else if (which == 8) {
        /* Collector Fireball with one Torch Head Tackling on each side. */
        e->move_index = 1;
        e->ai_turn = 1;
        g_battle.enemies[1].move_index = 0;
        g_battle.enemies[2].move_index = 0;
    } else if (which == 7) {
        /* Automaton Flail + left orb Support + right orb Stasis.  Stock the
         * draw pile so the Stasis turn actually steals a card and the status
         * panel grows its 静滞 row. */
        e->move_index = 0;
        e->ai_turn = 0;
        g_battle.enemies[1].move_index = 1;
        g_battle.enemies[2].move_index = 2;
        g_battle.enemies[2].used_stasis = 0;
        g_battle.draw_count = 0;
        const char *draw_ids[] = {"Bash", "Strike_R", "Defend_R"};
        for (int i = 0; i < 3; i++) {
            int idx = battle_find_card(draw_ids[i]);
            if (idx >= 0) g_battle.draw[g_battle.draw_count++] = CARD_ENTRY(idx, 0);
        }
    } else if (which == 13) {
        /* Acid Slimes (M): left Wound Tackle (7 + 黏液), right Lick (1 Weak). */
        g_battle.enemies[0].move_index = 1;
        g_battle.enemies[1].move_index = 2;
    } else if (which == 14) {
        /* Jaw Worm Thrash: 7 damage + 5 block. */
        e->move_index = 1;
    } else if (which == 15) {
        /* Spike Slime (L) Flame Tackle and Acid Slime (L) Normal Tackle. */
        g_battle.enemies[0].move_index = 0;
        g_battle.enemies[1].move_index = 1;
    }
}

void card_test_demo_event(int which) {
    if (which < 0 || which >= STS_EVENT_COUNT) which = 0;
    g_run.act = 2;
    g_map.gold = 99;
    g_map.event_id = which;
    g_map.event_done = 1;
    g_map.purge_from_event = 0;
    snprintf(g_map.event_result, sizeof(g_map.event_result), "%s",
             sts_events[which].result[0]);
    snprintf(g_map.message, sizeof(g_map.message), "%s",
             which == 0 ? "香蕉：回复 26 生命"
                        : (which == 1 ? "牧师：回复 20 生命" : "捞到 75 金币，失去 11 生命"));
    g_run.screen = SCREEN_EVENT;
}

static void test_batch17(void) {
    /* The save validator itself: a valid blob passes, a flipped checksum,
     * a newer version and hp > max_hp all fail. */
    check("save blob valid", save_self_test(), 1);

    /* Loading rebuilds the map from map_seed.  Generate twice from the same
     * seed and the room types and the chosen node must match. */
    g_run.act = 1;
    g_battle.rng = 0x12345678u;
    map_generate();
    unsigned int seed = g_map.map_seed;
    unsigned char types[MAP_ROWS][MAP_COLS];
    memcpy(types, g_map.type, sizeof(types));
    int node = map_node(0, 0);
    g_battle.rng = seed;
    map_generate();
    check("save map seed", (int)g_map.map_seed, (int)seed);
    check("save map types", memcmp(types, g_map.type, sizeof(types)) == 0, 1);
    check("save map node", map_node(0, 0), node);

    /* m13737: upgradeBaseCost is an absolute cost, so the preview and the
     * battle cost both have to see Body Slam's 1 -> 0. */
    int body_i = battle_find_card("Body Slam");
    int barricade_i = battle_find_card("Barricade");
    int bfb_i = battle_find_card("Blood for Blood");
    int strike_i = battle_find_card("Strike_R");
    int defend_i = battle_find_card("Defend_R");
    int bash_i = battle_find_card("Bash");
    int grit_i = battle_find_card("True Grit");
    check("cost body slam", battle_cost_of(&sts_cards[body_i], 0), 1);
    check("cost body slam+", battle_cost_of(&sts_cards[body_i], 1), 0);
    check("cost barricade+", battle_cost_of(&sts_cards[barricade_i], 1), 2);
    check("cost blood+", battle_cost_of(&sts_cards[bfb_i], 1), 3);
    check("cost strike+", battle_cost_of(&sts_cards[strike_i], 1), 1);
    check("true grit+ text", strstr(sts_cards[grit_i].text_up, "选择") != NULL, 1);

    /* A real won fight: two Acid Slimes at 1 HP, Cleave kills both, and the
     * loot screen must carry gold, three cards and a potion result. */
    battle_test_spawn_act1(2);
    g_battle.enemies[0].hp = 1;
    g_battle.enemies[1].hp = 1;
    g_battle.hand_count = 0;
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(battle_find_card("Cleave"), 0);
    g_battle.energy = 3;
    g_run.potion_count = 0;
    g_run.screen = SCREEN_FIGHT;
    int gold0 = g_map.gold;
    battle_play(0, -1);
    check("win screen", g_run.screen, SCREEN_REWARD);
    check("win three cards", g_run.reward_count, 3);
    check("win potion roll", g_run.potion_gain >= -2, 1);
    check("win gold", g_map.gold, gold0 + g_run.gold_gain);
    battle_skip_reward();
    check("skip leaves loot", g_run.screen == SCREEN_REWARD, 0);

    /* Potions: a full belt rejects the drop, an empty one takes it. */
    g_run.potion_count = 3;
    check("potion full rejected", battle_add_potion(POTION_HEAL), 0);
    check("potion full stays", g_run.potion_count, 3);
    g_run.potion_count = 0;
    check("potion added", battle_add_potion(POTION_HEAL), 1);
    check("potion added count", g_run.potion_count, 1);
    check("potion id kept", (int)g_run.potions[0], POTION_HEAL);

    /* Unupgraded True Grit exhausts a random other card, never itself. */
    battle_test_spawn_act1(2);
    g_battle.hand_count = 0;
    g_battle.energy = 3;
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(grit_i, 0);
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(strike_i, 0);
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(defend_i, 0);
    g_run.screen = SCREEN_FIGHT;
    battle_play(0, -1);
    check("true grit consumes one", g_battle.exhaust_count, 1);
    check("true grit hand left", g_battle.hand_count, 1);
    check("true grit not itself", CARD_INDEX(g_battle.exhaust[0]) == grit_i, 0);
    check("true grit keeps other",
          g_battle.hand[0] == CARD_ENTRY(strike_i, 0) ||
          g_battle.hand[0] == CARD_ENTRY(defend_i, 0), 1);

    /* The same card from slot 2 with one card to its right.  The random
     * victim can only be slot 0 or 1, so it always sits to the left: the
     * played slot has to move down with the hand or the card after True
     * Grit is discarded and True Grit itself stays in hand. */
    battle_test_spawn_act1(2);
    g_battle.hand_count = 0;
    g_battle.energy = 3;
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(strike_i, 0);
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(defend_i, 0);
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(grit_i, 0);
    g_battle.hand[g_battle.hand_count++] = CARD_ENTRY(bash_i, 0);
    g_run.screen = SCREEN_FIGHT;
    battle_play(2, -1);
    int grit_left = 0;
    for (int i = 0; i < g_battle.hand_count; i++) {
        if (CARD_INDEX(g_battle.hand[i]) == grit_i) grit_left = 1;
    }
    check("true grit shifted slot exhausts one", g_battle.exhaust_count, 1);
    check("true grit shifted slot hand", g_battle.hand_count, 2);
    check("true grit shifted slot gone", grit_left, 0);
    check("true grit shifted slot keeps right",
          g_battle.hand[1] == CARD_ENTRY(bash_i, 0), 1);
    check("true grit shifted slot discard", g_battle.discard_count, 1);
    check("true grit shifted slot discarded itself",
          g_battle.discard_count > 0 && CARD_INDEX(g_battle.discard[0]) == grit_i, 1);
}

static void test_batch18(void) {
    int sear = battle_find_card("Searing Blow");
    int perf = battle_find_card("Perfected Strike");
    int strike = battle_find_card("Strike_R");
    int defend = battle_find_card("Defend_R");
    int bash = battle_find_card("Bash");
    int ghostly = battle_find_card("Ghostly");
    int ghostly_armor = battle_find_card("Ghostly Armor");
    int brut = battle_find_card("Brutality");
    int burn = battle_find_card("Burn");
    int fore = battle_find_card("Forethought");
    int body = battle_find_card("Body Slam");
    int apoth = battle_find_card("Apotheosis");
    int wound = battle_find_card("Wound");

    /* Searing Blow: each upgrade adds 4+timesUpgraded, so the curve is
     * 12, 16, 21, 27... and canUpgrade stays true. */
    check("searing curve 0", battle_searing_damage(0), 12);
    check("searing curve 1", battle_searing_damage(1), 16);
    check("searing curve 2", battle_searing_damage(2), 21);
    check("searing curve 3", battle_searing_damage(3), 27);
    check("searing can upgrade", battle_test_card_can_upgrade(sear), 1);
    int se = CARD_ENTRY(sear, 0);
    check("searing entry can", battle_test_can_upgrade_entry(se), 1);
    se = battle_test_upgrade_entry(se);
    check("searing level 1", CARD_LEVEL(se), 1);
    check("searing level 1 can", battle_test_can_upgrade_entry(se), 1);
    se = battle_test_upgrade_entry(se);
    check("searing level 2", CARD_LEVEL(se), 2);
    check("searing level 2 can", battle_test_can_upgrade_entry(se), 1);
    g_run.deck_count = 1;
    g_run.deck[0] = CARD_ENTRY(sear, 0);
    g_run.screen = SCREEN_UPGRADE;
    check("searing confirm 1", battle_confirm_upgrade(0), 1);
    check("searing deck level 1", battle_deck_level(0), 1);
    g_run.screen = SCREEN_UPGRADE;
    check("searing confirm 2", battle_confirm_upgrade(0), 1);
    check("searing deck level 2", battle_deck_level(0), 2);
    fresh(200);
    g_battle.hand[0] = CARD_ENTRY_LEVEL(sear, 2);
    g_battle.hand_count = 1;
    battle_play(0, 0);
    check("searing +2 damage", 200 - g_battle.enemies[0].hp, 21);

    /* Perfected Strike counts hand + draw + discard, and the card itself is
     * still in hand when the damage is calculated.  Hand Perfected + draw
     * Strike + discard Strike = three names containing 打击. */
    fresh(200);
    hand("Perfected Strike", 0);
    g_battle.draw[0] = CARD_ENTRY(strike, 0);
    g_battle.draw_count = 1;
    g_battle.discard[0] = CARD_ENTRY(strike, 0);
    g_battle.discard_count = 1;
    battle_play(0, 0);
    check("perfected counts discard", 200 - g_battle.enemies[0].hp, 12);
    fresh(200);
    hand("Perfected Strike", 1);
    g_battle.draw[0] = CARD_ENTRY(strike, 0);
    g_battle.draw_count = 1;
    g_battle.discard[0] = CARD_ENTRY(strike, 0);
    g_battle.discard_count = 1;
    battle_play(0, 0);
    check("perfected + magic", 200 - g_battle.enemies[0].hp, 15);

    /* Burn+: the upgraded copy deals 4, not the base 2. */
    fresh(200);
    enemy_move_set(0, PREP1, 1);
    hand("Burn", 1);
    battle_end_turn();
    check("burn + damage", g_battle.hp, 76);

    /* Apparition: base is Ethereal, upgraded is not.  The base copy is
     * exhausted, the upgraded copy is discarded with the rest of the hand. */
    fresh(200);
    enemy_move_set(0, PREP1, 1);
    g_battle.hand[0] = CARD_ENTRY(ghostly, 0);
    g_battle.hand[1] = CARD_ENTRY(ghostly, 1);
    g_battle.hand_count = 2;
    battle_end_turn_powers_for_test();
    check("apparition base exhaust", g_battle.exhaust_count, 1);
    int upgraded_in_discard = 0;
    for (int i = 0; i < g_battle.discard_count; i++) {
        if (CARD_INDEX(g_battle.discard[i]) == ghostly &&
            CARD_LEVEL(g_battle.discard[i]) == 1) upgraded_in_discard = 1;
    }
    check("apparition + discarded", upgraded_in_discard, 1);

    /* Innate flags: Brutality only after upgrade, Apparition only before it. */
    check("brutality base not innate", battle_test_entry_innate(CARD_ENTRY(brut, 0)), 0);
    check("brutality + innate", battle_test_entry_innate(CARD_ENTRY(brut, 1)), 1);
    check("apparition base ethereal", battle_test_entry_ethereal(CARD_ENTRY(ghostly, 0)), 1);
    check("apparition + not ethereal", battle_test_entry_ethereal(CARD_ENTRY(ghostly, 1)), 0);
    check("ghostly armor ethereal", battle_test_entry_ethereal(CARD_ENTRY(ghostly_armor, 0)), 1);

    /* initializeDeck moves innate cards to the top of the draw pile (the end
     * of the array) while preserving the order of the other cards. */
    fresh(200);
    g_battle.draw[0] = CARD_ENTRY(strike, 0);
    g_battle.draw[1] = CARD_ENTRY(brut, 1);
    g_battle.draw[2] = CARD_ENTRY(defend, 0);
    g_battle.draw_count = 3;
    battle_test_apply_innate();
    check("innate kept order 0", CARD_INDEX(g_battle.draw[0]), strike);
    check("innate kept order 1", CARD_INDEX(g_battle.draw[1]), defend);
    check("innate moved to top", CARD_INDEX(g_battle.draw[2]), brut);
    check("innate level kept", CARD_LEVEL(g_battle.draw[2]), 1);

    /* A real battle_start puts the upgraded Brutality in the opening hand. */
    fresh(200);
    g_run.deck_count = 0;
    g_run.deck[g_run.deck_count++] = CARD_ENTRY(brut, 1);
    for (int i = 0; i < 9; i++) {
        g_run.deck[g_run.deck_count++] = CARD_ENTRY((i & 1) ? defend : strike, 0);
    }
    g_run.act = 1;
    g_battle.rng = 0x1234567u;
    battle_start(0);
    int brutal_in_hand = 0;
    for (int i = 0; i < g_battle.hand_count; i++) {
        if (CARD_INDEX(g_battle.hand[i]) == brut && CARD_LEVEL(g_battle.hand[i]) == 1)
            brutal_in_hand = 1;
    }
    check("brutality + opens in hand", brutal_in_hand, 1);

    /* Upgrade eligibility: pure cost upgrades and special upgrades count. */
    check("body slam can upgrade", battle_test_card_can_upgrade(body), 1);
    check("apotheosis can upgrade", battle_test_card_can_upgrade(apoth), 1);
    check("strike can upgrade", battle_test_card_can_upgrade(strike), 1);
    check("wound cannot upgrade", battle_test_card_can_upgrade(wound), 0);
    check("strike + cannot upgrade", battle_test_can_upgrade_entry(CARD_ENTRY(strike, 1)), 0);

    /* Text overrides: the upgraded card face no longer lies. */
    check("ghostly + no ethereal text", strstr(sts_cards[ghostly].text_up, "虚无") == NULL, 1);
    check("discovery + no exhaust text", strstr(sts_cards[battle_find_card("Discovery")].text_up, "消耗") == NULL, 1);
    check("thinking + no exhaust text", strstr(sts_cards[battle_find_card("Thinking Ahead")].text_up, "消耗") == NULL, 1);
    check("limit + no exhaust text", strstr(sts_cards[battle_find_card("Limit Break")].text_up, "消耗") == NULL, 1);
    check("brutality + says innate", strstr(sts_cards[brut].text_up, "固有") != NULL, 1);
    check("burn + says 4", strstr(sts_cards[burn].text_up, "4") != NULL, 1);
    check("armaments + all cards", strstr(sts_cards[battle_find_card("Armaments")].text_up, "所有") != NULL, 1);
    check("forethought + any cards", strstr(sts_cards[fore].text_up, "任意") != NULL, 1);
    check("perfected + magic 3", strstr(sts_cards[perf].text_up, "+3") != NULL, 1);
    check("searing + says 16", strstr(sts_cards[sear].text_up, "16") != NULL, 1);

    /* Forethought+: choose any number of cards, put them on the bottom and
     * make each free until played. */
    fresh(200);
    g_battle.hand[0] = CARD_ENTRY(fore, 1);
    g_battle.hand_count = 1;
    hand_add("Strike_R");
    hand_add("Defend_R");
    hand_add("Bash");
    battle_play(0, 0);
    check("forethought + picker", g_run.screen, SCREEN_PICK);
    check("forethought + extra", battle_pick_extra(), -1);
    check("forethought + multi", battle_pick_multi(), 1);
    check("forethought + count", battle_pick_count(), 3);
    battle_pick_toggle(0);
    battle_pick_toggle(1);
    check("forethought + marked", battle_pick_marked_count(), 2);
    battle_pick_confirm_multi();
    check("forethought + screen", g_run.screen, SCREEN_FIGHT);
    check("forethought + hand left", g_battle.hand_count, 1);
    check("forethought + keeps bash", CARD_INDEX(g_battle.hand[0]), bash);
    check("forethought + draw", g_battle.draw_count, 2);
    /* The first card the player ticked is drawn first after the old pile,
     * because the last ticked card is the one sitting deepest. */
    check("forethought + bottom defend", CARD_INDEX(g_battle.draw[0]), defend);
    check("forethought + bottom strike", CARD_INDEX(g_battle.draw[1]), strike);
    battle_draw_for_test(2);
    check("forethought + draws strike first", CARD_INDEX(g_battle.hand[1]), strike);
    check("forethought + draws defend second", CARD_INDEX(g_battle.hand[2]), defend);
    check("forethought + free strike", g_battle.zero_once[strike], 1);
    check("forethought + free defend", g_battle.zero_once[defend], 1);
}

void card_test_run(void) {
    s_cases = 0;
    s_fails = 0;
    memset(s_fail_text, 0, sizeof(s_fail_text));
    audio_sfx_mute(1);   /* the assertions play hundreds of cards */
    test_batch1();
    test_powers();
    test_batch3();
    test_batch4();
    test_batch5();
    test_batch6();
    test_batch7();
    test_batch8();
    test_batch9();
    test_batch10();
    test_batch11();
    test_batch12();
    test_batch13();
    test_batch14();
    test_batch15();
    test_batch16();
    test_batch17();
    test_batch18();
    g_run.screen = SCREEN_MAP;
    audio_sfx_mute(0);
}

void card_test_draw(C3D_RenderTarget *top) {
    C2D_TargetClear(top, RGB(10, 12, 14));
    C2D_SceneBegin(top);
    char buf[64];
    snprintf(buf, sizeof(buf), "CARD TEST %d cases %d failed", s_cases, s_fails);
    text_draw(FS_MID, buf, 8, 8, s_fails ? COL_HP : COL_GOLD);
    if (s_fails == 0) {
        text_draw(FS_MID, "ALL PASS", 8, 48, COL_GOLD);
    } else {
        for (int i = 0; i < s_fails && i < MAX_FAILS; i++) {
            text_draw(FS_SMALL, s_fail_text[i], 8.0f, 44.0f + (float)i * 14.0f, COL_INK);
        }
    }
    {   /* audio state: the streamer has no other visible surface yet */
        char abuf[96];
        char sbuf[128];
        audio_debug(abuf, sizeof(abuf));
        audio_sfx_debug(sbuf, sizeof(sbuf));
        text_draw(FS_SMALL, abuf, 8, 190, COL_DIM);
        text_draw(FS_SMALL, sbuf, 8, 204, COL_DIM);
    }
    text_draw(FS_SMALL, "START exit", 8, 220, COL_DIM);
}
