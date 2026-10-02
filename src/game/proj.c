/* Projectiles (0x4879b0-0x488e1f): see proj.h. */
#include "proj.h"
#include "mission.h"
#include "ped_internal.h"
#include "trigger.h"
#include <stdlib.h>

ProjList g_proj;
static CollBox proj_box;                     /* 0x7852d0 */
/* Proj_UpdateAll's local 0x2b: whether a bullet passes through a car. Cleared at the start of each call
   only, so a value set for one projectile carries over to the next ones. */
static bool proj_through;


/* the ped that fired: the original reads the record of a negative id before the table; the port
   reads a cleared record instead */
static Ped *shooter(int id)
{
    static Ped none;
    return id >= 0 && id < PED_MAX ? &g_peds[id] : &none;
}

static int bk(int32_t v) { return v >> 22; }

/* Proj_Reset 0x4879b0 (Game_Init): the list emptied */
void proj_reset(void) { g_proj.count = 0; }

/* Proj_Remove 0x4879c0: only for a spent projectile (life 0, which becomes 1: a second call does
   nothing); its id leaves the list (the last id takes its place) and the object is deleted
   (Obj_RemoveMoving). With one in the list the count drops to 0 whatever the id. */
void proj_remove(int obj)
{
    Obj *o = obj_get(obj);
    if (o->life != 0) return;
    o->life = 1;
    if (g_proj.count < 2) {
        if (g_proj.count == 1) {
            g_proj.count = 0;
            if ((int16_t)obj == g_proj.id[0]) {
                g_proj.id[0] = -1;
                obj_remove_moving(obj);
            }
        }
        return;
    }
    int n = g_proj.count;
    for (int i = 0; i < n; i++) {
        if ((int16_t)obj != g_proj.id[i]) continue;
        g_proj.count = (int16_t)(n - 1);
        g_proj.id[i] = g_proj.id[n - 1];
        g_proj.id[n - 1] = -1;
        obj_remove_moving(obj);
        n = g_proj.count;
    }
}

/* Proj_Detonate 0x488c70: a projectile that met a building or a wall (block bx, by, bz): a rocket
   explodes on the block's faces that are solid (Expl_AtFaceIfSolid, faces 0..3) or, with none,
   where it is; a bullet leaves a spark (object 0xd) with sound 0x2b; a flame just goes out. */
static void proj_detonate(Obj *o, int bx, int by, int bz)
{
    o->life = 0;
    int pl = player_find_by_ped(o->u1e);
    int kind = o->weight;
    if (kind == PROJ_ROCKET) {
        bool a = expl_at_face_if_solid(bx, by, bz, 0, pl);
        bool b = expl_at_face_if_solid(bx, by, bz, 1, pl);
        bool c = expl_at_face_if_solid(bx, by, bz, 2, pl);
        bool d = expl_at_face_if_solid(bx, by, bz, 3, pl);
        if (!a && !b && !c && !d) expl_create(o->spr.x, o->spr.y, o->spr.z, pl);
    } else if (kind == PROJ_BULLET) {
        obj_create(o->spr.x, o->spr.y, o->spr.z, 0xd, 0);
        Snd_PlayAt(o->spr.x, o->spr.y, o->spr.z, 0x2b);
    } else if (kind != PROJ_FLAME) {
        game_fatal(-0x4a, 0x17a, kind);
    }
}

/* Proj_UpdateShadowZ 0x488db0: the depth key: half a block above the projectile normally, but 25
   blocks' worth (0x640000) when its block or the one below is a slope (so it draws over the
   slope); at least 1. A negative z counts as layer 0. */
static void proj_update_shadow_z(Sprite *s)
{
    int z = s->z;
    int l = z < 0 ? 0 : bk(z);
    int8_t below = 0;
    if (l < 5) below = (int8_t)ped_type_cache(bk(s->x), bk(s->y), l + 1);
    int32_t k = ((int8_t)ped_type_cache(bk(s->x), bk(s->y), l) < 0 || below < 0) ? z - 0x640000 : z - 0x100000;
    s->zkey = k;
    if (k < 1) s->zkey = 1;
}

/* something solid (kind 10, the trains) in the box: a bullet sparks */
static bool hit_kind10(Obj *o, CollBox *box)
{
    CollHit *h = coll_query_box_first(box, COLL_KIND10, 3, o->id);
    coll_unlock();
    if (!h) return false;
    int kind = o->weight;
    if (kind != PROJ_ROCKET) {
        if (kind == PROJ_BULLET) {
            obj_create(o->spr.x, o->spr.y, o->spr.z, 0xd, 0);
            Snd_PlayAt(o->spr.x, o->spr.y, o->spr.z, 0x2b);
        } else if (kind != PROJ_FLAME) {
            game_fatal(-0x4a, 0x17c, kind);
        }
    }
    return true;
}

/* a power-up box (object 0x54) in the way is revealed */
static bool hit_powerup_box(Obj *o, CollBox *box)
{
    CollHit *h = coll_query_box(box, COLL_OBJECT, 3, o->id);
    if (h) {
        const Obj *b = h->owner;
        if (b->type == 0x54) {
            powerup_reveal(b->spr.x, b->spr.y);
            coll_unlock();
            return true;
        }
    }
    coll_unlock();
    return false;
}

/* the objectives that make peds allies for the bullet test (gang members and the like) */
static bool objective_group_a(int obj)
{
    switch (obj) {
    case 0x18: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x22: case 0x30: case 0x39:
        return true;
    }
    return false;
}

/* the ground under a projectile: it never stays more than 3 pixels above it (and is pulled down to
   that when the ground is up to half a block below) */
static void hug_ground(Obj *o)
{
    int32_t gz = ped_ground_z(o->spr.x, o->spr.y, o->spr.z - 0x200000);
    if (gz - 0x30000 < o->spr.z + 0x200000) o->spr.z = gz - 0x30000;
    proj_update_shadow_z(&o->spr);
}

/* a rocket: hits peds (not with the shooter's flag 0x4a), cars (damage 25, the driver killed, score
   100 when it wasn't wrecked yet), objects; it explodes on anything (its life never counts down) */
static bool update_rocket(Obj *o, int pl_top)
{
    (void)pl_top;
    hug_ground(o);
    CollBox *box = coll_build_box(o->spr.x, o->spr.y, o->spr.z, 4, o->speed, o->spr.angle, 0x10, &proj_box);
    bool hit = false;
    if (o->life >= 0x19) {
        o->life = 0;
        int32_t gz = ped_ground_z(o->spr.x, o->spr.y, o->spr.z);
        expl_create(o->spr.x, o->spr.y, gz - 0x20000, player_find_by_ped(o->u1e));
        return true;
    }
    if (++o->state == 4) o->state = 1;
    sprite_set_frame(&o->spr, g_obj_infos[o->type]->spr_num + o->state);
    if (o->speed < 0xf) o->speed += 3;
    const Ped *sh = shooter(o->u1e);
    CollHit *h = coll_query_box(box, COLL_OBJECT, 3, o->id);
    if (!h) {
        coll_unlock();
        h = coll_query_box_first(box, COLL_PED, 3, o->id);
        if (h) {
            coll_unlock();
            for (; h; h = h->next) {
                const Ped *q = h->owner;
                if (q->carried < 0 && q->id != o->u1e && q->health > 0 && sh->u4a == 0 && q->state != 7 &&
                    q->u7c != 0x11 && q->u7c != 0x13) {
                    expl_create(o->spr.x, o->spr.y, o->spr.z, player_find_by_ped(o->u1e));
                    hit = true;
                    break;
                }
            }
        }
        coll_unlock();
    } else {
        const Obj *b = h->owner;
        if (b->type == 0x54) {
            powerup_reveal(b->spr.x, b->spr.y);
            coll_unlock();
            expl_create(o->spr.x, o->spr.y, o->spr.z, player_find_by_ped(o->u1e));
            o->life = 0;
            hit = true;
        } else {
            coll_unlock();
        }
    }
    coll_unlock();
    h = coll_query_box_first(box, COLL_CAR, 3, o->id);
    coll_unlock();
    if (h) {
        Car *c = h->owner;
        if (c->driver == -1 || c->driver != o->u1e) {
            int16_t old = c->damage;
            int score = 0;
            c->damage = (int16_t)(old + 0x19);
            if (c->damage > 99 || c->model != 0x25) {   /* (model 0x25 takes 4 rockets) */
                c->damage = 100;
                if (o->u1e >= 0) c->player = (int16_t)player_find_by_ped(o->u1e);
            }
            if (c->model != 0x2f) {
                if (c->driver != -1) {
                    ped_set_health(c->driver, 0);
                    Ped *d = &g_peds[c->driver];
                    if (d->u5a == -1) d->u5a = o->u1e;
                }
                int pl = player_find_by_ped(o->u1e);
                if (old < 100) {
                    if (pl != -1) {
                        if (o->weight == PROJ_ROCKET) score = 100;
                        else game_fatal(-0x4a, 0x17b, o->weight);
                        if (c->damage != 100) player_add_score(pl, score, o->spr.x, o->spr.y, o->spr.z, 1);
                    }
                    expl_create(o->spr.x, o->spr.y, o->spr.z, pl);
                } else {
                    expl_create(o->spr.x, o->spr.y, o->spr.z, pl);
                    c->damage = old;
                }
                o->life = 0;
                hit = true;
            }
        }
    }
    if (hit_kind10(o, box)) {
        o->life = 0;
        hit = true;
    }
    return hit;
}

/* a bullet hits a ped: a spark (remap 0xf); an armoured player loses armour; a ped not already hit
   falls (anim 0x8d shot from behind / 0x89 from the front, state 5) and the shooter is reported */
static bool bullet_vs_ped(Obj *o, CollHit *h)
{
    Ped *q = h->owner;
    int16_t me = o->u1e;
    if (q->id == me) {
        while (h) {   /* the shooter in its own box: the last ped of the list instead */
            q = h->owner;
            h = h->next;
        }
        if (q->id == me) return false;
    }
    if (q->state == 7 || q->u7c == 0x13 || q->u7c == 0x11 || !(q->health > 0 || q->state == 9) ||
        (q->graphic == 2 && q->car != -1))
        return false;
    const Ped *sh = shooter(me);
    bool shooter_player = false, victim_ally = objective_group_a(q->objective), shooter_ally = false;
    bool victim_mission = false;
    switch (q->objective) {
    case 0x25: case 0x29: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: victim_mission = true; break;
    }
    if (sh->player_ctl == 1) {
        shooter_player = true;
        if (q->player_ctl == 1) victim_mission = false;
    }
    switch (sh->objective) {
    case 0x18: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x22: case 0x30: case 0x39:
        shooter_ally = true;
        break;
    case 0x29: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        shooter_player = true;
        break;
    }
    if ((q->graphic != 1 || sh->graphic != 1) && (!victim_ally || !shooter_ally) &&
        (!victim_mission || !shooter_player)) {
        int s = obj_create(o->spr.x, o->spr.y, o->spr.z, 0xd, 0);
        if ((int16_t)s >= 0) obj_get(s)->spr.remap = 0xf;
        if (q->u4a == 1) {
            if (q->objective == 0x25) player_dec_armour(player_find_by_ped(q->id));
        } else if (q->anim < 0x89) {
            if ((abs(q->spr.angle - o->spr.angle) & 0x3ff) < 0x101) {
                q->anim = 0x8d;
                q->spr.angle = o->spr.angle;
            } else {
                q->anim = 0x89;
                q->spr.angle = (int16_t)((o->spr.angle - 0x200) & 0x3ff);
            }
            q->state = 5;
            q->u7c = 2;
            q->attach_kind = 0;
            q->attach_id = 0;
            q->speed = (int16_t)(0x19 - o->life);
            q->u5a = me;
            police_report_crime(1, me, 8, q->spr.x, q->spr.y, q->spr.z);
        }
        o->life = 0;
    }
    return true;
}

/* a bullet hits a car: who may damage it (the police only cars of players, gang members only
   players' cars, ...), damage +2 (buses, model 0x1a) or +5 up to 75, the wanted level a player gets
   for shooting a police car (model 4), score 10; a stopped AI car's driver spins or gets out; at 75+
   the car is wrecked (100, score 150). Returns false when the bullet passes (local 0x2b). */
static bool bullet_vs_car(Obj *o, Car *c, int pl_top, bool *pass)
{
    const Ped *sh = shooter(o->u1e);
    bool damage = true;
    bool through = proj_through;
    if (sh->control == 1) {
        if (c->model != 4 && c->model != 0x20) goto other;
    own:
        if (c->driver < 0) {
            if (sh->id != c->driver) {
                int16_t cc = sh->car;
                if (cc != sh->id && cc != c->id) goto check;
            }
            proj_through = true;
            *pass = true;
            return false;
        }
        if (g_peds[c->driver].player_ctl == 1) {
            through = false;
        } else {
            if (sh->objective != 0x33) through = true;
            if (c->model == 0x2f) through = false;
            else goto check;
        }
    } else {
    other:
        if (c->model == 0x2f) goto own;
        switch (sh->objective) {
        case 0x18: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x29:
        case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            damage = false;
            if (c->driver == -1) goto check;
            if (g_peds[c->driver].player_ctl != 1) {
                through = false;
                break;
            }
            if (c->control == 0) goto check;
            through = false;
            damage = true;
            break;
        case 0x30: case 0x39:
            through = false;
            break;
        default:
        check:
            proj_through = through;
            if (through) {
                *pass = true;
                return false;
            }
            break;
        }
    }
    proj_through = through;
    int16_t dmg = c->damage;
    if (dmg < 0x4b) {
        if (damage) {
            dmg = (int16_t)(dmg + (c->vtype == 0 || c->vtype == 1 || c->vtype == 2 || c->model == 0x1a ? 2 : 5));
            c->damage = dmg;
            Snd_PlayAt(c->spr.x, c->spr.y, c->spr.z, 0x2b);
        }
        if (sh->objective == 0x25 && c->model == 4) {
            int pl = player_find_by_ped(sh->id);
            if (player_get_wanted_level(pl) == 0) {
                int16_t wp = (int16_t)player_get_wanted_points_idx(player_find_by_ped(sh->id));
                switch (mission_get_ini_section()) {
                case 1: case 2:
                    if (wp < 0x98) {
                        player_set_wanted_level(sh->id, 1);
                        player_clear_wanted_points(sh->id);
                        player_add_wanted_points(sh->id, 0x98);
                    }
                    break;
                default:
                    if (wp < 0x66) {
                        player_set_wanted_level(sh->id, 1);
                        player_clear_wanted_points(sh->id);
                        player_add_wanted_points(sh->id, 0x66);
                    }
                }
            }
        }
        police_report_crime(1, o->u1e, 7, c->spr.x, c->spr.y, c->spr.z);
        obj_create(o->spr.x, o->spr.y, o->spr.z, 0xd, 0);
        int pl = player_find_by_ped(o->u1e);
        if (pl > -1) player_add_score(pl, 10, c->spr.x, c->spr.y, c->spr.z, 1);
        if (c->control == 0) {
            if (c->speed < 1) {
                if (c->driver != -1) {
                    Ped *d = &g_peds[c->driver];
                    if ((int16_t)math_random() < 0x2711) {
                        if (d->player_ctl != 1) carphys_begin_spin(c->id);
                    } else {
                        coll_unlock();
                        d->target_ped = o->u1e;
                        if (d->player_ctl != 1 && c->script_line < 0) ped_driver_leave_car(c->id);
                    }
                }
            } else {
                c->input = 0x14;
            }
        }
    } else if (dmg < 100) {
        c->player = (int16_t)pl_top;
        c->damage = 100;
        police_report_crime(1, o->u1e, 7, c->spr.x, c->spr.y, c->spr.z);
        int pl = player_find_by_ped(o->u1e);
        if (pl > -1) player_add_score(pl, 0x96, c->spr.x, c->spr.y, c->spr.z, 1);
    } else {
        obj_create(o->spr.x, o->spr.y, o->spr.z, 0xd, 0);
    }
    o->life = 0;
    return true;
}

/* a bullet: peds, cars, trains; its life counts down from 20 */
static bool update_bullet(Obj *o, int pl_top)
{
    hug_ground(o);
    CollBox *box = coll_build_box(o->spr.x, o->spr.y, o->spr.z, 2, o->speed, o->spr.angle, 0x10, &proj_box);
    bool hit = false;
    if (hit_powerup_box(o, box)) {
        o->life = 0;
        hit = true;
    }
    CollHit *h = coll_query_box_first(box, COLL_PED, 3, o->id);
    if (h && bullet_vs_ped(o, h)) hit = true;
    coll_unlock();
    h = coll_query_box_first(box, COLL_CAR, 3, o->id);
    if (h) {
        bool pass = false;
        if (bullet_vs_car(o, h->owner, pl_top, &pass)) hit = true;
    }
    coll_unlock();
    if (hit_kind10(o, box)) {
        o->life = 0;
        hit = true;
    }
    if (--o->life < 1) hit = true;
    return hit;
}

/* a flame: 10 frames of animation; cars take 15 damage (an AI driver flees the car), peds catch
   fire (object 0x2e attached, they run, Score_PedKilled for a player's flame) */
static bool update_flame(Obj *o, int pl_top)
{
    hug_ground(o);
    CollBox *box = coll_build_box(o->spr.x, o->spr.y, o->spr.z, 4, o->speed, o->spr.angle, 0x10, &proj_box);
    if (++o->state == 0xb) {
        o->life = 0;
        return true;
    }
    sprite_set_frame(&o->spr, g_obj_infos[o->type]->spr_num + o->state);
    if (o->speed < 7) {
        o->speed += 2;
        if (o->speed > 7) o->speed = 7;
    }
    const Ped *sh = shooter(o->u1e);
    bool hit = false;
    if (hit_powerup_box(o, box)) {
        o->life = 0;
        hit = true;
    }
    CollHit *h = coll_query_box_first(box, COLL_CAR, 3, o->id);
    if (h) {
        Car *c = h->owner;
        /* (the car's id is compared with the shooter's ped id) */
        if (c->model != 0x2f && c->id != o->u1e && c->damage < 100) {
            c->damage = (int16_t)(c->damage + 0xf);
            if (c->damage > 99) {
                c->damage = 100;
                c->player = (int16_t)pl_top;
            }
            if (c->driver != -1 && c->control != 1 && c->script_line < 0 && c->model != 4) {
                coll_unlock();
                ped_driver_leave_car(c->id);
                CAR_I16(c, 0xb6) = 1;
            }
            o->life = 0;
            coll_unlock();
            int pl = player_find_by_ped(sh->id);
            if (pl > -1) player_add_score(pl, 10, o->spr.x, o->spr.y, o->spr.z, 1);
            return true;
        }
    }
    coll_unlock();
    for (h = coll_query_box_first(box, COLL_PED, 3, o->id); h; h = h->next) {
        Ped *q = h->owner;
        if (q->carried < 0 && q->id != o->u1e && q->health > 0 && q->state != 7 && q->u7c != 0x11 && q->u7c != 0x13) {
            q->carried = (int16_t)obj_create_animated(q->spr.x, q->spr.y, q->spr.z - 0x10000, 0x2e, q->id, 2);
            q->u5a = o->u1e;
            Snd_PlayAt(q->spr.x, q->spr.y, q->spr.z, 0x18);
            if (q->player_ctl != 1) {
                q->state = 1;
                q->target_ped = -1;
                if (o->u1e < 0) {
                    q->target_x = q->spr.x;
                    q->target_y = q->spr.y;
                } else {
                    const Ped *s2 = &g_peds[o->u1e];
                    q->target_x = s2->spr.x;
                    q->target_y = s2->spr.y;
                    q->spr.angle = s2->spr.angle;
                }
                q->speed = 4;
                police_report_crime(1, o->u1e, 8, q->spr.x, q->spr.y, q->spr.z);
            }
            hit = true;
            if (player_find_by_ped(o->u1e) > -1) score_ped_killed(q, 8);
        }
    }
    coll_unlock();
    if (hit_kind10(o, box)) {
        o->life = 0;
        hit = true;
    }
    return hit;
}

/* Proj_UpdateAll 0x487a70: for each projectile in the list: nearby peds panic (Ped_PanicNear); one
   that stopped animating or left the map (blocks 1..254) is removed; else it moves its speed along
   its heading (back into the grid, after its +0x21 frames), and by kind does its hits; then a
   building block or a map wall makes it detonate there, a slope (Coll_MapSlopesEx) ends it, and a
   projectile that hit something is removed. Removing moves the last id into the current slot,
   which the loop then skips this frame (as the original does). */
void proj_update_all(void)
{
    proj_through = false;
    for (int i = 0; i < g_proj.count; i++) {
        Obj *o = obj_get(g_proj.id[i]);
        bool hit = false;
        ped_panic_near(o->spr.x, o->spr.y, o->spr.z, o->u1e);
        int pl_top = player_find_by_ped(o->u1e);
        if (o->state == 0 || bk(o->spr.x) < 1 || bk(o->spr.x) > 0xfe || bk(o->spr.y) < 1 || bk(o->spr.y) > 0xfe) {
            o->life = 0;
            o->life = 0;
            proj_remove(g_proj.id[i]);
            continue;
        }
        coll_remove(o, o->spr.unk20);
        int32_t ny = o->spr.y;
        int32_t x = SIN(o->heading) * o->speed + o->spr.x, y = COS(o->heading) * o->speed + ny;
        o->spr.x = x;
        o->spr.y = y;
        if (o->u21 == 0) coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, x, y);
        else o->u21--;
        int kind = o->weight;
        if (kind == PROJ_ROCKET) hit = update_rocket(o, pl_top);
        else if (kind == PROJ_BULLET) hit = update_bullet(o, pl_top);
        else if (kind == PROJ_FLAME) hit = update_flame(o, pl_top);
        int bx = bk(o->spr.x) & 0xff, by = bk(o->spr.y) & 0xff, bz = o->spr.z < 0 ? 0 : bk(o->spr.z) & 0xff;
        if (ped_ground_type(bx, by, bz) == 5) {
            proj_detonate(o, bx, by, bz);
            hit = true;
        } else {
            coll_compute_bounds(&proj_box);
            int fx, fy, fz;
            if (coll_map_walls_ex(&proj_box, 0, &fx, &fy, &fz) != -1) {
                proj_detonate(o, fx, fy, fz);
                hit = true;
            }
        }
        coll_compute_bounds(&proj_box);
        if (coll_map_slopes_ex(&proj_box) != -1 || hit) {
            o->life = 0;
            proj_remove(g_proj.id[i]);
        }
    }
}
