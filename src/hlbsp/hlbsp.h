#pragma once

#include <map>
#include <memory>
#include <utility>

#include "common/bspfile.h"
#include "common/mathtypes.h"
#include "common/winding.h"

#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif

// hlbsp's in-memory plane: double precision, unlike the disk dplane_t (written out in writebsp.cpp).
struct plane_t
{
    vec3_t normal;
    vec3_t unused_origin;
    vec_t dist;
    planetypes type;
};
extern plane_t g_mapplanes[MAX_INTERNAL_MAP_PLANES];

constexpr int BOGUS_RANGE = 144000;

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
constexpr int SIDESPACE = 24;

//============================================================================

constexpr int DEFAULT_MAXNODE_SIZE = 1024;

constexpr int MAXEDGES = 48;  // 32
constexpr int MAXPOINTS = 28; // don't let a base face get past this,
                              // because it can be split more later

typedef enum
{
    face_normal = 0,
    face_hint,
    face_skip,
    face_null,
    face_discardable, // contents must not differ between front and back
} facestyle_e;

// The defaults below are what the allocators' memset gave (AllocFace then set planenum to -1).
struct face_t // This structure is layed out so 'pts' is on a quad-word boundary (and the pointers are as well)
{
    face_t *next = nullptr;
    int planenum = -1;
    int texturenum = 0;
    int contents = 0;            // contents in front of face
    int detaillevel = 0;         // defined by hlcsg
    std::vector<int> outputedges; // used in WriteDrawNodes

    face_t *original = nullptr; // face on node
    int outputnumber = 0;       // only valid for original faces after write surfaces
    int numpoints = 0;
    facestyle_e facestyle = face_normal;
    int referenced = 0; // only valid for original faces

    // vector quad word aligned
    vec3_t pts[MAXEDGES] = {}; // FIXME: change to use winding_t
};

struct node_t;
struct surface_t
{
    surface_t *next = nullptr;
    int planenum = 0;
    vec3_t mins = {}, maxs = {};
    node_t *onnode = nullptr; // true if surface has already been used
    // as a splitting node
    face_t *faces = nullptr; // links to all the faces on either side of the surf
    int detaillevel = 0;     // minimum detail level of its faces
};

struct surfchain_t
{
    vec3_t mins = {}, maxs = {};
    surface_t *surfaces = nullptr;
};

struct side_t
{
    side_t *next = nullptr;
    plane_t plane = {};          // facing inside (reversed when loading brush file)
    std::unique_ptr<Winding> w; // (also reversed)
};

struct brush_t
{
    brush_t *next = nullptr;
    side_t *sides = nullptr; // owned (FreeBrush)
};
// there is a node_t structure for every node and leaf in the bsp tree
struct portal_t;
struct node_t
{
    surface_t *surfaces = nullptr;
    brush_t *detailbrushes = nullptr;
    brush_t *boundsbrush = nullptr;
    vec3_t loosemins = {}, loosemaxs = {}; // all leafs and nodes have this, while 'mins' and 'maxs' are only valid for nondetail leafs and nodes.

    bool isdetail = false;         // is under a diskleaf
    bool isportalleaf = false;     // not detail and children are detail; only visleafs have contents, portals, mins, maxs
    bool iscontentsdetail = false; // inside a detail brush
    vec3_t mins = {}, maxs = {};   // bounding volume of portals;

    // information for decision nodes
    int planenum = 0;                       // -1 = leaf node
    node_t *children[2] = {nullptr, nullptr}; // only valid for decision nodes (FreeTree frees them)
    face_t *faces = nullptr;                // decision nodes only, list for both sides (owned)

    // information for leafs
    int contents = 0;                       // leaf nodes (0 for decision nodes)
    std::unique_ptr<face_t *[]> markfaces; // leaf nodes only, null-terminated, point to node faces; null for filled leafs
    portal_t *portals = nullptr;
    int visleafnum = 0; // -1 = solid
    int valid = 0;      // for flood filling
    int occupied = 0;   // light number in leaf for outside filling
    int empty = 0;
};

constexpr int NUM_HULLS = 4; // engine constant

// Merges identical clipnodes (writebsp.cpp, brink.cpp): planenum and children -> index.
using clipnodemap_t = std::map<std::pair<int, std::pair<int, int>>, int>;
inline clipnodemap_t::key_type MakeKey(const dclipnode_t &c)
{
    return std::make_pair(c.planenum, std::make_pair(c.children[0], c.children[1]));
}

//=============================================================================
// solidbsp.c
void SubdivideFace(face_t *f, face_t **prevptr);
node_t *SolidBSP(const surfchain_t *const surfhead,
                 brush_t *detailbrushes,
                 bool report_progress);

//=============================================================================
// merge.c
void MergePlaneFaces(surface_t *plane);
void MergeAll(surface_t *surfhead);

//=============================================================================
// surfaces.c
void MakeFaceEdges();
int GetEdge(const vec3_t p1, const vec3_t p2, face_t *f);

//=============================================================================
// portals.c
struct portal_t
{
    plane_t plane = {};
    node_t *onnode = nullptr;                // NULL = outside box
    node_t *nodes[2] = {nullptr, nullptr};   // [0] = front side of plane
    portal_t *next[2] = {nullptr, nullptr};
    std::unique_ptr<Winding> winding;
};

extern node_t g_outside_node; // portals outside the world face this

void AddPortalToNodes(portal_t *p, node_t *front, node_t *back);
void RemovePortalFromNode(portal_t *portal, node_t *l);
void MakeHeadnodePortals(node_t *node, const vec3_t mins, const vec3_t maxs);

void FreePortals(node_t *node);
// Frees every portal of one node (unlinking it from the node on its other side too).
void FreeNodePortals(node_t *node);
// Frees a whole tree once it is written: nodes, their faces (on hull 0, after tjunc, the list holds
// the tjunction fragments too), leaf markfaces and any portals still attached. Null-safe.
void FreeTree(node_t *node);
// Subtrees outside.cpp cut out of the tree while it is still being processed; freed (FreeTree)
// once the hull is written.
void FreeDroppedNodes();
void WritePortalfile(node_t *headnode);

//=============================================================================
// tjunc.c
void tjunc(node_t *headnode);

//=============================================================================
// writebsp.c
void WriteClipNodes(node_t *headnode);
void WriteDrawNodes(node_t *headnode);

void BeginBSPFile();
void FinishBSPFile();

//=============================================================================
// outside.c
node_t *FillOutside(node_t *node, bool leakfile, unsigned hullnum);
void LoadAllowableOutsideList(const char *const filename);
void FreeAllowableOutsideList();
void FillInside(node_t *node);

//=============================================================================
// misc functions
void GetParamsFromEnt(entity_t *mapent);

face_t *AllocFace();
void FreeFace(face_t *f);

portal_t *AllocPortal();
void FreePortal(portal_t *p);

surface_t *AllocSurface();
void FreeSurface(surface_t *s);

void FreeBrush(brush_t *b);
brush_t *NewBrushFromBrush(const brush_t *b);
void SplitBrush(brush_t *in, const plane_t *split, brush_t **front, brush_t **back);
brush_t *BrushFromBox(const vec3_t mins, const vec3_t maxs);
void CalcBrushBounds(const brush_t *b, vec3_t &mins, vec3_t &maxs);

node_t *AllocNode();
void FreeNode(node_t *node); // the node only (FreeTree frees what it owns)

bool CheckFaceForHint(const face_t *const f);
bool CheckFaceForSkip(const face_t *const f);
bool CheckFaceForNull(const face_t *const f);
bool CheckFaceForDiscardable(const face_t *f);

typedef enum
{
    BrinkNone = 0,
    BrinkFloorBlocking,
    BrinkFloor,
    BrinkWallBlocking,
    BrinkWall,
    BrinkAny,
} bbrinklevel_e;
// The brink analysis of one hull's clipnode tree (brink.cpp); its tree nodes are freed with it.
struct bbrinkinfo_t;
struct BrinkinfoDeleter
{
    void operator()(bbrinkinfo_t *info) const;
};
using Brinkinfo = std::unique_ptr<bbrinkinfo_t, BrinkinfoDeleter>;
Brinkinfo CreateBrinkinfo(const dclipnode_t *clipnodes, int headnode);
bool FixBrinks(const bbrinkinfo_t *info, bbrinklevel_e level, int &headnode_out, dclipnode_t *clipnodes_out, int maxsize, int size, int &size_out);

// Cpt_Andrew - UTSky Check
bool CheckFaceForEnv_Sky(const face_t *const f);
// =====================================================================================

//=============================================================================
// cull.c
void CullStuff();

//=============================================================================
// hlbsp.c
extern int g_maxnode_size;
extern int g_subdivide_size;
extern int g_hullnum;
extern bool g_bLeaked;
extern bool g_nohull2;
extern bool g_chart;

extern char g_portfilename[_MAX_PATH];
extern char g_pointfilename[_MAX_PATH];
extern char g_linefilename[_MAX_PATH];
extern char g_bspfilename[_MAX_PATH];
extern char g_extentfilename[_MAX_PATH];

extern vec3_t g_hull_size[NUM_HULLS][2];

face_t *NewFaceFromFace(const face_t *const in);
void SplitFace(face_t *in, const plane_t *const split, face_t **front, face_t **back);