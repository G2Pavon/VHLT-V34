#pragma once
// AJM: added file in
#include <cstdlib>

#include "common/win32fix.h"

constexpr int MAX_WADPATHS = 128; // arbitrary

typedef struct
{
    char path[_MAX_PATH];
    bool usedbymap;   // does this map requrie this wad to be included in the bsp?
    int usedtextures; // number of textures in this wad the map actually uses
    int totaltextures;
} wadpath_t; //!!! the above two are VERY DIFFERENT. ie (usedtextures == 0) != (usedbymap == false)

extern wadpath_t *g_pWadPaths[MAX_WADPATHS];
extern int g_iNumWadPaths;

void PushWadPath(const char *const path, bool inuse);
void FreeWadPaths();
void GetUsedWads();