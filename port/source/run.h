/* Map, shop, events and treasure: everything around a fight.
 *
 * The map is a 7x10 StS-like act: paths step at most one column per row, the
 * player picks one of the connected nodes, and the node type decides which
 * screen opens.
 */
#pragma once

#include "battle.h"

enum {
    NODE_NONE = 0,
    NODE_MONSTER,
    NODE_ELITE,
    NODE_REST,
    NODE_SHOP,
    NODE_EVENT,
    NODE_TREASURE,
    NODE_BOSS,
};

#define MAP_ROWS 15
#define MAP_COLS 7

typedef struct {
    unsigned char type[MAP_ROWS][MAP_COLS];
    unsigned char visited[MAP_ROWS][MAP_COLS];  /* rooms the player has stood in */
    int row;
    int col;
    int gold;
    int shop_colored[5];
    int shop_colorless[2];
    int shop_price[7];
    int shop_sold[7];
    int shop_sale;              /* index into the colored cards, -1 = none */
    int shop_relic[3];
    int shop_relic_price[3];
    int shop_relic_sold[3];
    int shop_potion[3];
    int shop_potion_price[3];
    int shop_potion_sold[3];
    int purge_cost;
    int purge_done;
    int purge_from_event;   /* 1 = the Cleric's purify, 0 = shop service */
    int event_id;
    int event_done;
    char event_result[512];   /* the result page the game shows after a choice */
    char message[96];
} MapState;

extern MapState g_map;

void map_generate(void);
/* AbstractDungeon.endAct: a new act, a new map, the same run. */
void run_advance_act(void);
int map_node(int row, int col);
int map_has_room(int row, int col);
int map_edges(int row, int col, int *cols, int max);
int map_can_move(int row, int col);
void map_enter(int row, int col);
void map_after_node(void);
void map_prepare_shop(void);
int map_buy(int index);
int map_buy_relic(int index);
int map_buy_potion(int index);
int map_purge(int deck_index);
int map_event_options(const char **labels, const char **hints, int max);
void map_event_choose(int index);
void map_treasure(void);
