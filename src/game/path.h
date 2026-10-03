/* The path finder (0x46e9e0-0x471940) and Map_FindNearestRoad 0x41a490. One search at a time: the
   controller that owns it is 0x4b3094 (-1 idle); a search runs a few node expansions per call and
   the owner calls Path_Find again on later frames until it ends. The route goes into the path slot of
   the controller's number (g_path_slots, route.h: 85 block triples ended by 0, 0, 0). See
   docs/traffic.md "Path finding". */
#pragma once
#include <stdint.h>

/* search modes (0x7537b0): 0 / 1 a grid walk over any drivable block, against the direction bits
   too (Path_ExpandDir; the original's only caller passes all zeros); 2..5 follow the road direction
   bits (Path_ExpandNode): 2 cops and ambulances to their station, 3 emergency vehicles and cops to a
   target (straight-line shortcut near the goal, U-turns across a lane, one more block past the goal
   on its direction), 4 like 3 with pavements allowed (no caller), 5 fire engines and mission dummies
   (a hop out of a block it can't leave that way needs 4 blocks) */
enum { PATH_FAIL = 0, PATH_FOUND = 1, PATH_OVERFLOW = 2, PATH_CONTINUE = 3, PATH_BUSY = -1 };

extern int16_t g_path_owner;                /* 0x4b3094 the controller the search serves (-1 idle) */
extern int16_t g_path_result;               /* 0x7537b2 the last result a dummy controller stored */

void path_reset(void);                      /* Path_Reset 0x46e9e0 */
/* Path_Find 0x4716f0: start or continue the search of controller ctrl from block (x, y, z) to block
   (dx, dy, dz). 1 found (or start = goal: an empty route), 2 node pool full (the best partial route is
   stored), 3 not done yet (call again next frame), 0 failed (no road at either end, or nothing left
   to expand), -1 another controller owns the search. The route is written to path slot ctrl. */
int path_find(int x, int y, int z, int dx, int dy, int dz, int mode, int ctrl);
/* Path_IsUpRamp 0x46ee20 / Path_IsDownRamp 0x46ee70: the slope type of a type map ((t >> 8) & 0x3f)
   leads up / down when leaving the block in direction dir (1, 2, 4, 8) */
int path_is_up_ramp(uint32_t type_map, int dir);
int path_is_down_ramp(uint32_t type_map, int dir);
void map_get_block_info_thunk(int x, int y, int z);   /* Map_GetBlockInfo_thunk 0x471940 (no effect) */

/* Map_FindNearestRoad 0x41a490: q[2], q[3], q[4] = block x, y, z (the layout of the original's
   callers). A block with direction bits steps to the neighbour with the same bits (quirk: bit 1 steps
   x - 1, 2 x + 1, 4 y + 1, 8 y - 1, not the -y / +y / -x / +x the bits mean elsewhere) and returns 1;
   otherwise a square spiral of up to 64 legs looks for a block with direction bits and moves q there
   (preferring one on the start's column or row). 0 when nothing is found. */
int map_find_nearest_road(uint8_t q[8]);
