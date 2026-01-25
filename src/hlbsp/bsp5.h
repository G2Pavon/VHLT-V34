#pragma once

#include "common/winding.h"
#include "common/mathtypes.h"
#include "common/bspfile.h"

constexpr const char *ENTITIES_VOID = "entities.void";
#define ENTITIES_VOID_EXT ".void"

#define BOGUS_RANGE 144000

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
constexpr int SIDESPACE = 24;

//============================================================================

constexpr int MIN_SUBDIVIDE_SIZE = 64;

constexpr int MAX_SUBDIVIDE_SIZE = 512;

constexpr int DEFAULT_SUBDIVIDE_SIZE = ((MAX_SURFACE_EXTENT - 1) * TEXTURE_STEP); //#define DEFAULT_SUBDIVIDE_SIZE  240 //--vluzacn

constexpr int MIN_MAXNODE_SIZE = 64;
constexpr int MAX_MAXNODE_SIZE = 65536;
constexpr int DEFAULT_MAXNODE_SIZE = 1024;

constexpr bool DEFAULT_NOFILL = false;
constexpr bool DEFAULT_NOINSIDEFILL = false;
constexpr bool DEFAULT_NOTJUNC = false;
constexpr bool DEFAULT_NOBRINK = false;
constexpr bool DEFAULT_NOCLIP = false;
constexpr bool DEFAULT_NOOPT = false;
constexpr bool DEFAULT_NOCLIPNODEMERGE = false;
constexpr bool DEFAULT_LEAKONLY = false;
constexpr bool DEFAULT_WATERVIS = false;
constexpr bool DEFAULT_CHART = false;
constexpr bool DEFAULT_INFO = true;

constexpr bool DEFAULT_NULLTEX = true;

constexpr bool DEFAULT_ESTIMATE = false;

constexpr int MAXEDGES = 48;      // 32
constexpr int MAXPOINTS = 28;     // don't let a base face get past this \
                         // because it can be split more later
constexpr int MAXNODESIZE = 1024; // Valve default is 1024

typedef enum
{
    face_normal = 0,
    face_hint,
    face_skip,
    face_null,
    face_discardable, // contents must not differ between front and back
} facestyle_e;

typedef struct face_s // This structure is layed out so 'pts' is on a quad-word boundary (and the pointers are as well)
{
    struct face_s *next;
    int planenum;
    int texturenum;
    int contents;     // contents in front of face
    int detaillevel;  // defined by hlcsg
    int *outputedges; // used in WriteDrawNodes

    struct face_s *original; // face on node
    int outputnumber;        // only valid for original faces after write surfaces
    int numpoints;
    facestyle_e facestyle;
    int referenced; // only valid for original faces

    // vector quad word aligned
    vec3_t pts[MAXEDGES]; // FIXME: change to use winding_t

} face_t;

typedef struct surface_s
{
    struct surface_s *next;
    int planenum;
    vec3_t mins, maxs;
    struct node_s *onnode; // true if surface has already been used
    // as a splitting node
    face_t *faces;   // links to all the faces on either side of the surf
    int detaillevel; // minimum detail level of its faces
} surface_t;

typedef struct
{
    vec3_t mins, maxs;
    surface_t *surfaces;
} surfchain_t;

typedef struct side_s
{
    struct side_s *next;
    dplane_t plane; // facing inside (reversed when loading brush file)
    Winding *w;     // (also reversed)
} side_t;

typedef struct brush_s
{
    struct brush_s *next;
    side_t *sides;
} brush_t;

//
// there is a node_t structure for every node and leaf in the bsp tree
//
constexpr int PLANENUM_LEAF = -1;
constexpr vec_t BOUNDS_EXPANSION = 1.0; // expand the bounds of detail leafs when clipping its boundsbrush, to prevent some strange brushes in the func_detail from clipping away the entire boundsbrush making the func_detail invisible.

typedef struct node_s
{
    surface_t *surfaces;
    brush_t *detailbrushes;
    brush_t *boundsbrush;
    vec3_t loosemins, loosemaxs; // all leafs and nodes have this, while 'mins' and 'maxs' are only valid for nondetail leafs and nodes.

    bool isdetail;         // is under a diskleaf
    bool isportalleaf;     // not detail and children are detail; only visleafs have contents, portals, mins, maxs
    bool iscontentsdetail; // inside a detail brush
    vec3_t mins, maxs;     // bounding volume of portals;

    // information for decision nodes
    int planenum;               // -1 = leaf node
    struct node_s *children[2]; // only valid for decision nodes
    face_t *faces;              // decision nodes only, list for both sides

    // information for leafs
    int contents;       // leaf nodes (0 for decision nodes)
    face_t **markfaces; // leaf nodes only, point to node faces
    struct portal_s *portals;
    int visleafnum; // -1 = solid
    int valid;      // for flood filling
    int occupied;   // light number in leaf for outside filling
    int empty;
} node_t;

constexpr int NUM_HULLS = 4; // engine constant

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
typedef struct portal_s
{
    dplane_t plane;
    node_t *onnode;   // NULL = outside box
    node_t *nodes[2]; // [0] = front side of plane
    struct portal_s *next[2];
    Winding *winding;
} portal_t;

extern node_t g_outside_node; // portals outside the world face this

void AddPortalToNodes(portal_t *p, node_t *front, node_t *back);
void RemovePortalFromNode(portal_t *portal, node_t *l);
void MakeHeadnodePortals(node_t *node, const vec3_t mins, const vec3_t maxs);

void FreePortals(node_t *node);
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

struct portal_s *AllocPortal();
void FreePortal(struct portal_s *p);

surface_t *AllocSurface();
void FreeSurface(surface_t *s);

void FreeBrush(brush_t *b);
brush_t *NewBrushFromBrush(const brush_t *b);
void SplitBrush(brush_t *in, const dplane_t *split, brush_t **front, brush_t **back);
brush_t *BrushFromBox(const vec3_t mins, const vec3_t maxs);
void CalcBrushBounds(const brush_t *b, vec3_t &mins, vec3_t &maxs);

node_t *AllocNode();

bool CheckFaceForHint(const face_t *const f);
bool CheckFaceForSkip(const face_t *const f);
bool CheckFaceForNull(const face_t *const f);
bool CheckFaceForDiscardable(const face_t *f);
#define BRINK_FLOOR_THRESHOLD 0.7
typedef enum
{
    BrinkNone = 0,
    BrinkFloorBlocking,
    BrinkFloor,
    BrinkWallBlocking,
    BrinkWall,
    BrinkAny,
} bbrinklevel_e;
void *CreateBrinkinfo(const dclipnode_t *clipnodes, int headnode);
bool FixBrinks(const void *brinkinfo, bbrinklevel_e level, int &headnode_out, dclipnode_t *clipnodes_out, int maxsize, int size, int &size_out);
void DeleteBrinkinfo(void *brinkinfo);

// =====================================================================================
//Cpt_Andrew - UTSky Check
// =====================================================================================
bool CheckFaceForEnv_Sky(const face_t *const f);
// =====================================================================================

//=============================================================================
// cull.c
void CullStuff();

//=============================================================================
// qbsp.c
extern bool g_nofill;
extern bool g_noinsidefill;
extern bool g_notjunc;
extern bool g_nobrink;
extern bool g_noclipnodemerge;
extern bool g_watervis;
extern bool g_chart;
extern bool g_estimate;
extern int g_maxnode_size;
extern int g_subdivide_size;
extern int g_hullnum;
extern bool g_bLeakOnly;
extern bool g_bLeaked;
extern char g_portfilename[_MAX_PATH];
extern char g_pointfilename[_MAX_PATH];
extern char g_linefilename[_MAX_PATH];
extern char g_bspfilename[_MAX_PATH];
extern char g_extentfilename[_MAX_PATH];

extern bool g_bUseNullTex;

extern bool g_nohull2;

face_t *NewFaceFromFace(const face_t *const in);
void SplitFace(face_t *in, const dplane_t *const split, face_t **front, face_t **back);