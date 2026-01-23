#pragma once

#include <cstdio>

extern bool q_exists(const char *const filename);
extern int q_filelength(FILE *f);

extern FILE *SafeOpenWrite(const char *const filename);
extern FILE *SafeOpenRead(const char *const filename);
extern void SafeRead(FILE *f, void *buffer, int count);
extern void SafeWrite(FILE *f, const void *const buffer, int count);

extern int LoadFile(const char *const filename, char **bufferptr);