#pragma once

#include <cstdlib>

constexpr int MAXTOKEN = 4096;
extern char g_token[MAXTOKEN];

typedef struct
{
    char filename[_MAX_PATH];
    char *buffer;
    char *script_p;
    char *end_p;
    int line;
} script_t;

static void AddScriptToStack(const char *const filename);
void LoadScriptFile(const char *const filename);
void ParseFromMemory(char *buffer, int size);
bool EndOfScript(const bool crossline);
bool GetToken(bool crossline);