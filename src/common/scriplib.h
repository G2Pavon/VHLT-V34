#pragma once

#include <cstdlib>

constexpr int MAXTOKEN = 4096;

extern char g_token[MAXTOKEN];

void LoadScriptFile(const char *const filename);
void ParseFromMemory(char *buffer, int size);

bool GetToken(bool crossline);

constexpr int MAX_WAD_PATHS = 42;
extern char g_szWadPaths[MAX_WAD_PATHS][_MAX_PATH];
extern int g_iNumWadPaths;