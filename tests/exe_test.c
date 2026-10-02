/* The original program is found through the file layer, checked, and read by virtual address
   (src/exe.c). Needs the data root (./game or OPENGTA_DATA). */
#include "exe.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("FAIL: %s\n", err); return 1; }
    int fail = 0;
    /* WinMain's mutex name (0x4b0468) and the style path format (0x4b3360) */
    if (strcmp(exe_str(0x4b0468), "Grand Theft Auto")) { printf("FAIL: mutex name '%s'\n", exe_str(0x4b0468)); fail = 1; }
    if (strcmp(exe_str(0x4b3360), "..\\gtadata\\style%03d.g24")) { printf("FAIL: style path '%s'\n", exe_str(0x4b3360)); fail = 1; }
    /* code bytes: WinMain starts with a stack-frame setup, not zero */
    if (!exe_data(0x437230, 16) || exe_u32(0x437230) == 0) { printf("FAIL: code at 0x437230\n"); fail = 1; }
    /* uninitialised data (bss) is not in the file */
    if (exe_data(0x511e28, 4)) { printf("FAIL: bss 0x511e28 readable\n"); fail = 1; }
    if (exe_data(0x3ff000, 4)) { printf("FAIL: below the image\n"); fail = 1; }
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
