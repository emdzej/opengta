/* The scripted helicopter 0x40dc80-0x40e19f (heli.h). docs/police.md. */
#include "heli.h"
#include "carinfo.h"
#include "car.h"
#include "coll.h"
#include "event.h"
#include "game.h"
#include "gmath.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"

Heli g_heli;
static HeliPos heli_pos;                    /* 0x501bb0 */

/* The sine table read at an angle that Heli_TurnTowards may leave at -4..-1 or 0x400..0x403 (it masks
   the angle only when it turns again). Below the table the original reads the tan table that lies
   just before it in the exe's bss (0x511a28..0x511e27), so that is what a negative angle gives. */
static int32_t heli_sin(int a)
{
    return a < 0 ? g_tan[0x100 + a] : g_sin[a];
}
/* the cosine likewise: past the table's end (angles 0x400..0x403) lie game flags (0x513228..), taken
   as 0 */
static int32_t heli_cos(int a)
{
    return a + 256 < 0x500 ? g_sin[a + 256] : 0;
}

/* Heli_GetPos 0x40dc80 */
const HeliPos *heli_get_pos(void)
{
    heli_pos.speed = g_heli.speed;
    heli_pos.x = g_heli.spr.x;
    heli_pos.z = g_heli.spr.z;
    heli_pos.y = g_heli.spr.y;
    return &heli_pos;
}

/* Heli_Init 0x40dcc0 */
void heli_init(void)
{
    Heli *h = &g_heli;
    h->id = -1;
    h->tx = h->ty = h->tz = -1;
    h->angle0 = 0;
    h->u12 = -1;
    h->u14 = 0;
    h->state = 0;
    h->u1c = 0xff;
    h->u1e = 0;
    h->speed = 0;
    h->timer = 0;
}

/* Heli_SetExitTarget 0x40dd10 */
void heli_set_exit_target(int32_t x, int32_t y)
{
    g_heli.ex = x;
    g_heli.ey = y;
}

/* Heli_Spawn 0x40dd30: one at a time (a second is fatal -0xf8); the shadow object (type 0x5c) 17
   pixels ahead along the heading, a block below (none: fatal -0xf7); the sprite is the car info
   sprite of model 88; the heli goes into the grid as kind 0x1e. */
void heli_spawn(int32_t x, int32_t y, int32_t z, int angle, int32_t tx, int32_t ty, int32_t tz)
{
    Heli *h = &g_heli;
    if (h->id != -1) game_fatal(-0xf8, 0x155, 0);
    h->tx = tx, h->ty = ty;
    h->angle0 = (int16_t)angle;
    h->tz = tz;
    h->u12 = -1;
    h->u1c = 0xff;
    h->u14 = 0, h->u1e = 0;
    h->speed = 0, h->timer = 0;
    h->id = 0;
    h->spr.zkey = z;
    h->state = 1;
    h->shadow = (int16_t)obj_create(x + math_sin(h->angle0) * 0x11, y + math_cos(h->angle0) * 0x11, z, 0x5c, angle);
    if (h->shadow < 0) game_fatal(-0xf7, 0x155, 0);
    Obj *o = obj_get(h->shadow);
    const uint8_t *info = car_info_of_model(88);
    sprite_init(&h->spr, x, y, z, angle, info ? carinfo_s16(info, 6) : 0);
    coll_insert(COLL_HELI, 0, h, h->spr.unk20, x, y);
    o->spr.z = h->spr.z - 0x10000;
    o->u12 = 1;
}

/* Heli_TurnTowards 0x40de90: the heading turns toward (x, y) by 4 a frame (the rest of the
   difference when it is 4 or less); 1 when it points there, -1 while turning. */
int heli_turn_towards(Heli *h, int32_t x, int32_t y)
{
    int a = math_atan2(y - h->spr.y, x - h->spr.x);
    unsigned d = (unsigned)(a - (uint16_t)h->spr.angle) & 0x3ff;
    if (d == 0) return 1;
    int cur = (uint16_t)h->spr.angle & 0x3ff;
    h->spr.angle = (int16_t)cur;
    if (d > 4 && d < 0x3fc) {
        h->spr.angle = (int16_t)(d < 0x200 ? cur + 4 : cur - 4);
        return -1;
    }
    if (d < 0x200) h->spr.angle = (int16_t)(cur + d);
    else h->spr.angle = (int16_t)(cur - 0x400 + d);
    return -1;
}

/* Heli_UpdateShadow 0x40df30: the shadow 17 pixels ahead along the heading, a block below */
void heli_update_shadow(Heli *h)
{
    Obj *o = obj_get(h->shadow);
    coll_remove(o, o->spr.unk20);
    int32_t y = heli_cos(h->spr.angle) * 0x11 + h->spr.y;
    int32_t x = heli_sin(h->spr.angle) * 0x11 + h->spr.x;
    o->spr.x = x;
    o->spr.y = y;
    o->spr.z = h->spr.z - 0x10000;
    o->spr.zkey = 0;
    coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, x, y);
}

/* Player_SetViewFixed4 0x462d00 is in stubs.c until the player module has it */
void player_set_view_fixed4(int32_t x, int32_t y, int32_t z, int n);

/* Heli_Update 0x40dfb0, by state:
     1 turn toward the landing point, then speed 5;
     2 fly (speed up to 15) until within a block of it;
     3 slow down (to 5, by 2 over the block itself, to 0), turn again, descend 2 pixels a frame to 66
       pixels above the ground;
     4 wait 40 frames, then take the player: his ped leaves the grid, his camera follows the heli
       (target kind 5);
     5 climb to z = 2 (0x20000) while turning to the exit point; there the shadow's depth key 0;
     6 fly (speed up to 39) until within a block of the exit point;
     7 the camera stays where it is (kind 4) and the level ends 30 frames later (Event_ScheduleExit
       with 1: mission passed); 8 done.
   Every frame it moves `speed` pixels along its heading and the shadow follows. */
void heli_update(void)
{
    Heli *h = &g_heli;
    int bx = h->spr.x >> 22, by = h->spr.y >> 22;
    int tbx = h->tx >> 22, tby = h->ty >> 22;
    if (h->id == -1) return;
    Obj *o = obj_get(h->shadow);
    switch (h->state) {
    case 1:
        if (heli_turn_towards(h, h->tx, h->ty) == 1) {
            h->speed = 5;
            h->state = 2;
        }
        break;
    case 2:
        if (bx < tbx - 1 || bx > tbx + 1 || by < tby - 1 || by > tby + 1) {
            if (h->speed < 0xf) h->speed++;
        } else {
            h->state = 3;
        }
        break;
    case 3: {
        int32_t g = map_get_ground_z(g_game.map, h->spr.x, h->spr.y, h->spr.z);
        if (bx == tbx && by == tby && h->speed > 0) {
            h->speed -= 2;
            if (h->speed < 0) h->speed = 0;
            else if (h->speed > 0) break;
        } else {
            if (h->speed > 5) h->speed--;
            if (h->speed > 0) break;
        }
        if (heli_turn_towards(h, h->tx, h->ty) == 1) {
            if (h->spr.z < g - 0x420000) {
                h->spr.z += 0x20000;
                h->spr.zkey = h->spr.z;
            } else {
                h->state = 4;
                h->timer = 0x28;
            }
        }
        break;
    }
    case 4:
        if (h->timer < 1) {
            h->state = 5;
            Ped *p = ped_get(player_get_ped(0));
            coll_remove(p, p->spr.unk20);
            player_set_view_target(0, 5, 0);
            camera_start_transition(0);
        } else {
            h->timer--;
        }
        break;
    case 5:
        if (h->spr.z < 0x20001) {
            if (heli_turn_towards(h, h->ex, h->ey) == 1) {
                h->spr.z = 0x20000;
                h->spr.zkey = 1;
                o->spr.zkey = 0;
                h->state = 6;
                h->speed = 5;
            }
        } else {
            h->spr.z -= 0x20000;
            h->spr.zkey = h->spr.z;
            heli_turn_towards(h, h->ex, h->ey);
        }
        break;
    case 6: {
        int ebx = h->ex >> 22, eby = h->ey >> 22;
        if (bx < ebx - 1 || bx > ebx + 1 || by < eby - 1 || by > eby + 1) {
            if (h->speed < 0x27) h->speed++;
        } else {
            h->state = 7;
        }
        break;
    }
    case 7:
        player_set_view_fixed4(h->spr.x, h->spr.y, h->spr.z, 0);
        event_schedule_exit(0x1e, 1);
        h->state = 8;
        break;
    }
    int32_t nx = heli_sin(h->spr.angle) * (int16_t)h->speed + h->spr.x;
    int32_t ny = heli_cos(h->spr.angle) * (int16_t)h->speed + h->spr.y;
    coll_remove(h, h->spr.unk20);
    h->spr.x = nx;
    h->spr.y = ny;
    coll_insert(COLL_HELI, 0, h, h->spr.unk20, nx, ny);
    heli_update_shadow(h);
}
