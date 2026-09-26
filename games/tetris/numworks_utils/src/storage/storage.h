#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

bool extapp_fileExists(const char *filename);
const char *extapp_fileRead(const char *filename, uint32_t *len);
bool extapp_fileWrite(const char *filename, const char *content, uint32_t len);
bool extapp_fileErase(const char *filename);

int np_app_run(void (*game)(void));
void np_app_leave(void);

#endif
