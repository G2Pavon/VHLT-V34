#pragma once

// GoldSrc BSP version 30 on-disk format.
//
// These structs are raw memory images of the little-endian file layout: lumps are
// loaded and saved with memcpy, so every field has a fixed width and every layout is
// checked at compile time below. Do not add members, reorder fields or change types.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "common/mathlib.h"

constexpr int BSPVERSION = 30;

constexpr int MAX_MAP_HULLS = 4;
// hard limit

struct lump_t
{
    std::int32_t fileofs;
    std::int32_t filelen;
};

constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_PLANES = 1;
constexpr int LUMP_TEXTURES = 2;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_VISIBILITY = 4;
constexpr int LUMP_NODES = 5;
constexpr int LUMP_TEXINFO = 6;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_CLIPNODES = 9;
constexpr int LUMP_LEAFS = 10;
constexpr int LUMP_MARKSURFACES = 11;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_MODELS = 14;
constexpr int HEADER_LUMPS = 15;

struct dmodel_t
{
    float mins[3], maxs[3];
    float origin[3];
    std::int32_t headnode[MAX_MAP_HULLS];
    std::int32_t visleafs; // not including the solid leaf 0
    std::int32_t firstface, numfaces;
};

struct dheader_t
{
    std::int32_t version;
    lump_t lumps[HEADER_LUMPS];
};

// Header of the texture lump: nummiptex, then nummiptex offsets (relative to the lump
// start; -1 for a missing texture), then the miptex_t records.
struct dmiptexlump_t
{
    std::int32_t nummiptex;
    std::int32_t dataofs[4]; // [nummiptex]; the real length is nummiptex, see MiptexLumpView
};

constexpr int MIPLEVELS = 4;
constexpr int MAX_TEXTURE_NAME_LENGTH = 16;
struct miptex_t
{
    char name[MAX_TEXTURE_NAME_LENGTH];
    std::uint32_t width, height;
    std::uint32_t offsets[MIPLEVELS]; // four mip maps stored
};

struct dvertex_t
{
    float point[3];
};

struct dplane_t
{
    float normal[3];
    float dist;
    planetypes type; // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
};

typedef enum
{
    CONTENTS_EMPTY = -1,
    CONTENTS_SOLID = -2,
    CONTENTS_WATER = -3,
    CONTENTS_SLIME = -4,
    CONTENTS_LAVA = -5,
    CONTENTS_SKY = -6,
    CONTENTS_ORIGIN = -7, // removed at csg time

    CONTENTS_CURRENT_0 = -9,
    CONTENTS_CURRENT_90 = -10,
    CONTENTS_CURRENT_180 = -11,
    CONTENTS_CURRENT_270 = -12,
    CONTENTS_CURRENT_UP = -13,
    CONTENTS_CURRENT_DOWN = -14,

    CONTENTS_TRANSLUCENT = -15,
    CONTENTS_HINT = -16, // Filters down to CONTENTS_EMPTY by bsp, ENGINE SHOULD NEVER SEE THIS

    CONTENTS_NULL = -17, // AJM  // removed in csg and bsp, VIS or RAD shouldnt have to deal with this, only clip planes!

    CONTENTS_BOUNDINGBOX = -19, // similar to CONTENTS_ORIGIN

    CONTENTS_TOEMPTY = -32,
} contents_t;

struct dnode_t
{
    std::int32_t planenum;
    std::int16_t children[2]; // negative numbers are -(leafs+1), not nodes
    std::int16_t mins[3];     // for sphere culling
    std::int16_t maxs[3];
    std::uint16_t firstface;
    std::uint16_t numfaces; // counting both sides
};

struct dclipnode_t
{
    std::int32_t planenum;
    std::int16_t children[2]; // negative numbers are contents
};

struct texinfo_t
{
    float vecs[2][4]; // [s/t][xyz offset]
    std::int32_t miptex;
    std::int32_t flags;
};

constexpr int TEX_SPECIAL = 1; // sky or slime or null, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct dedge_t
{
    std::uint16_t v[2]; // vertex numbers
};

constexpr int MAXLIGHTMAPS = 4;
struct dface_t
{
    std::uint16_t planenum;
    std::int16_t side;

    std::int32_t firstedge; // we must support > 64k edges
    std::int16_t numedges;
    std::int16_t texinfo;

    // lighting info
    std::uint8_t styles[MAXLIGHTMAPS];
    std::int32_t lightofs; // start of [numstyles*surfsize] samples
};

constexpr int AMBIENT_WATER = 0;
constexpr int AMBIENT_SKY = 1;
constexpr int AMBIENT_SLIME = 2;
constexpr int AMBIENT_LAVA = 3;

constexpr int NUM_AMBIENTS = 4; // automatic ambient sounds

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct dleaf_t
{
    std::int32_t contents;
    std::int32_t visofs; // -1 = no visibility info

    std::int16_t mins[3]; // for frustum culling
    std::int16_t maxs[3];

    std::uint16_t firstmarksurface;
    std::uint16_t nummarksurfaces;

    std::uint8_t ambient_level[NUM_AMBIENTS];
};

// Layout checks: (struct, size) then (struct, field, offset).
#define VHLT_DISK_STRUCT(T, size)                                                     \
    static_assert(sizeof(T) == (size), #T " must be " #size " bytes on disk");         \
    static_assert(std::is_trivially_copyable_v<T>, #T " is copied with memcpy");      \
    static_assert(std::is_standard_layout_v<T>, #T " must have a fixed field layout")
#define VHLT_DISK_FIELD(T, field, ofs) \
    static_assert(offsetof(T, field) == (ofs), #T "::" #field " must be at offset " #ofs)

static_assert(sizeof(planetypes) == 4, "dplane_t::type is a 32-bit int on disk");
static_assert(sizeof(float) == 4, "BSP floats are IEEE-754 single precision");

VHLT_DISK_STRUCT(lump_t, 8);
VHLT_DISK_FIELD(lump_t, fileofs, 0);
VHLT_DISK_FIELD(lump_t, filelen, 4);

VHLT_DISK_STRUCT(dheader_t, 124);
VHLT_DISK_FIELD(dheader_t, version, 0);
VHLT_DISK_FIELD(dheader_t, lumps, 4);

VHLT_DISK_STRUCT(dmodel_t, 64);
VHLT_DISK_FIELD(dmodel_t, mins, 0);
VHLT_DISK_FIELD(dmodel_t, maxs, 12);
VHLT_DISK_FIELD(dmodel_t, origin, 24);
VHLT_DISK_FIELD(dmodel_t, headnode, 36);
VHLT_DISK_FIELD(dmodel_t, visleafs, 52);
VHLT_DISK_FIELD(dmodel_t, firstface, 56);
VHLT_DISK_FIELD(dmodel_t, numfaces, 60);

VHLT_DISK_STRUCT(dmiptexlump_t, 20);
VHLT_DISK_FIELD(dmiptexlump_t, nummiptex, 0);
VHLT_DISK_FIELD(dmiptexlump_t, dataofs, 4);

VHLT_DISK_STRUCT(miptex_t, 40);
VHLT_DISK_FIELD(miptex_t, name, 0);
VHLT_DISK_FIELD(miptex_t, width, 16);
VHLT_DISK_FIELD(miptex_t, height, 20);
VHLT_DISK_FIELD(miptex_t, offsets, 24);

VHLT_DISK_STRUCT(dvertex_t, 12);
VHLT_DISK_FIELD(dvertex_t, point, 0);

VHLT_DISK_STRUCT(dplane_t, 20);
VHLT_DISK_FIELD(dplane_t, normal, 0);
VHLT_DISK_FIELD(dplane_t, dist, 12);
VHLT_DISK_FIELD(dplane_t, type, 16);

VHLT_DISK_STRUCT(dnode_t, 24);
VHLT_DISK_FIELD(dnode_t, planenum, 0);
VHLT_DISK_FIELD(dnode_t, children, 4);
VHLT_DISK_FIELD(dnode_t, mins, 8);
VHLT_DISK_FIELD(dnode_t, maxs, 14);
VHLT_DISK_FIELD(dnode_t, firstface, 20);
VHLT_DISK_FIELD(dnode_t, numfaces, 22);

VHLT_DISK_STRUCT(dclipnode_t, 8);
VHLT_DISK_FIELD(dclipnode_t, planenum, 0);
VHLT_DISK_FIELD(dclipnode_t, children, 4);

VHLT_DISK_STRUCT(texinfo_t, 40);
VHLT_DISK_FIELD(texinfo_t, vecs, 0);
VHLT_DISK_FIELD(texinfo_t, miptex, 32);
VHLT_DISK_FIELD(texinfo_t, flags, 36);

VHLT_DISK_STRUCT(dedge_t, 4);
VHLT_DISK_FIELD(dedge_t, v, 0);

VHLT_DISK_STRUCT(dface_t, 20);
VHLT_DISK_FIELD(dface_t, planenum, 0);
VHLT_DISK_FIELD(dface_t, side, 2);
VHLT_DISK_FIELD(dface_t, firstedge, 4);
VHLT_DISK_FIELD(dface_t, numedges, 8);
VHLT_DISK_FIELD(dface_t, texinfo, 10);
VHLT_DISK_FIELD(dface_t, styles, 12);
VHLT_DISK_FIELD(dface_t, lightofs, 16);

VHLT_DISK_STRUCT(dleaf_t, 28);
VHLT_DISK_FIELD(dleaf_t, contents, 0);
VHLT_DISK_FIELD(dleaf_t, visofs, 4);
VHLT_DISK_FIELD(dleaf_t, mins, 8);
VHLT_DISK_FIELD(dleaf_t, maxs, 14);
VHLT_DISK_FIELD(dleaf_t, firstmarksurface, 20);
VHLT_DISK_FIELD(dleaf_t, nummarksurfaces, 22);
VHLT_DISK_FIELD(dleaf_t, ambient_level, 24);

#undef VHLT_DISK_STRUCT
#undef VHLT_DISK_FIELD
