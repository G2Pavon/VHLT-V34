#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

#include "common/bspfile.h"

namespace fs = std::filesystem;

namespace
{
std::vector<byte> ReadAll(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::array<lump_t, HEADER_LUMPS> Lumps(const std::vector<byte> &file)
{
    std::array<lump_t, HEADER_LUMPS> lumps{};
    std::memcpy(lumps.data(), file.data() + 4, sizeof(lumps));
    return lumps;
}

std::vector<byte> LumpBytes(const std::vector<byte> &file, int lump)
{
    const lump_t l = Lumps(file)[lump];
    return {file.begin() + l.fileofs, file.begin() + l.fileofs + l.filelen};
}

// Number of "?_rad<digit>*" textures (lightmaps embedded by hlrad) in a BSP file image.
int CountEmbeddedLightmapTextures(const std::vector<byte> &file)
{
    const std::vector<byte> tex = LumpBytes(file, LUMP_TEXTURES);
    if (tex.size() < 4)
    {
        return 0;
    }
    int nummiptex;
    std::memcpy(&nummiptex, tex.data(), 4);
    int count = 0;
    for (int i = 0; i < nummiptex; i++)
    {
        int ofs;
        std::memcpy(&ofs, tex.data() + 4 + 4 * i, 4);
        if (ofs < 0 || static_cast<size_t>(ofs) + MAX_TEXTURE_NAME_LENGTH > tex.size())
        {
            continue;
        }
        const std::string name(reinterpret_cast<const char *>(tex.data() + ofs), strnlen(reinterpret_cast<const char *>(tex.data() + ofs), MAX_TEXTURE_NAME_LENGTH));
        if (name.size() >= 6 && strncasecmp(name.c_str() + 1, "_rad", 4) == 0 && name[5] >= '0' && name[5] <= '9')
        {
            count++;
        }
    }
    return count;
}

std::vector<byte> Compress(const std::vector<byte> &src)
{
    std::vector<byte> out(src.size() * 2 + 2);
    out.resize(CompressVis(src.data(), static_cast<unsigned>(src.size()), out.data(), static_cast<unsigned>(out.size())));
    return out;
}

std::vector<byte> Decompress(const std::vector<byte> &compressed, int visleafs)
{
    std::memcpy(g_dvisdata, compressed.data(), compressed.size());
    g_visdatasize = static_cast<int>(compressed.size());
    g_dmodels[0].visleafs = visleafs;
    std::vector<byte> out((visleafs + 7) / 8, 0xAA);
    DecompressVis(g_dvisdata, out.data(), static_cast<unsigned>(out.size()));
    return out;
}

using B = std::vector<byte>;
} // namespace

TEST_SUITE("bspfile")
{
    TEST_CASE("disk struct sizes")
    {
        CHECK(sizeof(lump_t) == 8);
        CHECK(sizeof(dheader_t) == 124);
        CHECK(sizeof(dmodel_t) == 64);
        CHECK(sizeof(dplane_t) == 20);
        CHECK(sizeof(dnode_t) == 24);
        CHECK(sizeof(dclipnode_t) == 8);
        CHECK(sizeof(texinfo_t) == 40);
        CHECK(sizeof(dface_t) == 20);
        CHECK(sizeof(dleaf_t) == 28);
        CHECK(sizeof(dedge_t) == 4);
        CHECK(sizeof(dvertex_t) == 12);
        CHECK(sizeof(miptex_t) == 40);
    }

    TEST_CASE("CompressVis run-length encodes zero bytes, runs capped at 255")
    {
        CHECK(Compress(B{0xff, 0, 0, 0, 0x01}) == B{0xff, 0x00, 0x03, 0x01});
        CHECK(Compress(B{0}) == B{0x00, 0x01});
        CHECK(Compress(B(300, 0)) == B{0x00, 0xff, 0x00, 0x2d});
        CHECK(Compress(B{1, 2, 3}) == B{1, 2, 3});
    }

    TEST_CASE("DecompressVis inverts CompressVis for one row of visleafs")
    {
        std::vector<byte> row(300);
        for (size_t i = 0; i < row.size(); i++)
        {
            row[i] = (i % 37 < 20) ? 0 : static_cast<byte>(i * 7 + 1);
        }
        CHECK(Decompress(Compress(row), 300 * 8) == row);
        CHECK(Decompress(Compress(B(300, 0)), 300 * 8) == B(300, 0));
        // visleafs not a multiple of 8: the row is (visleafs + 7) / 8 bytes
        CHECK(Decompress(Compress(B{0x0f, 0}), 12) == B{0x0f, 0});
    }

    TEST_CASE("CalcFaceExtents_test passes (float evaluation matches the engine)")
    {
        CHECK(CalcFaceExtents_test());
    }

    TEST_CASE("GetFaceExtents rounds texture-space bounds out to TEXTURE_STEP")
    {
        const float verts[4][3] = {{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};
        for (int i = 0; i < 4; i++)
        {
            std::memcpy(g_dvertexes[i].point, verts[i], sizeof(verts[i]));
            g_dedges[i + 1].v[0] = static_cast<unsigned short>(i);
            g_dedges[i + 1].v[1] = static_cast<unsigned short>((i + 1) % 4);
            g_dsurfedges[i] = i + 1;
        }
        texinfo_t tex{};
        tex.vecs[0][0] = 1;
        tex.vecs[0][3] = 8;
        tex.vecs[1][1] = -1;
        tex.miptex = -1;
        g_texinfo[0] = tex;
        g_numtexinfo = 1;
        dface_t face{};
        face.numedges = 4;
        face.texinfo = 0;
        g_dfaces[0] = face;
        g_numfaces = 1;

        int mins[2], maxs[2];
        GetFaceExtents(0, mins, maxs);
        CHECK(mins[0] == 0); // floor(8 / 16)
        CHECK(maxs[0] == 5); // ceil(72 / 16)
        CHECK(mins[1] == -4);
        CHECK(maxs[1] == 0);
        g_numfaces = 0;
        g_numtexinfo = 0;
    }

    TEST_CASE("Load/Write round trip on stock BSPs keeps every lump and is idempotent")
    {
        const fs::path dir = VHLT_TEST_BSP_DIR;
        std::vector<fs::path> files;
        if (fs::is_directory(dir))
        {
            for (const auto &e : fs::directory_iterator(dir))
            {
                if (e.path().extension() == ".bsp")
                {
                    files.push_back(e.path());
                }
            }
        }
        if (files.empty())
        {
            MESSAGE("skipped: no .bsp files in " << dir.string() << " (set VHLT_TEST_BSP_DIR)");
            return;
        }
        std::sort(files.begin(), files.end());

        const fs::path tmp = fs::temp_directory_path() / ("vhlt_common_tests_" + std::to_string(getpid()));
        fs::create_directories(tmp);
        const std::string t1 = (tmp / "a.bsp").string(), t2 = (tmp / "b.bsp").string(), t3 = (tmp / "c.bsp").string();

        // The order WriteBSPFile lays lumps out in.
        const int write_order[HEADER_LUMPS] = {LUMP_PLANES, LUMP_LEAFS, LUMP_VERTEXES, LUMP_NODES, LUMP_TEXINFO,
                                               LUMP_FACES, LUMP_CLIPNODES, LUMP_MARKSURFACES, LUMP_SURFEDGES,
                                               LUMP_EDGES, LUMP_MODELS, LUMP_LIGHTING, LUMP_VISIBILITY,
                                               LUMP_ENTITIES, LUMP_TEXTURES};
        dtexdata_init();
        int tested = 0, embedded = 0;
        for (const auto &path : files)
        {
            const std::vector<byte> orig = ReadAll(path);
            int version = 0;
            std::memcpy(&version, orig.data(), 4);
            if (version != BSPVERSION)
            {
                continue;
            }
            CAPTURE(path.filename().string());

            LoadBSPFile(path.string().c_str());
            WriteBSPFile(t1.c_str());
            LoadBSPFile(t1.c_str());
            WriteBSPFile(t2.c_str());
            DeleteEmbeddedLightmaps();
            WriteBSPFile(t3.c_str());

            const std::vector<byte> a = ReadAll(t1), b = ReadAll(t2), c = ReadAll(t3);
            CHECK(a == b);
            if (CountEmbeddedLightmapTextures(orig) == 0)
            {
                CHECK(a == c); // nothing to remove: must not change anything
            }
            else
            {
                // Only texinfo, faces and textures may change, and a second pass is a no-op.
                for (int lump = 0; lump < HEADER_LUMPS; lump++)
                {
                    CAPTURE(lump);
                    if (lump != LUMP_TEXINFO && lump != LUMP_FACES && lump != LUMP_TEXTURES)
                    {
                        CHECK(LumpBytes(a, lump) == LumpBytes(c, lump));
                    }
                }
                CHECK(LumpBytes(c, LUMP_TEXTURES).size() < LumpBytes(a, LUMP_TEXTURES).size());
                const std::string t4 = (tmp / "d.bsp").string();
                LoadBSPFile(t3.c_str());
                DeleteEmbeddedLightmaps();
                WriteBSPFile(t4.c_str());
                CHECK(ReadAll(t4) == c);
                embedded++;
            }
            for (int lump = 0; lump < HEADER_LUMPS; lump++)
            {
                CAPTURE(lump);
                CHECK(LumpBytes(orig, lump) == LumpBytes(a, lump));
            }
            const auto lumps = Lumps(a);
            int ofs = sizeof(dheader_t);
            for (int lump : write_order)
            {
                CAPTURE(lump);
                CHECK(lumps[lump].fileofs == ofs);
                for (int pad = ofs + lumps[lump].filelen; pad < ((ofs + lumps[lump].filelen + 3) & ~3); pad++)
                {
                    CHECK(a[pad] == 0); // padding after each lump is zero-filled
                }
                ofs += (lumps[lump].filelen + 3) & ~3;
            }
            CHECK(static_cast<size_t>(ofs) == a.size());
            tested++;
        }
        dtexdata_free();
        fs::remove_all(tmp);
        MESSAGE("round-tripped " << tested << " BSP files (" << embedded << " with embedded lightmaps) from " << dir.string());
        CHECK(tested > 0);
    }
}
