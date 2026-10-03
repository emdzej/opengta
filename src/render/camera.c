#include "camera.h"
#include "../exe.h"
#include "../game/gmath.h"
#include <string.h>

static CameraMode modes[CAMERA_MODES];

bool camera_init_tables(void)
{
    const uint8_t *d = exe_data(0x4b0d18, sizeof modes);
    if (!d) return false;
    memcpy(modes, d, sizeof modes);   /* little-endian host, int32 fields in exe order */
    return true;
}

void camera_set_viewport(CameraPlayer *p, int w, int h)
{
    Viewport *v = &p->vp;
    v->h16 = h << 16;
    v->w16 = w << 16;
    v->h = h;
    v->w = w;
    v->cx = w / 2;
    v->cy = h / 2;
    v->squash = w * 3 != h * 4;
}

/* Player_GetViewTargetPos 0x462d50: a fresh copy of the target record. For the fixed-point kinds the
   record is static in the original: x, y, z and the speed/angle dword are rewritten, the size stays
   (and keeps Camera_Follow's edits). */
static CameraTarget target_record(CameraPlayer *p)
{
    CameraTarget r = p->target;
    if (p->target_kind == CAM_TARGET_POINT || p->target_kind == CAM_TARGET_POINT4) r.speed = 0, r.angle = 0;
    return r;
}
static void target_keep(CameraPlayer *p, const CameraTarget *r)
{
    if (p->target_kind == CAM_TARGET_POINT || p->target_kind == CAM_TARGET_POINT4) p->target.w = r->w, p->target.h = r->h;
}

/* x / -10 as MSVC emits it (imul by -0x66666667, sar 2, add the sign) */
static int32_t div_neg10(int32_t x)
{
    int32_t t = (int32_t)(((int64_t)x * -0x66666667LL) >> 32);
    return (t >> 2) - (t >> 31);
}

static int16_t abs16(int16_t s) { return s < 0 ? (int16_t)-s : s; }

/* The easing step Camera_Follow repeats for every field: the field moves by half its velocity; the
   velocity grows by `up` (toward a larger target) or shrinks by `down`, up to +-max, and both snap
   when the target is reached or passed. */
static void ease(int32_t *cur, int32_t *vel, int32_t target, int32_t max, int up, int down)
{
    int32_t v = *vel, n = (v >> 1) + *cur;
    if (*cur < target) {
        if (n < target) {
            if (v < max && (*vel = v + up) > max) *vel = max;
        } else {
            *vel = 0;
            n = target;
        }
    } else {
        if (n <= target) {
            *vel = 0;
            n = target;
        } else if (-max < v && (*vel = v - down) < -max) *vel = -max;
    }
    *cur = n;
}

void camera_follow(CameraPlayer *p, const CameraWorld *w)
{
    CameraTarget r = target_record(p);
    const CameraMode *m = &modes[p->mode];
    Camera *c = &p->cam;
    int kind = p->target_kind;   /* Player_GetViewKind */
    if (kind == 1) r.speed = (int16_t)(r.speed * 10);
    else if (kind == 0 && w->car_model_7_or_33) r.speed = (int16_t)(r.speed * 3);
    if (r.speed > 0x30) r.speed = 0x30;
    c->target_x = (int16_t)(r.x >> 16);
    c->target_y = (int16_t)(r.y >> 16);
    if (r.h > 100) r.h = (int16_t)(r.h / 2);
    int16_t big = r.w > r.h ? r.w : r.h;
    c->size_margin = m->size * (big < 0x41 ? 0x40 : big);
    int32_t want_height = (int16_t)(r.z >> 16);
    int32_t want_zoom = c->zoom_bias + m->zoom + m->height;
    int32_t want_ahead_x, want_ahead_y, want_speed_height, want_speed_zoom;

    if (c->state == 1) {
        /* moving to a new focus: look-ahead back to 0 */
        if (r.speed < 8) r.speed = 8;
        int16_t s = abs16(r.speed);
        want_speed_height = div_neg10(m->speed_height * s * m->speed);
        want_speed_zoom = (s * m->speed / 2 * m->speed_zoom) / 10;
        ease(&c->ahead_x, &c->vel_ahead_x, 0, m->max_vel, 1, 1);
        ease(&c->ahead_y, &c->vel_ahead_y, 0, m->max_vel, 1, 1);
        ease(&c->speed_height, &c->vel_speed_height, want_speed_height, m->max_vel, 2, 1);
        ease(&c->speed_zoom, &c->vel_speed_zoom, want_speed_zoom, m->max_vel, 2, 1);
        ease(&c->zoom, &c->vel_zoom, want_zoom, m->max_vel, 1, 1);
        ease(&c->height, &c->vel_height, want_height, m->max_vel, 1, 1);
        if (c->ahead_x == 0 && c->ahead_y == 0) c->state = 0;
        target_keep(p, &r);
        return;
    }
    /* look-ahead along the target's heading (not for peds), from the unclamped speed */
    if (kind == CAM_TARGET_PED) want_ahead_x = want_ahead_y = 0;
    else {
        want_ahead_y = math_cos(r.angle) * r.speed * 10 >> 16;
        want_ahead_x = math_sin(r.angle) * r.speed * 10 >> 16;
    }
    if (r.speed < 8) r.speed = 8;
    int16_t s = abs16(r.speed);
    want_speed_height = div_neg10(m->speed_height * s * m->speed);
    want_speed_zoom = (s * m->speed / 2 * m->speed_zoom) / 10;
    if (c->state == 2) {
        /* snap: zoom and height jump, the rest eases */
        c->zoom = want_zoom;
        c->height = want_height;
        ease(&c->ahead_x, &c->vel_ahead_x, want_ahead_x, m->max_vel, 1, 1);
        ease(&c->ahead_y, &c->vel_ahead_y, want_ahead_y, m->max_vel, 1, 1);
        ease(&c->speed_height, &c->vel_speed_height, want_speed_height, m->max_vel, 2, 1);
        ease(&c->speed_zoom, &c->vel_speed_zoom, want_speed_zoom, m->max_vel, 2, 1);
        c->state = 0;
        target_keep(p, &r);
        return;
    }
    if (c->state != 0) {   /* other states: nothing */
        target_keep(p, &r);
        return;
    }
    if (!w->ped_state9_or_dead) {
        if (kind == CAM_TARGET_PED && w->ped_state10) want_zoom = c->zoom, want_height = c->height;
    } else {
        want_speed_height = c->speed_height;
        want_speed_zoom -= 300;
    }
    ease(&c->ahead_x, &c->vel_ahead_x, want_ahead_x, m->max_vel, 4, 4);
    ease(&c->ahead_y, &c->vel_ahead_y, want_ahead_y, m->max_vel, 4, 4);
    ease(&c->speed_height, &c->vel_speed_height, want_speed_height, m->max_vel, 2, 1);
    ease(&c->speed_zoom, &c->vel_speed_zoom, want_speed_zoom, m->max_vel, 2, 1);
    ease(&c->zoom, &c->vel_zoom, want_zoom, m->max_vel, 2, 1);
    ease(&c->height, &c->vel_height, want_height, m->max_vel, 2, 1);
    target_keep(p, &r);
}

void camera_compute_view_rect(CameraPlayer *p)
{
    const Viewport *v = &p->vp;
    ViewRect *r = &p->rect;
    int32_t hx = (v->height + 0x180) * 0xa0 / v->zoom;
    int32_t hy = hx * 3 / 4;
    r->left = v->x - hx;
    if (r->left < 0) r->left = 0;
    r->right = v->x + hx;
    if (r->right < 0) r->right = 0;
    r->top = v->y - hy;
    if (r->top < 0) r->top = 0;
    r->half = hx;
    r->bottom = hy + v->y;
    if (r->bottom < 0) r->bottom = 0;
}

void camera_update(CameraPlayer *p, const CameraWorld *w)
{
    if (w->peds || w->cars) camera_follow(p, w);
    const CameraMode *m = &modes[p->mode];
    Camera *c = &p->cam;
    int32_t x = c->ahead_x + c->target_x + c->dbg_x;
    int32_t y = c->ahead_y + c->target_y + c->dbg_y;
    int32_t height = m->height - c->speed_height - c->height - c->dbg_height;
    int32_t zoom = c->zoom - c->speed_zoom - c->dbg_zoom;
    if (height > 0x2cc) height = 0x2cc;
    else if (height < 0x10) height = 0x10;
    if (zoom > 500) zoom = 500;
    else if (zoom < 0x3c) zoom = 0x3c;
    /* keep the target on screen: the look-ahead is limited to the visible half-extent minus a margin */
    int32_t lx = (c->height + height) * 0xa0 / zoom, ly3 = lx * 3;
    if ((w->cars || w->peds) && c->dbg_x == 0 && c->dbg_y == 0 && c->dbg_zoom == 0 && c->state != 1) {
        lx -= c->size_margin;
        if (lx < 0) lx = 0;
        int32_t ly = ly3 / 4 - c->size_margin;
        if (ly < 0) ly = 0;
        if (x < c->target_x - lx) x = c->target_x - lx;
        if (c->target_x + lx < x) x = c->target_x + lx;
        if (y < c->target_y - ly) y = c->target_y - ly;
        if (c->target_y + ly < y) y = c->target_y + ly;
    }
    c->ahead_x = x - c->target_x - c->dbg_x;
    c->ahead_y = y - c->target_y - c->dbg_y;
    c->speed_zoom = c->zoom - c->dbg_zoom - zoom;
    c->speed_height = m->height - c->height - c->dbg_height - height;
    Viewport *v = &p->vp;
    v->y = y;
    v->x = x;
    v->height = height;
    v->zoom = zoom;
    v->scale = v->w * zoom / 0x140;
    camera_compute_view_rect(p);
}

/* Camera_StartTransition 0x43cac0: unless snapping (state 2), the camera eases from where it is to a
   new target: the look-ahead takes up the jump of the target (its old position minus the new one,
   pixels), the easing velocities restart, state 1 (Camera_Follow eases the look-ahead back to 0). */
void camera_start_transition(CameraPlayer *p)
{
    Camera *c = &p->cam;
    if (c->state == 2) return;
    c->ahead_x += c->target_x - (int16_t)(p->target.x >> 16);
    c->state = 1;
    c->ahead_y += c->target_y - (int16_t)(p->target.y >> 16);
    c->vel_ahead_x = c->vel_ahead_y = 0;
    c->vel_speed_height = c->vel_speed_zoom = 0;
    c->vel_zoom = c->vel_height = 0;
}

void camera_reset_motion(CameraPlayer *p)
{
    Camera *c = &p->cam;
    const CameraMode *m = &modes[p->mode];
    CameraTarget r = target_record(p);
    c->target_x = c->target_y = 0;
    c->height = c->zoom = 0;
    c->ahead_x = c->ahead_y = 0;
    c->vel_ahead_x = c->vel_ahead_y = 0;
    c->vel_speed_zoom = c->vel_speed_height = 0;
    c->vel_zoom = c->vel_height = 0;
    c->zoom_bias = 0;
    if (r.speed < 8) r.speed = 8;
    int16_t s = abs16(r.speed);
    c->speed_height = div_neg10(m->speed_height * m->speed * s);
    c->speed_zoom = (m->speed * s / 2 * m->speed_zoom) / 10;
    target_keep(p, &r);
}

void camera_snap(CameraPlayer *p, const CameraWorld *w)
{
    Camera *c = &p->cam;
    camera_reset_motion(p);
    c->dbg_x = c->dbg_y = c->dbg_height = c->dbg_zoom = 0;
    c->state = 2;
    if (!w->cars && !w->peds) c->dbg_x += 10, c->dbg_y += 10, c->dbg_zoom += 0x150;
}

/* Camera_InitAll 0x43c710 for one player: snap, then one update with the zoom bias -204 (the level
   start zooms in from there). Its inner loop is Camera_Update's body inlined; the network flag
   0x502f38 that skips it is taken as clear. */
void camera_init(CameraPlayer *p, const CameraWorld *w)
{
    camera_snap(p, w);
    p->cam.zoom_bias = -204;
    camera_update(p, w);
    p->cam.zoom_bias = 0;
}
