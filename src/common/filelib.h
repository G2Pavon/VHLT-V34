#pragma once

#include <cstdio>

extern bool q_exists(const char *const filename);
extern int q_filelength(std::FILE *f);

extern std::FILE *SafeOpenWrite(const char *const filename);
extern std::FILE *SafeOpenRead(const char *const filename);
extern void SafeRead(std::FILE *f, void *buffer, int count);
extern void SafeWrite(std::FILE *f, const void *const buffer, int count);

extern int LoadFile(const char *const filename, char **bufferptr);