/* The sprite draw trees (DrawList_* 0x480ed0-0x480fd0): unbalanced binary search trees of items keyed
   by a depth, one root per map layer, walked from the largest key to the smallest so that sprites lower
   in the world (world z grows downward) are drawn first. See docs/sprites.md.

   Memory mirrors the original: 6 roots (0x77d480, 16 bytes each) followed by a pool of 300 nodes
   (0x77d4e0); node and root counts at 0x77d478 / 0x77d47c; the walk's explicit stack at 0x774f20 with
   its pointer at 0x774f18. A root holds no item and has key 0x7fffffff, so everything hangs off its
   "less or equal" link. */
#pragma once
#include <stdint.h>

enum {
    DRAWLIST_NODES = 300,      /* checked by DrawList_Insert: further inserts are dropped */
    DRAWLIST_ROOTS = 6,        /* room before the node pool (Sprite_LoadInfo makes exactly 6) */
};

typedef struct DrawNode {
    void *item;                /* +0x00 */
    int32_t key;               /* +0x04 */
    struct DrawNode *greater;  /* +0x08: keys > this one */
    struct DrawNode *less_eq;  /* +0x0c: keys <= this one (equal keys go here: drawn after) */
} DrawNode;

void drawlist_init(void);                 /* DrawList_Init 0x480ed0: no nodes, no roots */
void drawlist_clear(void);                /* DrawList_Clear 0x480ef0 (and its copy 0x47c020): empty every tree */
DrawNode *drawlist_new_root(void);        /* DrawList_NewRoot 0x480f30 (NULL past DRAWLIST_ROOTS) */
void drawlist_insert(DrawNode *root, void *item, int32_t key);   /* DrawList_Insert 0x480f60 */
/* DrawList_Walk 0x480fd0: fn(item) for every item, largest key first. */
void drawlist_walk(const DrawNode *root, void (*fn)(void *item));
int drawlist_count(void);                 /* nodes in use (0x77d478) */
