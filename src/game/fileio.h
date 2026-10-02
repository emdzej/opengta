/* The shared sequential input file of fileio (0x42e270-0x42e540): one file open at a time, read with
   fgetc-style tokenisers (used by Mission_ReadIni 0x44ab90) or in exact-size blocks. The original
   reads through a CRT FILE; the port reads the whole file through the file layer at open and keeps a
   cursor. Paths are the original's ("..\gtadata\mission.ini"): a leading "..\" is dropped, the rest
   is relative to the data root. Errors are fatal (game_fatal), as in the original. */
#pragma once
#include <stddef.h>
#include <stdint.h>

void textfile_open(const char *path);        /* TextFile_Open 0x42e270 (closes the previous one) */
void textfile_close(void);                   /* TextFile_Close 0x42e2e0 */
void textfile_seek(long delta);              /* TextFile_Seek 0x42e320 (relative) */
void textfile_read(void *dst, size_t n);     /* TextFile_Read 0x42e390 (exactly n bytes) */
void textfile_skip_to(const char *s);        /* TextFile_SkipTo 0x42e400 */
int textfile_read_int(void);                 /* TextFile_ReadInt 0x42e440 */
void textfile_read_string(char *dst, int max, char delim);   /* TextFile_ReadString 0x42e540 */

/* The path of the data layer for an original path ("..\gtadata\x" -> "gtadata\x"). */
const char *textfile_rel(const char *path);
