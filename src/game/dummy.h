/* Mission dummy cars (0x473440-0x473b8f): a DUMMY car driven by an AI controller ("sentinel",
   0x507ea0, 0x98 bytes each, kind 9) to a block, with up to four followers in a convoy group. The
   controllers and their driving (path finding, traffic) are not ported: the record is handled as raw
   bytes at the original's offsets and the module calls are stubs (stubs.h). docs/missions.md. */
#pragma once
#include <stdint.h>

enum { DUMMY_GROUPS = 8, DUMMY_GROUP_CARS = 4 };

/* A convoy group (10 bytes): count, then the controllers of its cars. */
typedef struct {
    int16_t count;              /* +0 */
    int16_t ctrl[DUMMY_GROUP_CARS];   /* +2 */
} DummyGroup;
_Static_assert(sizeof(DummyGroup) == 10, "dummy group");
extern DummyGroup g_dummy_groups[DUMMY_GROUPS];   /* 0x771080 */

/* Controller record fields Dummy_* use (offsets into the 0x98-byte sentinel record):
   +0x00 u8 target (copied to +0x44 of the record as a u16 too), +0x02 u8 kind (9 dummy, 0 free),
   +0x0a i16 group, +0x0c u8 arrived, +0x1b u8 state (0xfa start, 2 path pending, 5 re-path,
   1 arrived, 0x27 path failed, 100..103 attack player n), +0x1c u8 sub state, +0x1e i16 car,
   +0x21 / +0x22 u8, +0x38 i32, +0x40 Car *, +0x44 i16, +0x64..+0x66 u8 destination block. */
enum {
    DUMMY_S_ARRIVED = 1, DUMMY_S_PATH_WAIT = 2, DUMMY_S_REPATH = 5, DUMMY_S_FAILED = 0x27,
    DUMMY_S_ATTACK0 = 100, DUMMY_S_START = 0xfa,
};

void dummy_init_groups(void);               /* Dummy_InitGroups 0x473440 */
/* Dummy_StartDrive 0x473460: car drives to the pixel position (x, y, z) in a new group; returns the
   controller (-1 none free) */
int dummy_start_drive(int car, int x, int y, int z);
/* Dummy_AddFollower 0x473570: car joins the group of `leader`'s controller; returns the controller */
int dummy_add_follower(int car, int leader);
/* Dummy_Update 0x473660: one step of a kind-9 controller (rec = the sentinel record) */
int dummy_update(uint8_t *rec);
