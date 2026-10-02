/* Layout checks for the structs that mirror the original's records (cars 0x2b0, peds 0x100, objects
   0x88, players 0x1bc). Offsets are the original's. Fields that lie before the first pointer of a
   record are checked on every build; fields after it only where pointers are 4 bytes (the gasm build),
   as src/render/sprite.h does for the embedded sprite object. */
#pragma once
#include <stddef.h>

#define GAME_PTR32 (sizeof(void *) == 4)
/* offset check that holds on every build */
#define GAME_OFS(T, f, o) _Static_assert(offsetof(T, f) == (o), #T "." #f " at " #o)
/* offset check for fields that follow a pointer (exact with 32-bit pointers) */
#define GAME_OFS32(T, f, o) _Static_assert(!GAME_PTR32 || offsetof(T, f) == (o), #T "." #f " at " #o)
#define GAME_SIZE32(T, n) _Static_assert(!GAME_PTR32 || sizeof(T) == (n), #T " is " #n " bytes")
