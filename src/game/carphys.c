/* The float rigid-body car model, carphys 0x4607b0-0x4616af (see carphys.h for the x87 precision
   rules this file follows, and docs/cars.md for the model).

   Notation in the comments: "D" is a value kept in an x87 register (rounded to double at 53-bit
   precision), "F" one stored to a float. Every expression below keeps the original's operand order;
   (float) casts mark the stores. */
#include "carphys.h"
#include "car.h"
#include "carcoll.h"
#include "carinfo.h"
#include "game.h"
#include "gmath.h"
#include "player.h"
#include <math.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

float g_carphys_skid;                       /* 0x74f128 */
static float carphys_74f124;                /* 0x74f124 (cleared, never read here) */

/* the constants of the module (instruction operands in the original: floats and doubles at 0x4a7xxx /
   0x4a8bxx / 0x4b21d4) */
#define K_HALF_PI 1.570796326795            /* 0x4a8bd0 (double, not exactly pi / 2) */
#define K_STEER_MAX 2.443796326795          /* 0x4a8c08 */
#define K_STEER_MIN 0.873                   /* 0x4a8c00 */
#define K_COS_LIMIT 0.642                   /* 0x4a8c10 */
#define K_CENTRE 0.139                      /* 0x4a8be8 (and -0.139 at 0x4a8be0) */
#define K_ALIGN 0.0278                      /* 0x4a8bf0 (stored as the double 0.027800000000000002) */
#define K_BOUNCE 0.625                      /* 0x4a8c18 */
static const float k_heading_rad = 0.006135923322290182f;   /* 0x4b21d4 (float 2pi / 1024) */
static const float k_rad_heading = 162.9746551513672f;      /* 0x4b21d8 (float 1024 / 2pi) */

static double x87_sin(double v) { return sin(v); }   /* fsin (see carphys.h on the last bit) */
static double x87_cos(double v) { return cos(v); }   /* fcos */

int32_t x87_ftol(double v)
{
    /* fistp qword with truncation; out-of-range values give the "integer indefinite" 0x8000000000000000 */
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) return 0;
    return (int32_t)(uint32_t)(uint64_t)(int64_t)v;
}

float car_heading_to_angle(int heading)
{
    /* fild heading; fmul dword 0x4b21d4; fchs; fstp dword */
    return (float)-((double)(int16_t)heading * (double)k_heading_rad);
}

/* Phys_AddForceAtPoint 0x4607b0 */
void phys_add_force_at_point(PhysBody *b, int mode, float px, float py, float fx, float fy)
{
    float wx, wy;
    switch (mode & 0xff) {
    case 0:
        wx = px, wy = py;
        break;
    case 1: {
        double c = x87_cos(b->angle), s = x87_sin(b->angle);
        float t = (float)((double)px * c - (double)py * s);
        double u = (double)px * s + (double)py * c;
        wx = (float)((double)b->cx + t);
        wy = (float)((double)b->cy + u);
        break;
    }
    default:
        wx = b->x, wy = b->y;
        break;
    }
    b->fx = (float)((double)fx + b->fx);
    b->fy = (float)((double)fy + b->fy);
    b->torque = (float)(((double)wx - b->x) * fy - ((double)wy - b->y) * fx + b->torque);
}

/* Phys_Integrate 0x460880: semi-implicit Euler (velocity first, then the position with the new
   velocity), then the geometric centre from the new angle; the accumulators are cleared. The sums
   that feed the position stay in registers (unrounded) except the angle's, which goes through the
   float vw store. */
void phys_integrate(PhysBody *b)
{
    float comx = b->comx, comy = b->comy;
    double vx = (double)b->vx + b->ax;          /* D */
    b->vx = (float)vx;
    double vy = (double)b->ay + b->vy;
    b->vy = (float)vy;
    double w = (double)b->w + b->aw;
    float wf = (float)w;
    b->w = wf;
    b->x = (float)(vx + b->x);
    b->y = (float)(vy + b->y);
    double a = (double)wf + b->angle;
    b->angle = (float)a;
    double c = x87_cos(a), s = x87_sin(a);
    double nx = -(double)comx, ny = -(double)comy;
    float t = (float)(nx * c - ny * s);
    double v = nx * s + ny * c;
    b->cx = (float)((double)b->x + t);
    b->ax = 0, b->ay = 0, b->torque = 0, b->aw = 0, b->fx = 0;
    b->cy = (float)((double)b->y + v);
    b->fy = 0;
}

/* Phys_PointVelocity 0x460950: the point's world position now (from the centre of mass x, y) and
   after one step of v and w, subtracted. The y of the future point goes through a float before the
   rotated offset is added; the x doesn't (kept as in the original). */
void phys_point_velocity(const PhysBody *b, float px, float py, float *rx, float *ry)
{
    double dx = (double)px - b->comx, dy = (double)py - b->comy;
    double c = x87_cos(b->angle), s = x87_sin(b->angle);
    float p1 = (float)(c * dx - s * dy);
    double q = s * dx + c * dy;
    float w0x = (float)((double)b->x + p1);
    float w0y = (float)((double)b->y + q);
    double a2 = (double)b->w + b->angle;
    float c2 = (float)x87_cos(a2);
    double s2 = x87_sin(a2);
    float p2 = (float)((double)c2 * dx - s2 * dy);
    double q2 = s2 * dx + (double)c2 * dy;
    double x1 = (double)b->vx + b->x;
    float y1 = (float)((double)b->vy + b->y);
    float w1y = (float)((double)y1 + q2);
    *rx = (float)((x1 + p2) - w0x);
    *ry = (float)((double)w1y - w0y);
}

/* Phys_BounceX/Y/XY 0x461450 / 0x4614a0 / 0x4613c0: a component whose truncated magnitude is below 1
   is zeroed, else scaled by 0.625; then negated (so a zeroed component becomes -0.0). */
static void bounce(float *v)
{
    int32_t i = x87_ftol(*v);
    i = (i ^ (i >> 31)) - (i >> 31);
    if ((double)i < 1.0) *v = 0;
    else *v = (float)((double)*v * K_BOUNCE);
    *v = -*v;
}
void phys_bounce_xy(PhysBody *b) { bounce(&b->vx); bounce(&b->vy); }
void phys_bounce_x(PhysBody *b) { bounce(&b->vx); }
void phys_bounce_y(PhysBody *b) { bounce(&b->vy); }

/* Phys_SyncFromCentre 0x4614f0 */
void phys_sync_from_centre(PhysBody *b)
{
    double c = x87_cos(b->angle), s = x87_sin(b->angle);
    float comx = b->comx;
    double comy = b->comy;
    float t = (float)((double)comx * c - comy * s);
    double u = c * comy + (double)comx * s;
    b->x = (float)((double)b->cx + t);
    b->y = (float)((double)b->cy + u);
}

/* CarPhys_LoadParams 0x460ad0 */
void carphys_load_params(Car *c)
{
    const uint8_t *in = c->info;
    PhysBody *b = &c->body;
    b->comx = (float)(int8_t)in[0x76];
    b->comy = (float)(int8_t)in[0x77];
    b->inertia = (float)(int32_t)(in[0x78] | in[0x79] << 8 | in[0x7a] << 16 | (uint32_t)in[0x7b] << 24);
    b->mass = carinfo_float(in, 0x7c);
    c->thrust = carinfo_float(in, 0x80);
    b->cx = (float)(int16_t)(c->spr.x >> 16);
    b->vx = 0, b->vy = 0, b->w = 0, b->ax = 0;
    b->cy = (float)(int16_t)(c->spr.y >> 16);
    b->ay = 0, b->aw = 0, b->fx = 0, b->fy = 0, b->torque = 0;
    c->gear = 0;
    c->thrust_in = 0;
}

/* the steering angle of a straight wheel: the body angle (stored), + pi/2 from the register */
static void reset_angles(Car *c)
{
    double a = -((double)c->spr.angle * (double)k_heading_rad);
    c->body.angle = (float)a;
    c->steer = (float)(a + K_HALF_PI);
}

/* the rear wheel point from the heading (CarPhys_Begin, CarPhys_BeginSpin) */
static void set_rear_point(Car *c)
{
    int h = c->length >> 1;
    c->rear_x = math_sin(c->spr.angle) * h + c->spr.x;
    c->rear_y = math_cos(c->spr.angle) * h + c->spr.y;
}

/* CarPhys_Begin 0x460a20 */
void carphys_begin(int n)
{
    Car *c = car_get(n);
    carphys_load_params(c);
    reset_angles(c);
    c->physics = 1;
    set_rear_point(c);
}

/* CarPhys_End 0x460ab0 */
void carphys_end(int n) { car_get(n)->physics = 0; }

/* CarPhys_Reset 0x460b90 */
void carphys_reset(int n)
{
    Car *c = car_get(n);
    carphys_load_params(c);
    reset_angles(c);
}

/* CarPhys_BeginSpin 0x4615e0: CarPhys_Begin, then the wheel turned fully to one side at random */
void carphys_begin_spin(int n)
{
    Car *c = car_get(n);
    carphys_begin(c->id);
    if ((int16_t)math_random() > 0x3fff) c->steer = (float)((double)c->body.angle + K_STEER_MAX);
    else c->steer = (float)(((double)c->body.angle + K_HALF_PI) - K_STEER_MIN);
}

/* CarPhys_SetImpact 0x461570 */
void carphys_set_impact(const Car *a, Car *b, float px, float py, float fx, float fy)
{
    float hy = (float)((double)a->body.fy * 0.5f);
    double hx = (double)a->body.fx * 0.5f;
    b->impulse_py = py;
    b->impulse_px = px;
    b->impulse_x = (float)(hx + fx);
    b->impulse_y = (float)((double)fy + hy);
    if (b->impulse_state == 0) b->impulse_state = 1;
}

/* Player_GetSteerByPed 0x4646b0 / Player_GetMoveFlagByPed 0x464700: the control bytes of the player
   whose ped this is (+0x194 steer, +0x195 gear); 0 when no player has it (the original indexes
   player -1 then). */
static int ctl_by_ped(int ped, int k)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if (g_players[n].ped == (int16_t)ped) return g_players[n].ctl[k];
    return 0;
}

/* one tyre: the lateral / longitudinal velocity of wheel point (0, off) in the wheel's frame (c, s
   its direction), times the adhesions, rotated back; push = the extra longitudinal force (the
   thrust for the rear wheels). The lateral part goes to the skid sum unrounded. */
static void tyre_force(Car *c, float off, float cs, float sn, float adh_x, float adh_y, double push, bool rear)
{
    float rx, ry;
    phys_point_velocity(&c->body, 0, off, &rx, &ry);
    double a = (double)ry * sn + (double)rx * cs;
    double bb = (double)ry * cs - (double)rx * sn;
    float fl = (float)-((double)adh_x * a);
    double g = -((double)adh_y * bb);
    g_carphys_skid = (float)((double)g_carphys_skid + g);
    float wfx, wfy;
    if (!rear) {
        wfx = (float)((double)cs * fl - (double)sn * g);
        wfy = (float)((double)cs * g + (double)sn * fl);
    } else {
        double h = (double)fl + push;
        wfx = (float)((double)cs * h - g * sn);
        wfy = (float)(g * cs + h * sn);
    }
    phys_add_force_at_point(&c->body, 1, 0, off, wfx, wfy);
}

/* CarPhys_Step 0x460be0. Front tyre at car info +0x9c (steering_wheel_offset) along the wheel angle,
   rear tyre at +0x9a (drive_wheel_offset) along the body plus the drive force; adhesions from car
   info +0x84 / +0x88 (x, y), with the footbrake (+0x90, front share +0x94) and the handbrake (+0x8c,
   rear only, which also softens the rear's lateral grip: 0.6 for every model but 9, 3.0 there). Then
   integrate, auto-centre the wheel, align with a road, and set the next pose and the pending box. */
void carphys_step(Car *c)
{
    const uint8_t *in = c->info;
    float f84 = carinfo_float(in, 0x84), f88 = carinfo_float(in, 0x88), f8c = carinfo_float(in, 0x8c);
    float f90 = carinfo_float(in, 0x90), f94 = carinfo_float(in, 0x94);
    g_carphys_skid = 0;
    carphys_74f124 = 0;
    (void)carphys_74f124;
    /* the wheel at most 0.873 rad from straight: past it (cos < 0.642) it snaps to the limit */
    double base = (double)c->body.angle + K_HALF_PI;
    double d = (double)c->steer - base;
    if (x87_cos(d) < K_COS_LIMIT) {
        if (x87_sin(d) <= 0.0) c->steer = (float)(base - K_STEER_MIN);
        else c->steer = (float)((double)c->body.angle + K_STEER_MAX);
    }
    c->steer_cos = (float)x87_cos(c->steer);
    c->steer_sin = (float)x87_sin(c->steer);
    float cs = c->steer_cos, sn = c->steer_sin;
    float foff = (float)(int16_t)(in[0x9c] | in[0x9d] << 8);
    if (c->falling == 0) {
        float ax = c->brake_in ? (float)((double)f94 * f90 + f84) : f84;
        tyre_force(c, foff, cs, sn, ax, f88, 0, false);
    }
    float roff = (float)(int16_t)(in[0x9a] | in[0x9b] << 8);
    double rb = (double)c->body.angle + K_HALF_PI;
    float rc = (float)x87_cos(rb), rs = (float)x87_sin(rb);
    float k1, k2;
    if (c->model == 9) k1 = 3.0f, k2 = 3.0f;
    else k1 = 1.5f, k2 = 0.6f;
    if (c->falling == 0) {
        float adx, ady;
        if (c->brake_in) {
            double t = (1.0f - (double)f94) * f90;
            if (c->handbrake_in) adx = (float)(((t + f8c) + f84) * k1);
            else adx = (float)((t + f84) * k1);
            ady = (float)((double)k1 * f88);
        } else if (c->handbrake_in) {
            adx = (float)(((double)f8c + f84) * k1);
            ady = (float)((double)k2 * f88);
        } else {
            adx = (float)((double)k1 * f84);
            ady = (float)((double)k1 * f88);
        }
        tyre_force(c, roff, rc, rs, adx, ady, c->thrust_in, true);
    }
    PhysBody *b = &c->body;
    double inv = 1.0f / (double)b->mass;
    b->ax = (float)(inv * b->fx);
    b->ay = (float)(inv * b->fy);
    b->aw = (float)((double)b->torque / b->inertia);
    phys_integrate(b);
    c->skid = g_carphys_skid < 0.0f ? -g_carphys_skid : g_carphys_skid;
    if (c->control == CAR_CTL_PHYSICS) {
        if (c->driver > -1 && ctl_by_ped(c->driver, 3) == 0) {
            /* no steering input: the wheel returns toward straight by 0.139 rad a frame */
            double e = (double)c->steer - ((double)b->angle + K_HALF_PI);
            double ce = x87_cos(e);
            if (ce > K_COS_LIMIT || ce < K_COS_LIMIT) {   /* (two compares: only equality is skipped) */
                double se = x87_sin(e);
                if (se >= K_CENTRE) c->steer = (float)((double)c->steer - K_CENTRE);
                else if (se < -K_CENTRE) c->steer = (float)((double)c->steer + K_CENTRE);
            }
        }
        if (c->control == CAR_CTL_PHYSICS && c->driver > -1 && ctl_by_ped(c->driver, 3) == 0 &&
            ctl_by_ped(c->driver, 4) != -1 && c->speed > 6 &&
            (car_type_cache(c->spr.x, c->spr.y, c->spr.z) & 0x70) == 0x20) {
            /* on a road, going straight: pull the heading onto the axis it is near */
            int lo = (uint8_t)c->spr.angle;
            if (lo == 0) {
                int q = (uint8_t)((uint16_t)c->spr.angle >> 8);
                b->w = 0;
                c->next_heading = (int16_t)(c->next_heading & ~0xff);   /* (a byte store; overwritten below) */
                static const uint32_t axes[4] = { 0x00000000, 0xbfc90fdb, 0xc0490fdb, 0xc096cbe4 };
                if (q > 3) game_fatal(-0x4a, 0x1ca, q);
                else {
                    union { uint32_t u; float f; } v = { axes[q] };
                    b->angle = v.f;
                }
                b->aw = 0;
                b->ax = 0;
                c->steer = (float)((double)b->angle + K_HALF_PI);
            } else if (lo < 0x20) {
                c->steer = (float)((double)c->steer + K_ALIGN);
            } else if (lo > 0xe0) {
                c->steer = (float)((double)c->steer - K_ALIGN);
            }
        }
    }
    int32_t nx = x87_ftol((double)b->cx * 65536.0f);
    int32_t ny = x87_ftol((double)b->cy * 65536.0f);
    c->next_x = nx;
    c->next_z = c->spr.z;
    c->next_y = ny;
    int16_t h;
    if (c->map_hit == 0) {
        h = (int16_t)(x87_ftol(-((double)k_rad_heading * b->angle) + 0.5) & 0x3ff);
    } else {
        h = c->spr.angle;
        b->w = 0;
    }
    c->next_heading = h;
    c->front_x = math_sin(h) * c->half_l + nx;
    c->front_y = math_cos(h) * c->half_l + ny;
    c->rear_x = nx * 2 - c->front_x;
    c->rear_y = ny * 2 - c->front_y;
    if (c->control == CAR_CTL_PHYSICS) c->front_heading = c->spr.angle;
    coll_build_box(nx, ny, c->spr.z, c->half_w, c->half_l, h, c->depth, &c->box_saved);
    if (c->handbrake_in && c->speed > 2 && c->gear == 1) c->skid = 21.0f;
    car_emit_skidmarks(c);
}
