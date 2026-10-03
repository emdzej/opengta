/* The car tuning file (tuning module 0x412d20-0x4131cf): Tune_LoadFile reads ..\gtadata\config.ini
   after Game_Init, a text of "[car <model> <parameter>] <value>" entries that overwrite fields of the
   style's car info records (carinfo.h) in place. The game data has no config.ini (a development
   leftover): the file is optional and the game runs with the style's values. */
#pragma once

void tune_load_file(const char *name);       /* Tune_LoadFile 0x412d20 ("..\gtadata\<name>") */
/* Tune_SetCarParam 0x412e90: the entry key (what was between the brackets, trimmed; e.g. "car 12
   mass") with its value as an integer and as a 16.16 fixed-point float */
void tune_set_car_param(char *key, int value, float fvalue);
