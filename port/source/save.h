/* Persistent run save: the map, the deck and the current HP.
 *
 * A run is saved whenever the player stands on the map, so a power-off costs
 * at most the fight that was in progress.  Loading rebuilds the map from the
 * saved seed instead of storing its edge table: map_generate() is deterministic
 * once battle_rand() is rewound to the saved seed.
 */
#pragma once

/* 1 when a valid save was loaded.  Safe to call before any run exists. */
int save_load(void);
/* Writes the current run, but only while on the map screen. */
void save_write(void);
/* Removes the file (new run / abandoned run). */
void save_clear(void);

#if defined(CARD_TEST)
/* Checks the blob validator and its checksum without touching the SD card. */
int save_self_test(void);
#endif
