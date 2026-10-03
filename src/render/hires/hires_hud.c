/* The HUD at hires (see hires_hud.h). */
#include "hires_hud.h"
#include "../city.h"
#include "hires.h"
#include "hires_tex.h"
#include <string.h>

static const Style *S;

void hires_hud_begin(const Style *s)
{
    if (!hires_active()) return;
    S = s;
    hires_ui_begin();
}

void hires_hud_end(void)
{
    if (!hires_active() || !hires_ui_recording) return;
    hires_ui_end();
    int w, h;
    /* the hires frame is hires.c's; this layer is the only other writer, after the city pass */
    uint32_t *px = (uint32_t *)hires_pixels(&w, &h);
    if (!px) return;
    HrTarget t = { px, w, h, w };
    hires_ui_replay(&t, hires_scale());
}

/* ---- hooks ---- */

void hires_hud_screen_sprite(int x, int y, const SpriteInfo *in)
{
    if (!in || in->w <= 0 || in->h <= 0) return;
    HrUiCmd *c = hires_ui_push(HRC_SPRITE);
    if (!c) return;
    c->x = x, c->y = y, c->w = in->w, c->h = in->h;
    c->u.s.info = in;
    c->u.s.n = (int)(in - sprite_get_info(0));
    c->u.s.clut = sprite_remap_clut(in->clut, 0, 0);   /* Sprite_DrawScreen's palette, without setting poly_clut */
    c->u.s.chash = c->u.s.clut ? hires_ui_clut_hash(c->u.s.clut, 64) : 0;
}

void hires_hud_world_sprite(const Sprite *sp)
{
    if (!sp->info) return;
    HrUiCmd *c = hires_ui_push(HRC_WORLD);
    if (!c) return;
    c->u.wd.x = sp->x, c->u.wd.y = sp->y, c->u.wd.z = sp->z;
    c->u.wd.angle = sp->angle, c->u.wd.palette = sp->palette, c->u.wd.frame = sp->frame;
    c->u.wd.remap = sp->remap, c->u.wd.blend = sp->blend, c->u.wd.deltas = sp->deltas;
    c->u.wd.info = sp->info;
}

/* ---- replay ---- */

/* A skin's sprite (the overlay stack: hires_tex.h) when one replaces it; the original's conversion has
   the sprite's own size, a skin image (almost always) another. */
static HiresTexture *skin_sprite(int n, const SpriteInfo *in, const uint32_t *clut, int remap, const uint32_t *own,
                                 uint32_t deltas)
{
    if (!S || !hires_overlay_count()) return NULL;
    HiresTexture *t = hires_sprite(S, n, in, clut, deltas, remap, own);
    return t && (t->w != in->w || t->h != in->h) ? t : NULL;
}

static void replay_screen(const HrTarget *t, const HrUiCmd *c, int n)
{
    const SpriteInfo *in = c->u.s.info;
    if (!c->u.s.clut) return;
    HiresTexture *k = skin_sprite(c->u.s.n, in, c->u.s.clut, 0, c->u.s.clut, 0);
    if (k) {
        hires_ui_stretch(t, k, c->x * n, c->y * n, (c->x + in->w) * n, (c->y + in->h) * n, 0, 0, k->w << 16, k->h << 16);
        return;
    }
    const uint32_t *pm = hires_ui_art(in->data, 256, in->w, in->h, c->u.s.clut, 64, true, c->u.s.chash, n);
    if (pm) hires_ui_blit(t, pm, in->w * n, 0, 0, in->w * n, in->h * n, c->x * n, c->y * n);
}

/* Sprite_Draw 0x47bc00 (sprite.c): the centre (integer world position, depth from the high short of z)
   projected; the rotated corner offsets added in screen pixels. The faithful corners decide culling,
   the drop tests, the raw-or-composite choice and the winding as there; the hires ones are the exact
   centre times N plus the offsets times N. */
static void replay_world(const HrTarget *t, const HrUiCmd *c, int n)
{
    const SpriteInfo *in = c->u.wd.info;
    int32_t cr[8];
    sprite_get_corners(in, c->u.wd.angle, cr);
    const int32_t X = (int32_t)((uint32_t)c->u.wd.x & 0xffff0000u), Y = (int32_t)((uint32_t)c->u.wd.y & 0xffff0000u);
    const int32_t z = (int32_t)((uint32_t)(int16_t)(c->u.wd.z >> 16) << 16);
    int32_t fcx, fcy;
    render_world_to_screen(X, Y, z, &fcx, &fcy);
    const int64_t K = (int64_t)n * HR_SUB;
    const int32_t d = (z >> 16) + render_cam.height;
    const int64_t dx = (int32_t)((uint32_t)X - (uint32_t)render_cam.x * 0x10000u);
    const int64_t dy = (int32_t)((uint32_t)Y - (uint32_t)render_cam.y * 0x10000u);
    int64_t hcx = d ? (dx * render_cam.scale * K / d) >> 16 : 0, hcy = d ? (dy * render_cam.scale * K / d) >> 16 : 0;
    if (render_cam.squash) hcy = hcy * 5 / 6;
    hcx += render_cam.cx * K, hcy += render_cam.cy * K;
    int32_t fx[4], fy[4], hx[4], hy[4];
    for (int i = 0; i < 4; i++) {
        fx[i] = (cr[2 * i] >> 16) + fcx, fy[i] = fcy - (cr[2 * i + 1] >> 16);
        hx[i] = (int32_t)(hcx + (cr[2 * i] * K >> 16)), hy[i] = (int32_t)(hcy - (cr[2 * i + 1] * K >> 16));
    }
    int32_t xmax = fx[0], xmin = fx[0], ymax = fy[0], ymin = fy[0];
    for (int i = 1; i < 4; i++) {
        if (fx[i] > xmax) xmax = fx[i];
        if (fx[i] < xmin) xmin = fx[i];
        if (fy[i] > ymax) ymax = fy[i];
        if (fy[i] < ymin) ymin = fy[i];
    }
    if (xmax < 0 || ymax < 0 || xmin >= render_cam.w || ymin >= render_cam.h) return;
    if ((int16_t)xmin >= (int16_t)xmax || (int16_t)ymin >= (int16_t)ymax) return;
    int32_t ady = fy[3] - fy[0], adx = fx[3] - fx[0];
    if (ady < 0) ady = -ady;
    if (adx < 0) adx = -adx;
    const uint32_t *clut = sprite_remap_clut(in->clut, c->u.wd.remap, c->u.wd.palette);
    if (!S || !clut) return;
    HiresTexture *tex = hires_sprite(S, c->u.wd.frame, in, clut, ady + adx < 10 ? 0 : c->u.wd.deltas, c->u.wd.remap,
                                     sprite_remap_clut(in->clut, 0, 0));
    if (!tex) return;
    /* Poly_DrawSprite's vertex order: top right, top left, bottom left, bottom right (corners 1, 0, 2, 3) */
    static const int order[4] = { 1, 0, 2, 3 };
    int32_t ox[4], oy[4], px[4], py[4], tu[4], tv[4];
    const int64_t W = (int64_t)tex->w << 16, H = (int64_t)tex->h << 16;
    for (int i = 0; i < 4; i++) {
        const int k = order[i];
        ox[i] = (int16_t)fx[k], oy[i] = (int16_t)fy[k], px[i] = hx[k], py[i] = hy[k];
        const bool right = k == 1 || k == 3, bottom = k >= 2;
        tu[i] = (int32_t)((right ? (2 * in->w - 1) : 1) * W / (2 * in->w));
        tv[i] = (int32_t)((bottom ? (2 * in->h - 1) : 1) * H / (2 * in->h));
    }
    if (!hr_winding_draws(4, ox, oy)) return;
    hr_polygon(t, c->u.wd.blend && sprite_blend_option ? HR_BLEND : HR_KEYED, tex, 4, px, py, tu, tv);
}

void hires_hud_replay(const HrTarget *t, const HrUiCmd *c, int n)
{
    if (c->kind == HRC_SPRITE) replay_screen(t, c, n);
    else if (c->kind == HRC_WORLD) replay_world(t, c, n);
}
