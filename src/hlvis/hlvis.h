#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/bspfile.h"

constexpr int MAX_POINTS_ON_FIXED_WINDING = 32;

// A portal's winding (any size up to MAX_POINTS_ON_WINDING, points in its own array) or a winding
// on the flow stack (points in storage). The portal windings used to be allocated shorter or longer
// than the struct and indexed past points[MAX_POINTS_ON_FIXED_WINDING].
struct winding_t
{
    bool original; // don't free, it's part of the portal
    int numpoints;
    vec3_t *points; // storage, or the portal's array
    vec3_t storage[MAX_POINTS_ON_FIXED_WINDING];
};

// Visibility bit strings: g_bitbytes bytes, allocated as 64-bit words so that the flow can compare
// them a word at a time; bits are read and set through a byte view (the code used to read byte
// arrays through long*, which breaks the aliasing rules).
using bitword_t = std::uint64_t;
constexpr int MAX_BITBYTES = ((MAX_MAP_LEAFS + 63) & ~63) >> 3; // g_bitbytes for MAX_MAP_LEAFS leafs
inline byte *BitBytes(bitword_t *words)
{
    return reinterpret_cast<byte *>(words);
}
inline const byte *BitBytes(const bitword_t *words)
{
    return reinterpret_cast<const byte *>(words);
}

struct portalplane_t
{
    vec3_t normal;
    float dist;
};

typedef enum
{
    stat_none,
    stat_working,
    stat_done
} vstatus_t;

struct portal_t
{
    portalplane_t plane{}; // normal pointing into neighbor
    int leaf = 0;          // neighbor
    std::unique_ptr<winding_t> winding;
    std::unique_ptr<vec3_t[]> windingpoints; // winding->points
    vstatus_t status = stat_none;
    int order = 0;                // position in the processing order (SortPortals)
    std::atomic<bool> done{false}; // visbits final; set (release) after status = stat_done
    std::vector<bitword_t> visbits;  // g_bitlongs words once computed
    std::vector<bitword_t> mightsee; // g_bitlongs words once computed
    unsigned nummightsee = 0;
    int numcansee = 0;
};

constexpr int MAX_PORTALS_ON_LEAF = 256;
struct leaf_t
{
    unsigned numportals = 0;
    portal_t *portals[MAX_PORTALS_ON_LEAF] = {}; // into g_portals
};

struct pstack_t
{
    bitword_t mightsee[MAX_BITBYTES / sizeof(bitword_t)]; // bit string (was MAX_MAP_LEAFS / 8 bytes: one short)
    pstack_t *head;

    leaf_t *leaf;
    portal_t *portal; // portal exiting
    winding_t *source;
    winding_t *pass;

    winding_t windings[3]; // source, pass, temp in any order
    char freewindings[3];

    const portalplane_t *portalplane;

    int clipPlaneCount;
    portalplane_t *clipPlane;
};

struct threaddata_t
{
    bitword_t *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t *base;
    pstack_t pstack_head;
};

extern bool g_fullvis;
// -fastthreads: a flow uses every portal finished at that moment (faster with many threads, output
// can differ from run to run). Default: only the portals before it in the processing order, which
// is what a single thread does, so the output is the same for any thread count.
extern bool g_fastthreads;

extern int g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;
//extern bool		g_postcompile;
struct overview_t
{
    vec3_t origin;
    int visleafnum;
    int reverse;
};

struct leafinfo_t
{
    bool isoverviewpoint;
    bool isskyboxpoint;
};

extern std::vector<portal_t> g_portals; // 2 * g_numportals: each file portal, both directions
extern std::vector<leaf_t> g_leafs;     // g_portalleafs

extern unsigned g_bitbytes;
extern unsigned g_bitlongs; // g_bitbytes in bitword_t units

// A zeroed bit string of g_bitbytes bytes.
std::vector<bitword_t> NewBitString();

void BasePortalVis(int /*threadnum*/);

void MaxDistVis(int /*threadnum*/);
//extern void		PostMaxDistVis(int threadnum);

void PortalFlow(portal_t *p);
void CalcAmbientSounds();