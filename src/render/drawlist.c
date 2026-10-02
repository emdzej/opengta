/* DrawList_* 0x480ed0-0x480fd0 (see drawlist.h). */
#include "drawlist.h"
#include <stddef.h>

static DrawNode roots[DRAWLIST_ROOTS];     /* 0x77d480 */
static DrawNode nodes[DRAWLIST_NODES];     /* 0x77d4e0 */
static int nnodes, nroots;                 /* 0x77d478, 0x77d47c */
/* 0x774f20: the walk stack. A tree of n nodes can be n deep; the original's stack is bss with no
   bound check, sized here for the worst case. */
static const DrawNode *stack[DRAWLIST_NODES + 1];

/* DrawList_Init 0x480ed0 (also resets the walk stack, DrawList_ResetStack 0x47cd00) */
void drawlist_init(void)
{
    nnodes = 0;
    nroots = 0;
}

/* DrawList_Clear 0x480ef0: the pool is emptied and each root forgets its children. */
void drawlist_clear(void)
{
    nnodes = 0;
    for (int i = 0; i < nroots; i++) {
        roots[i].key = 0x7fffffff;
        roots[i].greater = NULL;
        roots[i].less_eq = NULL;
    }
}

/* DrawList_NewRoot 0x480f30: the next root slot, key 0x7fffffff. (Its item word is left alone.) More
   than 6 would run into the node pool in the original. */
DrawNode *drawlist_new_root(void)
{
    if (nroots >= DRAWLIST_ROOTS) return NULL;
    DrawNode *r = &roots[nroots++];
    r->greater = NULL;
    r->less_eq = NULL;
    r->key = 0x7fffffff;
    return r;
}

/* DrawList_Insert 0x480f60: descend (greater if the node's key is below the new key, else less-or-
   equal) and hang a new leaf there. When the 300 nodes are used up the item is silently not drawn. */
void drawlist_insert(DrawNode *root, void *item, int32_t key)
{
    if (nnodes >= DRAWLIST_NODES || !root) return;
    DrawNode *parent = NULL;
    for (DrawNode *n = root; n; n = n->key < key ? n->greater : n->less_eq) parent = n;
    DrawNode *leaf = &nodes[nnodes++];
    leaf->item = item;
    leaf->key = key;
    leaf->greater = leaf->less_eq = NULL;
    if (parent->key < key) parent->greater = leaf;
    else parent->less_eq = leaf;
}

/* DrawList_Walk 0x480fd0: iterative reverse in-order walk from the root's less-or-equal child: push the
   chain of greater links, pop, visit, continue with the popped node's less-or-equal child. */
void drawlist_walk(const DrawNode *root, void (*fn)(void *item))
{
    if (!root) return;
    int sp = 0;
    const DrawNode *n = root->less_eq;
    for (;;) {
        for (; n; n = n->greater) stack[sp++] = n;
        if (sp == 0) break;
        const DrawNode *t = stack[--sp];
        fn(t->item);
        n = t->less_eq;
    }
}

int drawlist_count(void) { return nnodes; }
