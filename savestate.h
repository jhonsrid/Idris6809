#ifndef SAVESTATE_H
#define SAVESTATE_H

#include "dragon.h"

#define SAVESTATE_MAGIC   0x53364449  /* "ID6S" little-endian */
#define SAVESTATE_VERSION 1

/* Save current emulator state to file. Returns 0 on success. */
int savestate_save(const Dragon *d, const char *path);

/* Load emulator state from file. The Dragon must already be initialized
 * (dragon_init + dragon_load_rom called) so ROM and I/O handlers are set up.
 * Returns 0 on success. */
int savestate_load(Dragon *d, const char *path);

/* Generate a filename: yyyy-mm-dd-hh-mm-ss.state */
void savestate_make_filename(char *buf, size_t bufsize);

#endif
