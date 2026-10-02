/* The snapshot of the world the sound module reads each game frame (SndWorld, src/audio/audio.h): what
   Snd_GatherLoops 0x402690 and Music_UpdateRadio 0x40cde0 query through Cars_GetCount / Car_Get,
   Player_GetControlledKind / Id, Player_GetViewKind / Id, Crane_Get and the helicopter globals. */
#include "sndworld.h"
#include "../audio/audio.h"
#include "car.h"
#include "obj.h"
#include "player.h"
#include "trigger.h"

static SndCar cars[CAR_MAX];
static SndWorld world;

void snd_world_update(void)
{
    for (int i = 0; i < CAR_MAX; i++) car_fill_snd(i, &cars[i]);
    const Player *p = &g_players[g_player_local];
    world.ncars = CAR_MAX;
    world.cars = cars;
    world.player_kind = p->ctl_kind, world.player_id = p->ctl_id;
    world.view_kind = p->view_kind, world.view_id = p->view_id;
    world.ntrains = 0;   /* TODO(0x46e7c0): trains are not ported yet */
    world.trains = NULL;
    for (int i = 0; i < 4; i++) {
        const Crane *c = crane_get(i);
        const Obj *o = c->obj >= 0 && c->obj < OBJ_MAX ? &g_objs[c->obj] : NULL;   /* its object's sprite */
        world.cranes[i] = (SndCrane){ c->state, o ? o->spr.x : 0, o ? o->spr.y : 0 };
    }
    world.heli_state = 0;   /* TODO(0x501be0): the helicopter is not ported yet */
    world.leader = false;   /* TODO(0x44ef10): Ped_GetGroupLeaderPos */
    Snd_SetWorld(&world);
}
