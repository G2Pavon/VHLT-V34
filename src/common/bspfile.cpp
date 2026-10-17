#include "common/bspfile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <optional>
#include <string>
#include <vector>

#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/hlassert.h"
#include "common/log.h"
#include "common/messages.h"
#include "common/mathlib.h"
#include "common/tokenizer.h"

// BSP lumps are read and written as raw memory images of the little-endian disk format.
static_assert(std::endian::native == std::endian::little, "BSP I/O assumes a little-endian host");

int g_max_map_miptex = DEFAULT_MAX_MAP_MIPTEX;
int g_max_map_lightdata = DEFAULT_MAX_MAP_LIGHTDATA;

int g_nummodels;
dmodel_t g_dmodels[MAX_MAP_MODELS];

int g_visdatasize;
byte g_dvisdata[MAX_MAP_VISIBILITY];

int g_lightdatasize;
byte *g_dlightdata;
static long long s_lightdata_capacity = 0; // bytes allocated for g_dlightdata

int g_texdatasize;
byte *g_dtexdata; // (dmiptexlump_t)
static long long s_texdata_capacity = 0; // bytes allocated for g_dtexdata

int g_entdatasize;
char g_dentdata[MAX_MAP_ENTSTRING];

int g_numleafs;
dleaf_t g_dleafs[MAX_MAP_LEAFS];

int g_numplanes;
dplane_t g_dplanes[MAX_INTERNAL_MAP_PLANES];

int g_numvertexes;
dvertex_t g_dvertexes[MAX_MAP_VERTS];

int g_numnodes;
dnode_t g_dnodes[MAX_MAP_NODES];

int g_numtexinfo;

texinfo_t g_texinfo[MAX_INTERNAL_MAP_TEXINFO];

int g_numfaces;
dface_t g_dfaces[MAX_MAP_FACES];

int g_numclipnodes;
dclipnode_t g_dclipnodes[MAX_MAP_CLIPNODES];

int g_numedges;
dedge_t g_dedges[MAX_MAP_EDGES];

int g_nummarksurfaces;
unsigned short g_dmarksurfaces[MAX_MAP_MARKSURFACES];

int g_numsurfedges;
int g_dsurfedges[MAX_MAP_SURFEDGES];

int g_numentities;
entity_t g_entities[MAX_MAP_ENTITIES];

int CompressVis(const byte *const src, const unsigned int src_length, byte *dest, unsigned int dest_length)
{
    byte *dest_p = dest;
    unsigned int current_length = 0;

    for (unsigned int j = 0; j < src_length; j++)
    {
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = src[j];
        dest_p++;

        if (src[j])
        {
            continue;
        }

        unsigned char rep = 1;

        for (j++; j < src_length; j++)
        {
            if (src[j] || rep == 255)
            {
                break;
            }
            else
            {
                rep++;
            }
        }
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = rep;
        dest_p++;
        j--;
    }

    return dest_p - dest;
}

void DecompressVis(const byte *src, byte *const dest, const unsigned int dest_length)
{
    unsigned int current_length = 0;

    int row = (g_dmodels[0].visleafs + 7) >> 3; // same as the length used by VIS program in CompressVis
                                                // The wrong size will cause DecompressVis to spend extremely long time once the source pointer runs into the invalid area in g_dvisdata (for example, in BuildFaceLights, some faces could hang for a few seconds), and sometimes to crash.

    byte *out = dest;

    do
    {
        hlassume(src >= g_dvisdata && src - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        if (*src)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = *src;
            out++;
            src++;
            continue;
        }

        hlassume(&src[1] - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        int c = src[1];
        src += 2;
        while (c)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = 0;
            out++;
            c--;

            if (out - dest >= row)
            {
                return;
            }
        }
    } while (out - dest < row);
}

namespace
{
// One BSP lump as it lives in memory: the global array (or buffer) and its record count.
// For the byte lumps (textures, visibility, lighting, entities) the record is one byte
// and the count is the data size.
struct LumpStorage
{
    const char *name;
    void *data;
    int *count;
    int recordsize;
    long long capacity; // records the destination can hold
};

template <typename T, std::size_t N>
LumpStorage Records(const char *name, T (&array)[N], int &count)
{
    return {name, array, &count, static_cast<int>(sizeof(T)), static_cast<long long>(N)};
}

// Indexed by LUMP_* number.
std::array<LumpStorage, HEADER_LUMPS> LumpTable()
{
    std::array<LumpStorage, HEADER_LUMPS> t{};
    t[LUMP_ENTITIES] = Records("entities", g_dentdata, g_entdatasize);
    t[LUMP_PLANES] = Records("planes", g_dplanes, g_numplanes);
    t[LUMP_TEXTURES] = {"textures", g_dtexdata, &g_texdatasize, 1, s_texdata_capacity};
    t[LUMP_VERTEXES] = Records("vertexes", g_dvertexes, g_numvertexes);
    t[LUMP_VISIBILITY] = Records("visibility", g_dvisdata, g_visdatasize);
    t[LUMP_NODES] = Records("nodes", g_dnodes, g_numnodes);
    t[LUMP_TEXINFO] = Records("texinfo", g_texinfo, g_numtexinfo);
    t[LUMP_FACES] = Records("faces", g_dfaces, g_numfaces);
    t[LUMP_LIGHTING] = {"lighting", g_dlightdata, &g_lightdatasize, 1, s_lightdata_capacity};
    t[LUMP_CLIPNODES] = Records("clipnodes", g_dclipnodes, g_numclipnodes);
    t[LUMP_LEAFS] = Records("leafs", g_dleafs, g_numleafs);
    t[LUMP_MARKSURFACES] = Records("marksurfaces", g_dmarksurfaces, g_nummarksurfaces);
    t[LUMP_EDGES] = Records("edges", g_dedges, g_numedges);
    t[LUMP_SURFEDGES] = Records("surfedges", g_dsurfedges, g_numsurfedges);
    t[LUMP_MODELS] = Records("models", g_dmodels, g_nummodels);
    return t;
}

// The order LoadBSPFile has always processed lumps in (decides which error a broken file reports).
constexpr int k_load_order[HEADER_LUMPS] = {LUMP_MODELS, LUMP_VERTEXES, LUMP_PLANES, LUMP_LEAFS, LUMP_NODES,
                                            LUMP_TEXINFO, LUMP_CLIPNODES, LUMP_FACES, LUMP_MARKSURFACES,
                                            LUMP_SURFEDGES, LUMP_EDGES, LUMP_TEXTURES, LUMP_VISIBILITY,
                                            LUMP_LIGHTING, LUMP_ENTITIES};

// The order WriteBSPFile lays lumps out in the file; compat: must not change.
constexpr int k_write_order[HEADER_LUMPS] = {LUMP_PLANES, LUMP_LEAFS, LUMP_VERTEXES, LUMP_NODES, LUMP_TEXINFO,
                                             LUMP_FACES, LUMP_CLIPNODES, LUMP_MARKSURFACES, LUMP_SURFEDGES,
                                             LUMP_EDGES, LUMP_MODELS, LUMP_LIGHTING, LUMP_VISIBILITY,
                                             LUMP_ENTITIES, LUMP_TEXTURES};

} // namespace

// Rejects files whose lumps refer to records that do not exist. Only references that the
// tools follow as array indices are checked. visofs and lightofs are not: real maps carry
// garbage there (e.g. visofs in stock plants_soc_final.bsp) and only hlrad reads them.
static void ValidateReferences()
{
    for (int i = 0; i < g_numfaces; i++)
    {
        const dface_t &f = g_dfaces[i];
        if (f.planenum >= g_numplanes)
        {
            Error("LoadBSPFile: face %d refers to plane %d, but there are only %d", i, f.planenum, g_numplanes);
        }
        if (f.texinfo < 0 || f.texinfo >= g_numtexinfo)
        {
            Error("LoadBSPFile: face %d refers to texinfo %d, but there are only %d", i, f.texinfo, g_numtexinfo);
        }
        if (f.firstedge < 0 || f.numedges < 0 || static_cast<long long>(f.firstedge) + f.numedges > g_numsurfedges)
        {
            Error("LoadBSPFile: face %d uses surfedges %d..%lld, but there are only %d", i, f.firstedge, static_cast<long long>(f.firstedge) + f.numedges - 1, g_numsurfedges);
        }
    }
    for (int i = 0; i < g_numsurfedges; i++)
    {
        const long long e = g_dsurfedges[i];
        if (e <= -g_numedges || e >= g_numedges)
        {
            Error("LoadBSPFile: surfedge %d refers to edge %lld, but there are only %d", i, e, g_numedges);
        }
    }
    for (int i = 0; i < g_numedges; i++)
    {
        for (const int v : g_dedges[i].v)
        {
            if (v >= g_numvertexes)
            {
                Error("LoadBSPFile: edge %d refers to vertex %d, but there are only %d", i, v, g_numvertexes);
            }
        }
    }
    for (int i = 0; i < g_nummarksurfaces; i++)
    {
        if (g_dmarksurfaces[i] >= g_numfaces)
        {
            Error("LoadBSPFile: marksurface %d refers to face %d, but there are only %d", i, g_dmarksurfaces[i], g_numfaces);
        }
    }
    for (int i = 0; i < g_numleafs; i++)
    {
        const dleaf_t &l = g_dleafs[i];
        if (l.firstmarksurface + l.nummarksurfaces > g_nummarksurfaces)
        {
            Error("LoadBSPFile: leaf %d uses marksurfaces %d..%d, but there are only %d", i, l.firstmarksurface, l.firstmarksurface + l.nummarksurfaces - 1, g_nummarksurfaces);
        }
    }
    for (int i = 0; i < g_numnodes; i++)
    {
        const dnode_t &n = g_dnodes[i];
        if (n.planenum < 0 || n.planenum >= g_numplanes)
        {
            Error("LoadBSPFile: node %d refers to plane %d, but there are only %d", i, n.planenum, g_numplanes);
        }
        for (int side = 0; side < 2; side++)
        {
            const int c = n.children[side];
            if (c >= 0 && c >= g_numnodes)
            {
                Error("LoadBSPFile: node %d child %d refers to node %d, but there are only %d", i, side, c, g_numnodes);
            }
            if (c < 0 && -(c + 1) >= g_numleafs)
            {
                Error("LoadBSPFile: node %d child %d refers to leaf %d, but there are only %d", i, side, -(c + 1), g_numleafs);
            }
        }
        if (n.firstface + n.numfaces > g_numfaces)
        {
            Error("LoadBSPFile: node %d uses faces %d..%d, but there are only %d", i, n.firstface, n.firstface + n.numfaces - 1, g_numfaces);
        }
    }
    for (int i = 0; i < g_numclipnodes; i++)
    {
        const dclipnode_t &c = g_dclipnodes[i];
        if (c.planenum < 0 || c.planenum >= g_numplanes)
        {
            Error("LoadBSPFile: clipnode %d refers to plane %d, but there are only %d", i, c.planenum, g_numplanes);
        }
        for (int side = 0; side < 2; side++)
        {
            if (c.children[side] >= g_numclipnodes) // negative children are contents
            {
                Error("LoadBSPFile: clipnode %d child %d refers to clipnode %d, but there are only %d", i, side, c.children[side], g_numclipnodes);
            }
        }
    }
    for (int i = 0; i < g_nummodels; i++)
    {
        const dmodel_t &m = g_dmodels[i];
        const int head = m.headnode[0];
        if (head >= 0 && head >= g_numnodes)
        {
            Error("LoadBSPFile: model %d head node refers to node %d, but there are only %d", i, head, g_numnodes);
        }
        if (head < 0 && -(static_cast<long long>(head) + 1) >= g_numleafs)
        {
            Error("LoadBSPFile: model %d head node refers to leaf %lld, but there are only %d", i, -(static_cast<long long>(head) + 1), g_numleafs);
        }
        for (int hull = 1; hull < MAX_MAP_HULLS; hull++)
        {
            if (m.headnode[hull] >= g_numclipnodes) // negative head nodes are contents
            {
                Error("LoadBSPFile: model %d hull %d refers to clipnode %d, but there are only %d", i, hull, m.headnode[hull], g_numclipnodes);
            }
        }
        if (m.firstface < 0 || m.numfaces < 0 || static_cast<long long>(m.firstface) + m.numfaces > g_numfaces)
        {
            Error("LoadBSPFile: model %d uses faces %d..%lld, but there are only %d", i, m.firstface, static_cast<long long>(m.firstface) + m.numfaces - 1, g_numfaces);
        }
    }
}

void LoadBSPFile(const char *const filename)
{
    const std::vector<char> file = ReadFile(filename);
    const long long filesize = static_cast<long long>(file.size());

    if (file.size() < sizeof(dheader_t))
    {
        Error("LoadBSPFile: %s is too small to be a BSP file (%lld bytes, header is %d)", filename, filesize, static_cast<int>(sizeof(dheader_t)));
    }
    dheader_t header;
    std::memcpy(&header, file.data(), sizeof(header));

    if (header.version != BSPVERSION)
    {
        Error("BSP is version %i, not %i", header.version, BSPVERSION);
    }
    if (!g_dtexdata || !g_dlightdata)
    {
        Error("LoadBSPFile: internal error: texture and lighting buffers are not allocated (dtexdata_init)");
    }

    // Validate every lump before anything is copied.
    const std::array<LumpStorage, HEADER_LUMPS> table = LumpTable();
    for (const int lump : k_load_order)
    {
        const LumpStorage &l = table[lump];
        const lump_t &disk = header.lumps[lump];
        if (disk.fileofs < 0 || disk.filelen < 0)
        {
            Error("LoadBSPFile: lump %s (%d) has a negative offset or length (ofs %d, len %d)", l.name, lump, disk.fileofs, disk.filelen);
        }
        if (static_cast<long long>(disk.fileofs) + disk.filelen > filesize)
        {
            Error("LoadBSPFile: lump %s (%d) extends past end of file (ofs %d + len %d > %lld)", l.name, lump, disk.fileofs, disk.filelen, filesize);
        }
        if (disk.filelen % l.recordsize)
        {
            Error("LoadBSPFile: odd lump size");
        }
        // special handling for tex and lightdata to keep things from exploding - KGP
        if (lump == LUMP_TEXTURES)
        {
            hlassume(g_max_map_miptex > disk.filelen, assume_MAX_MAP_MIPTEX);
        }
        else if (lump == LUMP_LIGHTING)
        {
            hlassume(g_max_map_lightdata > disk.filelen, assume_MAX_MAP_LIGHTING);
        }
        const long long records = disk.filelen / l.recordsize;
        if (records > l.capacity)
        {
            Error("LoadBSPFile: lump %s (%d) has %lld records, limit is %lld", l.name, lump, records, l.capacity);
        }
    }

    for (const int lump : k_load_order)
    {
        const LumpStorage &l = table[lump];
        const lump_t &disk = header.lumps[lump];
        std::memcpy(l.data, file.data() + disk.fileofs, disk.filelen);
        *l.count = disk.filelen / l.recordsize;
    }
    ValidateReferences();
}

void WriteBSPFile(const char *const filename)
{
    dheader_t header{};
    header.version = BSPVERSION;

    std::vector<byte> image(sizeof(dheader_t));
    const std::array<LumpStorage, HEADER_LUMPS> table = LumpTable();
    for (const int lump : k_write_order)
    {
        const LumpStorage &l = table[lump];
        const long long length = static_cast<long long>(*l.count) * l.recordsize;
        if (*l.count < 0 || *l.count > l.capacity)
        {
            Error("WriteBSPFile: internal error: lump %s (%d) has %d records, limit is %lld", l.name, lump, *l.count, l.capacity);
        }
        if (static_cast<long long>(image.size()) + length + 3 > INT32_MAX)
        {
            Error("WriteBSPFile: %s would exceed 2 GB", filename);
        }
        header.lumps[lump].fileofs = static_cast<std::int32_t>(image.size());
        header.lumps[lump].filelen = static_cast<std::int32_t>(length);
        const byte *data = static_cast<const byte *>(l.data);
        image.insert(image.end(), data, data + length);
        image.resize((image.size() + 3) & ~std::size_t{3}, 0); // lumps are 4-byte aligned; padding is zero
    }
    std::memcpy(image.data(), &header, sizeof(header));

    UniqueFile bspfile = SafeOpen(filename, "wb");
    SafeWrite(bspfile.get(), image.data(), static_cast<int>(image.size()));
}

static float CalculatePointVecsProduct(const volatile float *point, const volatile float *vecs)
{
    volatile double val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
    volatile double tmp = (double)point[1] * (double)vecs[1];
    val = val + tmp;
    tmp = (double)point[2] * (double)vecs[2];
    val = val + tmp;
    val = val + (double)vecs[3];

    return (float)val;
}

// (int)v without undefined behaviour. compat: out-of-range values and NaN (only seen with
// damaged geometry) give INT_MIN, which is what the x86 conversion instruction returned.
static int TruncateToInt(double v)
{
    if (!(v >= -2147483648.0 && v < 2147483648.0))
    {
        return INT_MIN;
    }
    return static_cast<int>(v);
}

bool CalcFaceExtents_test()
{
    const int numtestcases = 6;
    volatile float testcases[numtestcases][8] = {
        {1, 1, 1, 1, 0.375 * DBL_EPSILON, 0.375 * DBL_EPSILON, -1, 0},
        {1, 1, 1, 0.375 * DBL_EPSILON, 0.375 * DBL_EPSILON, 1, -1, DBL_EPSILON},
        {DBL_EPSILON, DBL_EPSILON, 1, 0.375, 0.375, 1, -1, DBL_EPSILON},
        {1, 1, 1, 1, 1, 0.375 * FLT_EPSILON, -2, 0.375 * FLT_EPSILON},
        {1, 1, 1, 1, 0.375 * FLT_EPSILON, 1, -2, 0.375 * FLT_EPSILON},
        {1, 1, 1, 0.375 * FLT_EPSILON, 1, 1, -2, 0.375 * FLT_EPSILON}};

    // If the test failed, please check:
    //   1. whether the calculation is performed on FPU
    //   2. whether the register precision is too low

    bool ok = true;
    for (int i = 0; i < 6; i++)
    {
        float val = CalculatePointVecsProduct(&testcases[i][0], &testcases[i][3]);
        if (val != testcases[i][7])
        {
            Warning("internal error: CalcFaceExtents_test failed on case %d (%.20f != %.20f).", i, val, testcases[i][7]);
            ok = false;
        }
    }
    return ok;
}

// purpose: get the actual texinfo for a face. the tools shouldn't directly use f->texinfo after embedlightmap is done
static int ParseImplicitTexinfoFromTexture(int miptex)
{
    const MiptexLumpView lump = TextureLump();
    const int numtextures = lump.count();
    char name[MAX_TEXTURE_NAME_LENGTH];

    if (miptex < 0 || miptex >= numtextures)
    {
        Warning("ParseImplicitTexinfoFromTexture: internal error: invalid texture number %d.", miptex);
        return -1;
    }
    const std::optional<int> offset = lump.dataofs(miptex);
    if (!offset || *offset < MiptexLumpView::DataofsPosition(numtextures))
    {
        return -1; // inside the header (or a missing texture's -1)
    }
    const std::optional<miptex_t> mt = lump.miptex_at(*offset);
    if (!mt)
    {
        return -1;
    }

    safe_strncpy(name, mt->name, MAX_TEXTURE_NAME_LENGTH);

    if (!(std::strlen(name) >= 6 && !strncasecmp(&name[1], "_rad", 4) && '0' <= name[5] && name[5] <= '9'))
    {
        return -1;
    }

    int texinfo = std::atoi(&name[5]);
    if (texinfo < 0 || texinfo >= g_numtexinfo)
    {
        Warning("Invalid index of original texinfo: %d parsed from texture name '%s'.", texinfo, name);
        return -1;
    }

    return texinfo;
}

static int ParseTexinfoForFace(const dface_t *f)
{
    int texinfo = f->texinfo;
    int miptex = g_texinfo[texinfo].miptex;
    if (miptex != -1)
    {
        int texinfo2 = ParseImplicitTexinfoFromTexture(miptex);
        if (texinfo2 != -1)
        {
            texinfo = texinfo2;
        }
    }

    return texinfo;
}

void GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2])
{
    dvertex_t *v;
    int bmins[2], bmaxs[2];

    dface_t *f = &g_dfaces[facenum];

    float mins[2] = {999999.0f, 999999.0f};
    float maxs[2] = {-99999.0f, -99999.0f};

    texinfo_t *tex = &g_texinfo[ParseTexinfoForFace(f)];

    for (int i = 0; i < f->numedges; i++)
    {
        int e = g_dsurfedges[f->firstedge + i];
        if (e >= 0)
        {
            v = &g_dvertexes[g_dedges[e].v[0]];
        }
        else
        {
            v = &g_dvertexes[g_dedges[-e].v[1]];
        }
        for (int j = 0; j < 2; j++)
        {
            // The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
            //   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
            // The new code will produce the same result as the old code, but it's portable for different platforms.
            // See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

            // The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
            // The counterpart of game engine is the function CalcFaceExtents in HLSDK.
            // So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
            float val = CalculatePointVecsProduct(v->point, tex->vecs[j]);
            if (val < mins[j])
            {
                mins[j] = val;
            }
            if (val > maxs[j])
            {
                maxs[j] = val;
            }
        }
    }

    for (int i = 0; i < 2; i++)
    {
        bmins[i] = TruncateToInt(std::floor(mins[i] / TEXTURE_STEP));
        bmaxs[i] = TruncateToInt(std::ceil(maxs[i] / TEXTURE_STEP));
    }

    for (int i = 0; i < 2; i++)
    {
        mins_out[i] = bmins[i];
        maxs_out[i] = bmaxs[i];
    }
}

void WriteExtentFile(const char *const filename)
{
    UniqueFile file = SafeOpen(filename, "w");
    std::FILE *f = file.get();
    std::fprintf(f, "%i\n", g_numfaces);
    for (int i = 0; i < g_numfaces; i++)
    {
        int mins[2];
        int maxs[2];
        GetFaceExtents(i, mins, maxs);
        std::fprintf(f, "%i %i %i %i\n", mins[0], mins[1], maxs[0], maxs[1]);
    }
}

namespace
{
struct lightmapblock_t
{
    static constexpr int BLOCK_WIDTH = 128;
    static constexpr int BLOCK_HEIGHT = 128;

    bool used;
    int allocated[BLOCK_WIDTH];
};
} // namespace

static void DoAllocBlock(std::vector<lightmapblock_t> &blocks, int w, int h)
{
    // code from Quake
    int j;
    int x = 0;
    if (w < 1 || h < 1)
    {
        Error("DoAllocBlock: internal error.");
    }
    for (std::size_t b = 0; b < blocks.size(); b++)
    {
        lightmapblock_t &block = blocks[b];
        int best = lightmapblock_t::BLOCK_HEIGHT;
        for (int i = 0; i < lightmapblock_t::BLOCK_WIDTH - w; i++)
        {
            int best2 = 0;
            for (j = 0; j < w; j++)
            {
                if (block.allocated[i + j] >= best)
                    break;
                if (block.allocated[i + j] > best2)
                    best2 = block.allocated[i + j];
            }
            if (j == w)
            {
                x = i;
                best = best2;
            }
        }
        if (best + h <= lightmapblock_t::BLOCK_HEIGHT)
        {
            block.used = true;
            for (int i = 0; i < w; i++)
            {
                block.allocated[x + i] = best + h;
            }
            return;
        }
        if (b + 1 == blocks.size())
        { // need to allocate a new block
            if (!block.used)
            {
                Warning("CountBlocks: invalid extents %dx%d", w, h);
                return;
            }
            blocks.push_back(lightmapblock_t{}); // invalidates block; the loop continues with the new one
        }
    }
}

static int CountBlocks()
{
    std::vector<lightmapblock_t> blocks(1);

    for (int k = 0; k < g_numfaces; k++)
    {
        dface_t *f = &g_dfaces[k];
        const char *texname = GetTextureByNumber(ParseTexinfoForFace(f));
        if (!std::strncmp(texname, "sky", 3)                                                                            //sky, no lightmap allocation.
            || !std::strncmp(texname, "!", 1) || !strncasecmp(texname, "water", 5) || !strncasecmp(texname, "laser", 5) //water, no lightmap allocation.
            || (g_texinfo[ParseTexinfoForFace(f)].flags & TEX_SPECIAL)                                                  //aaatrigger, I don't know.
        )
        {
            continue;
        }
        int extents[2];
        vec3_t point;
        {
            int bmins[2];
            int bmaxs[2];
            GetFaceExtents(k, bmins, bmaxs);
            for (int i = 0; i < 2; i++)
            {
                // compat: wraps like the old signed arithmetic did on x86, without the UB
                extents[i] = static_cast<int>((static_cast<unsigned>(bmaxs[i]) - static_cast<unsigned>(bmins[i])) * TEXTURE_STEP);
            }

            VectorClear(point);
            if (f->numedges > 0)
            {
                int e = g_dsurfedges[f->firstedge];
                dvertex_t *v = &g_dvertexes[g_dedges[std::abs(e)].v[e >= 0 ? 0 : 1]];
                VectorCopy(v->point, point);
            }
        }
        if (extents[0] < 0 || extents[1] < 0 || extents[0] > qmax(512, MAX_SURFACE_EXTENT * TEXTURE_STEP) || extents[1] > qmax(512, MAX_SURFACE_EXTENT * TEXTURE_STEP))
        // the default restriction from the engine is 512, but place 'max (512, MAX_SURFACE_EXTENT * TEXTURE_STEP)' here in case someone raise the limit
        {
            Warning("Bad surface extents %d/%d at position (%.0f,%.0f,%.0f)", extents[0], extents[1], point[0], point[1], point[2]);
            continue;
        }
        DoAllocBlock(blocks, (extents[0] / TEXTURE_STEP) + 1, (extents[1] / TEXTURE_STEP) + 1);
    }
    int count = 0;
    for (const lightmapblock_t &block : blocks)
    {
        if (block.used)
        {
            count++;
        }
    }
    return count;
}

static bool NoWadTextures()
{
    // copied from loadtextures.cpp
    const MiptexLumpView lump = TextureLump();
    const int numtextures = lump.count();
    for (int i = 0; i < numtextures; i++)
    {
        const std::optional<int> offset = lump.dataofs(i);
        if (!offset)
        {
            break; // nummiptex claims more slots than the buffer holds
        }
        const std::optional<miptex_t> mt = lump.miptex_at(*offset);
        if (!mt)
        {
            // missing textures have ofs -1
            continue;
        }
        if (!mt->offsets[0])
        {
            return false;
        }
    }
    return true;
}

static std::optional<std::string> FindWadValue()
// return nullopt for syntax error
// this function needs to be as stable as possible because it might be called from ripent
{
    int lineend;
    bool inentity = false;
    for (int linestart = 0; linestart < g_entdatasize;)
    {
        for (lineend = linestart; lineend < g_entdatasize; lineend++)
            if (g_dentdata[lineend] == '\r' || g_dentdata[lineend] == '\n')
                break;
        if (lineend == linestart + 1)
        {
            if (g_dentdata[linestart] == '{')
            {
                if (inentity)
                    return std::nullopt;
                inentity = true;
            }
            else if (g_dentdata[linestart] == '}')
            {
                if (!inentity)
                    return std::nullopt;
                inentity = false;
                return std::string(); // only parse the first entity
            }
            else
                return std::nullopt;
        }
        else
        {
            if (!inentity)
                return std::nullopt;
            int quotes[4];
            int i, j;
            for (i = 0, j = linestart; i < 4; i++, j++)
            {
                for (; j < lineend; j++)
                    if (g_dentdata[j] == '\"')
                        break;
                if (j >= lineend)
                    break;
                quotes[i] = j;
            }
            if (i != 4 || quotes[0] != linestart || quotes[3] != lineend - 1)
            {
                return std::nullopt;
            }
            if (quotes[1] - (quotes[0] + 1) == (int)std::strlen("wad") && !std::strncmp(&g_dentdata[quotes[0] + 1], "wad", std::strlen("wad")))
            {
                int len = quotes[3] - (quotes[2] + 1);
                return std::string(&g_dentdata[quotes[2] + 1], len);
            }
        }
        for (linestart = lineend; linestart < g_entdatasize; linestart++)
            if (g_dentdata[linestart] != '\r' && g_dentdata[linestart] != '\n')
                break;
    }
    return std::nullopt;
}

#define ENTRIES(a) (sizeof(a) / sizeof(*(a)))
#define ENTRYSIZE(a) (sizeof(*(a)))

static int ArrayUsage(const char *const szItem, const int items, const int maxitems, const int itemsize)
{
    float percentage = maxitems ? items * 100.0 / maxitems : 0.0;

    Log("%-13s %7i/%-7i %8i/%-8i (%4.1f%%)\n", szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage);

    return items * itemsize;
}

// pritn out global ussage line in chart
static int GlobUsage(const char *const szItem, const int itemstorage, const int maxstorage)
{
    float percentage = maxstorage ? itemstorage * 100.0 / maxstorage : 0.0;

    Log("%-13s    [variable]   %8i/%-8i (%4.1f%%)\n", szItem, itemstorage, maxstorage, percentage);

    return itemstorage;
}

// Dumps info about current file
void PrintBSPFileSizes()
{
    int numtextures = TextureLump().count();
    int totalmemory = 0;
    int numallocblocks = CountBlocks();
    int maxallocblocks = 64;
    bool nowadtextures = NoWadTextures(); // We don't have this check at hlcsg, because only legacy compile tools don't empty "wad" value in "-nowadtextures" compiles.
    const std::optional<std::string> wadvalue = FindWadValue();

    Log("\n");
    Log("Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n");
    Log("------------  ---------------  ---------------  --------\n");

    totalmemory += ArrayUsage("models", g_nummodels, ENTRIES(g_dmodels), ENTRYSIZE(g_dmodels));
    totalmemory += ArrayUsage("planes", g_numplanes, MAX_MAP_PLANES, ENTRYSIZE(g_dplanes));
    totalmemory += ArrayUsage("vertexes", g_numvertexes, ENTRIES(g_dvertexes), ENTRYSIZE(g_dvertexes));
    totalmemory += ArrayUsage("nodes", g_numnodes, ENTRIES(g_dnodes), ENTRYSIZE(g_dnodes));
    totalmemory += ArrayUsage("texinfos", g_numtexinfo, MAX_MAP_TEXINFO, ENTRYSIZE(g_texinfo));
    totalmemory += ArrayUsage("faces", g_numfaces, ENTRIES(g_dfaces), ENTRYSIZE(g_dfaces));
    totalmemory += ArrayUsage("* worldfaces", (g_nummodels > 0 ? g_dmodels[0].numfaces : 0), MAX_MAP_WORLDFACES, 0);
    totalmemory += ArrayUsage("clipnodes", g_numclipnodes, ENTRIES(g_dclipnodes), ENTRYSIZE(g_dclipnodes));
    totalmemory += ArrayUsage("leaves", g_numleafs, MAX_MAP_LEAFS, ENTRYSIZE(g_dleafs));
    totalmemory += ArrayUsage("* worldleaves", (g_nummodels > 0 ? g_dmodels[0].visleafs : 0), MAX_MAP_LEAFS_ENGINE, 0);
    totalmemory += ArrayUsage("marksurfaces", g_nummarksurfaces, ENTRIES(g_dmarksurfaces), ENTRYSIZE(g_dmarksurfaces));
    totalmemory += ArrayUsage("surfedges", g_numsurfedges, ENTRIES(g_dsurfedges), ENTRYSIZE(g_dsurfedges));
    totalmemory += ArrayUsage("edges", g_numedges, ENTRIES(g_dedges), ENTRYSIZE(g_dedges));

    totalmemory += GlobUsage("texdata", g_texdatasize, g_max_map_miptex);
    totalmemory += GlobUsage("lightdata", g_lightdatasize, g_max_map_lightdata);
    totalmemory += GlobUsage("visdata", g_visdatasize, sizeof(g_dvisdata));
    totalmemory += GlobUsage("entdata", g_entdatasize, sizeof(g_dentdata));
    if (numallocblocks == -1)
    {
        Log("* AllocBlock    [ not available to the " PLATFORM_VERSIONSTRING " version ]\n");
    }
    else
    {
        totalmemory += ArrayUsage("* AllocBlock", numallocblocks, maxallocblocks, 0);
    }

    Log("%i textures referenced\n", numtextures);

    Log("=== Total BSP file data space used: %d bytes ===\n", totalmemory);
    if (nowadtextures)
    {
        Log("Wad files required to run the map: (None)\n");
    }
    else if (!wadvalue)
    {
        Log("Wad files required to run the map: (Couldn't parse wad keyvalue from entity data)\n");
    }
    else
    {
        Log("Wad files required to run the map: \"%s\"\n", wadvalue->c_str());
    }
}

//      removes all "?_rad*" textures that are created by hlrad
//      this function does nothing if the map has no textures with name "?_rad*"

void DeleteEmbeddedLightmaps()
{
    int countrestoredfaces = 0;
    int countremovedtexinfos = 0;
    int countremovedtextures = 0;
    int i;
    MiptexLumpView lump = TextureLump();
    int numtextures = std::max(lump.count(), 0);

    // Step 1: parse the original texinfo index stored in each "?_rad*" texture
    //         and restore the texinfo for the faces that have had their lightmap embedded

    for (i = 0; i < g_numfaces; i++)
    {
        dface_t *f = &g_dfaces[i];
        int texinfo;

        texinfo = ParseTexinfoForFace(f);
        if (texinfo != f->texinfo)
        {
            f->texinfo = texinfo;
            countrestoredfaces++;
        }
    }

    // Step 2: remove redundant texinfo
    {
        std::vector<char> texinfoused(g_numtexinfo, false);

        for (i = 0; i < g_numfaces; i++)
        {
            dface_t *f = &g_dfaces[i];

            if (f->texinfo < 0 || f->texinfo >= g_numtexinfo)
            {
                continue;
            }
            texinfoused[f->texinfo] = true;
        }
        for (i = g_numtexinfo - 1; i > -1; i--)
        {
            texinfo_t *info = &g_texinfo[i];

            if (texinfoused[i])
            {
                break; // still used by a face; should not remove this texinfo
            }
            if (info->miptex < 0 || info->miptex >= numtextures)
            {
                break; // invalid; should not remove this texinfo
            }
            if (ParseImplicitTexinfoFromTexture(info->miptex) == -1)
            {
                break; // not added by hlrad; should not remove this texinfo
            }
            countremovedtexinfos++;
        }
        g_numtexinfo = i + 1; // shrink g_texinfo
    }

    // Step 3: remove redundant textures
    {
        std::vector<char> textureused(numtextures, false);

        for (i = 0; i < g_numtexinfo; i++)
        {
            texinfo_t *info = &g_texinfo[i];

            if (info->miptex < 0 || info->miptex >= numtextures)
            {
                continue;
            }
            textureused[info->miptex] = true;
        }
        for (i = numtextures - 1; i > -1; i--)
        {
            if (textureused[i] || ParseImplicitTexinfoFromTexture(i) == -1)
            {
                break; // should not remove this texture
            }
            countremovedtextures++;
        }
        int numremaining = i + 1; // number of remaining textures

        if (numremaining < numtextures)
        {
            // The kept textures' data runs from the end of the old dataofs table up to the data of
            // the first removed texture; move it down to the end of the shortened table.
            const long long oldheader = MiptexLumpView::DataofsPosition(numtextures);
            const long long newheader = MiptexLumpView::DataofsPosition(numremaining);
            const std::optional<int> keptend = lump.dataofs(numremaining);
            if (!keptend || *keptend < oldheader || *keptend > g_texdatasize)
            {
                Warning("DeleteEmbeddedLightmaps: texture lump is damaged; embedded lightmap textures were not removed.");
                countremovedtextures = 0;
            }
            else
            {
                const long long datasize = *keptend - oldheader;
                std::memmove(g_dtexdata + newheader, g_dtexdata + oldheader, datasize);
                g_texdatasize = static_cast<int>(newheader + datasize);
                lump = TextureLump();
                lump.set_count(numremaining);
                for (i = 0; i < numremaining; i++)
                {
                    const int ofs = *lump.dataofs(i);
                    if (ofs < 0) // bad texture
                    {
                        continue;
                    }
                    lump.set_dataofs(i, static_cast<int>(ofs + (newheader - oldheader)));
                }

                numtextures = numremaining;
            }
        }
    }

    if (countrestoredfaces > 0 || countremovedtexinfos > 0 || countremovedtextures > 0)
    {
        Log("DeleteEmbeddedLightmaps: restored %d faces, removed %d texinfos and %d textures.\n",
            countrestoredfaces, countremovedtexinfos, countremovedtextures);
    }
}

epair_t *ParseEpair()
{
    epair_t *e = (epair_t *)std::calloc(1, sizeof(epair_t));

    if (std::strlen(g_token) >= MAX_KEY - 1)
        Error("ParseEpair: Key token too long (%i > MAX_KEY)", (int)std::strlen(g_token));

    e->key = _strdup(g_token);
    GetToken(false);

    if (std::strlen(g_token) >= MAX_VAL - 1) //MAX_VALUE //vluzacn
        Error("ParseEpar: Value token too long (%i > MAX_VALUE)", (int)std::strlen(g_token));

    e->value = _strdup(g_token);

    return e;
}

bool ParseEntity()
{
    if (!GetToken(true))
    {
        return false;
    }

    if (std::strcmp(g_token, "{"))
    {
        Error("ParseEntity: { not found");
    }

    if (g_numentities == MAX_MAP_ENTITIES)
    {
        Error("g_numentities == MAX_MAP_ENTITIES");
    }

    entity_t *mapent = &g_entities[g_numentities];
    g_numentities++;

    while (1)
    {
        if (!GetToken(true))
        {
            Error("ParseEntity: EOF without closing brace");
        }
        if (!std::strcmp(g_token, "}"))
        {
            break;
        }
        epair_t *e = ParseEpair();
        e->next = mapent->epairs;
        mapent->epairs = e;
    }

    if (!std::strcmp(ValueForKey(mapent, "classname"), "info_compile_parameters"))
    {
        Log("Map entity info_compile_parameters detected, using compile settings\n");
        GetParamsFromEnt(mapent);
    }
    // ugly code
    if (!std::strncmp(ValueForKey(mapent, "classname"), "light", 5) && *ValueForKey(mapent, "_tex"))
    {
        SetKeyValue(mapent, "convertto", ValueForKey(mapent, "classname"));
        SetKeyValue(mapent, "classname", "light_surface");
    }
    if (!std::strcmp(ValueForKey(mapent, "convertfrom"), "light_shadow") || !std::strcmp(ValueForKey(mapent, "convertfrom"), "light_bounce"))
    {
        SetKeyValue(mapent, "convertto", ValueForKey(mapent, "classname"));
        SetKeyValue(mapent, "classname", ValueForKey(mapent, "convertfrom"));
        SetKeyValue(mapent, "convertfrom", "");
    }
    if (!std::strcmp(ValueForKey(mapent, "classname"), "light_environment") &&
        !std::strcmp(ValueForKey(mapent, "convertfrom"), "info_sunlight"))
    {
        while (mapent->epairs)
        {
            DeleteKey(mapent, mapent->epairs->key);
        }
        std::memset(mapent, 0, sizeof(entity_t));
        g_numentities--;
        return true;
    }
    if (!std::strcmp(ValueForKey(mapent, "classname"), "light_environment") &&
        IntForKey(mapent, "_fake"))
    {
        SetKeyValue(mapent, "classname", "info_sunlight");
    }

    return true;
}

// Parses the dentdata string into entities
void ParseEntities()
{
    g_numentities = 0;
    ParseFromMemory(g_dentdata, g_entdatasize);

    while (ParseEntity())
    {
    }
}

// Generates the dentdata string from all the entities
static int anglesforvector(float angles[3], const float vector[3])
{
    float z = vector[2], r = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1]);
    if (std::sqrt(z * z + r * r) < NORMAL_EPSILON)
    {
        return -1;
    }
    else
    {
        float tmp = std::sqrt(z * z + r * r);
        z /= tmp, r /= tmp;
        if (r < NORMAL_EPSILON)
        {
            if (z < 0)
            {
                angles[0] = -90, angles[1] = 0;
            }
            else
            {
                angles[0] = 90, angles[1] = 0;
            }
        }
        else
        {
            angles[0] = std::atan(z / r) / Q_PI * 180;
            float x = vector[0], y = vector[1];
            tmp = std::sqrt(x * x + y * y);
            x /= tmp, y /= tmp;
            if (x < -1 + NORMAL_EPSILON)
            {
                angles[1] = -180;
            }
            else
            {
                if (y >= 0)
                {
                    angles[1] = 2 * std::atan(y / (1 + x)) / Q_PI * 180;
                }
                else
                {
                    angles[1] = 2 * std::atan(y / (1 + x)) / Q_PI * 180 + 360;
                }
            }
        }
    }
    angles[2] = 0;
    return 0;
}

void UnparseEntities()
{
    char line[MAXTOKEN];

    char *buf = g_dentdata;
    char *end = buf;
    *end = 0;

    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *mapent = &g_entities[i];
        if (!std::strcmp(ValueForKey(mapent, "classname"), "info_sunlight") ||
            !std::strcmp(ValueForKey(mapent, "classname"), "light_environment"))
        {
            float vec[3] = {0, 0, 0};
            {
                std::sscanf(ValueForKey(mapent, "angles"), "%f %f %f", &vec[0], &vec[1], &vec[2]);
                float pitch = FloatForKey(mapent, "pitch");
                if (pitch)
                    vec[0] = pitch;

                const char *target = ValueForKey(mapent, "target");
                if (target[0])
                {
                    entity_t *targetent = FindTargetEntity(target);
                    if (targetent)
                    {
                        float origin1[3] = {0, 0, 0}, origin2[3] = {0, 0, 0}, normal[3];
                        std::sscanf(ValueForKey(mapent, "origin"), "%f %f %f", &origin1[0], &origin1[1], &origin1[2]);
                        std::sscanf(ValueForKey(targetent, "origin"), "%f %f %f", &origin2[0], &origin2[1], &origin2[2]);
                        VectorSubtract(origin2, origin1, normal);
                        anglesforvector(vec, normal);
                    }
                }
            }
            char stmp[1024];
            safe_snprintf(stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
            SetKeyValue(mapent, "angles", stmp);
            DeleteKey(mapent, "pitch");

            if (!std::strcmp(ValueForKey(mapent, "classname"), "info_sunlight"))
            {
                if (g_numentities == MAX_MAP_ENTITIES)
                {
                    Error("g_numentities == MAX_MAP_ENTITIES");
                }
                entity_t *newent = &g_entities[g_numentities++];
                newent->epairs = mapent->epairs;
                SetKeyValue(newent, "classname", "light_environment");
                SetKeyValue(newent, "_fake", "1");
                mapent->epairs = nullptr;
            }
        }
    }
    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *mapent = &g_entities[i];
        if (!std::strcmp(ValueForKey(mapent, "classname"), "light_shadow") || !std::strcmp(ValueForKey(mapent, "classname"), "light_bounce"))
        {
            SetKeyValue(mapent, "convertfrom", ValueForKey(mapent, "classname"));
            SetKeyValue(mapent, "classname", (*ValueForKey(mapent, "convertto") ? ValueForKey(mapent, "convertto") : "light"));
            SetKeyValue(mapent, "convertto", "");
        }
    }
    // ugly code
    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *mapent = &g_entities[i];
        if (!std::strcmp(ValueForKey(mapent, "classname"), "light_surface"))
        {
            if (!*ValueForKey(mapent, "_tex"))
            {
                SetKeyValue(mapent, "_tex", "                ");
            }
            const char *newclassname = ValueForKey(mapent, "convertto");
            if (!*newclassname)
            {
                SetKeyValue(mapent, "classname", "light");
            }
            else if (std::strncmp(newclassname, "light", 5))
            {
                Error("New classname for 'light_surface' should begin with 'light' not '%s'.\n", newclassname);
            }
            else
            {
                SetKeyValue(mapent, "classname", newclassname);
            }
            SetKeyValue(mapent, "convertto", "");
        }
    }
#ifdef HLCSG
    extern bool g_nolightopt;
    if (!g_nolightopt)
    {
        int j;
        int count = 0;
        bool *lightneedcompare = (bool *)std::malloc(g_numentities * sizeof(bool));
        hlassume(lightneedcompare != nullptr, assume_NoMemory);
        std::memset(lightneedcompare, 0, g_numentities * sizeof(bool));
        for (int i = g_numentities - 1; i > -1; i--)
        {
            entity_t *ent = &g_entities[i];
            const char *classname = ValueForKey(ent, "classname");
            const char *targetname = ValueForKey(ent, "targetname");
            int style = IntForKey(ent, "style");
            if (!targetname[0] ||
                (std::strcmp(classname, "light") && std::strcmp(classname, "light_spot") && std::strcmp(classname, "light_environment")))
                continue;
            for (j = i + 1; j < g_numentities; j++)
            {
                if (!lightneedcompare[j])
                    continue;
                entity_t *ent2 = &g_entities[j];
                const char *targetname2 = ValueForKey(ent2, "targetname");
                int style2 = IntForKey(ent2, "style");
                if (style == style2 && !std::strcmp(targetname, targetname2))
                    break;
            }
            if (j < g_numentities)
            {
                DeleteKey(ent, "targetname");
                count++;
            }
            else
            {
                lightneedcompare[i] = true;
            }
        }
        if (count > 0)
        {
            Log("%d redundant named lights optimized.\n", count);
        }
        std::free(lightneedcompare);
    }
#endif
    for (int i = 0; i < g_numentities; i++)
    {
        epair_t *ep = g_entities[i].epairs;
        if (!ep)
        {
            continue; // ent got removed
        }

        std::strcat(end, "{\n");
        end += 2;

        for (ep = g_entities[i].epairs; ep; ep = ep->next)
        {
            std::sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value);
            std::strcat(end, line);
            end += std::strlen(line);
        }
        std::strcat(end, "}\n");
        end += 2;

        if (end > buf + MAX_MAP_ENTSTRING)
        {
            Error("Entity text too long");
        }
    }
    g_entdatasize = end - buf + 1;
}

entity_t *FindTargetEntity(const char *const target)
{
    for (int i = 0; i < g_numentities; i++)
    {
        const char *n = ValueForKey(&g_entities[i], "targetname");
        if (!std::strcmp(n, target))
        {
            return &g_entities[i];
        }
    }

    return nullptr;
}

namespace
{
struct FreeDeleter
{
    void operator()(byte *p) const { std::free(p); }
};
using ZeroedBuffer = std::unique_ptr<byte, FreeDeleter>;

// Owners of g_dtexdata / g_dlightdata. calloc rather than new byte[n]() so the tens of
// megabytes reserved here stay untouched (lazily zeroed pages) until a tool uses them.
ZeroedBuffer s_texdata;
ZeroedBuffer s_lightdata;

ZeroedBuffer AllocZeroed(int size)
{
    if (size <= 0)
    {
        Warning("Attempting to allocate %d bytes", size);
    }
    ZeroedBuffer p(static_cast<byte *>(std::calloc(1, size > 0 ? size : 1)));
    hlassume(p != nullptr, assume_NoMemory);
    return p;
}
} // namespace

void dtexdata_init()
{
    s_texdata = AllocZeroed(g_max_map_miptex);
    g_dtexdata = s_texdata.get();
    s_texdata_capacity = g_max_map_miptex;
    s_lightdata = AllocZeroed(g_max_map_lightdata);
    g_dlightdata = s_lightdata.get();
    s_lightdata_capacity = g_max_map_lightdata;
}

void dtexdata_free()
{
    s_texdata.reset();
    g_dtexdata = nullptr;
    s_texdata_capacity = 0;
    s_lightdata.reset();
    g_dlightdata = nullptr;
    s_lightdata_capacity = 0;
}

MiptexLumpView TextureLump()
{
    return MiptexLumpView(g_dtexdata, g_texdatasize, s_texdata_capacity);
}

// Returns "" where the texture name would lie outside the texture buffer (missing or
// damaged texture data); this used to read arbitrary memory.
static char emptystring[1] = {'\0'};
char *GetTextureByNumber(int texturenumber)
{
    if (texturenumber == -1)
        return emptystring;
    if (texturenumber < 0 || texturenumber >= MAX_INTERNAL_MAP_TEXINFO)
        return emptystring;

    const texinfo_t *info = &g_texinfo[texturenumber];
    // compat: a texinfo with miptex -1 reads dataofs[-1], which is nummiptex, and returns
    // whatever lies at that offset; it stays inside the buffer, so it is kept as it was.
    const MiptexLumpView lump = TextureLump();
    const std::optional<int> ofs = lump.dataofs(info->miptex);
    if (!ofs || *ofs < 0 || static_cast<long long>(*ofs) + MAX_TEXTURE_NAME_LENGTH > lump.capacity())
        return emptystring;

    static_assert(offsetof(miptex_t, name) == 0);
    return reinterpret_cast<char *>(&g_dtexdata[*ofs]); // miptex_t::name; the record may be misaligned
}

// returns entity addy for given modelnum
entity_t *EntityForModel(const int modnum)
{
    char name[MAX_TEXTURE_NAME_LENGTH];

    std::sprintf(name, "*%i", modnum);
    // search the entities for one using modnum
    for (int i = 0; i < g_numentities; i++)
    {
        const char *s = ValueForKey(&g_entities[i], "model");
        if (!std::strcmp(s, name))
        {
            return &g_entities[i];
        }
    }

    return &g_entities[0];
}