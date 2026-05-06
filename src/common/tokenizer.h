#pragma once

#include "common/win32fix.h"

constexpr int MAXTOKEN = 4096;
extern char g_token[MAXTOKEN];

struct TokenStream
{
    char filename[_MAX_PATH];
    char *buffer;
    char *cursor;
    char *end;
    int line;
};

void OpenTokenStream(const char *const filename);
void ParseFromMemory(char *buffer, int size);
void CloseTokenStream();
bool GetToken(bool crossline);