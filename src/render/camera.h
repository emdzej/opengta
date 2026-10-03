/* The per-player camera (0x43b910-0x43cbb0): Camera_Follow eases a camera toward its target (a ped, a
   car, a fixed point) with speed-dependent zoom and look-ahead, Camera_Update turns that into the
   viewport the renderer projects with, Camera_ComputeViewRect into the rectangle traffic spawns
   around. The three structs mirror the player record (base 0x74f148, stride 0x1bc) at +4 (view rect,
   Player_GetViewRect 0x462c30), +0x18 (viewport, Player_GetViewport 0x4644e0) and +0x48 (camera,
   Player_GetCamera 0x464580); the mode byte is +0x188.

   Units: x, y in world pixels (64 per block, the high word of 16.16 world coordinates); height is the
   camera's distance above the top (z = 0) plane, so layer plane z (0..6) is at depth height + 64 z;
   a world point at depth d projects to centre + (p - camera) * scale / d. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Player viewport (+0x18; Player_SetViewport 0x464500 fills the first six fields) */
typedef struct {
    int32_t cx, cy;             /* +0x00, +0x04: half width, half height (screen centre) */
    int32_t w, h;               /* +0x08, +0x0c */
    int32_t w16, h16;           /* +0x10, +0x14: w << 16, h << 16 */
    int32_t height;             /* +0x18: camera height, 16..716 */
    int32_t x, y;               /* +0x1c, +0x20: camera centre */
    int32_t scale;              /* +0x24: w * zoom / 320 */
    int32_t zoom;               /* +0x28: 60..500 */
    uint8_t squash;             /* +0x2c: not 4:3, project y * 5 / 6 */
} Viewport;

/* View rectangle (+4): pixels around the camera, used by traffic generation */
typedef struct { int32_t left, right, top, bottom, half; } ViewRect;

/* Camera (+0x48). "vel" fields are the easing velocities of the field named. */
typedef struct {
    int32_t dbg_x, dbg_y, dbg_zoom, dbg_height;   /* +0x00..+0x0c: debug free-camera offsets */
    int32_t target_x, target_y;                   /* +0x10, +0x14 */
    int32_t zoom, height;                         /* +0x18, +0x1c: eased toward mode zoom / target z */
    int32_t ahead_x, ahead_y;                     /* +0x20, +0x24: look-ahead */
    int32_t speed_zoom, speed_height;             /* +0x28, +0x2c: zoom out / rise with speed */
    int32_t unk30[4];                             /* +0x30..+0x3c (not used here) */
    int32_t vel_ahead_x, vel_ahead_y;             /* +0x40, +0x44 */
    int32_t vel_speed_height, vel_speed_zoom;     /* +0x48, +0x4c */
    int32_t vel_zoom, vel_height;                 /* +0x50, +0x54 */
    int32_t zoom_bias;                            /* +0x58: -204 during Camera_InitAll */
    int32_t size_margin;                          /* +0x5c: mode size factor * target size */
    int32_t state;                                /* +0x60: 0 follow, 1 move to a new focus, 2 snap */
} Camera;

/* Camera target record (Player_GetViewTargetPos 0x462d50: a 0x14-byte record per target kind) */
typedef struct {
    int32_t x, y, z;            /* 16.16 world */
    int16_t w, h;               /* +0x0c, +0x0e: size (8 x 8 for peds) */
    int16_t speed;              /* +0x10 */
    int16_t angle;              /* +0x12: 0..1023 */
} CameraTarget;

enum { CAM_TARGET_CAR = 0, CAM_TARGET_KIND1 = 1, CAM_TARGET_PED = 2, CAM_TARGET_POINT = 3, CAM_TARGET_POINT4 = 4,
       CAM_TARGET_HELI = 5 };

/* Camera mode parameter set (0x4b0d18 + mode * 0x24, read from the exe) */
typedef struct {
    int32_t speed;              /* +0x00 (0x4b0d18) */
    int32_t size;               /* +0x04 */
    int32_t max_vel;            /* +0x08 */
    int32_t height;             /* +0x0c: base height (300) */
    int32_t zoom;               /* +0x10: zoom offset */
    int32_t speed_height;       /* +0x14 */
    int32_t speed_zoom;         /* +0x18 */
    int32_t unk1c, unk20;
} CameraMode;
enum { CAMERA_MODES = 3 };

/* The game state Camera_Follow consults besides the target. */
typedef struct {
    bool peds, cars;            /* feature switches 0x502f40 / 0x503180 (both on in a normal game) */
    bool car_model_7_or_33;     /* Car_IsModel7or33 of the viewed car (kind 0) */
    bool ped_state9_or_dead;    /* Ped_IsState9 / Ped_IsDead of the player's ped */
    bool ped_state10;           /* Ped_IsState10 */
} CameraWorld;

typedef struct {
    ViewRect rect;
    Viewport vp;
    Camera cam;
    uint8_t mode;               /* +0x188 */
    int target_kind;            /* +0xd0 */
    CameraTarget target;        /* the record the target kind yields (filled by the caller) */
} CameraPlayer;

/* Loads the mode table from the exe; false if the exe isn't available. */
bool camera_init_tables(void);
void camera_set_viewport(CameraPlayer *p, int w, int h);                 /* Player_SetViewport 0x464500 */
void camera_follow(CameraPlayer *p, const CameraWorld *w);               /* Camera_Follow 0x43bbd0 */
void camera_update(CameraPlayer *p, const CameraWorld *w);               /* Camera_Update 0x43b910 (one player) */
void camera_compute_view_rect(CameraPlayer *p);                          /* Camera_ComputeViewRect 0x43ca30 */
void camera_reset_motion(CameraPlayer *p);                               /* Camera_ResetMotion 0x43c640 */
void camera_start_transition(CameraPlayer *p);                           /* Camera_StartTransition 0x43cac0 */
void camera_snap(CameraPlayer *p, const CameraWorld *w);                 /* Camera_Snap 0x43c5d0 */
void camera_init(CameraPlayer *p, const CameraWorld *w);                 /* Camera_InitAll 0x43c710 (one player) */
