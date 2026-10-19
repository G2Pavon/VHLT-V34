#include "hlcsg/wadpath.h"

#include <cstring>
#include <string>

#include "common/bspfile.h"
#include "common/cmdlib.h"
#include "common/log.h"
#include "common/mathlib.h"
#include "common/win32fix.h"

std::deque<wadpath_t> g_WadPaths;

// adds a wadpath into the wadpaths list (duplicates are kept)
void PushWadPath(const char *const path, bool inuse)
{
    hlassume(g_WadPaths.size() < MAX_WADPATHS, assume_MAX_TEXFILES);

    // compat: the path was stored in a char[_MAX_PATH], truncating longer ones
    g_WadPaths.push_back({std::string(path, strnlen(path, _MAX_PATH - 1)), inuse});
}

// parse the "wad" keyvalue into wadpath_t structs
void GetUsedWads()
{
    int j;

    const char *pszWadPaths = ValueForKey(&g_entities[0], "wad");

    for (int i = 0;;)
    {
        for (j = i; pszWadPaths[j] != '\0'; j++)
        {
            if (pszWadPaths[j] == ';')
            {
                break;
            }
        }
        if (j - i > 0)
        {
            // compat: each entry is cut at _MAX_PATH - 1 bytes (it was copied into a char[_MAX_PATH]);
            // the cut name is what lands in the BSP's "wad" key
            const std::string entry(&pszWadPaths[i], qmin(j - i, _MAX_PATH - 1));

            if (g_WadPaths.size() >= MAX_WADPATHS)
            {
                Error("Too many wad files");
            }
            PushWadPath(entry.c_str(), true);
        }
        if (pszWadPaths[j] == '\0')
        {
            break;
        }
        else
        {
            i = j + 1;
        }
    }
}