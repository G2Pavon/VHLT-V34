#include "hlcsg/wadpath.h"

#include <cstring>

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
    char szTmp[_MAX_PATH];
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
            int count = qmin(j - i, _MAX_PATH - 1);
            std::memcpy(szTmp, &pszWadPaths[i], count);
            szTmp[count] = '\0';

            if (g_WadPaths.size() >= MAX_WADPATHS)
            {
                Error("Too many wad files");
            }
            PushWadPath(szTmp, true);
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