/* Shared between the hires renderer's files. */
#pragma once
#include "../../map.h"
#include "../../style.h"
#include "../camera.h"
#include "hires_raster.h"
#include "hires_tex.h"

/* The city and its sprites at n times the faithful resolution into target (hires_city.c). Uses the
   faithful renderer's per-frame state (render_rects, render_cam, the sprite draw trees) read-only. */
void hires_city_draw(const Map *m, const Style *s, const Viewport *vp, const HrTarget *target, int n);
