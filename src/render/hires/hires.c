/* The hires frame: buffers, the world pass and the HUD layer (see hires.h). */
#include "hires.h"
#include "hires_internal.h"
#include <stdlib.h>
#include <string.h>

static int N = 1;
static bool active;
static const uint32_t *fb;         /* the faithful back buffer */
static int fw, fh;
static uint32_t *snap;             /* the faithful frame before the HUD */
static uint32_t *frame;            /* the hires frame, 0xAABBGGRR */
static bool begun;

bool hires_init(int n, const uint32_t *back, int w, int h)
{
    free(snap);
    free(frame);
    snap = frame = NULL;
    N = 1;
    active = begun = false;
    hires_textures_reset();
    if (n == 0) return true;
    if (n < 1 || n > HIRES_MAX || !back || w <= 0 || h <= 0) return false;
    snap = calloc((size_t)w * (size_t)h, 4);
    frame = calloc((size_t)w * n * (size_t)h * n, 4);
    if (!snap || !frame) {
        free(snap);
        free(frame);
        snap = frame = NULL;
        return false;
    }
    for (size_t i = 0; i < (size_t)w * n * (size_t)h * n; i++) frame[i] = 0xff000000u;
    N = n, fb = back, fw = w, fh = h;
    active = true;
    return true;
}

int hires_scale(void) { return N; }
bool hires_active(void) { return active; }

void hires_frame_begin(const Map *m, const Style *s, const Viewport *vp)
{
    if (!active) return;
    HrTarget t = { frame, fw * N, fh * N, fw * N };
    if (m && s && vp) hires_city_draw(m, s, vp, &t, N);
    memcpy(snap, fb, (size_t)fw * (size_t)fh * 4);
    begun = true;
}

/* Every faithful pixel the HUD wrote (it differs from the copy) becomes an N x N block. A HUD pixel of
   exactly the colour below it is not seen, and shows the hires city there instead: the same colour. */
void hires_frame_end(void)
{
    if (!active || !begun) return;
    const int W = fw * N;
    for (int y = 0; y < fh; y++) {
        const uint32_t *a = fb + (size_t)y * fw, *b = snap + (size_t)y * fw;
        for (int x = 0; x < fw; x++) {
            if (a[x] == b[x]) continue;
            const uint32_t c = hr_from_xrgb(a[x], 0xff);
            uint32_t *d = frame + (size_t)y * N * W + (size_t)x * N;
            for (int j = 0; j < N; j++, d += W)
                for (int i = 0; i < N; i++) d[i] = c;
        }
    }
}

const uint32_t *hires_pixels(int *w, int *h)
{
    *w = fw * N;
    *h = fh * N;
    return frame;
}
