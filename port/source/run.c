#include <stdio.h>
#include <string.h>

#include "common.h"
#include "cards_gen.h"
#include "battle.h"
#include "run.h"
#include "l10n_gen.h"

MapState g_map;

/* The room graph, exactly as MapGenerator builds it: 15 rows x 7 columns and
 * six random walks, each edge stored on the room it leaves and each room
 * remembering its parents (the ancestor-gap rule needs them).  The port used to
 * keep only a type per cell and let any +-1 column step count as an edge, which
 * produced braids the game never makes.
 *
 * MapGenerator._createPaths (MapGenerator.java:129-207) is followed line for
 * line: a walk steps one row up and at most one column either way, avoids
 * re-joining a room it already shares an ancestor with, and never crosses the
 * edges its neighbours have already placed (:189-200).  Starting columns come
 * from randRange(0,rowSize), i.e. all seven. */
#define MAP_MAX_EDGES 3

static unsigned char s_edge[MAP_ROWS][MAP_COLS][MAP_MAX_EDGES];   /* dst columns */
static unsigned char s_edge_n[MAP_ROWS][MAP_COLS];
static unsigned char s_parent[MAP_ROWS][MAP_COLS][MAP_MAX_EDGES]; /* src columns */
static unsigned char s_parent_n[MAP_ROWS][MAP_COLS];

/* MapGenerator.randRange(rng, lo, hi) = rng.random(hi - lo) + lo, and the
 * game's own Random.random(int range) is nextInt(range + 1) -- both bounds are
 * inclusive (random/Random.java:49-52).  Getting this wrong funnels every walk
 * to the left edge of the map. */
static int map_rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(battle_rand() % (unsigned int)(hi - lo + 1));
}

/* currentNode.addEdge() + Collections.sort(edges, EdgeComparator): the list
 * stays sorted by dstX, which is what getMinEdge/getMaxEdge rely on. */
static void map_add_edge(int x, int y, int dst_x) {
    int n = s_edge_n[y][x];
    if (n >= MAP_MAX_EDGES) return;
    int i = n;
    while (i > 0 && s_edge[y][x][i - 1] > dst_x) {
        s_edge[y][x][i] = s_edge[y][x][i - 1];
        i--;
    }
    s_edge[y][x][i] = (unsigned char)dst_x;
    s_edge_n[y][x] = (unsigned char)(n + 1);
}

/* targetNodeCandidate.addParent(currentNode): insertion order, no sorting --
 * getNodeWithMaxX/getNodeWithMinX break ties by keeping the first, so the order
 * is part of the behaviour. */
static void map_add_parent(int x, int y, int src_x) {
    int n = s_parent_n[y][x];
    if (n >= MAP_MAX_EDGES) return;
    s_parent[y][x][n] = (unsigned char)src_x;
    s_parent_n[y][x] = (unsigned char)(n + 1);
}

static int map_parents_max_x(int y, int x) {
    int best = s_parent[y][x][0];
    for (int i = 1; i < s_parent_n[y][x]; i++)
        if (s_parent[y][x][i] > best) best = s_parent[y][x][i];
    return best;
}

static int map_parents_min_x(int y, int x) {
    int best = s_parent[y][x][0];
    for (int i = 1; i < s_parent_n[y][x]; i++)
        if (s_parent[y][x][i] < best) best = s_parent[y][x][i];
    return best;
}

/* MapGenerator.getCommonAncestor (:107-127): walk both rooms' parent chains up
 * to max_depth rows and report where they meet.  Kept bug-compatible with the
 * game, which compares node1.x against node2.y when picking the left room
 * (:112) -- on rows past column 6 that swaps the two sides.  Reproducing the
 * bug reproduces the game's maps; "fixing" it would not. */
static int map_common_ancestor(int ax, int ay, int bx, int by, int max_depth,
                               int *out_x, int *out_y) {
    (void)by;                                  /* ay == by, as asserted there */
    int lx, rx;
    if (ax < by) { lx = ax; rx = bx; } else { lx = bx; rx = ax; }
    for (int cy = ay; cy >= 0 && cy >= ay - max_depth; cy--) {
        if (s_parent_n[cy][lx] == 0 || s_parent_n[cy][rx] == 0) return 0;
        lx = map_parents_max_x(cy, lx);
        rx = map_parents_min_x(cy, rx);
        if (lx == rx) { *out_x = lx; *out_y = cy - 1; return 1; }
    }
    return 0;
}

/* One walk, from row 0 up to the boss edge.  This is the recursion in
 * _createPaths turned into a loop: every step creates exactly one edge. */
static void map_create_path(int x) {
    int y = 0;
    for (;;) {
        if (y + 1 >= MAP_ROWS) {
            /* The act's boss hangs off every row-14 room, dead centre (:136). */
            map_add_edge(x, y, MAP_COLS / 2);
            return;
        }
        int row_end = MAP_COLS - 1;
        int lo, hi;
        if (x == 0) { lo = 0; hi = 1; }
        else if (x == row_end) { lo = -1; hi = 0; }
        else { lo = -1; hi = 1; }
        int next_y = y + 1;
        int cand = x + map_rand_range(lo, hi);

        /* Ancestor gap: two rooms that already share an ancestor within three
         * rows must not meet again (:156-188). */
        unsigned char par[MAP_MAX_EDGES];
        int par_n = s_parent_n[next_y][cand];
        for (int i = 0; i < par_n; i++) par[i] = s_parent[next_y][cand][i];
        for (int i = 0; i < par_n; i++) {
            int anc_x, anc_y;
            if (par[i] == x) continue;
            if (!map_common_ancestor(par[i], y, x, y, 5, &anc_x, &anc_y)) continue;
            int gap = next_y - anc_y;
            if (gap < 3) {
                if (cand > x) {
                    cand = x + map_rand_range(-1, 0);
                    if (cand < 0) cand = x;
                } else if (cand == x) {
                    cand = x + map_rand_range(-1, 1);
                    if (cand > row_end) cand = x - 1;
                    else if (cand < 0) cand = x + 1;
                } else {
                    cand = x + map_rand_range(0, 1);
                    if (cand > row_end) cand = x;
                }
                continue;
            }
            if (gap < 5) continue;
        }

        /* Do not cross the neighbours' edges (:189-200). */
        if (x != 0 && s_edge_n[y][x - 1] > 0) {
            int right_of_left = s_edge[y][x - 1][s_edge_n[y][x - 1] - 1];
            if (right_of_left > cand) cand = right_of_left;
        }
        if (x < row_end && s_edge_n[y][x + 1] > 0) {
            int left_of_right = s_edge[y][x + 1][0];
            if (left_of_right < cand) cand = left_of_right;
        }
        if (cand < 0) cand = 0;
        if (cand > row_end) cand = row_end;

        map_add_edge(x, y, cand);
        map_add_parent(cand, next_y, x);
        y = next_y;
        x = cand;
    }
}

/* MapGenerator.filterRedundantEdgesFromRow (:26-44): among the row-0 rooms,
 * drop an edge when an earlier row-0 room already leads to the same place. */
static void map_filter_row0_edges(void) {
    unsigned char seen[MAP_COLS * MAP_MAX_EDGES];
    int seen_n = 0;
    for (int x = 0; x < MAP_COLS; x++) {
        int n = s_edge_n[0][x];
        if (n == 0) continue;
        int keep = 0;
        for (int i = 0; i < n; i++) {
            int dup = 0;
            for (int j = 0; j < seen_n; j++)
                if (seen[j] == s_edge[0][x][i]) { dup = 1; break; }
            if (dup) continue;
            seen[seen_n++] = s_edge[0][x][i];
            s_edge[0][x][keep++] = s_edge[0][x][i];
        }
        s_edge_n[0][x] = (unsigned char)keep;
    }
}

/* ---------------------------------------------------------------- rooms -- */

/* RoomTypeAssigner.getSiblings/ruleParentMatches/ruleSiblingMatches (:49-87)
 * and ruleAssignableToRow (:80-87) decide whether a room type may go here:
 *  - row <= 4: no rest, no elite; row >= 13: no rest (:80-87)
 *  - a room may not repeat its parent's type  (rest/treasure/shop/elite)
 *  - a room may not repeat a sibling's type   (rest/monster/event/elite/shop),
 *    where the siblings are every other room reached from any parent.
 * The game returns a type only when neither rule matches (:89-101). */
static int map_room_free_for(int y, int x, int cand) {
    if (y <= 4 && (cand == NODE_REST || cand == NODE_ELITE)) return 0;
    if (y >= 13 && cand == NODE_REST) return 0;

    if (cand == NODE_REST || cand == NODE_TREASURE ||
        cand == NODE_SHOP || cand == NODE_ELITE) {
        for (int i = 0; i < s_parent_n[y][x]; i++)
            if (g_map.type[y - 1][s_parent[y][x][i]] == cand) return 0;
    }
    if (cand == NODE_REST || cand == NODE_MONSTER || cand == NODE_EVENT ||
        cand == NODE_SHOP || cand == NODE_ELITE) {
        for (int i = 0; i < s_parent_n[y][x]; i++) {
            int px = s_parent[y][x][i];
            for (int j = 0; j < s_edge_n[y - 1][px]; j++) {
                int dx = s_edge[y - 1][px][j];
                if (dx == x) continue;
                if (g_map.type[y][dx] == cand) return 0;
            }
        }
    }
    return 1;
}

#define ROOM_LIST_MAX 128

/* AbstractDungeon.generateMap (:490-512) + RoomTypeAssigner (whole file).
 * Rows 0, 8 and 14 are fixed (monster / treasure / rest); the rest of the
 * rooms draw from a list holding the act's shares (shop 5%, rest 12%, elite
 * 8%, event 22%) padded with monsters, shuffled, then handed out row by row.
 * The count deliberately skips row 13 -- the row under the campfire -- which
 * is why that row is nearly all monsters in the real game too. */
static void map_assign_room_types(void) {
    static unsigned char list[ROOM_LIST_MAX];
    int list_n = 0;

    int count = 0;
    for (int y = 0; y < MAP_ROWS; y++) {
        if (y == MAP_ROWS - 2) continue;
        for (int x = 0; x < MAP_COLS; x++) count += s_edge_n[y][x] > 0;
    }
    int shops = (count * 5 + 50) / 100;
    int rests = (count * 12 + 50) / 100;
    int elites = (count * 8 + 50) / 100;
    int events = (count * 22 + 50) / 100;

    for (int i = 0; i < shops && list_n < ROOM_LIST_MAX; i++) list[list_n++] = NODE_SHOP;
    for (int i = 0; i < rests && list_n < ROOM_LIST_MAX; i++) list[list_n++] = NODE_REST;
    for (int i = 0; i < elites && list_n < ROOM_LIST_MAX; i++) list[list_n++] = NODE_ELITE;
    for (int i = 0; i < events && list_n < ROOM_LIST_MAX; i++) list[list_n++] = NODE_EVENT;

    /* Fixed rows, assigned before the rest (:505-511). */
    for (int x = 0; x < MAP_COLS; x++) {
        if (s_edge_n[0][x] > 0) g_map.type[0][x] = NODE_MONSTER;
        if (s_edge_n[8][x] > 0) g_map.type[8][x] = NODE_TREASURE;
        if (s_edge_n[MAP_ROWS - 1][x] > 0) g_map.type[MAP_ROWS - 1][x] = NODE_REST;
    }

    /* Pad the list up to the number of rooms still waiting (:124-130). */
    int open = 0;
    for (int y = 0; y < MAP_ROWS; y++)
        for (int x = 0; x < MAP_COLS; x++)
            open += s_edge_n[y][x] > 0 && g_map.type[y][x] == NODE_NONE;
    while (list_n < open && list_n < ROOM_LIST_MAX) list[list_n++] = NODE_MONSTER;

    /* Collections.shuffle (:131). */
    for (int i = list_n - 1; i > 0; i--) {
        int j = (int)(battle_rand() % (unsigned int)(i + 1));
        unsigned char t = list[i];
        list[i] = list[j];
        list[j] = t;
    }

    /* assignRoomsToNodes (:113-121): first type in the list that fits, used. */
    for (int y = 0; y < MAP_ROWS; y++) {
        for (int x = 0; x < MAP_COLS; x++) {
            if (s_edge_n[y][x] == 0 || g_map.type[y][x] != NODE_NONE) continue;
            for (int i = 0; i < list_n; i++) {
                if (!map_room_free_for(y, x, list[i])) continue;
                g_map.type[y][x] = list[i];
                for (int k = i + 1; k < list_n; k++) list[k - 1] = list[k];
                list_n--;
                break;
            }
        }
    }

    /* lastMinuteNodeChecker (:103-111). */
    for (int y = 0; y < MAP_ROWS; y++)
        for (int x = 0; x < MAP_COLS; x++)
            if (s_edge_n[y][x] > 0 && g_map.type[y][x] == NODE_NONE)
                g_map.type[y][x] = NODE_MONSTER;
}

/* Map generation follows Exordium + MapGenerator + RoomTypeAssigner:
 * 15 rows x 7 columns, six paths, fixed rows 0/8/14, and the per-act type
 * shares (shop 5%, rest 12%, event 22%, elite 8%). */
void map_generate(void) {
    memset(&g_map, 0, sizeof(g_map));
    memset(s_edge, 0, sizeof(s_edge));
    memset(s_edge_n, 0, sizeof(s_edge_n));
    memset(s_parent, 0, sizeof(s_parent));
    memset(s_parent_n, 0, sizeof(s_parent_n));
    g_map.row = -1;
    g_map.gold = 99;

    /* createPaths (:58-73): six walks, and the second one is forced to start
     * somewhere else so the bottom row always offers a choice. */
    int first_start = -1;
    for (int i = 0; i < 6; i++) {
        int start = map_rand_range(0, MAP_COLS - 1);
        if (i == 0) first_start = start;
        while (start == first_start && i == 1) start = map_rand_range(0, MAP_COLS - 1);
        map_create_path(start);
    }
    map_filter_row0_edges();

    map_assign_room_types();
}

/* AbstractDungeon.endAct / nextRoomTransition: beating the boss opens the next
 * act.  TheCity.java:68-72 repeats Exordium's room shares, so the map is built
 * exactly the same way; HP, gold, deck, relics and potions carry over, and so
 * does the card blizzard counter (only a whole run resets that: :2615). */
void run_advance_act(void) {
    int gold = g_map.gold;

    g_run.act++;
    g_run.node = -1;
    map_generate();
    g_map.gold = gold;
    g_run.screen = SCREEN_MAP;
}

int map_node(int row, int col) {
    /* The boss is a virtual row above the act. */
    if (row == MAP_ROWS && col == MAP_COLS / 2) return NODE_BOSS;
    if (row < 0 || row >= MAP_ROWS || col < 0 || col >= MAP_COLS) return NODE_NONE;
    return g_map.type[row][col];
}

/* A room exists once a walk passed through it. */
int map_has_room(int row, int col) {
    if (row < 0 || row >= MAP_ROWS || col < 0 || col >= MAP_COLS) return 0;
    return s_edge_n[row][col] > 0;
}

/* The rooms this room leads to, left to right (MapRoomNode.getEdges()). */
int map_edges(int row, int col, int *cols, int max) {
    if (!map_has_room(row, col)) return 0;
    int n = s_edge_n[row][col];
    if (n > max) n = max;
    for (int i = 0; i < n; i++) cols[i] = s_edge[row][col][i];
    return n;
}

/* Is there an edge from (row,col) to (row+1,dst)?  All edges span one row. */
static int map_edge_to(int row, int col, int dst) {
    if (!map_has_room(row, col)) return 0;
    for (int i = 0; i < s_edge_n[row][col]; i++)
        if (s_edge[row][col][i] == dst) return 1;
    return 0;
}

int map_can_move(int row, int col) {
    if (row != g_map.row + 1) return 0;
    if (g_map.row < 0) return row == 0 && map_has_room(0, col);
    if (row == MAP_ROWS) return col == MAP_COLS / 2;   /* every row-14 room leads to the boss */
    return map_edge_to(g_map.row, g_map.col, col);
}

void map_after_node(void) {
    g_run.screen = SCREEN_MAP;
}

/* AbstractCard.getPrice (:1900-1922).  Basics and specials are 9999: the
 * game never puts them in a shop. */
static int card_base_price(int rarity) {
    if (rarity == 3) return 150;
    if (rarity == 2) return 75;
    if (rarity == 1) return 50;
    return 9999;
}

/* (int)(price * merchantRng.random(0.9f, 1.1f)) — truncated, not rounded. */
static int card_price(int base) {
    int percent = 90 + (int)(battle_rand() % 21u);
    return base * percent / 100;
}

/* MathUtils.round(price * merchantRng.random(0.95f, 1.05f)) — relics and
 * potions go through MathUtils.round, cards do not. */
static int price_round(int base) {
    int percent = 95 + (int)(battle_rand() % 11u);
    return (base * percent + 50) / 100;
}

/* One card of a colour, type (-1 = any) and rarity, like getCardFromPool. */
static int pick_card(int color, int type, int rarity) {
    int candidates[128];
    int count = 0;
    for (int i = 0; i < STS_CARD_COUNT; i++) {
        if (sts_cards[i].color != color) continue;
        if (type >= 0 && sts_cards[i].type != type) continue;
        if (sts_cards[i].rarity != rarity) continue;
        if (count < 128) candidates[count++] = i;
    }
    return count ? candidates[battle_rand() % (unsigned int)count] : -1;
}

/* getCardFromPool (AbstractDungeon:1518-1557): a POWER of that rarity that is
 * missing from the pool steps one rarity up before the search gives up. */
static int pick_card_pool(int color, int type, int rarity) {
    int card = pick_card(color, type, rarity);
    if (card >= 0 || type != 2) return card;
    if (rarity == 1) return pick_card(color, type, 2);
    if (rarity == 2) return pick_card(color, type, 3);
    return -1;
}

void map_prepare_shop(void) {
    /* Merchant() (shop/Merchant.java:52-92): two attacks, two skills, one
     * power; the second card of each pair is rerolled while its cardID is the
     * same as the first one's. */
    static const int slot_type[5] = {0, 0, 1, 1, 2};
    g_map.shop_sale = (int)(battle_rand() % 5);

    for (int i = 0; i < 5; i++) {
        int rarity = battle_roll_rarity(9, 37);   /* ShopRoom: 9% rare, 37% uncommon */
        int card = pick_card_pool(0, slot_type[i], rarity);
        if (i % 2 == 1 && card >= 0 && card == g_map.shop_colored[i - 1]) {
            card = pick_card_pool(0, slot_type[i], rarity);
        }
        g_map.shop_colored[i] = card;
        int price = card >= 0 ? card_price(card_base_price(sts_cards[card].rarity)) : 0;
        if (i == g_map.shop_sale) price /= 2;
        g_map.shop_price[i] = price;
        g_map.shop_sold[i] = 0;
    }

    /* getColorlessCardFromPool: exactly one uncommon and one rare, priced at
     * x1.2f and truncated (ShopScreen.initCards :247-256). */
    for (int i = 0; i < 2; i++) {
        int card = pick_card_pool(4, -1, i == 0 ? 2 : 3);
        g_map.shop_colorless[i] = card;
        float price = card >= 0 ? (float)card_price(card_base_price(sts_cards[card].rarity)) : 0.0f;
        g_map.shop_price[5 + i] = (int)(price * 1.2f);
        g_map.shop_sold[5 + i] = 0;
    }

    /* initRelics (:345-357): two relics from rollRelicTier and one SHOP-tier
     * relic.  The port only ships common relics, so every slot draws from the
     * common pool — starter and the one special relic never reach a shop. */
    for (int i = 0; i < 3; i++) {
        int relic = -1;
        for (int attempt = 0; attempt < 40 && relic < 0; attempt++) {
            int id = (int)(battle_rand() % (unsigned int)battle_relic_count());
            if (battle_has_relic(id)) continue;
            if (battle_relic_tier(id) != 1) continue;
            int dup = 0;
            for (int k = 0; k < i; k++) {
                if (g_map.shop_relic[k] == id) dup = 1;
            }
            if (!dup) relic = id;
        }
        g_map.shop_relic[i] = relic;
        g_map.shop_relic_price[i] = relic >= 0 ? price_round(battle_relic_price(relic)) : 0;
        g_map.shop_relic_sold[i] = 0;
    }

    for (int i = 0; i < 3; i++) {
        int potion = (int)(battle_rand() % POTION_COUNT);
        g_map.shop_potion[i] = potion;
        g_map.shop_potion_price[i] = price_round(battle_potion_price(potion));
        g_map.shop_potion_sold[i] = 0;
    }

    g_map.purge_cost = 75;
    g_map.purge_from_event = 0;
}

int map_buy(int index) {
    if (index < 0 || index > 6) return 0;
    if (g_map.shop_sold[index]) return 0;
    int card = index < 5 ? g_map.shop_colored[index] : g_map.shop_colorless[index - 5];
    if (card < 0) return 0;
    if (g_map.gold < g_map.shop_price[index]) {
        snprintf(g_map.message, sizeof(g_map.message), "金币不足");
        return 0;
    }
    if (g_run.deck_count >= 60) {
        snprintf(g_map.message, sizeof(g_map.message), "牌组已满");
        return 0;
    }
    g_map.gold -= g_map.shop_price[index];
    g_run.deck[g_run.deck_count++] = CARD_ENTRY(card, 0);
    g_map.shop_sold[index] = 1;
    snprintf(g_map.message, sizeof(g_map.message), "买下 %s", sts_cards[card].name);
    return 1;
}

int map_purge(int deck_index) {
    if (deck_index < 0 || deck_index >= g_run.deck_count) return 0;
    if (g_run.deck_count <= 1) return 0;
    if (!g_map.purge_from_event && g_map.purge_done) return 0;
    if (g_map.gold < g_map.purge_cost) {
        snprintf(g_map.message, sizeof(g_map.message), "金币不足");
        return 0;
    }
    g_map.gold -= g_map.purge_cost;
    snprintf(g_map.message, sizeof(g_map.message), "移除了 %s",
             sts_cards[CARD_INDEX(g_run.deck[deck_index])].name);
    for (int i = deck_index; i < g_run.deck_count - 1; i++) g_run.deck[i] = g_run.deck[i + 1];
    g_run.deck_count--;

    if (g_map.purge_from_event) {
        g_map.purge_from_event = 0;
        g_map.event_done = 1;
        g_run.screen = SCREEN_EVENT;
    } else {
        g_map.purge_done = 1;
        g_map.purge_cost += 25;
        g_run.screen = SCREEN_SHOP;
    }
    return 1;
}

int map_buy_relic(int index) {
    if (index < 0 || index > 2) return 0;
    if (g_map.shop_relic_sold[index]) return 0;
    int id = g_map.shop_relic[index];
    if (id < 0) return 0;
    if (g_map.gold < g_map.shop_relic_price[index]) {
        snprintf(g_map.message, sizeof(g_map.message), "金币不足");
        return 0;
    }
    if (!battle_add_relic(id)) {
        snprintf(g_map.message, sizeof(g_map.message), "无法获得该遗物");
        return 0;
    }
    g_map.gold -= g_map.shop_relic_price[index];
    g_map.shop_relic_sold[index] = 1;
    snprintf(g_map.message, sizeof(g_map.message), "买下遗物「%s」", battle_relic_name(id));
    return 1;
}

int map_buy_potion(int index) {
    if (index < 0 || index > 2) return 0;
    if (g_map.shop_potion_sold[index]) return 0;
    if (g_map.gold < g_map.shop_potion_price[index]) {
        snprintf(g_map.message, sizeof(g_map.message), "金币不足");
        return 0;
    }
    if (!battle_add_potion(g_map.shop_potion[index])) {
        snprintf(g_map.message, sizeof(g_map.message), "药水已满");
        return 0;
    }
    g_map.gold -= g_map.shop_potion_price[index];
    g_map.shop_potion_sold[index] = 1;
    snprintf(g_map.message, sizeof(g_map.message), "买下 %s", battle_potion_name(g_map.shop_potion[index]));
    return 1;
}

static void heal_player(int amount) {
    g_battle.hp += amount;
    if (g_battle.hp > g_battle.max_hp) g_battle.hp = g_battle.max_hp;
}

int map_event_options(const char **labels, const char **hints, int max) {
    if (max < 3) return 0;
    if (g_map.event_id < 0 || g_map.event_id >= STS_EVENT_COUNT) return 0;
    const EventText *ev = &sts_events[g_map.event_id];
    for (int i = 0; i < 3; i++) {
        labels[i] = ev->option[i];
        hints[i] = ev->option_desc[i];
    }
    return 3;
}

void map_event_choose(int index) {
    if (index < 0 || index > 2 || g_map.event_done) return;
    if (g_map.event_id >= 0 && g_map.event_id < STS_EVENT_COUNT) {
        snprintf(g_map.event_result, sizeof(g_map.event_result), "%s",
                 sts_events[g_map.event_id].result[index]);
    }

    switch (g_map.event_id) {
    case 0: /* 大鱼 */
        if (index == 0) {
            int amount = g_battle.max_hp / 3;
            heal_player(amount);
            snprintf(g_map.message, sizeof(g_map.message), "香蕉：回复 %d 生命", amount);
        } else if (index == 1) {
            g_battle.max_hp += 5;
            heal_player(5);
            snprintf(g_map.message, sizeof(g_map.message), "甜甜圈：最大生命 +5");
        } else {
            int gained = -1;
            for (int attempt = 0; attempt < 10 && gained < 0; attempt++) {
                int id = (int)(battle_rand() % (unsigned int)battle_relic_count());
                if (battle_add_relic(id)) gained = id;
            }
            int curse = battle_find_card("Regret");
            if (curse >= 0 && g_run.deck_count < 60) {
                g_run.deck[g_run.deck_count++] = CARD_ENTRY(curse, 0);
            }
            if (gained >= 0) {
                snprintf(g_map.message, sizeof(g_map.message), "盒子：获得遗物「%s」与诅咒「悔恨」",
                         battle_relic_name(gained));
            } else {
                snprintf(g_map.message, sizeof(g_map.message), "盒子：获得诅咒「悔恨」");
            }
        }
        break;
    case 1: /* 牧师 */
        if (index == 0) {
            if (g_map.gold < 35) {
                snprintf(g_map.message, sizeof(g_map.message), "金币不足（需要 35）");
                return;
            }
            g_map.gold -= 35;
            int amount = g_battle.max_hp / 4;
            heal_player(amount);
            snprintf(g_map.message, sizeof(g_map.message), "牧师：回复 %d 生命", amount);
        } else if (index == 1) {
            if (g_map.gold < 50) {
                snprintf(g_map.message, sizeof(g_map.message), "金币不足（需要 50）");
                return;
            }
            g_map.purge_cost = 50;
            g_map.purge_from_event = 1;
            g_run.screen = SCREEN_PURGE;
            return;
        } else {
            snprintf(g_map.message, sizeof(g_map.message), "你走开了");
        }
        break;
    default: /* 黏液世界 */
        if (index == 0) {
            g_map.gold += 75;
            g_battle.hp -= 11;
            if (g_battle.hp < 1) g_battle.hp = 1;
            snprintf(g_map.message, sizeof(g_map.message), "捞到 75 金币，失去 11 生命");
        } else if (index == 1) {
            int loss = 20 + (int)(battle_rand() % 31);
            if (loss > g_map.gold) loss = g_map.gold;
            g_map.gold -= loss;
            snprintf(g_map.message, sizeof(g_map.message), "放手吧：失去 %d 金币", loss);
        } else {
            snprintf(g_map.message, sizeof(g_map.message), "你决定不值得");
        }
        break;
    }
    g_map.event_done = 1;
}

void map_treasure(void) {
    g_map.gold += 60;
    heal_player(8);
    if ((battle_rand() % 100) < 50) battle_add_random_potion();
    int gained = -1;
    for (int attempt = 0; attempt < 10 && gained < 0; attempt++) {
        int id = (int)(battle_rand() % (unsigned int)battle_relic_count());
        if (battle_add_relic(id)) gained = id;
    }
    if (gained >= 0) {
        snprintf(g_map.message, sizeof(g_map.message), "宝箱：+60 金币，获得遗物「%s」",
                 battle_relic_name(gained));
    } else {
        snprintf(g_map.message, sizeof(g_map.message), "宝箱：+60 金币，回复 8 生命");
    }
}

void map_enter(int row, int col) {
    int type = map_node(row, col);
    if (type == NODE_NONE) return;
    g_map.row = row;
    g_map.col = col;
    if (row >= 0 && row < MAP_ROWS && col >= 0 && col < MAP_COLS) g_map.visited[row][col] = 1;
    g_map.message[0] = 0;

    switch (type) {
    case NODE_MONSTER:
    case NODE_ELITE:
    case NODE_BOSS:
        battle_start(type);
        break;
    case NODE_REST:
        g_run.campfire_choice = 0;
        g_run.screen = SCREEN_CAMPFIRE;
        break;
    case NODE_SHOP:
        map_prepare_shop();
        g_run.screen = SCREEN_SHOP;
        break;
    case NODE_EVENT:
        g_map.event_id = (int)(battle_rand() % STS_EVENT_COUNT);
        g_map.event_done = 0;
        g_map.event_result[0] = '\0';
        g_run.screen = SCREEN_EVENT;
        break;
    case NODE_TREASURE:
        map_treasure();
        g_run.screen = SCREEN_TREASURE;
        break;
    default:
        g_run.screen = SCREEN_MAP;
        break;
    }
}
