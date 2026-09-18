/**
 * anim_data.c — Character animation data (64-bit safe)
 * Extracted from SONICR.EXE ROM at 0x4FBDF8.
 * All pointer arrays use uintptr_t for 64-bit compatibility.
 * All 18 entries per character read from binary (including OOB past documented count).
 */

#include <stdint.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"

/* ====================================================================
 * Frame streams — extracted from SONICR.EXE ROM, keyed by VA
 * Format: frame indices, -1 = loop back, -2 = transition, final short = length
 * ==================================================================== */

/* Sonic */
static short s_fs_004fa2c0[] = { 1, 2, 3, 4100, 5, 6, 7, 8, 9, 8202, 11, 12, -1, 13 };
static short s_fs_004fa2dc[] = { 91, 92, 4189, 94, 95, 96, 8289, 98, -1, 9 };
static short s_fs_004fa2f0[] = { 13, 14, 15, 16, 17, 18, -1, 7 };
static short s_fs_004fa300[] = { 19, 20, 21, 22, 23, -1, 2 };
static short s_fs_004fa30e[] = { 24, 25, 26, 27, 28, -1, 2 };
static short s_fs_004fa31c[] = { 23, 22, 21, 20, 19, -2, 2, 28, 27, 26, 25, 24, -2, 2, 86, 87, 88, 89, 90, -1, 2 };
static short s_fs_004fa32a[] = { 28, 27, 26, 25, 24, -2, 2, 86, 87, 88, 89, 90, -1, 2 };
static short s_fs_004fa338[] = { 86, 87, 88, 89, 90, -1, 2 };
static short s_fs_004fa346[] = { 90, 89, 88, 87, 86, -2, 2, 81, 82, 83, 84, 85, -1, 2 };
static short s_fs_004fa354[] = { 81, 82, 83, 84, 85, -1, 2 };
static short s_fs_004fa362[] = { 65, 66, 67, 68, 69, 70, 71, 72, 71, 70, 69, 68, 67, 66, -1, 15 };
static short s_fs_004fa382[] = { 73, 74, 75, 76, 77, 78, 79, 80, 79, 78, 77, 76, 75, 74, -1, 15 };
static short s_fs_004fa3a2[] = { 99, -1, 2 };
static short s_fs_004fa3a8[] = { 101, -1, 2 };
static short s_fs_004fa3ae[] = { 103, -1, 2 };
static short s_fs_004fa3b4[] = { 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, -1, 23 };
static short s_fs_004fa48e[] = { 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, -1, 23 };

/* Tails */
static short s_fs_004fa508[] = { 1, 2, 3, 4100, 5, 6, 7, 8, 9, 8202, 11, 12, -1, 13 };
static short s_fs_004fa524[] = { 13, 14, 15, 16, 17, 18, -1, 7 };
static short s_fs_004fa534[] = { 19, 20, 21, 22, 23, -1, 2 };
static short s_fs_004fa542[] = { 24, 25, 26, 27, 28, -1, 2 };
static short s_fs_004fa550[] = { 23, 22, 21, 20, 19, -2, 2, 28, 27, 26, 25, 24, -2, 2, 102, 103, 104, 105, 106, -1, 2 };
static short s_fs_004fa55e[] = { 28, 27, 26, 25, 24, -2, 2, 102, 103, 104, 105, 106, -1, 2 };
static short s_fs_004fa56c[] = { 102, 103, 104, 105, 106, -1, 2 };
static short s_fs_004fa57a[] = { 106, 105, 104, 103, 102, -2, 2, 111, -1, 2 };
static short s_fs_004fa588[] = { 111, -1, 2 };
static short s_fs_004fa58e[] = { 107, -1, 2 };
static short s_fs_004fa594[] = { 109, -1, 2 };
static short s_fs_004fa59a[] = { 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, -1, 23 };
static short s_fs_004fa5ca[] = { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, -1, 21 };
static short s_fs_004fa5f6[] = { 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, -1, 23 };
static short s_fs_004fa626[] = { 94, 4191, 96, 97, 98, 4195, 100, 101, -1, 9 };
static short s_fs_004fa63a[] = { 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, -1, 17 };
static short s_fs_004fa65e[] = { 73, 74, 75, 76, 77, -1, 2 };
static short s_fs_004fa66c[] = { 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, -1, 45 };

/* Knuckles */
static short s_fs_004fa710[] = { 1, 2, 3, 4100, 5, 6, 7, 8, 9, 8202, 11, 12, -1, 13 };
static short s_fs_004fa72c[] = { 112, 113, 4210, 115, 116, 117, 8310, 119, -1, 9 };
static short s_fs_004fa740[] = { 13, 14, 15, 16, 17, 18, -1, 7 };
static short s_fs_004fa750[] = { 19, 20, 21, 22, 23, -1, 2 };
static short s_fs_004fa75e[] = { 24, 25, 26, 27, 28, -1, 2 };
static short s_fs_004fa76c[] = { 23, 22, 21, 20, 19, -2, 2, 28, 27, 26, 25, 24, -2, 2, 100, 101, 102, 103, 104, 105, -1, 2 };
static short s_fs_004fa77a[] = { 28, 27, 26, 25, 24, -2, 2, 100, 101, 102, 103, 104, 105, -1, 2 };
static short s_fs_004fa788[] = { 100, 101, 102, 103, 104, 105, -1, 2 };
static short s_fs_004fa798[] = { 105, 104, 103, 102, 101, 100, -2, 2, 110, -1, 2 };
static short s_fs_004fa7a8[] = { 110, -1, 2 };
static short s_fs_004fa7ae[] = { 108, -1, 2 };
static short s_fs_004fa7b4[] = { 106, -1, 2 };
static short s_fs_004fa7ba[] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, -1, 23 };
static short s_fs_004fa876[] = { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, -1, 23 };
static short s_fs_004fa8a6[] = { 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, -1, 2 };
static short s_fs_004fa92e[] = { 79, 80, 81, 82, 83, -1, 2 };
static short s_fs_004fa93c[] = { 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, -1, 17 };
static short s_fs_004fa960[] = { 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123, 122, 121, -1, 47 };

/* Amy */
static short s_fs_004faa08[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, -1, 23 };
static short s_fs_004faa38[] = { 13, 14, 15, 16, 17, -1, 2 };
static short s_fs_004faa46[] = { 18, 19, 20, 21, 22, -1, 2 };
static short s_fs_004faa54[] = { 17, 16, 15, 14, 13, -2, 2, 22, 21, 20, 19, 18, -2, 2, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, -1, 2 };
static short s_fs_004faa62[] = { 22, 21, 20, 19, 18, -2, 2, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, -1, 2 };
static short s_fs_004faa70[] = { 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, -1, 2 };
static short s_fs_004faa94[] = { 39, 40, 41, 42, 43, 44, -1, 7 };
static short s_fs_004faaa4[] = { 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, -1, 23 };
static short s_fs_004faad4[] = { 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, -1, 23 };
static short s_fs_004fab04[] = { 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 107, 106, 105, 104, 103, 102, -1, 15 };
static short s_fs_004fab44[] = { 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, -2, 2, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, -1, 7 };
static short s_fs_004fab68[] = { 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, -1, 7 };
static short s_fs_004fab8c[] = { 80, 79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 69, -2, 2, 109, 110, 111, 112, -1, 2 };
static short s_fs_004faba8[] = { 109, 110, 111, 112, -1, 2 };
static short s_fs_004fabb4[] = { 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, -1, 23 };

/* Eggman */
static short s_fs_004fac54[] = { 7, 6, 5, 4, 3, 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, -1, 23 };
static short s_fs_004fac90[] = { 13, 14, 15, 16, 17, -1, 2 };
static short s_fs_004fac9e[] = { 18, 19, 20, 21, 22, -1, 2 };
static short s_fs_004facac[] = { 17, 16, 15, 14, 13, -2, 2, 22, 21, 20, 19, 18, -2, 2, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, -1, 23 };
static short s_fs_004facba[] = { 22, 21, 20, 19, 18, -2, 2, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, -1, 23 };
static short s_fs_004facc8[] = { 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, -1, 23 };
static short s_fs_004facf8[] = { 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, -1, 23 };
static short s_fs_004fad28[] = { 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, -1, 2 };
static short s_fs_004fad4c[] = { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 74, 73, 72, 72, 70, 69, 68, 67, 66, 65, 64, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 74, 73, 72, 72, 70, 69, 68, 67, 66, 65, 64, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 74, 73, 72, 72, 70, 69, 68, 67, 66, 65, 64, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, -1, 165 };
static short s_fs_004faed6[] = { 99, 100, 101, 102, -1, 2 };
static short s_fs_004faee2[] = { 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, -1, 23 };

/* Metal Sonic */
static short s_fs_004faf5c[] = { 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, -1, 15 };
static short s_fs_004faf7c[] = { 9, 10, 11, 12, 13, 14, 15, 16, 15, 14, 13, 12, 11, 10, -1, 15 };
static short s_fs_004faf9c[] = { 88, 89, 90, 91, 92, -1, 2 };
static short s_fs_004fafaa[] = { 26, 27, 28, 29, 30, -1, 2 };
static short s_fs_004fafb8[] = { 31, 32, 33, 34, 35, -1, 2 };
static short s_fs_004fafc6[] = { 30, 29, 28, 27, 26, -2, 2, 35, 34, 33, 32, 31, -2, 2, 109, -1, 2 };
static short s_fs_004fafd4[] = { 35, 34, 33, 32, 31, -2, 2, 109, -1, 2 };
static short s_fs_004fafe2[] = { 109, -1, 2 };
static short s_fs_004fafe8[] = { 22, -1, 2 };
static short s_fs_004fafee[] = { 24, -1, 2 };
static short s_fs_004faff4[] = { 17, 18, 19, 20, 21, -1, 2 };
static short s_fs_004fb002[] = { 44, 45, 46, 47, 48, 49, 50, 51, 50, 49, 48, 47, 46, 45, -1, 15 };
static short s_fs_004fb022[] = { 36, 37, 38, 39, 40, 41, 42, 43, 42, 41, 40, 39, 38, 37, -1, 15 };
static short s_fs_004fb042[] = { 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, -1, 23 };
static short s_fs_004fb11a[] = { 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, -1, 31 };

/* Tails Doll */
static short s_fs_004fb1a4[] = { 8, 7, 6, 5, 4, 3, 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, -1, 31 };
static short s_fs_004fb1f2[] = { 23, 22, 21, 20, 19, 18, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, -1, 23 };
static short s_fs_004fb266[] = { 29, -1, 2 };
static short s_fs_004fb26c[] = { 33, -1, 2 };
static short s_fs_004fb272[] = { 31, -1, 2 };
static short s_fs_004fb278[] = { 110, 111, 112, 113, 114, -1, 2 };
static short s_fs_004fb286[] = { 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, -1, 23 };
static short s_fs_004fb2b6[] = { 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, -1, 2 };
static short s_fs_004fb362[] = { 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, -1, 31 };
static short s_fs_004fb3b8[] = { 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, -1, 37 };

/* Metal Knuckles */
static short s_fs_004fb44c[] = { 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, -1, 15 };
static short s_fs_004fb46c[] = { 9, 10, 11, 12, 13, 14, 15, 16, 15, 14, 13, 12, 11, 10, -1, 15 };
static short s_fs_004fb48c[] = { 129, 130, 131, 132, 133, -1, 2 };
static short s_fs_004fb49a[] = { 133, -1, 2 };
static short s_fs_004fb4a0[] = { 22, 23, 24, 25, 26, -1, 2 };
static short s_fs_004fb4ae[] = { 27, 28, 29, 30, 31, -1, 2 };
static short s_fs_004fb4bc[] = { 26, 25, 24, 23, 22, -2, 2, 31, 30, 29, 28, 27, -2, 2, 162, -1, 2 };
static short s_fs_004fb4ca[] = { 31, 30, 29, 28, 27, -2, 2, 162, -1, 2 };
static short s_fs_004fb4d8[] = { 162, -1, 2 };
static short s_fs_004fb4de[] = { 134, -1, 2 };
static short s_fs_004fb4e4[] = { 136, -1, 2 };
static short s_fs_004fb4ea[] = { 17, 18, 19, 20, 21, -1, 2 };
static short s_fs_004fb4f8[] = { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, -1, 40 };
static short s_fs_004fb54a[] = { 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72, -1, 23 };
static short s_fs_004fb57a[] = { 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, -1, 173 };
static short s_fs_004fb6da[] = { 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, -1, 47 };

/* Egg Robo */
static short s_fs_004fb784[] = { 1, 2, 3, 4, 5, 4102, 7, 8, 9, 10, 11, 8204, -1, 13 };
static short s_fs_004fb7a0[] = { 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, -1, 23 };
static short s_fs_004fb7d0[] = { 88, 89, 90, 91, 92, -1, 2 };
static short s_fs_004fb7de[] = { 25, 26, 27, 28, 29, -1, 2 };
static short s_fs_004fb7ec[] = { 30, 31, 32, 33, 34, -1, 2 };
static short s_fs_004fb7fa[] = { 29, 28, 27, 26, 25, -2, 2, 34, 33, 32, 31, 30, -2, 2, 130, -1, 2 };
static short s_fs_004fb808[] = { 34, 33, 32, 31, 30, -2, 2, 130, -1, 2 };
static short s_fs_004fb816[] = { 130, -1, 2 };
static short s_fs_004fb81c[] = { 132, -1, 2 };
static short s_fs_004fb822[] = { 134, -1, 2 };
static short s_fs_004fb828[] = { 71, 72, 73, 74, 75, -1, 2 };
static short s_fs_004fb836[] = { 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, -1, 23 };
static short s_fs_004fb866[] = { 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, -1, 2 };
static short s_fs_004fb8fa[] = { 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, -1, 158 };
static short s_fs_004fba38[] = { 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138, 137, -1, 47 };

/* Super Sonic */
static short s_fs_004fbae0[] = { 1, 2, 3, 4, 4101, 6, 7, 8, 9, 10, 8203, 12, -1, 13 };
static short s_fs_004fbafc[] = { 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, -1, 23 };
static short s_fs_004fbb2c[] = { 29, 30, 31, 32, 33, 34, -1, 7 };
static short s_fs_004fbb3c[] = { 78, 79, 80, 81, 82, -1, 2 };
static short s_fs_004fbb4a[] = { 83, 84, 85, 86, 87, -1, 2 };
static short s_fs_004fbb58[] = { 82, 81, 80, 79, 78, -2, 2, 87, 86, 85, 84, 83, -2, 2, 88, 89, 90, 91, 92, -1, 2 };
static short s_fs_004fbb66[] = { 87, 86, 85, 84, 83, -2, 2, 88, 89, 90, 91, 92, -1, 2 };
static short s_fs_004fbb74[] = { 88, 89, 90, 91, 92, -1, 2 };
static short s_fs_004fbb82[] = { 92, 91, 90, 89, 88, -2, 2, 40, -1, 2 };
static short s_fs_004fbb90[] = { 40, -1, 2 };
static short s_fs_004fbb96[] = { 27, -1, 2 };
static short s_fs_004fbb9c[] = { 25, -1, 2 };
static short s_fs_004fbba2[] = { 35, 36, 37, 38, 39, -1, 2 };
static short s_fs_004fbbb0[] = { 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 109, -1, 23 };
static short s_fs_004fbc42[] = { 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123, 122, -1, 31 };
static short s_fs_004fbc82[] = { 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 76, 75, 74, 73, 72, 71, 70, 69, 68, 67, -1, 23 };
static short s_fs_004fbd4e[] = { 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138, -1, 47 };

/* ====================================================================
 * Per-character animation pointer tables — 18 entries each
 * Indices: 0=idle 1=jump 2=still 3=turnL 4=turnR 5=brake 6=prerace
 *          7=celebrate 8=trickL 9=trickR 10=landing 11=special
 *          12=boost 13=char_specific 14=falling 15=highspeed 16=(unused) 17=intro
 * ==================================================================== */

static uintptr_t s_ap_Sonic[] = {
    (uintptr_t)s_fs_004fa2c0,  /* 0: idle */
    (uintptr_t)s_fs_004fa2f0,  /* 1: jump */
    (uintptr_t)s_fs_004fa3b4,  /* 2: still */
    (uintptr_t)s_fs_004fa300,  /* 3: turn L */
    (uintptr_t)s_fs_004fa30e,  /* 4: turn R */
    (uintptr_t)s_fs_004fa354,  /* 5: brake */
    (uintptr_t)s_fs_004fa362,  /* 6: prerace */
    (uintptr_t)s_fs_004fa382,  /* 7: celebrate */
    (uintptr_t)s_fs_004fa31c,  /* 8: trick L */
    (uintptr_t)s_fs_004fa32a,  /* 9: trick R */
    (uintptr_t)s_fs_004fa338,  /* 10: landing */
    (uintptr_t)s_fs_004fa346,  /* 11: special */
    (uintptr_t)s_fs_004fa2dc,  /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004fa3a2,  /* 14: falling */
    (uintptr_t)s_fs_004fa3a8,  /* 15: highspeed */
    (uintptr_t)s_fs_004fa3ae,  /* 16 */
    (uintptr_t)s_fs_004fa48e   /* 17: intro */
};

static uintptr_t s_ap_Tails[] = {
    (uintptr_t)s_fs_004fa508,  /* 0: idle */
    (uintptr_t)s_fs_004fa524,  /* 1: jump */
    (uintptr_t)s_fs_004fa59a,  /* 2: still */
    (uintptr_t)s_fs_004fa534,  /* 3: turn L */
    (uintptr_t)s_fs_004fa542,  /* 4: turn R */
    (uintptr_t)s_fs_004fa65e,  /* 5: brake */
    (uintptr_t)s_fs_004fa5ca,  /* 6: prerace */
    (uintptr_t)s_fs_004fa5f6,  /* 7: celebrate */
    (uintptr_t)s_fs_004fa550,  /* 8: trick L */
    (uintptr_t)s_fs_004fa55e,  /* 9: trick R */
    (uintptr_t)s_fs_004fa56c,  /* 10: landing */
    (uintptr_t)s_fs_004fa57a,  /* 11: special */
    (uintptr_t)s_fs_004fa626,  /* 12: boost */
    (uintptr_t)s_fs_004fa63a,  /* 13: char_specific */
    (uintptr_t)s_fs_004fa588,  /* 14: falling */
    (uintptr_t)s_fs_004fa58e,  /* 15: highspeed */
    (uintptr_t)s_fs_004fa594,  /* 16 */
    (uintptr_t)s_fs_004fa66c   /* 17: intro */
};

static uintptr_t s_ap_Knuckles[] = {
    (uintptr_t)s_fs_004fa710,  /* 0: idle */
    (uintptr_t)s_fs_004fa740,  /* 1: jump */
    (uintptr_t)s_fs_004fa7ba,  /* 2: still */
    (uintptr_t)s_fs_004fa75e,  /* 3: turn L */
    (uintptr_t)s_fs_004fa750,  /* 4: turn R */
    (uintptr_t)s_fs_004fa92e,  /* 5: brake */
    (uintptr_t)s_fs_004fa876,  /* 6: prerace */
    (uintptr_t)s_fs_004fa8a6,  /* 7: celebrate */
    (uintptr_t)s_fs_004fa77a,  /* 8: trick L */
    (uintptr_t)s_fs_004fa76c,  /* 9: trick R */
    (uintptr_t)s_fs_004fa788,  /* 10: landing */
    (uintptr_t)s_fs_004fa798,  /* 11: special */
    (uintptr_t)s_fs_004fa72c,  /* 12: boost */
    (uintptr_t)s_fs_004fa93c,  /* 13: char_specific (glide) */
    (uintptr_t)s_fs_004fa7a8,  /* 14: falling */
    (uintptr_t)s_fs_004fa7ae,  /* 15: highspeed */
    (uintptr_t)s_fs_004fa7b4,  /* 16 */
    (uintptr_t)s_fs_004fa960   /* 17: intro */
};

static uintptr_t s_ap_Amy[] = {
    (uintptr_t)s_fs_004faa08,  /* 0: idle */
    0,                          /* 1: jump */
    (uintptr_t)s_fs_004faa94,  /* 2: still */
    (uintptr_t)s_fs_004faa46,  /* 3: turn L */
    (uintptr_t)s_fs_004faa38,  /* 4: turn R */
    (uintptr_t)s_fs_004faa70,  /* 5: brake */
    (uintptr_t)s_fs_004faaa4,  /* 6: prerace */
    (uintptr_t)s_fs_004faad4,  /* 7: celebrate */
    (uintptr_t)s_fs_004faa62,  /* 8: trick L */
    (uintptr_t)s_fs_004faa54,  /* 9: trick R */
    (uintptr_t)s_fs_004fab68,  /* 10: landing */
    (uintptr_t)s_fs_004fab04,  /* 11: special */
    (uintptr_t)s_fs_004fab44,  /* 12: boost */
    (uintptr_t)s_fs_004fab8c,  /* 13: char_specific */
    (uintptr_t)s_fs_004faba8,  /* 14: falling */
    0,                          /* 15: highspeed */
    0,                          /* 16 */
    (uintptr_t)s_fs_004fabb4   /* 17: intro */
};

static uintptr_t s_ap_Eggman[] = {
    (uintptr_t)s_fs_004fac54,  /* 0: idle */
    0,                          /* 1: jump */
    (uintptr_t)s_fs_004fad4c,  /* 2: still */
    (uintptr_t)s_fs_004fac9e,  /* 3: turn L */
    (uintptr_t)s_fs_004fac90,  /* 4: turn R */
    (uintptr_t)s_fs_004fad28,  /* 5: brake */
    (uintptr_t)s_fs_004facc8,  /* 6: prerace */
    (uintptr_t)s_fs_004facf8,  /* 7: celebrate */
    (uintptr_t)s_fs_004facba,  /* 8: trick L */
    (uintptr_t)s_fs_004facac,  /* 9: trick R */
    0,                          /* 10: landing */
    0,                          /* 11: special */
    0,                          /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004faed6,  /* 14: falling */
    0,                          /* 15: highspeed */
    0,                          /* 16 */
    (uintptr_t)s_fs_004faee2   /* 17: intro */
};

static uintptr_t s_ap_Metal[] = {
    (uintptr_t)s_fs_004faf5c,  /* 0: idle */
    (uintptr_t)s_fs_004faf9c,  /* 1: jump */
    (uintptr_t)s_fs_004fb042,  /* 2: still */
    (uintptr_t)s_fs_004fafb8,  /* 3: turn L */
    (uintptr_t)s_fs_004fafaa,  /* 4: turn R */
    (uintptr_t)s_fs_004faff4,  /* 5: brake */
    (uintptr_t)s_fs_004fb002,  /* 6: prerace */
    (uintptr_t)s_fs_004fb022,  /* 7: celebrate */
    (uintptr_t)s_fs_004fafd4,  /* 8: trick L */
    (uintptr_t)s_fs_004fafc6,  /* 9: trick R */
    0,                          /* 10: landing */
    0,                          /* 11: special */
    (uintptr_t)s_fs_004faf7c,  /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004fafe2,  /* 14: falling */
    (uintptr_t)s_fs_004fafe8,  /* 15: highspeed */
    (uintptr_t)s_fs_004fafee,  /* 16 */
    (uintptr_t)s_fs_004fb11a   /* 17: intro */
};

static uintptr_t s_ap_TailsDoll[] = {
    (uintptr_t)s_fs_004fb1a4,  /* 0: idle */
    0,                          /* 1: jump */
    (uintptr_t)s_fs_004fb362,  /* 2: still */
    0,                          /* 3: turn L */
    0,                          /* 4: turn R */
    (uintptr_t)s_fs_004fb278,  /* 5: brake */
    (uintptr_t)s_fs_004fb286,  /* 6: prerace */
    (uintptr_t)s_fs_004fb2b6,  /* 7: celebrate */
    0,                          /* 8: trick L */
    0,                          /* 9: trick R */
    0,                          /* 10: landing */
    0,                          /* 11: special */
    (uintptr_t)s_fs_004fb1f2,  /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004fb266,  /* 14: falling */
    (uintptr_t)s_fs_004fb26c,  /* 15: highspeed */
    (uintptr_t)s_fs_004fb272,  /* 16 */
    (uintptr_t)s_fs_004fb3b8   /* 17: intro */
};

static uintptr_t s_ap_MKnux[] = {
    (uintptr_t)s_fs_004fb44c,  /* 0: idle */
    (uintptr_t)s_fs_004fb48c,  /* 1: jump */
    (uintptr_t)s_fs_004fb57a,  /* 2: still */
    (uintptr_t)s_fs_004fb4ae,  /* 3: turn L */
    (uintptr_t)s_fs_004fb4a0,  /* 4: turn R */
    (uintptr_t)s_fs_004fb4ea,  /* 5: brake */
    (uintptr_t)s_fs_004fb4f8,  /* 6: prerace */
    (uintptr_t)s_fs_004fb54a,  /* 7: celebrate */
    (uintptr_t)s_fs_004fb4ca,  /* 8: trick L */
    (uintptr_t)s_fs_004fb4bc,  /* 9: trick R */
    0,                          /* 10: landing */
    0,                          /* 11: special */
    (uintptr_t)s_fs_004fb46c,  /* 12: boost */
    (uintptr_t)s_fs_004fb49a,  /* 13: char_specific */
    (uintptr_t)s_fs_004fb4d8,  /* 14: falling */
    (uintptr_t)s_fs_004fb4de,  /* 15: highspeed */
    (uintptr_t)s_fs_004fb4e4,  /* 16 */
    (uintptr_t)s_fs_004fb6da   /* 17: intro */
};

static uintptr_t s_ap_EggRobo[] = {
    (uintptr_t)s_fs_004fb784,  /* 0: idle */
    (uintptr_t)s_fs_004fb7d0,  /* 1: jump */
    (uintptr_t)s_fs_004fb8fa,  /* 2: still */
    (uintptr_t)s_fs_004fb7de,  /* 3: turn L */
    (uintptr_t)s_fs_004fb7ec,  /* 4: turn R */
    (uintptr_t)s_fs_004fb828,  /* 5: brake */
    (uintptr_t)s_fs_004fb836,  /* 6: prerace */
    (uintptr_t)s_fs_004fb866,  /* 7: celebrate */
    (uintptr_t)s_fs_004fb7fa,  /* 8: trick L */
    (uintptr_t)s_fs_004fb808,  /* 9: trick R */
    0,                          /* 10: landing */
    0,                          /* 11: special */
    (uintptr_t)s_fs_004fb7a0,  /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004fb816,  /* 14: falling */
    (uintptr_t)s_fs_004fb81c,  /* 15: highspeed */
    (uintptr_t)s_fs_004fb822,  /* 16 */
    (uintptr_t)s_fs_004fba38   /* 17: intro */
};

static uintptr_t s_ap_Super[] = {
    (uintptr_t)s_fs_004fbae0,  /* 0: idle */
    (uintptr_t)s_fs_004fbb2c,  /* 1: jump */
    (uintptr_t)s_fs_004fbc82,  /* 2: still */
    (uintptr_t)s_fs_004fbb4a,  /* 3: turn L */
    (uintptr_t)s_fs_004fbb3c,  /* 4: turn R */
    (uintptr_t)s_fs_004fbba2,  /* 5: brake */
    (uintptr_t)s_fs_004fbbb0,  /* 6: prerace */
    (uintptr_t)s_fs_004fbc42,  /* 7: celebrate */
    (uintptr_t)s_fs_004fbb66,  /* 8: trick L */
    (uintptr_t)s_fs_004fbb58,  /* 9: trick R */
    (uintptr_t)s_fs_004fbb74,  /* 10: landing */
    (uintptr_t)s_fs_004fbb82,  /* 11: special */
    (uintptr_t)s_fs_004fbafc,  /* 12: boost */
    0,                          /* 13: char_specific */
    (uintptr_t)s_fs_004fbb90,  /* 14: falling */
    (uintptr_t)s_fs_004fbb96,  /* 15: highspeed */
    (uintptr_t)s_fs_004fbb9c,  /* 16 */
    (uintptr_t)s_fs_004fbd4e   /* 17: intro */
};

/* Main table: uintptr_t[20]. Even = anim array ptr, odd = count.
 * Accessed as ((uintptr_t*)g_charAnimTables)[charId * 2].
 * All tables have 18 entries (indices 0-17 read from binary ROM). */
static uintptr_t s_mainAnimTable[20] = {
    (uintptr_t)s_ap_Sonic, 18,      /* Sonic */
    (uintptr_t)s_ap_Tails, 18,      /* Tails */
    (uintptr_t)s_ap_Knuckles, 18,   /* Knuckles */
    (uintptr_t)s_ap_Amy, 18,        /* Amy */
    (uintptr_t)s_ap_Eggman, 18,     /* Eggman */
    (uintptr_t)s_ap_Metal, 18,      /* Metal Sonic */
    (uintptr_t)s_ap_TailsDoll, 18,  /* Tails Doll */
    (uintptr_t)s_ap_MKnux, 18,     /* Metal Knuckles */
    (uintptr_t)s_ap_EggRobo, 18,   /* Egg Robo */
    (uintptr_t)s_ap_Super, 18,     /* Super Sonic */
};

void InitAnimData(void)
{
    g_charAnimTables = s_mainAnimTable;
}
