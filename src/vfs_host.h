/* Host-filesystem backends of the file layer (POSIX): directories and disc-image files. Used by the
   SDL build and the tests; a backend without a filesystem (gasm) mounts a VfsSource instead. */
#pragma once
#include "vfs.h"

/* Mounts a data root: a directory (the installed game folder, or the unzipped installer folder), a disc
   image (.iso, raw .bin), or a .cue sheet (its FILE line names the image, its TRACK line the sector
   format). Without GTADATA/MISSION.INI under its root but with InstallShield cabinets (data1.cab, as in
   GTA's installer, or data1.hdr; also under Setup/), the game folder inside them is mounted instead
   (vfs_find_game). False if it can't be mounted; whether it holds the game is up to the caller
   (vfs_exists("GTADATA/MISSION.INI")). */
bool vfs_mount_path(const char *path);
/* $OPENGTA_DATA if set, else ./game (the installed game directory; tests and tools run from the
   project root). Either may be the installed game folder, the unzipped installer folder or a disc
   image holding either. */
bool vfs_mount_default(void);
