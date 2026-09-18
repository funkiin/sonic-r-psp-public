/*
 * debug_flags.h — Debug rigs for forcing specific race-end states.
 *
 * Each flag is a compile-time toggle (1 = on, 0 = off). Independent of
 * one another. Combine as needed for whatever you're testing.
 */
#ifndef DEBUG_FLAGS_H
#define DEBUG_FLAGS_H

/* GP fast-finish: GP races complete after 1 lap and player 0 is forced
 * into 1st place. Lets credits-path / unlock-path testing iterate fast.
 * Touches race_timing.c (Section 4 lap loop) + race.c (race-position
 * sort post-pass). */
#define DEBUG_GP_FAST_FIRST 0

/* Force g_raceCheckpoint bit 0 at race finish, simulating a chaos-emerald
 * pickup during the race. Combined with DEBUG_GP_FAST_FIRST=1, this
 * triggers the post-race ShowEmeraldUnlockScreen path on every GP run. */
#define DEBUG_FORCE_EMERALD_PICKUP 0

/* Force g_p1CollectionCount = 5 at race finish, simulating having grabbed
 * all 5 coins during the race. Combined with DEBUG_GP_FAST_FIRST=1
 * (1st-place GP finish), this satisfies the trigger condition for
 * SetupSpecialRace at main.c:1849 — the unlock race against the metal
 * counterpart. Touches race_timing.c. */
#define DEBUG_FORCE_5_COINS 0

#endif /* DEBUG_FLAGS_H */
