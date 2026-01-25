#pragma once

#include <cstdio>

bool q_exists(const char *const filename);
int q_filelength(std::FILE *f);

std::FILE *SafeOpenWrite(const char *const filename);
std::FILE *SafeOpenRead(const char *const filename);
void SafeRead(std::FILE *f, void *buffer, int count);
void SafeWrite(std::FILE *f, const void *const buffer, int count);

int LoadFile(const char *const filename, char **bufferptr);