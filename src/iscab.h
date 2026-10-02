/* InstallShield cabinet reader (clean room, from OpenBallance) and the file-layer backend built on it:
   GTA's installer (the 2002 freeware GTAINSTALLER.zip) ships the game as data1.cab + data2.cab
   (InstallShield 5 era, header version 0x01000004, the header inside data1.cab, no data1.hdr), and this
   lets the game run straight from the unzipped installer folder. The InstallShield 6 layout
   (Setup/data1.hdr + dataN.cab, as on the Ballance CD) is kept from OpenBallance.

   Format notes: written from the format description in the unshield project's documentation and
   headers (github.com/twogood/unshield: lib/cabfile.h, the field layout comments of lib/libunshield.c,
   lib/file.c, lib/component.c, lib/file_group.c; MIT licensed) and checked field by field against
   Ballance's data1.hdr and GTA's data1.cab / data2.cab; no code was taken from it. See iscab.c for the
   layouts.

   Reading goes through a VfsBackend (the mount underneath: a host directory, the ISO 9660 reader or
   gasm assets), so the cabinet works on any source the file layer has. */
#pragma once
#include "vfs.h"

typedef struct IsCab IsCab;

/* File descriptor flags (cabfile.h). */
enum { ISCAB_SPLIT = 1, ISCAB_OBFUSCATED = 2, ISCAB_COMPRESSED = 4, ISCAB_INVALID = 8 };

typedef struct {
    const char *name;     /* file name */
    const char *dir;      /* its directory inside the group's target, '\\'-separated ("" = the target) */
    uint16_t flags;       /* ISCAB_* */
    uint64_t size;        /* expanded */
    uint64_t stored;      /* bytes in the volumes (compressed size, or size) */
    uint64_t offset;      /* of the stored data in its first volume */
    uint16_t volume;      /* first volume (N of dataN.cab) */
    uint8_t md5[16];      /* of the expanded data (all zero: none recorded, as in GTA's cabinets) */
} IsCabFile;

/* Opens <hdr> through io: a header file ("Setup/data1.hdr") or a first volume that carries the cab
   descriptor itself ("data1.cab", GTA); volumes are <dir>/<prefix>N.cab next to it. io is borrowed (it
   must outlive the cabinet). NULL if the header isn't a supported cabinet (major version 5 or later,
   0x01000004 counting as 5); err (may be NULL) says why. */
IsCab *iscab_open(const VfsBackend *io, const char *hdr, char *err, size_t errcap);
void iscab_close(IsCab *c);
int iscab_version(const IsCab *c);                    /* major version (5 for GTA, 6 for Ballance) */

uint32_t iscab_file_count(const IsCab *c);
const IsCabFile *iscab_file(const IsCab *c, uint32_t index);
/* The expanded file into dst (iscab_file()->size bytes): reassembled across volumes, deobfuscated,
   inflated and checked against its MD5. False on any error (missing volume, corrupt data). */
bool iscab_extract(IsCab *c, uint32_t index, uint8_t *dst);

/* One file read in pieces without expanding it whole (for big files: music): reads expand only the
   compressed chunks they need, keeping the last one and the start of every chunk seen, so sequential
   reads expand each chunk once and a seek costs at most a walk from the nearest known chunk. Opening
   opens the volumes the file spans; reading only does positional reads of them and touches nothing but
   the stream, so streams may be read from another thread (one thread per stream). The MD5 (when there
   is one) is checked once the file has been read in order up to its end: that read fails on a mismatch. */
typedef struct IsCabStream IsCabStream;
IsCabStream *iscab_stream_open(IsCab *c, uint32_t index);   /* NULL if missing volume / invalid */
/* Up to len bytes at off: the count (0 at or after the end), -1 on error. */
int64_t iscab_stream_read(IsCabStream *s, uint64_t off, void *dst, size_t len);
void iscab_stream_close(IsCabStream *s);

/* File groups: a name and a range of file indices; components list the groups they install. */
int iscab_group_count(const IsCab *c);
const char *iscab_group(const IsCab *c, int g, uint32_t *first, uint32_t *last);
int iscab_component_count(const IsCab *c);
const char *iscab_component(const IsCab *c, int k, int *ngroups);
const char *iscab_component_group(const IsCab *c, int k, int i);

/* ---- file-layer backend ---- */

/* Mounts the installed game folder held by the cabinets of the current mount, as a layer over it: the
   files of every non-InstallShield file group (the ones whose name isn't "<...>": GTA has only
   "Program Executable Files"), at their install paths. hdr: "data1.cab" (GTA), "data1.hdr", or either
   under "Setup/". Files over 8 MB are streamed (iscab_stream_*), the rest expanded whole on their first
   read. On failure the current mount stays as it was. */
bool vfs_mount_cab(const char *hdr);

/* Makes the current mount the game folder: kept as it is if it has GTADATA/MISSION.INI (an installed
   game), else its cabinets (data1.hdr, else data1.cab, at the root or under Setup/: the unzipped
   installer) are mounted over it. False (the mount is left as it was) if neither. */
bool vfs_find_game(void);

/* Counters of the cabinet backend since it was mounted (for measurements). */
typedef struct { uint32_t opens, extracts, cache_hits, streams; uint64_t bytes_out; } IsCabStats;
bool vfs_cab_stats(IsCabStats *s);
