#pragma once
// AJM: added file in
#include <deque>
#include <string>

constexpr int MAX_WADPATHS = 128; // arbitrary

struct wadpath_t
{
    std::string path;
    bool usedbymap;       // does this map requrie this wad to be included in the bsp?
    int usedtextures = 0; // number of textures in this wad the map actually uses
    int totaltextures = 0;
}; //!!! the above two are VERY DIFFERENT. ie (usedtextures == 0) != (usedbymap == false)

// A deque, so entries never move: textures.cpp keeps pointers to them.
extern std::deque<wadpath_t> g_WadPaths;

void PushWadPath(const char *const path, bool inuse);
void GetUsedWads();
