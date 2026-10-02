#include "car.h"
#include "../exe.h"
#include "game.h"
#include "gmath.h"
#include <string.h>

Car g_cars[CAR_MAX] = { 0 };
int16_t g_car_model_index[CAR_MODELS];
int g_cars_count;
uint16_t g_traffic_models[3][100];
static int car_info_count;                   /* 0x501570 */
static const uint8_t *car_info_recs[256];    /* 0x501574 */
/* other state Cars_Init / Traffic_InitModelTables reset (meaning unknown) */
static struct {
    int remap_cycle;                         /* 0x501558 / 0x501559 */
    int u501550;
    int16_t u4be23c, u4be23e, u4be244;
    int u504f38, u504f3c;
} cs;
int g_traffic_cycle;                         /* 0x504f40 */

/* The car info records of the style (CarInfo_Setup 0x40c100 walks them: 0xae + 8 * doors bytes each,
   the door count a s16 at +0xac; the model byte is +0x6b). The original keeps a pointer table
   (0x501574, count 0x501570, at most 256); only the model byte is needed here. */
static int car_info_models(uint8_t *models, int cap)
{
    const Style *s = g_game.style;
    if (!s || !s->car_info) return 0;
    const uint8_t *p = s->car_info, *end = s->car_info + s->h.car_size;
    int n = 0;
    while (p + 0xae <= end && n < cap) {
        car_info_recs[n] = p;
        models[n++] = p[0x6b];
        p += 0xae + 8 * (int16_t)(p[0xac] | p[0xad] << 8);
    }
    return n;
}

const uint8_t *car_info_of_model(int model)
{
    if (model < 0 || model >= CAR_MODELS || g_car_model_index[model] < 0) return NULL;
    return car_info_recs[g_car_model_index[model]];
}

/* Cars_Init 0x4070a0: the model -> record table (models of records 0..99; -1 for models without a
   record), every slot free (status -1, +0x139 = 0), the traffic tables shuffled. */
void cars_init(void)
{
    uint8_t models[256];
    car_info_count = car_info_models(models, 256);
    memset(g_car_model_index, 0xff, sizeof g_car_model_index);
    for (int i = 0; i < 100; i++)
        if (i < car_info_count && models[i] < CAR_MODELS) g_car_model_index[models[i]] = (int16_t)i;
    cs.u4be23c = 8;
    cs.u4be23e = 0;
    cs.u4be244 = cs.u4be23e;
    g_cars_count = 0;   /* 0x501554 = 0x4be170 (0) */
    for (int i = 0; i < CAR_MAX; i++) {
        g_cars[i].status = -1;
        cs.remap_cycle = 0;
        g_cars[i].unk139 = 0;
    }
    cs.u501550 = 10;
    traffic_init_model_tables();
}

/* Traffic_InitModelTables 0x418f80: three rows of 100 car models (0x4ac108, read from the exe), each
   shuffled by swapping every entry with a random one of its row (Math_Random % 100). */
void traffic_init_model_tables(void)
{
    const uint8_t *src = exe_data(0x4ac108, sizeof g_traffic_models);
    if (!src) game_fatal(-2, 0, 0x4ac108);
    for (int i = 0; i < 300; i++) (&g_traffic_models[0][0])[i] = (uint16_t)(src[2 * i] | src[2 * i + 1] << 8);
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 100; i++) {
            int j = (int16_t)math_random() % 100;
            uint16_t t = g_traffic_models[r][i];
            g_traffic_models[r][i] = g_traffic_models[r][j];
            g_traffic_models[r][j] = t;
        }
    g_traffic_cycle = 0;
    cs.u504f38 = 0;
    cs.u504f3c = 0;
}

/* Car_SetPhysicsControl 0x4082d0 */
void car_set_physics_control(int n)
{
    g_cars[n].control = 1;
    g_cars[n].unkc0 = 1;
}

int cars_in_use(void)
{
    int k = 0;
    for (int i = 0; i < CAR_MAX; i++) k += g_cars[i].status != -1;
    return k;
}
