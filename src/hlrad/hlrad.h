#pragma once

#include "compress.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/winding.h"
#include "common/bspfile.h"

#pragma warning(disable : 4142 4028)
#pragma warning(default : 4142 4028)

constexpr bool DEFAULT_LERP_ENABLED = true;
constexpr bool DEFAULT_SKY_LIGHTING_FIX = true;

constexpr bool DEFAULT_EMBEDLIGHTMAP_POWEROFTWO = true;
constexpr float DEFAULT_EMBEDLIGHTMAP_DENOMINATOR = 188.0;
constexpr float DEFAULT_EMBEDLIGHTMAP_GAMMA = 1.05;
constexpr int DEFAULT_EMBEDLIGHTMAP_RESOLUTION = 1;

// DEFAULT_HUNT_OFFSET is how many units in front of the plane to place the samples
// Unit of '1' causes the 1 unit crate trick to cause extra shadows
constexpr float DEFAULT_HUNT_OFFSET = 0.5;

constexpr float DEFAULT_EDGE_WIDTH = 0.8;

constexpr float PATCH_HUNT_OFFSET = 0.5;              //--vluzacn
constexpr float HUNT_WALL_EPSILON = (3 * ON_EPSILON); // place sample at least this distance away from any wall //--vluzacn

constexpr float MINIMUM_PATCH_DISTANCE = ON_EPSILON;

constexpr int ALLSTYLES = 64; // HL limit. //--vluzacn

#define BOGUS_RANGE 131072

typedef struct
{
    vec_t v[4][3];
} matrix_t;

// a 4x4 matrix that represents the following transformation (see the ApplyMatrix function)
//
//  / X \    / v[0][0] v[1][0] v[2][0] v[3][0] \ / X \.
//  | Y | -> | v[0][1] v[1][1] v[2][1] v[3][1] | | Y |
//  | Z |    | v[0][2] v[1][2] v[2][2] v[3][2] | | Z |
//  \ 1 /    \    0       0       0       1    / \ 1 /

//
// LIGHTMAP.C STUFF
//

typedef enum
{
    emit_surface,
    emit_point,
    emit_spotlight,
    emit_skylight
} emittype_t;

typedef struct directlight_s
{
    struct directlight_s *next;
    emittype_t type;
    int style;
    vec3_t origin;
    vec3_t intensity;
    vec3_t normal;  // for surfaces and spotlights
    float stopdot;  // for spotlights
    float stopdot2; // for spotlights

    // 'Arghrad'-like features
    vec_t fade; // falloff scaling for linear and inverse square falloff 1.0 = normal, 0.5 = farther, 2.0 = shorter etc

    // -----------------------------------------------------------------------------------
    // Changes by Adam Foster - afoster@compsoc.man.ac.uk
    // Diffuse light_environment light colour
    // Really horrible hack which probably won't work!
    vec3_t diffuse_intensity;
    // -----------------------------------------------------------------------------------
    vec3_t diffuse_intensity2;
    vec_t sunspreadangle;
    int numsunnormals;
    vec3_t *sunnormals;
    vec_t *sunnormalweights;

    vec_t patch_area;
    vec_t patch_emitter_range;
    struct patch_s *patch;
    vec_t texlightgap;
    bool topatch;
} directlight_t;

typedef struct
{
    unsigned size : 12;
    unsigned index : 20;
} transfer_index_t;

typedef unsigned transfer_raw_index_t;
typedef unsigned char transfer_data_t;

typedef unsigned char rgb_transfer_data_t;

constexpr int MAX_PATCHES = (65535 * 16); // limited by transfer_index_t
constexpr int MAX_VISMATRIX_PATCHES = 65535;
constexpr int MAX_SPARSE_VISMATRIX_PATCHES = MAX_PATCHES;

typedef enum
{
    ePatchFlagNull = 0,
    ePatchFlagOutside = 1
} ePatchFlags;

typedef struct patch_s
{
    struct patch_s *next; // next in face
    vec3_t origin;        // Center centroid of winding (cached info calculated from winding)
    vec_t area;           // Surface area of this patch (cached info calculated from winding)
    vec_t exposure;
    vec_t emitter_range;  // Range from patch origin (cached info calculated from winding)
    int emitter_skylevel; // The "skylevel" used for sampling of normals, when the receiver patch is within the range of ACCURATEBOUNCE_THRESHOLD * this->radius. (cached info calculated from winding)
    Winding *winding;     // Winding (patches are triangles, so its easy)
    vec_t scale;          // Texture scale for this face (blend of S and T scale)
    vec_t chop;           // Texture chop for this face factoring in S and T scale

    unsigned iIndex;
    unsigned iData;

    transfer_index_t *tIndex;
    transfer_data_t *tData;
    rgb_transfer_data_t *tRGBData;

    int faceNumber;
    ePatchFlags flags;
    bool translucent_b; // gather light from behind
    vec3_t translucent_v;
    vec3_t texturereflectivity;
    vec3_t bouncereflectivity;

    unsigned char totalstyle[MAXLIGHTMAPS];
    unsigned char directstyle[MAXLIGHTMAPS];
    // HLRAD_AUTOCORING: totallight: all light gathered by patch
    vec3_t totallight[MAXLIGHTMAPS]; // accumulated by radiosity does NOT include light accounted for by direct lighting
    // HLRAD_AUTOCORING: directlight: emissive light gathered by sample
    vec3_t directlight[MAXLIGHTMAPS]; // direct light only
    int bouncestyle;                  // light reflected from this patch must convert to this style. -1 = normal (don't convert)
    unsigned char emitstyle;
    vec3_t baselight; // emissivity only, uses emitstyle
    bool emitmode;    // texlight emit mode. 1 for normal, 0 for fast.
    vec_t samples;
    vec3_t *samplelight_all;       // NULL, or [ALLSTYLES] during BuildFacelights
    unsigned char *totalstyle_all; // NULL, or [ALLSTYLES] during BuildFacelights
    vec3_t *totallight_all;        // NULL, or [ALLSTYLES] during BuildFacelights
    vec3_t *directlight_all;       // NULL, or [ALLSTYLES] during BuildFacelights
    int leafnum;
} patch_t;

//LRC
vec3_t *GetTotalLight(patch_t *patch, int style);

typedef struct facelist_s
{
    dface_t *face;
    facelist_s *next;
} facelist_t;
typedef struct
{
    dface_t *faces[2];
    vec3_t interface_normal; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
    vec3_t vertex_normal[2];
    vec_t cos_normals_angle; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
    bool coplanar;
    bool smooth;
    facelist_t *vertex_facelist[2]; //possible smooth faces, not include faces[0] and faces[1]
    matrix_t textotex[2];           // how we translate texture coordinates from one face to the other face
} edgeshare_t;

extern edgeshare_t g_edgeshare[MAX_MAP_EDGES];

//
// lerp.c stuff
//

// These are bitflags for lighting adjustments for special cases
typedef enum
{
    eModelLightmodeNull = 0,
    eModelLightmodeOpaque = 0x02,
    eModelLightmodeNonsolid = 0x08, // for opaque entities with {texture
} eModelLightmodes;

typedef struct
{
    int entitynum;
    int modelnum;
    vec3_t origin;

    vec3_t transparency_scale;
    bool transparency;
    int style; // -1 = no style; transparency must be false if style >= 0
    // style0 and same style will change to this style, other styles will be blocked.
    bool block; // this entity can't be seen inside, so all lightmap sample should move outside.

} opaqueList_t;

typedef struct
{
    char name[MAX_TEXTURE_NAME_LENGTH]; // not always same with the name in texdata
    int width, height;
    byte *canvas; //[height][width]
    byte palette[256][3];
    vec3_t reflectivity;
} radtexture_t;
extern int g_numtextures;
extern radtexture_t *g_textures;
void AddWadFolder(const char *path);
void LoadTextures();
void EmbedLightmapInTextures();

//
// hlrad globals
//

extern patch_t *g_face_patches[MAX_MAP_FACES];
extern entity_t *g_face_entity[MAX_MAP_FACES];
extern vec3_t g_face_offset[MAX_MAP_FACES]; // for models with origins
extern eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
extern vec3_t g_face_centroids[MAX_MAP_EDGES];
extern entity_t *g_face_texlights[MAX_MAP_FACES];
extern patch_t *g_patches; // shrinked to its real size, because 1048576 patches * 256 bytes = 256MB will be too big
extern unsigned g_num_patches;

extern int g_lerp_enabled;

void MakeShadowSplits();

//==============================================

extern bool g_fastmode;
extern bool g_extra;
extern vec3_t g_ambient;
extern vec_t g_direct_scale;
extern vec_t g_limitthreshold;
extern float g_indirect_sun;
extern float g_smoothing_threshold_2;
extern vec_t *g_smoothvalues; //[nummiptex]
extern bool g_estimate;
extern char g_source[_MAX_PATH];
extern vec_t g_fade;
extern bool g_incremental;
extern bool g_circus;
extern bool g_allow_spread;
extern bool g_sky_lighting_fix;

extern opaqueList_t *g_opaque_face_list;
extern unsigned g_opaque_face_count; // opaque entity count //HLRAD_OPAQUE_NODE

// ------------------------------------------------------------------------
// Changes by Adam Foster - afoster@compsoc.man.ac.uk

extern vec3_t g_colour_qgamma;
extern vec3_t g_colour_lightscale;

// ------------------------------------------------------------------------

extern bool g_customshadow_with_bouncelight;
extern bool g_rgb_transfers;
extern unsigned char g_minlight;
extern float_type g_transfer_compress_type;
extern vector_type g_rgbtransfer_compress_type;
extern bool g_softsky;
extern vec_t g_corings[ALLSTYLES];
extern int stylewarningcount; // not thread safe
extern int stylewarningnext;  // not thread safe
extern vec3_t *g_translucenttextures;
extern vec_t g_translucentdepth;
extern vec3_t *g_lightingconeinfo; //[nummiptex]; X component = power, Y component = scale, Z component = nothing
extern bool g_notextures;
extern vec_t g_texreflectgamma;
extern vec_t g_texreflectscale;
extern vec_t g_blur;
extern vec_t g_maxdiscardedlight;
extern vec3_t g_maxdiscardedpos;
extern vec_t g_texlightgap;

void MakeTnodes(dmodel_t *bm);
void PairEdges();
#define SKYLEVELMAX 8
extern int g_numskynormals[SKYLEVELMAX + 1];     // 0, 6, 18, 66, 258, 1026, 4098, 16386, 65538
extern vec3_t *g_skynormals[SKYLEVELMAX + 1];    //[numskynormals]
extern vec_t *g_skynormalsizes[SKYLEVELMAX + 1]; // the weight of each normal
void BuildDiffuseNormals();
void BuildFacelights(int facenum);
void PrecompLightmapOffsets();
void ReduceLightmap();
void FinalLightFace(int facenum);
void ScaleDirectLights();             // run before AddPatchLights
void CreateFacelightDependencyList(); // run before AddPatchLights
void AddPatchLights(int facenum);
void FreeFacelightDependencyList();
int TestLine(const vec3_t start, const vec3_t stop, vec_t *skyhitout = nullptr);

typedef struct
{
    vec3_t mins, maxs;
    int headnode;
} opaquemodel_t;
extern opaquemodel_t *opaquemodels;

void CreateOpaqueNodes();
int TestLineOpaque(int modelnum, const vec3_t modelorigin, const vec3_t start, const vec3_t stop);
int CountOpaqueFaces(int modelnum);
void DeleteOpaqueNodes();

int TestPointOpaque_r(int nodenum, bool solid, const vec3_t point);
FORCEINLINE int TestPointOpaque(int modelnum, const vec3_t modelorigin, bool solid, const vec3_t point) // use "forceinline" because "inline" does nothing here
{
    opaquemodel_t *thismodel = &opaquemodels[modelnum];
    vec3_t newpoint;
    VectorSubtract(point, modelorigin, newpoint);
    int axial;
    for (axial = 0; axial < 3; axial++)
    {
        if (newpoint[axial] > thismodel->maxs[axial])
            return 0;
        if (newpoint[axial] < thismodel->mins[axial])
            return 0;
    }
    return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}
void CreateDirectLights();
void DeleteDirectLights();
void GetPhongNormal(int facenum, const vec3_t spot, vec3_t phongnormal); // added "const" --vluzacn

typedef bool (*funcCheckVisBit)(unsigned, unsigned, vec3_t &, unsigned int &);
extern funcCheckVisBit g_CheckVisBit;
bool CheckVisBitBackwards(unsigned receiver, unsigned emitter, const vec3_t &backorigin, const vec3_t &backnormal, vec3_t &transparency_out);
void MdlLightHack(void);

// hlradutil.c
vec_t PatchPlaneDist(const patch_t *const patch);
dleaf_t *PointInLeaf(const vec3_t point);
void MakeBackplanes();
const dplane_t *getPlaneFromFace(const dface_t *const face);
const dplane_t *getPlaneFromFaceNumber(unsigned int facenum);
dleaf_t *HuntForWorld(vec_t *point, const vec_t *plane_offset, const dplane_t *plane, int hunt_size, vec_t hunt_scale, vec_t hunt_offset);
void ApplyMatrix(const matrix_t &m, const vec3_t in, vec3_t &out);
void ApplyMatrixOnPlane(const matrix_t &m_inverse, const vec3_t in_normal, vec_t in_dist, vec3_t &out_normal, vec_t &out_dist);
void MultiplyMatrix(const matrix_t &m_left, const matrix_t &m_right, matrix_t &m);
matrix_t MultiplyMatrix(const matrix_t &m_left, const matrix_t &m_right);
void MatrixForScale(const vec3_t center, vec_t scale, matrix_t &m);
matrix_t MatrixForScale(const vec3_t center, vec_t scale);
vec_t CalcMatrixSign(const matrix_t &m);
void TranslateWorldToTex(int facenum, matrix_t &m);
bool InvertMatrix(const matrix_t &m, matrix_t &m_inverse);
void FindFacePositions(int facenum);
void FreePositionMaps();
bool FindNearestPosition(int facenum, const Winding *texwinding, const dplane_t &texplane, vec_t s, vec_t t, vec3_t &pos, vec_t *best_s, vec_t *best_t, vec_t *best_dist, bool *nudged);

// makescales.c
void MakeScalesVismatrix();
void MakeScalesSparseVismatrix();
void MakeScalesNoVismatrix();

// transfers.c
extern std::size_t g_total_transfer;
bool readtransfers(const char *const transferfile, long numpatches);
void writetransfers(const char *const transferfile, long total_patches);

// vismatrixutil.c (shared between vismatrix.c and sparse.c)
void MakeScales(int threadnum);
void DumpTransfersMemoryUsage();
void MakeRGBScales(int threadnum);

// transparency.c (transparency array functions - shared between vismatrix.c and sparse.c)
void GetTransparency(const unsigned p1, const unsigned p2, vec3_t &trans, unsigned int &next_index);
void AddTransparencyToRawArray(const unsigned p1, const unsigned p2, const vec3_t trans);
void CreateFinalTransparencyArrays(const char *print_name);
void FreeTransparencyArrays();
void GetStyle(const unsigned p1, const unsigned p2, int &style, unsigned int &next_index);
void AddStyleToStyleArray(const unsigned p1, const unsigned p2, const int style);
void CreateFinalStyleArrays(const char *print_name);
void FreeStyleArrays();

// lerp.c
void CreateTriangulations(int facenum);
void GetTriangulationPatches(int facenum, int *numpatches, const int **patches);
void InterpolateSampleLight(const vec3_t position, int surface, int numstyles, const int *styles, vec3_t *outs);
void FreeTriangulations();

// mathutil.c
bool TestSegmentAgainstOpaqueList(const vec_t *p1, const vec_t *p2, vec3_t &scaleout, int &opaquestyleout);
bool intersect_line_plane(const dplane_t *const plane, const vec_t *const p1, const vec_t *const p2, vec3_t point);
bool point_in_winding(const Winding &w, const dplane_t &plane, const vec_t *point, vec_t epsilon = 0.0);
bool point_in_winding_noedge(const Winding &w, const dplane_t &plane, const vec_t *point, vec_t width);
void snap_to_winding(const Winding &w, const dplane_t &plane, vec_t *point);
vec_t snap_to_winding_noedge(const Winding &w, const dplane_t &plane, vec_t *point, vec_t width, vec_t maxmove);
void SnapToPlane(const dplane_t *const plane, vec_t *const point, vec_t offset);
vec_t CalcSightArea(const vec3_t receiver_origin, const vec3_t receiver_normal, const Winding *emitter_winding, int skylevel, vec_t lighting_power, vec_t lighting_scale);
vec_t CalcSightArea_SpotLight(const vec3_t receiver_origin, const vec3_t receiver_normal, const Winding *emitter_winding, const vec3_t emitter_normal, vec_t emitter_stopdot, vec_t emitter_stopdot2, int skylevel, vec_t lighting_power, vec_t lighting_scale);
void GetAlternateOrigin(const vec3_t pos, const vec3_t normal, const patch_t *patch, vec3_t &origin);