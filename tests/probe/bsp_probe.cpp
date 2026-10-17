// bsp_probe: a minimal driver around src/common's BSP I/O.
//
// Built from the current tree (tests/CMakeLists.txt) and, by parity.py, from the oracle's
// sources, so the two builds can be compared on the same input:
//
//   bsp_probe load FILE                load only; exit 0 if accepted (Error() exits with 1)
//   bsp_probe roundtrip IN OUT         load, write
//   bsp_probe embedded IN OUT          load, DeleteEmbeddedLightmaps, write
//   bsp_probe sizes IN                 load, PrintBSPFileSizes (stdout)
//   bsp_probe textures IN              load, print GetTextureByNumber for every texinfo
//
// Must compile against both the oracle's and the current common/, so it only uses
// functions that exist in both.

#include <cstdio>
#include <cstring>

#include "common/bspfile.h"
#include "common/log.h"

#ifdef HLCSG
bool g_nolightopt = true;
#endif

void GetParamsFromEnt(entity_t *)
{
}

namespace
{
int Usage()
{
    std::fprintf(stderr, "usage: bsp_probe load|roundtrip|embedded|sizes|textures FILE [OUT]\n");
    return 2;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        return Usage();
    }
    const char *mode = argv[1];
    const char *in = argv[2];
    const bool needs_out = !std::strcmp(mode, "roundtrip") || !std::strcmp(mode, "embedded");
    if (needs_out && argc < 4)
    {
        return Usage();
    }

    g_log = false;
    dtexdata_init();
    LoadBSPFile(in);

    if (!std::strcmp(mode, "load"))
    {
    }
    else if (!std::strcmp(mode, "roundtrip"))
    {
        WriteBSPFile(argv[3]);
    }
    else if (!std::strcmp(mode, "embedded"))
    {
        DeleteEmbeddedLightmaps();
        WriteBSPFile(argv[3]);
    }
    else if (!std::strcmp(mode, "sizes"))
    {
        PrintBSPFileSizes();
    }
    else if (!std::strcmp(mode, "textures"))
    {
        for (int i = -1; i < g_numtexinfo; i++)
        {
            std::printf("%d %d \"%.16s\"\n", i, i >= 0 ? g_texinfo[i].miptex : -1, GetTextureByNumber(i));
        }
    }
    else
    {
        return Usage();
    }
    dtexdata_free();
    return 0;
}
