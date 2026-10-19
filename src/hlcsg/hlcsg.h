#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/boundingbox.h"
#include "common/winding.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
// AJM: added in

#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif

// AJM: added in
#define UNLESS(a) if (!(a))

#define BOGUS_RANGE 65534

constexpr int MAX_HULLSHAPES = 128; // arbitrary

struct faceplane_t
{
    vec3_t normal;
    vec3_t origin;
    vec_t dist;
    planetypes type;
};

struct valve_vects
{
    vec3_t UAxis;
    vec3_t VAxis;
    vec_t shift[2];
    vec_t rotate;
    vec_t scale[2];
};

struct brush_texture_t
{
    valve_vects vects;
    char name[32];
};

struct bside_t
{
    brush_texture_t td;
    bool bevel;
    vec_t planepts[3][3];
};

// A face in a singly linked list. Lists are built and spliced by hand; whoever unlinks a face
// deletes it (deleting a face does not delete the rest of its list).
struct bface_t
{
    bface_t *next = nullptr;
    int planenum = 0;
    faceplane_t *plane = nullptr;
    std::unique_ptr<Winding> w;
    int texinfo = 0;
    int contents = 0;
    int backcontents = 0;
    bool bevel = false; //used for ExpandBrush
    BoundingBox bounds;
};

// Deletes every face of a list.
// bface_t nodes are created and destroyed only here (faces.cpp); brushhull_t owns its list.
bface_t *NewFace(); // zeroed
void FreeFace(bface_t *f);
void FreeFaceList(bface_t *faces);
// A copy of a list, in the same order.
bface_t *CopyFaceList(const bface_t *faces);

// NUM_HULLS should be no larger than MAX_MAP_HULLS
constexpr int NUM_HULLS = 4;

// Owns its face list.
struct brushhull_t
{
    BoundingBox bounds;
    bface_t *faces = nullptr;

    brushhull_t() = default;
    brushhull_t(const brushhull_t &other) : bounds(other.bounds), faces(CopyFaceList(other.faces)) {}
    brushhull_t(brushhull_t &&other) noexcept : bounds(other.bounds), faces(std::exchange(other.faces, nullptr)) {}
    brushhull_t &operator=(const brushhull_t &other)
    {
        if (this != &other)
        {
            bface_t *copy = CopyFaceList(other.faces);
            FreeFaceList(faces);
            faces = copy;
            bounds = other.bounds;
        }
        return *this;
    }
    brushhull_t &operator=(brushhull_t &&other) noexcept
    {
        if (this != &other)
        {
            FreeFaceList(faces);
            faces = std::exchange(other.faces, nullptr);
            bounds = other.bounds;
        }
        return *this;
    }
    ~brushhull_t() { FreeFaceList(faces); }

    void ClearFaces()
    {
        FreeFaceList(faces);
        faces = nullptr;
    }
};

struct brush_t
{
    int originalentitynum;
    int originalbrushnum;
    int entitynum;
    int brushnum;

    int firstside;
    int numsides;

    unsigned int noclip; // !!!FIXME: this should be a flag bitfield so we can use it for other stuff (ie. is this a detail brush...)
    unsigned int cliphull;
    bool bevel;
    int detaillevel;
    int chopdown; // allow this brush to chop brushes of lower detail level
    int chopup;   // allow this brush to be chopped by brushes of higher detail level
    int clipnodedetaillevel;
    int coplanarpriority;
    std::string hullshapes[NUM_HULLS]; // info_hullshape name per hull; empty: the default hull

    int contents;
    brushhull_t hulls[NUM_HULLS];
};

struct hullbrushface_t
{
    vec3_t normal;
    vec3_t point;

    std::vector<std::array<vec_t, 3>> vertexes;
};

struct hullbrushedge_t
{
    vec3_t normals[2];
    vec3_t point;

    vec3_t vertexes[2];
    vec3_t delta; // delta has the same direction as CrossProduct(normals[0],normals[1])
};

struct hullbrushvertex_t
{
    vec3_t point;
};

struct hullbrush_t
{
    std::vector<hullbrushface_t> faces;
    std::vector<hullbrushedge_t> edges;
    std::vector<hullbrushvertex_t> vertexes;
};

struct hullshape_t
{
    std::string id;
    bool disabled = false;
    std::vector<hullbrush_t> brushes; // 0 or 1
};

//=============================================================================
// map.c

extern int g_nummapbrushes;
extern brush_t g_mapbrushes[MAX_MAP_BRUSHES];

constexpr int MAX_MAP_SIDES = (MAX_MAP_BRUSHES * 6);

extern bside_t g_brushsides[MAX_MAP_SIDES];

extern void LoadMapFile(const char *const filename);

//=============================================================================
// textures.c

typedef std::deque<std::string>::iterator WadInclude_i;
extern std::deque<std::string> g_WadInclude; // List of substrings to wadinclude

void WriteMiptex();
int TexinfoForBrushTexture(brush_texture_t *bt, const vec3_t origin);
const char *GetTextureByNumber_CSG(int texturenumber);

//=============================================================================
// brush.c

brush_t *Brush_LoadEntity(entity_t *ent, int hullnum);
contents_t CheckBrushContents(const brush_t *const b);

void CreateBrush(int brushnum);
void CreateHullShape(int entitynum, bool disabled, const char *id, int defaulthulls);
void InitDefaultHulls();

//=============================================================================
// hlcsg.c

extern bool g_onlyents;
extern bool g_wadtextures;
extern bool g_skyclip;

#define EnumPrint(a) #a
typedef enum
{
    clip_smallest,
    clip_normalized,
    clip_simple,
    clip_precise,
    clip_legacy
} cliptype;
extern cliptype g_cliptype;

extern bool g_nolightopt;

extern faceplane_t g_csg_mapplanes[MAX_INTERNAL_MAP_PLANES];
extern int g_nummapplanes;

void GetParamsFromEnt(entity_t *mapent);

// brush.cpp
extern const char *ContentsToString(const contents_t type);

//============================================================================
// hullfile.cpp
extern vec3_t g_hull_size[NUM_HULLS][2];

//============================================================================
// autowad.cpp      AJM

extern bool g_bWadAutoDetect;

//============================================================================