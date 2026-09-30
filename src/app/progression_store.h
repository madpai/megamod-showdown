#ifndef MEGAMOD_PROGRESSION_STORE_H
#define MEGAMOD_PROGRESSION_STORE_H
#include "../engine/progression.h"
#include <stddef.h>
typedef enum { MM_SAVE_OK, MM_SAVE_MISSING, MM_SAVE_CORRUPT, MM_SAVE_IO } mm_save_result;
/* Directory belongs to the caller. Identity is 'solo' or 32 hex characters;
 * it never becomes a caller-controlled relative path. */
mm_save_result mm_profile_load(const char *dir, const char *identity, mm_progression *p,
                               char *err, size_t n);
bool mm_profile_save(const char *dir, const char *identity, const mm_progression *p,
                     char *err, size_t n);
bool mm_profile_identity(const char *dir, uint8_t identity[16], char *err, size_t n);
void mm_identity_hex(const uint8_t identity[16], char out[33]);
#endif
