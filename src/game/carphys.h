/* The float rigid-body car model (carphys, 0x4607b0-0x4616af). See docs/cars.md.

   The original is x87 code built by MSVC; the C runtime sets the precision control to 53 bits
   (_controlfp(_PC_53, _MCW_PC) at start-up, 0x49e925) and nothing changes it later (MGL's
   _control87(0x9001f, 0xffff) at 0x48cbbb only touches the exception masks and rounding, which stay
   "all masked, nearest"). So every x87 add / sub / mul / div rounds to double, values stay in
   registers between instructions and are rounded to float only where the code stores them
   (fst / fstp dword). The port does its arithmetic in double in the same order and converts to float
   exactly where the original stores. What is not reproduced bit for bit: fsin / fcos return a
   64-bit-mantissa result that the next multiply rounds once; the port takes libm's double sin / cos
   (already rounded to 53 bits) and then multiplies, a double rounding that can differ in the last bit
   of a double now and then (rarely visible after the float store). The 80-bit exponent range doesn't
   matter for these magnitudes. Contraction into fused multiply-adds is switched off in the .c files
   that do this arithmetic. */
#pragma once
#include <stdint.h>

struct Car;

/* The rigid body (0x48 bytes, at car +0x190). x, y is the centre of mass in pixels, cx, cy the
   geometric centre (the sprite position), angle in radians (= -heading * 2pi/1024, so it decreases as
   the heading grows), forces / torque accumulate until Phys_Integrate turns them into the
   accelerations ax, ay, aw (set by CarPhys_Step: force / mass, torque / inertia). */
typedef struct PhysBody {
    float x, y, angle;          /* +0x00 */
    float vx, vy, w;            /* +0x0c velocity (pixels / frame), angular velocity */
    float cx, cy;               /* +0x18 */
    float mass, inertia;        /* +0x20 car info +0x7c (16.16 -> float), +0x78 (int -> float) */
    float comx, comy;           /* +0x28 centre of mass offset, car info +0x76 / +0x77 (s8) */
    float ax, ay, aw;           /* +0x30 */
    float fx, fy, torque;       /* +0x3c */
} PhysBody;
_Static_assert(sizeof(PhysBody) == 0x48, "rigid body layout");

/* 0x4607b0: force (fx, fy) at a point: mode 0 world point (px, py), 1 body point (px, py) rotated by
   the angle around cx, cy, other: the centre of mass. Torque about the centre of mass. */
void phys_add_force_at_point(PhysBody *b, int mode, float px, float py, float fx, float fy);
void phys_integrate(PhysBody *b);           /* Phys_Integrate 0x460880 */
/* Phys_PointVelocity 0x460950: velocity of body point (px, py) (body frame, relative to the centre of
   mass offset) as the difference of its world position after one step and now. */
void phys_point_velocity(const PhysBody *b, float px, float py, float *rx, float *ry);
void phys_bounce_xy(PhysBody *b);           /* Phys_BounceXY 0x4613c0 */
void phys_bounce_x(PhysBody *b);            /* Phys_BounceX 0x461450 */
void phys_bounce_y(PhysBody *b);            /* Phys_BounceY 0x4614a0 */
void phys_sync_from_centre(PhysBody *b);    /* Phys_SyncFromCentre 0x4614f0: x, y from cx, cy */

void carphys_begin(int car);                /* CarPhys_Begin 0x460a20 */
void carphys_end(int car);                  /* CarPhys_End 0x460ab0 */
void carphys_load_params(struct Car *c);    /* CarPhys_LoadParams 0x460ad0 */
void carphys_reset(int car);                /* CarPhys_Reset 0x460b90 */
void carphys_step(struct Car *c);           /* CarPhys_Step 0x460be0 */
/* CarPhys_SetImpact 0x461570: a's half force plus (fx, fy) goes to b's pending impulse at (px, py) */
void carphys_set_impact(const struct Car *a, struct Car *b, float px, float py, float fx, float fy);
void carphys_begin_spin(int car);           /* CarPhys_BeginSpin 0x4615e0 */

/* MSVC __ftol 0x49cb00: truncation toward zero through a 64-bit fistp, low 32 bits returned */
int32_t x87_ftol(double v);
/* the radians of a heading, as the original computes them: (float)-(heading * 0.0061359233f) */
float car_heading_to_angle(int heading);
extern float g_carphys_skid;                /* 0x74f128 lateral tyre force sum of the last step */
