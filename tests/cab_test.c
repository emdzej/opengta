/* GTA's installer cabinets as the game folder (iscab.c, ported from OpenBallance's cab_test): every file
   the cabinet layer exposes is byte-compared with the installed game (the unshield extraction, the
   ground truth), and every installed file must be exposed. Music/Track8.wav, which unshield can't
   extract (it is split across data1.cab and data2.cab), must be exposed too: it is checked for its size,
   a consistent RIFF/WAVE header, the same bytes through a 4 KB sequential stream, random seeks, and the
   whole-file extraction path (iscab_extract on a separately opened cabinet). GTA's cabinets record no
   MD5s, so there is no checksum to compare it with.

     OPENGTA_DATA=installer ./build/cab_test [installed folder, default ./game]

   Skips unless OPENGTA_DATA mounts cabinets. */
#include "iscab.h"
#include "vfs_host.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *ref;
static int files, failures;
static uint64_t bytes;
static double read_s;   /* in vfs_read_all */

/* Track8: not in the unshield extraction; its expected size is unshield's listing of it. */
static const char split_file[] = "Music/Track8.wav";
enum { SPLIT_SIZE = 46305044 };
static uint8_t *split_data;
static size_t split_size;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint8_t *read_host(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *size = (size_t)n;
    return b;
}

typedef struct { char names[512][256]; bool dir[512]; int n; } Listing;

static void collect(const char *name, bool is_dir, void *user)
{
    Listing *l = user;
    if (l->n < 512) snprintf(l->names[l->n], 256, "%s", name), l->dir[l->n++] = is_dir;
}

/* Every exposed file under dir equals <ref>/<path> (the split file is kept for the checks below). */
static void walk_vfs(const char *dir)
{
    Listing *l = calloc(1, sizeof *l);
    if (!vfs_list(dir, collect, l)) { printf("FAIL: can't list '%s'\n", dir); failures++; }
    for (int i = 0; i < l->n; i++) {
        char rel[1024], host[2048];
        snprintf(rel, sizeof rel, "%s%s%s", dir, *dir ? "/" : "", l->names[i]);
        if (l->dir[i]) { walk_vfs(rel); continue; }
        snprintf(host, sizeof host, "%s/%s", ref, rel);
        size_t n = 0, m = 0;
        double t = now();
        uint8_t *a = vfs_read_all(rel, &n);
        read_s += now() - t;
        files++;
        bytes += n;
        if (!strcasecmp(rel, split_file)) {
            if (!a) printf("FAIL: %s: can't be read from the cabinets\n", rel), failures++;
            split_data = a, split_size = n;
            continue;
        }
        uint8_t *b = read_host(host, &m);
        if (!a) printf("FAIL: %s: can't be read from the cabinets\n", rel), failures++;
        else if (!b) printf("FAIL: %s: not in %s\n", rel, ref), failures++;
        else if (n != m || memcmp(a, b, n)) printf("FAIL: %s: differs (%zu vs %zu bytes)\n", rel, n, m), failures++;
        free(a);
        free(b);
    }
    free(l);
}

/* Every installed file under <ref>/dir is exposed. */
static void walk_ref(const char *dir)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", ref, dir);
    DIR *d = opendir(path);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char rel[1024], full[3072];
        if (snprintf(rel, sizeof rel, "%s%s%s", dir, *dir ? "/" : "", e->d_name) >= (int)sizeof rel ||
            snprintf(full, sizeof full, "%s/%s", path, e->d_name) >= (int)sizeof full)
            continue;   /* longer than any path in the game */
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) walk_ref(rel);
        else if (!vfs_exists(rel)) printf("FAIL: %s: installed but not in the cabinets\n", rel), failures++;
    }
    if (d) closedir(d);
}

static bool check(bool ok, const char *what)
{
    if (!ok) printf("FAIL: %s: %s\n", split_file, what), failures++;
    return ok;
}

/* RIFF "WAVE": the RIFF size covers the file, a PCM fmt chunk, and the data chunk ends at the end. */
static void check_wav(const uint8_t *p, size_t n)
{
    if (!check(n >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4), "no RIFF/WAVE header")) return;
    check(rd32(p + 4) + 8ull == n, "RIFF size doesn't match the file size");
    uint64_t data_end = 0, data_size = 0;
    unsigned ch = 0, rate = 0, bits = 0, fmt = 0;
    for (size_t o = 12; o + 8 <= n;) {
        uint32_t len = rd32(p + o + 4);
        if (!memcmp(p + o, "fmt ", 4) && len >= 16 && o + 24 <= n)
            fmt = rd16(p + o + 8), ch = rd16(p + o + 10), rate = rd32(p + o + 12), bits = rd16(p + o + 22);
        if (!memcmp(p + o, "data", 4)) data_size = len, data_end = o + 8ull + len;
        o += 8ull + len + (len & 1);
    }
    check(fmt == 1 && ch && rate && bits, "no PCM fmt chunk");
    check(data_end == n, "data chunk doesn't end at the end of the file");
    printf("%s: %zu bytes (expected %d), RIFF/WAVE PCM %u Hz, %u ch, %u bit, data chunk %llu bytes (%.1f s)\n",
           split_file, n, SPLIT_SIZE, rate, ch, bits, (unsigned long long)data_size,
           rate && ch && bits ? data_size / (double)(rate * ch * (bits / 8)) : 0.0);
}

/* The file through vfs_open / vfs_read_at: 4 KB sequential reads, then some seeks (back and forth). */
static void check_stream(const uint8_t *want, size_t n)
{
    VfsFile *f = vfs_open(split_file);
    if (!check(f != NULL, "can't be opened")) return;
    uint8_t *got = malloc(n ? n : 1);
    double t = now();
    bool ok = got != NULL;
    for (size_t o = 0; ok && o < n; o += 4096) {
        size_t k = n - o < 4096 ? n - o : 4096;
        ok = vfs_read_at(f, o, got + o, k) == (int64_t)k;
    }
    double dt = now() - t;
    check(ok && !memcmp(got, want, n), "4 KB sequential reads differ from vfs_read_all");
    printf("%s: streamed in 4 KB reads in %.2f s (%.0f MB/s)\n", split_file, dt, n / 1e6 / dt);
    static const double at[] = { 0.9, 0.1, 0.5, 0.0, 0.93, 0.92, 0.999 };
    uint8_t buf[65536];
    t = now();
    for (size_t k = 0; ok && k < sizeof at / sizeof *at; k++) {
        size_t o = (size_t)(at[k] * n), m = n - o < sizeof buf ? n - o : sizeof buf;
        ok = vfs_read_at(f, o, buf, sizeof buf) == (int64_t)m && !memcmp(buf, want + o, m);
    }
    check(ok, "seeking reads differ");
    printf("%s: %zu seeking 64 KB reads in %.1f ms\n", split_file, sizeof at / sizeof *at, (now() - t) * 1e3);
    free(got);
    vfs_close(f);
}

/* ---- the cabinets opened directly (iscab_extract: the whole-file path) ---- */

static void *io_open(void *ctx, const char *path, uint64_t *size)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    *size = (uint64_t)st.st_size;
    return (void *)(intptr_t)(fd + 1);
}
static int64_t io_read_at(void *ctx, void *h, uint64_t off, void *dst, size_t len)
{
    ssize_t r = pread((int)(intptr_t)h - 1, dst, len, (off_t)off);
    return r < 0 ? -1 : (int64_t)r;
}
static void io_close(void *ctx, void *h) { close((int)(intptr_t)h - 1); }

static void check_extract(const uint8_t *want, size_t n)
{
    const char *root = getenv("OPENGTA_DATA");
    char hdr[1024];
    struct stat st;
    snprintf(hdr, sizeof hdr, "%s/data1.cab", root);
    if (stat(hdr, &st) != 0) { printf("(%s: no data1.cab in %s, iscab_extract not compared)\n", split_file, root); return; }
    VfsBackend io = { NULL, io_open, io_read_at, io_close, NULL, NULL };
    char err[128] = "";
    IsCab *c = iscab_open(&io, hdr, err, sizeof err);
    if (!check(c != NULL, err)) return;
    for (uint32_t i = 0; i < iscab_file_count(c); i++) {
        const IsCabFile *f = iscab_file(c, i);
        if (strcasecmp(f->dir, "Music") || strcasecmp(f->name, "Track8.wav")) continue;
        static const uint8_t zero[16];
        printf("%s: file %u, flags %#x, %llu bytes stored from volume %u; MD5 %s\n", split_file, i, f->flags,
               (unsigned long long)f->stored, f->volume, memcmp(f->md5, zero, 16) ? "recorded (checked)" : "not recorded in this cabinet version");
        uint8_t *d = malloc(f->size ? (size_t)f->size : 1);
        double t = now();
        bool ok = d && iscab_extract(c, i, d);
        double dt = now() - t;
        check(ok && f->size == n && !memcmp(d, want, n), "iscab_extract differs from the stream");
        printf("%s: iscab_extract agrees with the stream (%.2f s)\n", split_file, dt);
        free(d);
    }
    iscab_close(c);
}

int main(int argc, char **argv)
{
    ref = argc > 1 ? argv[1] : "game";
    double t0 = now();
    if (!vfs_mount_default() || !strstr(vfs_describe(), "InstallShield")) {
        printf("SKIP: OPENGTA_DATA is not GTA's installer (folder or image) (mounted: %s)\n", vfs_describe());
        return 0;
    }
    double t1 = now();
    printf("mounted %s in %.1f ms\n", vfs_describe(), (t1 - t0) * 1e3);
    walk_vfs("");
    double t2 = now();
    walk_ref("");
    IsCabStats st = {0};
    vfs_cab_stats(&st);
    printf("%d files, %.1f MB expanded in %.2f s (%.1f MB/s; %u extracts, %u streams, %u cache hits; %.2f s with the comparison)\n",
           files, bytes / 1e6, read_s, bytes / 1e6 / read_s, st.extracts, st.streams, st.cache_hits, t2 - t1);
    if (check(split_data != NULL, "not exposed by the cabinets")) {
        check(split_size == SPLIT_SIZE, "wrong size");
        check_wav(split_data, split_size);
        check_stream(split_data, split_size);
        check_extract(split_data, split_size);
    }
    free(split_data);
    vfs_unmount();
    if (failures) printf("%d failure(s)\n", failures);
    else printf("PASS\n");
    return failures != 0;
}
