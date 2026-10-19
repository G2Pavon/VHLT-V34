#include "hlrad/hlrad.h"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>

#include "common/cmdline.h"
#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/tokenizer.h"
#include "common/threads.h"


#define DEFAULT_METHOD eMethodSparseVismatrix
static constexpr bool DEFAULT_FASTMODE = false;
static constexpr float DEFAULT_FADE = 1.0;
static constexpr int DEFAULT_BOUNCE = 8;
static constexpr float DEFAULT_AMBIENT_RED = 0.0;
static constexpr float DEFAULT_AMBIENT_GREEN = 0.0;
static constexpr float DEFAULT_AMBIENT_BLUE = 0.0;
// 188 is the fullbright threshold for Goldsrc, regardless of the brightness and gamma settings in the graphic options.
// However, hlrad can only control the light values of each single light style. So the final in-game brightness may exceed 188 if you have set a high value in the "custom appearance" of the light, or if the face receives light from different styles.
static constexpr float DEFAULT_LIMITTHRESHOLD = 188.0;
static constexpr bool DEFAULT_TEXSCALE = true;
static constexpr float DEFAULT_CHOP = 64.0;
static constexpr float DEFAULT_TEXCHOP = 32.0;
static constexpr float DEFAULT_LIGHTSCALE = 2.0; //1.0 //vluzacn
static constexpr float DEFAULT_DLIGHT_THRESHOLD = 10.0;
static constexpr float DEFAULT_DLIGHT_SCALE = 1.0; //2.0 //vluzacn
static constexpr float DEFAULT_SMOOTHING_VALUE = 50.0;
static constexpr float DEFAULT_SMOOTHING2_VALUE = -1.0;
static constexpr float DEFAULT_INDIRECT_SUN = 1.0;
static constexpr bool DEFAULT_EXTRA = false;
static constexpr bool DEFAULT_CIRCUS = false;
static constexpr float DEFAULT_CORING = 0.01;
static constexpr bool DEFAULT_SUBDIVIDE = true;
static constexpr bool DEFAULT_CHART = false;
static constexpr bool DEFAULT_ALLOW_SPREAD = true;
static constexpr float DEFAULT_COLOUR_GAMMA_RED = 0.55;
static constexpr float DEFAULT_COLOUR_GAMMA_GREEN = 0.55;
static constexpr float DEFAULT_COLOUR_GAMMA_BLUE = 0.55;
static constexpr float DEFAULT_COLOUR_LIGHTSCALE_RED = 2.0;   //1.0 //vluzacn
static constexpr float DEFAULT_COLOUR_LIGHTSCALE_GREEN = 2.0; //1.0 //vluzacn
static constexpr float DEFAULT_COLOUR_LIGHTSCALE_BLUE = 2.0;  //1.0 //vluzacn

// Transparency light support for bounced light(transfers) is extreamly slow
// for 'vismatrix' and 'sparse' atm.
// Only recommended to be used with 'nomatrix' mode
static constexpr bool DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT = false;
// RGB Transfers support for HLRAD .. to be used with -customshadowwithbounce
static constexpr bool DEFAULT_RGB_TRANSFERS = false;
static constexpr unsigned int DEFAULT_MINLIGHT = 0;
static constexpr float_type DEFAULT_TRANSFER_COMPRESS_TYPE = FLOAT16;
static constexpr vector_type DEFAULT_RGBTRANSFER_COMPRESS_TYPE = VECTOR32;
static constexpr bool DEFAULT_SOFTSKY = true;
static constexpr int DEFAULT_BLOCKOPAQUE = 1;
static constexpr float DEFAULT_TRANSLUCENTDEPTH = 2.0f;
static constexpr bool DEFAULT_NOTEXTURES = false;
static constexpr float DEFAULT_TEXREFLECTGAMMA = 1.76f; // 2.0(texgamma cvar) / 2.5 (gamma cvar) * 2.2 (screen gamma) = 1.76
static constexpr float DEFAULT_TEXREFLECTSCALE = 0.7f;  // arbitrary (This is lower than 1.0, because textures are usually brightened in order to look better in Goldsrc. Textures are made brightened because Goldsrc is only able to darken the texture when combining the texture with the lightmap.)
static constexpr float DEFAULT_BLUR = 1.5;              // classic lighting is equivalent to "-blur 1.0"
static constexpr float DEFAULT_TEXLIGHTGAP = 0.0;
static constexpr bool DEFAULT_ESTIMATE = false;
static constexpr float ACCURATEBOUNCE_THRESHOLD = 4.0; // If the receiver patch is closer to emitter patch than EXACTBOUNCE_THRESHOLD * emitter_patch->radius, calculate the exact visibility amount.
#define ACCURATEBOUNCE_DEFAULT_SKYLEVEL 5              // sample 1026 normals

typedef enum
{
    eMethodVismatrix,
    eMethodSparseVismatrix,
    eMethodNoVismatrix
} eVisMethods;

static unsigned g_numbounce = DEFAULT_BOUNCE; // 3; /* Originally this was 8 */
static float g_lightscale = DEFAULT_LIGHTSCALE;
static float g_dlight_threshold = DEFAULT_DLIGHT_THRESHOLD; // was DIRECT_LIGHT constant
static float g_smoothing_value = DEFAULT_SMOOTHING_VALUE;
static float g_smoothing_value_2 = DEFAULT_SMOOTHING2_VALUE;
static int g_blockopaque = DEFAULT_BLOCKOPAQUE;
// Cosine of smoothing angle(in radians)
static float g_coring = DEFAULT_CORING;      // Light threshold to force to blackness(minimizes lightmaps)

// [g_num_patches + 1], only while the bounces run
static std::unique_ptr<vec3_t[][MAXLIGHTMAPS]> emitlight; //LRC
static std::unique_ptr<vec3_t[][MAXLIGHTMAPS]> addlight;  //LRC
static std::unique_ptr<unsigned char[][MAXLIGHTMAPS]> newstyles;
static float g_smoothing_threshold;

patch_t *g_face_patches[MAX_MAP_FACES];
entity_t *g_face_entity[MAX_MAP_FACES];
eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
patch_t *g_patches; // s_patches.data()
// Owners of the patches and of their windings (patch_t only points to its winding: see hlrad.h).
static std::vector<patch_t> s_patches;
static std::vector<std::unique_ptr<Winding>> s_patch_windings;
std::vector<patch_transfers_t> g_patch_transfers;
entity_t *g_face_texlights[MAX_MAP_FACES];
unsigned g_num_patches;
vec3_t g_face_offset[MAX_MAP_FACES]; // for rotating bmodels
float g_smoothing_threshold_2;

bool g_fastmode = DEFAULT_FASTMODE;
eVisMethods g_method = DEFAULT_METHOD;
vec_t g_fade = DEFAULT_FADE;
vec_t g_direct_scale = DEFAULT_DLIGHT_SCALE;
vec3_t g_ambient = {DEFAULT_AMBIENT_RED, DEFAULT_AMBIENT_GREEN, DEFAULT_AMBIENT_BLUE};
vec_t g_limitthreshold = DEFAULT_LIMITTHRESHOLD;
char g_source[_MAX_PATH] = "";
float g_indirect_sun = DEFAULT_INDIRECT_SUN;
bool g_extra = DEFAULT_EXTRA;
bool g_texscale = DEFAULT_TEXSCALE;
bool g_circus = DEFAULT_CIRCUS;
bool g_allow_spread = DEFAULT_ALLOW_SPREAD;
bool g_customshadow_with_bouncelight = DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT;
bool g_rgb_transfers = DEFAULT_RGB_TRANSFERS;
vec3_t g_colour_qgamma = {DEFAULT_COLOUR_GAMMA_RED, DEFAULT_COLOUR_GAMMA_GREEN, DEFAULT_COLOUR_GAMMA_BLUE};
vec3_t g_colour_lightscale = {DEFAULT_COLOUR_LIGHTSCALE_RED, DEFAULT_COLOUR_LIGHTSCALE_GREEN, DEFAULT_COLOUR_LIGHTSCALE_BLUE};
unsigned char g_minlight = DEFAULT_MINLIGHT;
float_type g_transfer_compress_type = DEFAULT_TRANSFER_COMPRESS_TYPE;
vector_type g_rgbtransfer_compress_type = DEFAULT_RGBTRANSFER_COMPRESS_TYPE;
bool g_softsky = DEFAULT_SOFTSKY;
bool g_notextures = DEFAULT_NOTEXTURES;
vec_t g_texreflectgamma = DEFAULT_TEXREFLECTGAMMA;
vec_t g_texreflectscale = DEFAULT_TEXREFLECTSCALE;
bool g_chart = DEFAULT_CHART;
bool g_estimate = DEFAULT_ESTIMATE;
// Patch creation and subdivision criteria
bool g_subdivide = DEFAULT_SUBDIVIDE;
vec_t g_chop = DEFAULT_CHOP;       // Chop value for normal textures
vec_t g_texchop = DEFAULT_TEXCHOP; // Chop value for texture lights
// Opaque faces
opaqueList_t *g_opaque_face_list = nullptr; // a view of s_opaque_face_list
static std::vector<opaqueList_t> s_opaque_face_list;
unsigned g_opaque_face_count = 0;
float g_corings[ALLSTYLES];
vec3_t *g_translucenttextures = nullptr; // [nummiptex], a view of s_translucenttextures
static std::unique_ptr<vec3_t[]> s_translucenttextures;
vec_t g_translucentdepth = DEFAULT_TRANSLUCENTDEPTH;
vec_t g_blur = DEFAULT_BLUR;
vec_t g_texlightgap = DEFAULT_TEXLIGHTGAP;
// Misc
int leafparents[MAX_MAP_LEAFS];
int nodeparents[MAX_MAP_NODES];
static std::atomic<vec_t> s_maxdiscardedlight{0}; // see NoteDiscardedLight
static vec3_t s_maxdiscardedpos = {0, 0, 0};

void NoteDiscardedLight(vec_t light, const vec3_t pos)
{
    if (light > s_maxdiscardedlight.load(std::memory_order_relaxed) + NORMAL_EPSILON) // cheap test before the lock
    {
        ThreadLock();
        if (light > s_maxdiscardedlight.load(std::memory_order_relaxed) + NORMAL_EPSILON)
        {
            s_maxdiscardedlight.store(light, std::memory_order_relaxed);
            VectorCopy(pos, s_maxdiscardedpos);
        }
        ThreadUnlock();
    }
}

static float totalarea = 0;

//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own version of this
//      to handle its own specific settings.

void GetParamsFromEnt(entity_t *mapent)
{
    char szTmp[256]; //lightdata

    Log("\nCompile Settings detected from info_compile_parameters entity\n");
    Log("%30s [ %-9s ]\n", "Compile Option", "setting");

    int iTmp = KilobytesToBytes(IntForKey(mapent, "lightdata")); //lightdata
    if (iTmp > g_max_map_lightdata)                   //--vluzacn
    {
        g_max_map_lightdata = iTmp;
        safe_snprintf(szTmp, sizeof(szTmp), "%i", g_max_map_lightdata);
        Log("%30s [ %-9s ]\n", "Lighting Data Memory", szTmp);
    }

    if (IntForKey(mapent, "estimate"))
    {
        g_estimate = true;
    }
    else
    {
        g_estimate = false;
    }
    Log("%30s [ %-9s ]\n", "Estimate Compile Times", g_estimate ? "on" : "off");

    iTmp = IntForKey(mapent, "bounce");
    if (iTmp)
    {
        g_numbounce = std::abs(iTmp);
        Log("%30s [ %-9s ]\n", "Number of radiosity bounces", ValueForKey(mapent, "bounce"));
    }

    iTmp = IntForKey(mapent, "customshadowwithbounce");
    if (iTmp)
    {
        g_customshadow_with_bouncelight = true;
        Log("%30s [ %-9s ]\n", "Custom Shadow with Bounce Light", ValueForKey(mapent, "customshadowwithbounce"));
    }
    iTmp = IntForKey(mapent, "rgbtransfers");
    if (iTmp)
    {
        g_rgb_transfers = true;
        Log("%30s [ %-9s ]\n", "RGB Transfers", ValueForKey(mapent, "rgbtransfers"));
    }

    const char *pszTmp = ValueForKey(mapent, "ambient");
    if (pszTmp)
    {
        float red = 0;
        float green = 0;
        float blue = 0;
        if (std::sscanf(pszTmp, "%f %f %f", &red, &green, &blue))
        {
            if (red < 0 || red > 1 || green < 0 || green > 1 || blue < 0 || blue > 1)
            {
                Error("info_compile_parameters: Ambient World Light (ambient) all 3 values must be within the range of 0.0 to 1.0\n"
                      "Parsed values:\n"
                      "    red [ %1.3f ] %s\n"
                      "  green [ %1.3f ] %s\n"
                      "   blue [ %1.3f ] %s\n",
                      red, (red < 0 || red > 1) ? "OUT OF RANGE" : "", green, (green < 0 || green > 1) ? "OUT OF RANGE" : "", blue, (blue < 0 || blue > 1) ? "OUT OF RANGE" : "");
            }

            if (red == 0 && green == 0 && blue == 0)
            {
            } // dont bother setting null values
            else
            {
                g_ambient[0] = red * 128;
                g_ambient[1] = green * 128;
                g_ambient[2] = blue * 128;
                Log("%30s [ %1.3f %1.3f %1.3f ]\n", "Ambient world light (R G B)", red, green, blue);
            }
        }
        else
        {
            Error("info_compile_parameters: Ambient World Light (ambient) has unrecognised value\n"
                  "This keyvalue accepts 3 numeric values from 0.000 to 1.000, use \"0 0 0\" if in doubt");
        }
    }

    float flTmp = FloatForKey(mapent, "smooth");
    if (flTmp)
    {
        g_smoothing_threshold = std::cos(g_smoothing_value * (Q_PI / 180.0)); // --vluzacn
        Log("%30s [ %-9s ]\n", "Smoothing threshold", ValueForKey(mapent, "smooth"));
    }

    flTmp = FloatForKey(mapent, "dscale");
    if (flTmp)
    {
        g_direct_scale = flTmp;
        Log("%30s [ %-9s ]\n", "Direct Lighting Scale", ValueForKey(mapent, "dscale"));
    }

    iTmp = IntForKey(mapent, "chop");
    if (iTmp)
    {
        g_chop = iTmp;
        Log("%30s [ %-9s ]\n", "Chop Size", ValueForKey(mapent, "chop"));
    }

    flTmp = FloatForKey(mapent, "texchop");
    if (flTmp)
    {
        g_texchop = flTmp;
        Log("%30s [ %-9s ]\n", "Texture Light Chop Size", ValueForKey(mapent, "texchop"));
    }

    iTmp = IntForKey(mapent, "hlrad");
    if (iTmp == 0)
    {
        Fatal(assume_TOOL_CANCEL,
              "%s flag was not checked in info_compile_parameters entity, execution of %s cancelled", g_Program, g_Program);
        CheckFatal();
    }
    else if (iTmp == 1)
    {
        g_extra = false;
    }
    else if (iTmp == 2)
    {
        g_extra = true;
    }
    Log("%30s [ %-9s ]\n", "Extra RAD", g_extra ? "on" : "off");

    iTmp = IntForKey(mapent, "sparse");
    if (iTmp == 1)
    {
        g_method = eMethodSparseVismatrix;
    }
    else if (iTmp == 0)
    {
        g_method = eMethodNoVismatrix;
    }
    else if (iTmp == 2)
    {
        g_method = eMethodVismatrix;
    }
    Log("%30s [ %-9s ]\n", "Sparse Vismatrix", g_method == eMethodSparseVismatrix ? "on" : "off");
    Log("%30s [ %-9s ]\n", "NoVismatrix", g_method == eMethodNoVismatrix ? "on" : "off");

    iTmp = IntForKey(mapent, "circus");
    if (iTmp == 0)
    {
        g_circus = false;
    }
    else if (iTmp == 1)
    {
        g_circus = true;
    }
    Log("%30s [ %-9s ]\n", "Circus Lighting Mode", g_circus ? "on" : "off");

    Log("\n");
}

static void MakeParents(const int nodenum, const int parent)
{
    nodeparents[nodenum] = parent;
    dnode_t *node = g_dnodes + nodenum;

    for (int i = 0; i < 2; i++)
    {
        int j = node->children[i];
        if (j < 0)
        {
            leafparents[-j - 1] = nodenum;
        }
        else
        {
            MakeParents(j, nodenum);
        }
    }
}

// TEXTURE LIGHT VALUES
// misc
typedef struct
{
    std::string name;
    vec3_t value;
    const char *filename;
} texlight_t;

static std::vector<texlight_t> s_texlights;
using texlight_i = std::vector<texlight_t>::iterator;

static void LightForTexture(const char *const name, vec3_t result)
{
    for (auto it = s_texlights.begin(); it != s_texlights.end(); it++)
    {
        if (!strcasecmp(name, it->name.c_str()))
        {
            VectorCopy(it->value, result);
            return;
        }
    }
    VectorClear(result);
}

// MAKE FACES
static void BaseLightForFace(const dface_t *const f, vec3_t light)
{
    int fn = f - g_dfaces;
    if (g_face_texlights[fn])
    {
        double r;
        double g;
        double b;
        double scaler;
        switch (std::sscanf(ValueForKey(g_face_texlights[fn], "_light"), "%lf %lf %lf %lf", &r, &g, &b, &scaler))
        {
        case -1:
        case 0:
            r = 0.0;
            [[fallthrough]];
        case 1:
            g = b = r;
            [[fallthrough]];
        case 3:
            break;
        case 4:
            r *= scaler / 255.0;
            g *= scaler / 255.0;
            b *= scaler / 255.0;
            break;
        default:
            vec3_t origin;
            GetVectorForKey(g_face_texlights[fn], "origin", origin);
            Log("light at (%f,%f,%f) has bad or missing '_light' value : '%s'\n",
                origin[0], origin[1], origin[2], ValueForKey(g_face_texlights[fn], "_light"));
            r = g = b = 0;
            break;
        }
        light[0] = r > 0 ? r : 0;
        light[1] = g > 0 ? g : 0;
        light[2] = b > 0 ? b : 0;
        return;
    }
    // check for light emited by texture
    LightForTexture(GetTextureByNumber(f->texinfo), light);
}

static bool IsSpecial(const dface_t *const f)
{
    return g_texinfo[f->texinfo].flags & TEX_SPECIAL;
}

static bool PlacePatchInside(patch_t *patch)
{
    const vec_t *face_offset = g_face_offset[patch->faceNumber];

    const dplane_t *plane = getPlaneFromFaceNumber(patch->faceNumber);

    vec_t pointsfound = 0;
    vec_t pointstested = 0;
    vec3_t center;
    vec3_t bestpoint = {0, 0, 0}; // set when found
    vec_t bestdist = -1.0;
    vec3_t point;
    vec3_t v;

    patch->winding->getCenter(center);
    bool found = false;

    VectorMA(center, PATCH_HUNT_OFFSET, plane->normal, point);
    pointstested++;
    if (HuntForWorld(point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET) ||
        HuntForWorld(point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET))
    {
        pointsfound++;
        VectorSubtract(point, center, v);
        vec_t dist = VectorLength(v);
        if (!found || dist < bestdist)
        {
            found = true;
            VectorCopy(point, bestpoint);
            bestdist = dist;
        }
    }
    {
        for (int i = 0; i < (int)patch->winding->m_NumPoints; i++)
        {
            const vec_t *p1 = patch->winding->m_Points[i];
            const vec_t *p2 = patch->winding->m_Points[(i + 1) % patch->winding->m_NumPoints];
            VectorAdd(p1, p2, point);
            VectorAdd(point, center, point);
            VectorScale(point, 1.0 / 3.0, point);
            VectorMA(point, PATCH_HUNT_OFFSET, plane->normal, point);
            pointstested++;
            if (HuntForWorld(point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET) ||
                HuntForWorld(point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET))
            {
                pointsfound++;
                VectorSubtract(point, center, v);
                vec_t dist = VectorLength(v);
                if (!found || dist < bestdist)
                {
                    found = true;
                    VectorCopy(point, bestpoint);
                    bestdist = dist;
                }
            }
        }
    }

    patch->exposure = pointsfound / pointstested;
    if (found)
    {
        VectorCopy(bestpoint, patch->origin);
        patch->flags = ePatchFlagNull;
        return true;
    }
    else // patch outside world
    {
        VectorMA(center, PATCH_HUNT_OFFSET, plane->normal, patch->origin);
        patch->flags = ePatchFlagOutside;
        return false;
    }
}

static void UpdateEmitterInfo(patch_t *patch)
{
#if ACCURATEBOUNCE_DEFAULT_SKYLEVEL + 3 > SKYLEVELMAX
#error "please raise SKYLEVELMAX"
#endif
    const vec_t *origin = patch->origin;
    const Winding *winding = patch->winding;
    vec_t radius = ON_EPSILON;
    for (int x = 0; x < (int)winding->m_NumPoints; x++)
    {
        vec3_t delta;
        VectorSubtract(winding->m_Points[x], origin, delta);
        vec_t dist = VectorLength(delta);
        if (dist > radius)
        {
            radius = dist;
        }
    }
    int skylevel = ACCURATEBOUNCE_DEFAULT_SKYLEVEL;
    vec_t area = winding->getArea();
    vec_t size = 0.8f;
    if (area < size * radius * radius) // the shape is too thin
    {
        skylevel++;
        size *= 0.25f;
        if (area < size * radius * radius)
        {
            skylevel++;
            size *= 0.25f;
            if (area < size * radius * radius)
            {
                // stop here
                radius = std::sqrt(area / size);
                // just decrease the range to limit the use of the new method. because when the area is small, the new method becomes randomized and unstable.
            }
        }
    }
    patch->emitter_range = ACCURATEBOUNCE_THRESHOLD * radius; // fix pointy texlights
    patch->emitter_skylevel = skylevel;
}

// SUBDIVIDE PATCHES
// NOTE:  every surface must be divided into at least two g_patches each axis

// Cuts the patch's winding along a grid of planes A and B into pieces (strips along A, then each strip
// along B), in the order the patches are made from them.
// This function has been rewritten because the original one is not totally correct and may fail to do what it claims.
static std::vector<std::unique_ptr<Winding>> cutWindingWithGrid(const patch_t *patch, const dplane_t *plA, const dplane_t *plB)
{
    // patch->winding->m_NumPoints must > 0
    // plA->dist and plB->dist will not be used
    const int max_gridsize = 64;
    vec_t gridstartA;
    vec_t gridstartB;
    int gridsizeA;
    int gridsizeB;
    vec_t gridchopA;
    vec_t gridchopB;

    auto winding = std::make_unique<Winding>(*patch->winding); // perform all the operations on the copy
    vec_t chop = patch->chop;
    chop = qmax(1.0, chop);
    vec_t epsilon = 0.6;

    // optimize the grid
    {
        vec_t minA = BOGUS_RANGE;
        vec_t maxA = -BOGUS_RANGE;
        vec_t minB = BOGUS_RANGE;
        vec_t maxB = -BOGUS_RANGE;

        for (int x = 0; x < (int)winding->m_NumPoints; x++)
        {
            vec_t *point = winding->m_Points[x];
            vec_t dotA = DotProduct(point, plA->normal);
            minA = qmin(minA, dotA);
            maxA = qmax(maxA, dotA);
            vec_t dotB = DotProduct(point, plB->normal);
            minB = qmin(minB, dotB);
            maxB = qmax(maxB, dotB);
        }

        gridchopA = chop;
        gridsizeA = (int)std::ceil((maxA - minA - 2 * epsilon) / gridchopA);
        gridsizeA = qmax(1, gridsizeA);
        if (gridsizeA > max_gridsize)
        {
            gridsizeA = max_gridsize;
            gridchopA = (maxA - minA) / (vec_t)gridsizeA;
        }
        gridstartA = (minA + maxA) / 2.0 - (gridsizeA / 2.0) * gridchopA;

        gridchopB = chop;
        gridsizeB = (int)std::ceil((maxB - minB - 2 * epsilon) / gridchopB);
        gridsizeB = qmax(1, gridsizeB);
        if (gridsizeB > max_gridsize)
        {
            gridsizeB = max_gridsize;
            gridchopB = (maxB - minB) / (vec_t)gridsizeB;
        }
        gridstartB = (minB + maxB) / 2.0 - (gridsizeB / 2.0) * gridchopB;
    }

    // cut the winding by the direction of plane A into strips
    std::vector<std::unique_ptr<Winding>> strips;
    for (int i = 1; i < gridsizeA; i++)
    {
        vec_t dist = gridstartA + i * gridchopA;
        WindingSplit split = winding->Split(plA->normal, dist);

        if (!split.front || split.front->WindingOnPlaneSide(plA->normal, dist, epsilon) == SIDE_ON) // ended
        {
            break;
        }
        if (!split.back || split.back->WindingOnPlaneSide(plA->normal, dist, epsilon) == SIDE_ON) // didn't begin
        {
            continue;
        }
        strips.push_back(std::move(split.back));
        winding = std::move(split.front);
    }
    strips.push_back(std::move(winding));

    // cut each strip by the direction of plane B
    std::vector<std::unique_ptr<Winding>> pieces;
    for (std::unique_ptr<Winding> &strip : strips)
    {
        for (int j = 1; j < gridsizeB; j++)
        {
            vec_t dist = gridstartB + j * gridchopB;
            WindingSplit split = strip->Split(plB->normal, dist);

            if (!split.front || split.front->WindingOnPlaneSide(plB->normal, dist, epsilon) == SIDE_ON) // ended
            {
                break;
            }
            if (!split.back || split.back->WindingOnPlaneSide(plB->normal, dist, epsilon) == SIDE_ON) // didn't begin
            {
                continue;
            }
            pieces.push_back(std::move(split.back));
            strip = std::move(split.front);
        }
        pieces.push_back(std::move(strip));
    }
    return pieces;
}

//      From patch, determine perpindicular grid planes to subdivide with (returned in planeA and planeB)
//      assume S and T is perpindicular (they SHOULD be in worldcraft 3.3 but aren't always . . .)

static void getGridPlanes(const patch_t *const p, dplane_t *const pl)
{
    const patch_t *patch = p;
    dplane_t *planes = pl;
    const dface_t *f = g_dfaces + patch->faceNumber;
    texinfo_t *tx = &g_texinfo[f->texinfo];
    dplane_t *plane = planes;
    const dplane_t *faceplane = getPlaneFromFaceNumber(patch->faceNumber);

    for (int x = 0; x < 2; x++, plane++)
    {
        // cut the patch along texel grid planes
        vec_t val = DotProduct(faceplane->normal, tx->vecs[!x]);
        VectorMA(tx->vecs[!x], -val, faceplane->normal, plane->normal);
        VectorNormalize(plane->normal);
        plane->dist = DotProduct(plane->normal, patch->origin);
    }
}

// Replaces patch's winding with the first grid piece and makes a patch (a copy of this one) from each
// other piece. The pieces go to the winding store; patch's old winding stays with its owner.
static void SubdividePatch(patch_t *patch)
{
    dplane_t planes[2];
    dplane_t *plA = &planes[0];
    dplane_t *plB = &planes[1];

    getGridPlanes(patch, planes);
    std::vector<std::unique_ptr<Winding>> pieces = cutWindingWithGrid(patch, plA, plB);

    patch->next = nullptr;
    patch->winding = s_patch_windings.emplace_back(std::move(pieces[0])).get();
    patch->area = patch->winding->getArea();
    patch->winding->getCenter(patch->origin);
    PlacePatchInside(patch);
    UpdateEmitterInfo(patch);

    for (std::size_t x = 1; x < pieces.size(); x++)
    {
        patch_t *new_patch = &s_patches.emplace_back(*patch); // s_patches has room for MAX_PATCHES: patch stays valid

        new_patch->winding = s_patch_windings.emplace_back(std::move(pieces[x])).get();
        new_patch->area = new_patch->winding->getArea();
        new_patch->winding->getCenter(new_patch->origin);
        PlacePatchInside(new_patch);
        UpdateEmitterInfo(new_patch);

        g_num_patches++;
        hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
    }

    // ATTENTION: We let SortPatches relink all the ->next correctly! instead of doing it here too which is somewhat complicated
}

static std::unique_ptr<vec_t[]> chopscales; //[nummiptex]
static void ReadCustomChopValue()
{
    int num = TextureLump().count();
    chopscales = std::make_unique_for_overwrite<vec_t[]>(num); // every entry is set below
    for (int i = 0; i < num; i++)
    {
        chopscales[i] = 1.0;
    }
    for (int k = 0; k < g_numentities; k++)
    {
        entity_t *mapent = &g_entities[k];
        if (std::strcmp(ValueForKey(mapent, "classname"), "info_chopscale"))
            continue;
        for (int i = 0; i < num; i++)
        {
            const char *texname = GetMiptexName(i);
            for (const EntityKeyValue &ep : mapent->keys)
            {
                if (strcasecmp(ep.key.c_str(), texname))
                    continue;
                if (!strcasecmp(ep.key.c_str(), "origin"))
                    continue;
                if (std::atof(ep.value.c_str()) <= 0)
                    continue;
                chopscales[i] = std::atof(ep.value.c_str());
            }
        }
    }
}

static vec_t ChopScaleForTexture(int facenum)
{
    return chopscales[g_texinfo[g_dfaces[facenum].texinfo].miptex];
}

vec_t *g_smoothvalues; //[nummiptex], a view of s_smoothvalues
static std::unique_ptr<vec_t[]> s_smoothvalues;
static void ReadCustomSmoothValue()
{
    int num = TextureLump().count();
    s_smoothvalues = std::make_unique_for_overwrite<vec_t[]>(num); // every entry is set below
    g_smoothvalues = s_smoothvalues.get();
    for (int i = 0; i < num; i++)
    {
        g_smoothvalues[i] = g_smoothing_threshold;
    }
    for (int k = 0; k < g_numentities; k++)
    {
        entity_t *mapent = &g_entities[k];
        if (std::strcmp(ValueForKey(mapent, "classname"), "info_smoothvalue"))
            continue;
        for (int i = 0; i < num; i++)
        {
            const char *texname = GetMiptexName(i);
            for (const EntityKeyValue &ep : mapent->keys)
            {
                if (strcasecmp(ep.key.c_str(), texname))
                    continue;
                if (!strcasecmp(ep.key.c_str(), "origin"))
                    continue;
                g_smoothvalues[i] = std::cos(std::atof(ep.value.c_str()) * (Q_PI / 180.0));
            }
        }
    }
}

static void ReadTranslucentTextures()
{
    int num = TextureLump().count();
    s_translucenttextures = std::make_unique_for_overwrite<vec3_t[]>(num); // every entry is set below
    g_translucenttextures = s_translucenttextures.get();
    for (int i = 0; i < num; i++)
    {
        VectorClear(g_translucenttextures[i]);
    }
    for (int k = 0; k < g_numentities; k++)
    {
        entity_t *mapent = &g_entities[k];
        if (std::strcmp(ValueForKey(mapent, "classname"), "info_translucent"))
            continue;
        for (int i = 0; i < num; i++)
        {
            const char *texname = GetMiptexName(i);
            for (const EntityKeyValue &ep : mapent->keys)
            {
                if (strcasecmp(ep.key.c_str(), texname))
                    continue;
                if (!strcasecmp(ep.key.c_str(), "origin"))
                    continue;
                double r;
                double g;
                double b;
                int count = std::sscanf(ep.value.c_str(), "%lf %lf %lf", &r, &g, &b);
                if (count == 1)
                {
                    g = b = r;
                }
                else if (count != 3)
                {
                    Warning("ignore bad translucent value '%s'", ep.value.c_str());
                    continue;
                }
                if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 || b < 0.0 || b > 1.0)
                {
                    Warning("translucent value should be 0.0-1.0");
                    continue;
                }
                g_translucenttextures[i][0] = r;
                g_translucenttextures[i][1] = g;
                g_translucenttextures[i][2] = b;
            }
        }
    }
}

vec3_t *g_lightingconeinfo; //[nummiptex], a view of s_lightingconeinfo
static std::unique_ptr<vec3_t[]> s_lightingconeinfo;
static vec_t DefaultScaleForPower(vec_t power)
{
    // scale = Pi / Integrate [2 Pi * Sin [x] * Cos[x] ^ power, {x, 0, Pi / 2}]
    vec_t scale = (1 + power) / 2.0;
    return scale;
}

static void ReadLightingCone()
{
    int num = TextureLump().count();
    s_lightingconeinfo = std::make_unique_for_overwrite<vec3_t[]>(num); // every entry is set below
    g_lightingconeinfo = s_lightingconeinfo.get();
    for (int i = 0; i < num; i++)
    {
        g_lightingconeinfo[i][0] = 1.0; // default power
        g_lightingconeinfo[i][1] = 1.0; // default scale
    }
    for (int k = 0; k < g_numentities; k++)
    {
        entity_t *mapent = &g_entities[k];
        if (std::strcmp(ValueForKey(mapent, "classname"), "info_angularfade"))
            continue;
        for (int i = 0; i < num; i++)
        {
            const char *texname = GetMiptexName(i);
            for (const EntityKeyValue &ep : mapent->keys)
            {
                if (strcasecmp(ep.key.c_str(), texname))
                    continue;
                if (!strcasecmp(ep.key.c_str(), "origin"))
                    continue;
                double power;
                double scale;
                int count = std::sscanf(ep.value.c_str(), "%lf %lf", &power, &scale);
                if (count == 1)
                {
                    scale = 1.0;
                }
                else if (count != 2)
                {
                    Warning("ignore bad angular fade value '%s'", ep.value.c_str());
                    continue;
                }
                if (power < 0.0 || scale < 0.0)
                {
                    Warning("ignore disallowed angular fade value '%s'", ep.value.c_str());
                    continue;
                }
                scale *= DefaultScaleForPower(power);
                g_lightingconeinfo[i][0] = power;
                g_lightingconeinfo[i][1] = scale;
            }
        }
    }
}

static vec_t getScale(const patch_t *const patch)
{
    dface_t *f = g_dfaces + patch->faceNumber;
    texinfo_t *tx = &g_texinfo[f->texinfo];

    if (g_texscale)
    {
        const dplane_t *faceplane = getPlaneFromFace(f);
        vec3_t vecs_perpendicular[2];
        vec_t scale[2];

        // snap texture "vecs" to faceplane without affecting texture alignment
        for (int x = 0; x < 2; x++)
        {
            vec_t dot = DotProduct(faceplane->normal, tx->vecs[x]);
            VectorMA(tx->vecs[x], -dot, faceplane->normal, vecs_perpendicular[x]);
        }

        scale[0] = 1 / qmax(NORMAL_EPSILON, VectorLength(vecs_perpendicular[0]));
        scale[1] = 1 / qmax(NORMAL_EPSILON, VectorLength(vecs_perpendicular[1]));

        // don't care about the angle between vecs[0] and vecs[1] (given the length of "vecs", smaller angle = larger texel area), because gridplanes will have the same angle (also smaller angle = larger patch area)

        return std::sqrt(scale[0] * scale[1]);
    }
    else
    {
        return 1.0;
    }
}

static bool getEmitMode(const patch_t *patch)
{
    bool emitmode = false;
    vec_t value =
        DotProduct(patch->baselight, patch->texturereflectivity) / 3;
    if (g_face_texlights[patch->faceNumber])
    {
        if (*ValueForKey(g_face_texlights[patch->faceNumber], "_scale"))
        {
            value *= FloatForKey(g_face_texlights[patch->faceNumber], "_scale");
        }
    }
    if (value > 0.0)
    {
        emitmode = true;
    }
    if (value < g_dlight_threshold)
    {
        emitmode = false;
    }
    if (g_face_texlights[patch->faceNumber])
    {
        switch (IntForKey(g_face_texlights[patch->faceNumber], "_fast"))
        {
        case 1:
            emitmode = false;
            break;
        case 2:
            emitmode = true;
            break;
        }
    }
    return emitmode;
}

static vec_t getChop(const patch_t *const patch)
{
    vec_t rval;

    if (g_face_texlights[patch->faceNumber])
    {
        if (*ValueForKey(g_face_texlights[patch->faceNumber], "_chop"))
        {
            rval = FloatForKey(g_face_texlights[patch->faceNumber], "_chop");
            if (rval < 1.0)
            {
                rval = 1.0;
            }
            return rval;
        }
    }
    if (!patch->emitmode)
    {
        rval = g_chop * getScale(patch);
    }
    else
    {
        rval = g_texchop * getScale(patch);
        // we needn't do this now, so let's save our compile time.
    }

    rval *= ChopScaleForTexture(patch->faceNumber);
    return rval;
}

// w becomes the patch's winding (or its grid pieces do); it is dropped if the face gets no patch.
static void MakePatchForFace(const int fn, std::unique_ptr<Winding> w, int style, int bouncestyle) //LRC
{
    const dface_t *f = g_dfaces + fn;

    // No g_patches at all for the sky!
    if (!IsSpecial(f))
    {
        if (g_face_texlights[fn])
        {
            style = IntForKey(g_face_texlights[fn], "style");
            if (style < 0)
                style = -style;
            style = (unsigned char)style;
            if (style >= ALLSTYLES)
            {
                Error("invalid light style: style (%d) >= ALLSTYLES (%d)", style, ALLSTYLES);
            }
        }
        vec3_t light;
        vec3_t centroid = {0, 0, 0};

        int numpoints = w->m_NumPoints;

        if (numpoints < 3) // WTF! (Actually happens in real-world maps too)
        {
            return;
        }
        if (numpoints > MAX_POINTS_ON_WINDING)
        {
            Error("numpoints %d > MAX_POINTS_ON_WINDING", numpoints);
            return;
        }

        hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
        patch_t *patch = &s_patches.emplace_back(); // zeroed

        patch->winding = w.get();

        patch->area = patch->winding->getArea();
        patch->winding->getCenter(patch->origin);
        patch->faceNumber = fn;

        totalarea += patch->area;

        BaseLightForFace(f, light);
        //LRC        VectorCopy(light, patch->totallight);
        VectorCopy(light, patch->baselight);

        patch->emitstyle = style;

        VectorCopy(g_textures[g_texinfo[f->texinfo].miptex].reflectivity, patch->texturereflectivity);
        if (g_face_texlights[fn] && *ValueForKey(g_face_texlights[fn], "_texcolor"))
        {
            vec3_t texturecolor;
            vec3_t texturereflectivity;
            GetVectorForKey(g_face_texlights[fn], "_texcolor", texturecolor);
            for (int k = 0; k < 3; k++)
            {
                texturecolor[k] = std::floor(texturecolor[k] + 0.001);
            }
            if (VectorMinimum(texturecolor) < -0.001 || VectorMaximum(texturecolor) > 255.001)
            {
                vec3_t origin;
                GetVectorForKey(g_face_texlights[fn], "origin", origin);
                Error("light_surface entity at (%g,%g,%g): texture color (%g,%g,%g) must be numbers between 0 and 255.", origin[0], origin[1], origin[2], texturecolor[0], texturecolor[1], texturecolor[2]);
            }
            VectorScale(texturecolor, 1.0 / 255.0, texturereflectivity);
            for (int k = 0; k < 3; k++)
            {
                texturereflectivity[k] = std::pow(texturereflectivity[k], g_texreflectgamma);
            }
            VectorScale(texturereflectivity, g_texreflectscale, texturereflectivity);
            if (VectorMaximum(texturereflectivity) > 1.0 + NORMAL_EPSILON)
            {
                Warning("Texture '%s': reflectivity (%f,%f,%f) greater than 1.0.", g_textures[g_texinfo[f->texinfo].miptex].name, texturereflectivity[0], texturereflectivity[1], texturereflectivity[2]);
            }
            VectorCopy(texturereflectivity, patch->texturereflectivity);
        }
        {
            vec_t opacity = 0.0;
            if (g_face_entity[fn] - g_entities == 0)
            {
                opacity = 1.0;
            }
            else
            {
                int x;
                for (x = 0; x < (int)g_opaque_face_count; x++)
                {
                    opaqueList_t *op = &g_opaque_face_list[x];
                    if (op->entitynum == g_face_entity[fn] - g_entities)
                    {
                        opacity = 1.0;
                        if (op->transparency)
                        {
                            opacity = 1.0 - VectorAvg(op->transparency_scale);
                            opacity = opacity > 1.0 ? 1.0 : opacity < 0.0 ? 0.0
                                                                          : opacity;
                        }
                        if (op->style != -1)
                        { // toggleable opaque entity
                            if (bouncestyle == -1)
                            {                  // by default
                                opacity = 0.0; // doesn't reflect light
                            }
                        }
                        break;
                    }
                }
                if (x == (int)g_opaque_face_count)
                { // not opaque
                    if (bouncestyle != -1)
                    {                  // with light_bounce
                        opacity = 1.0; // reflects light
                    }
                }
            }
            VectorScale(patch->texturereflectivity, opacity, patch->bouncereflectivity);
        }
        patch->bouncestyle = bouncestyle;
        if (bouncestyle == 0)
        {                            // there is an unnamed light_bounce
            patch->bouncestyle = -1; // reflects light normally
        }
        patch->emitmode = getEmitMode(patch);
        patch->scale = getScale(patch);
        patch->chop = getChop(patch);
        VectorCopy(g_translucenttextures[g_texinfo[f->texinfo].miptex], patch->translucent_v);
        patch->translucent_b = !VectorCompare(patch->translucent_v, vec3_origin);
        PlacePatchInside(patch);
        UpdateEmitterInfo(patch);

        g_face_patches[fn] = patch;
        g_num_patches++;

        // Per-face data
        {
            // Centroid of face for nudging samples in direct lighting pass
            for (int j = 0; j < f->numedges; j++)
            {
                int edge = g_dsurfedges[f->firstedge + j];

                if (edge > 0)
                {
                    VectorAdd(g_dvertexes[g_dedges[edge].v[0]].point, centroid, centroid);
                    VectorAdd(g_dvertexes[g_dedges[edge].v[1]].point, centroid, centroid);
                }
                else
                {
                    VectorAdd(g_dvertexes[g_dedges[-edge].v[1]].point, centroid, centroid);
                    VectorAdd(g_dvertexes[g_dedges[-edge].v[0]].point, centroid, centroid);
                }
            }

            // Fixup centroid for anything with an altered origin (rotating models/turrets mostly)
            // Save them for moving direct lighting points towards the face center
            VectorScale(centroid, 1.0 / (f->numedges * 2), centroid);
            VectorAdd(centroid, g_face_offset[fn], g_face_centroids[fn]);
        }

        {
            vec3_t mins;
            vec3_t maxs;

            patch->winding->getBounds(mins, maxs);

            if (g_subdivide)
            {
                vec3_t delta;

                VectorSubtract(maxs, mins, delta);
                vec_t length = VectorLength(delta);
                vec_t amt = patch->chop;

                if (length > amt)
                {
                    if (patch->area >= 1.0) // not a tiny area, so subdive it
                    {
                        SubdividePatch(patch);
                    }
                }
            }
        }
        if (patch->winding == w.get()) // not subdivided
        {
            s_patch_windings.push_back(std::move(w));
        }
    }
}

static void AddFaceToOpaqueList(
    int entitynum, int modelnum, const vec3_t origin, const vec3_t &transparency_scale, const bool transparency, int style, bool block)
{
    {
        opaqueList_t *opaque = &s_opaque_face_list.emplace_back();
        g_opaque_face_list = s_opaque_face_list.data();
        g_opaque_face_count = s_opaque_face_list.size();

        if (transparency && style != -1)
        {
            Warning("Dynamic shadow is not allowed in entity with custom shadow.\n");
            style = -1;
        }
        VectorCopy(transparency_scale, opaque->transparency_scale);
        opaque->transparency = transparency;
        opaque->entitynum = entitynum;
        opaque->modelnum = modelnum;
        VectorCopy(origin, opaque->origin);
        opaque->style = style;
        opaque->block = block;
    }
}

static void FreeOpaqueFaceList()
{
    s_opaque_face_list = std::vector<opaqueList_t>();
    g_opaque_face_list = nullptr;
    g_opaque_face_count = 0;
}

static void LoadOpaqueEntities()
{
    for (int modelnum = 0; modelnum < g_nummodels; modelnum++)
    {
        char stringmodel[16];
        std::sprintf(stringmodel, "*%i", modelnum);
        for (int entnum = 0; entnum < g_numentities; entnum++)
        {
            entity_t *ent = &g_entities[entnum];
            if (std::strcmp(ValueForKey(ent, "model"), stringmodel))
                continue;
            vec3_t origin;
            {
                GetVectorForKey(ent, "origin", origin);
                if (*ValueForKey(ent, "light_origin") && *ValueForKey(ent, "model_center"))
                {
                    entity_t *ent2 = FindTargetEntity(ValueForKey(ent, "light_origin"));
                    if (ent2)
                    {
                        vec3_t light_origin;
                        vec3_t model_center;
                        GetVectorForKey(ent2, "origin", light_origin);
                        GetVectorForKey(ent, "model_center", model_center);
                        VectorSubtract(light_origin, model_center, origin);
                    }
                }
            }
            bool opaque = false;
            {
                if ((IntForKey(ent, "zhlt_lightflags") & eModelLightmodeOpaque))
                    opaque = true;
            }
            vec3_t d_transparency;
            VectorFill(d_transparency, 0.0);
            bool b_transparency = false;
            {
                const char *s;
                if (*(s = ValueForKey(ent, "zhlt_customshadow")))
                {
                    double r1 = 1.0;
                    double g1 = 1.0;
                    double b1 = 1.0;
                    double tmp = 1.0;
                    if (std::sscanf(s, "%lf %lf %lf", &r1, &g1, &b1) == 3) //RGB version
                    {
                        if (r1 < 0.0)
                            r1 = 0.0;
                        if (g1 < 0.0)
                            g1 = 0.0;
                        if (b1 < 0.0)
                            b1 = 0.0;
                        d_transparency[0] = r1;
                        d_transparency[1] = g1;
                        d_transparency[2] = b1;
                    }
                    else if (std::sscanf(s, "%lf", &tmp) == 1) //Greyscale version
                    {
                        if (tmp < 0.0)
                            tmp = 0.0;
                        VectorFill(d_transparency, tmp);
                    }
                }
                if (!VectorCompare(d_transparency, vec3_origin))
                    b_transparency = true;
            }
            int opaquestyle = -1;
            {
                for (int j = 0; j < g_numentities; j++)
                {
                    entity_t *lightent = &g_entities[j];
                    if (!std::strcmp(ValueForKey(lightent, "classname"), "light_shadow") && *ValueForKey(lightent, "target") && !std::strcmp(ValueForKey(lightent, "target"), ValueForKey(ent, "targetname")))
                    {
                        opaquestyle = IntForKey(lightent, "style");
                        if (opaquestyle < 0)
                            opaquestyle = -opaquestyle;
                        opaquestyle = (unsigned char)opaquestyle;
                        if (opaquestyle >= ALLSTYLES)
                        {
                            Error("invalid light style: style (%d) >= ALLSTYLES (%d)", opaquestyle, ALLSTYLES);
                        }
                        break;
                    }
                }
            }
            bool block = false;
            {
                if (g_blockopaque)
                {
                    block = true;
                    if (IntForKey(ent, "zhlt_lightflags") & eModelLightmodeNonsolid)
                        block = false;
                    if (b_transparency)
                        block = false;
                    if (opaquestyle != -1)
                        block = false;
                }
            }
            if (opaque)
            {
                AddFaceToOpaqueList(entnum, modelnum, origin, d_transparency, b_transparency, opaquestyle, block);
            }
        }
    }
    {
        Log("%i opaque models\n", g_opaque_face_count);
        int i;
        int facecount;
        for (facecount = 0, i = 0; i < (int)g_opaque_face_count; i++)
        {
            facecount += CountOpaqueFaces(g_opaque_face_list[i].modelnum);
        }
        Log("%i opaque faces\n", facecount);
    }
}

static entity_t *FindTexlightEntity(int facenum)
{
    dface_t *face = &g_dfaces[facenum];
    const dplane_t *dplane = getPlaneFromFace(face);
    const char *texname = GetTextureByNumber(face->texinfo);
    entity_t *faceent = g_face_entity[facenum];
    vec3_t centroid;
    Winding(*face).getCenter(centroid);
    VectorAdd(centroid, g_face_offset[facenum], centroid);

    entity_t *found = nullptr;
    vec_t bestdist = -1;
    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *ent = &g_entities[i];
        if (std::strcmp(ValueForKey(ent, "classname"), "light_surface"))
            continue;
        if (strcasecmp(ValueForKey(ent, "_tex"), texname))
            continue;
        vec3_t delta;
        GetVectorForKey(ent, "origin", delta);
        VectorSubtract(delta, centroid, delta);
        vec_t dist = VectorLength(delta);
        if (*ValueForKey(ent, "_frange"))
        {
            if (dist > FloatForKey(ent, "_frange"))
                continue;
        }
        if (*ValueForKey(ent, "_fdist"))
        {
            if (std::abs(DotProduct(delta, dplane->normal)) > FloatForKey(ent, "_fdist"))
                continue;
        }
        if (*ValueForKey(ent, "_fclass"))
        {
            if (std::strcmp(ValueForKey(faceent, "classname"), ValueForKey(ent, "_fclass")))
                continue;
        }
        if (*ValueForKey(ent, "_fname"))
        {
            if (std::strcmp(ValueForKey(faceent, "targetname"), ValueForKey(ent, "_fname")))
                continue;
        }
        if (bestdist >= 0 && dist > bestdist)
            continue;
        found = ent;
        bestdist = dist;
    }
    return found;
}

static void MakePatches()
{
    vec3_t origin;
    const char *s;
    vec3_t light_origin;
    vec3_t model_center;

    int style; //LRC

    Log("%i faces\n", g_numfaces);

    Log("Create Patches : ");
    s_patches.clear();
    s_patches.reserve(MAX_PATCHES); // patches are made through pointers to earlier ones: no reallocation
    g_patches = s_patches.data();

    for (int i = 0; i < g_nummodels; i++)
    {
        bool b_light_origin = false;
        bool b_model_center = false;
        eModelLightmodes lightmode = eModelLightmodeNull;

        dmodel_t *mod = g_dmodels + i;
        entity_t *ent = EntityForModel(i);
        VectorCopy(vec3_origin, origin);

        if (*(s = ValueForKey(ent, "zhlt_lightflags")))
        {
            lightmode = (eModelLightmodes)std::atoi(s);
        }

        // models with origin brushes need to be offset into their in-use position
        if (*(s = ValueForKey(ent, "origin")))
        {
            double v1, v2, v3;

            if (std::sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
            {
                origin[0] = v1;
                origin[1] = v2;
                origin[2] = v3;
            }
        }

        // Allow models to be lit in an alternate location (pt1)
        if (*(s = ValueForKey(ent, "light_origin")))
        {
            entity_t *e = FindTargetEntity(s);

            if (e)
            {
                if (*(s = ValueForKey(e, "origin")))
                {
                    double v1, v2, v3;

                    if (std::sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
                    {
                        light_origin[0] = v1;
                        light_origin[1] = v2;
                        light_origin[2] = v3;

                        b_light_origin = true;
                    }
                }
            }
        }

        // Allow models to be lit in an alternate location (pt2)
        if (*(s = ValueForKey(ent, "model_center")))
        {
            double v1, v2, v3;

            if (std::sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
            {
                model_center[0] = v1;
                model_center[1] = v2;
                model_center[2] = v3;

                b_model_center = true;
            }
        }

        // Allow models to be lit in an alternate location (pt3)
        if (b_light_origin && b_model_center)
        {
            VectorSubtract(light_origin, model_center, origin);
        }

        //LRC:
        if (*(s = ValueForKey(ent, "style")))
        {
            style = std::atoi(s);
            if (style < 0)
                style = -style;
        }
        else
        {
            style = 0;
        }
        //LRC (ends)
        style = (unsigned char)style;
        if (style >= ALLSTYLES)
        {
            Error("invalid light style: style (%d) >= ALLSTYLES (%d)", style, ALLSTYLES);
        }
        int bouncestyle = -1;
        {
            for (int j = 0; j < g_numentities; j++)
            {
                entity_t *lightent = &g_entities[j];
                if (!std::strcmp(ValueForKey(lightent, "classname"), "light_bounce") && *ValueForKey(lightent, "target") && !std::strcmp(ValueForKey(lightent, "target"), ValueForKey(ent, "targetname")))
                {
                    bouncestyle = IntForKey(lightent, "style");
                    if (bouncestyle < 0)
                        bouncestyle = -bouncestyle;
                    bouncestyle = (unsigned char)bouncestyle;
                    if (bouncestyle >= ALLSTYLES)
                    {
                        Error("invalid light style: style (%d) >= ALLSTYLES (%d)", bouncestyle, ALLSTYLES);
                    }
                    break;
                }
            }
        }

        for (int j = 0; j < mod->numfaces; j++)
        {
            int fn = mod->firstface + j;
            g_face_entity[fn] = ent;
            VectorCopy(origin, g_face_offset[fn]);
            g_face_texlights[fn] = FindTexlightEntity(fn);
            g_face_lightmode[fn] = lightmode;
            dface_t *f = g_dfaces + fn;
            auto w = std::make_unique<Winding>(*f);
            for (unsigned int k = 0; k < w->m_NumPoints; k++)
            {
                VectorAdd(w->m_Points[k], origin, w->m_Points[k]);
            }
            MakePatchForFace(fn, std::move(w), style, bouncestyle); //LRC
        }
    }

    Log("%i base patches\n", g_num_patches);
    Log("%i square feet [%.2f square inches]\n", (int)(totalarea / 144), totalarea);
}

static int patch_sorter(const void *p1, const void *p2)
{
    auto *patch1 = (patch_t *)p1;
    auto *patch2 = (patch_t *)p2;

    if (patch1->faceNumber < patch2->faceNumber)
    {
        return -1;
    }
    else if (patch1->faceNumber > patch2->faceNumber)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

// This sorts the patches by facenumber, which makes their runs compress even better
static void SortPatches()
{
    // SortPatches is the ideal place to do this, because the address of the patches are going to be invalidated.
    std::vector<patch_t> sorted(s_patches.begin(), s_patches.begin() + g_num_patches);
    sorted.emplace_back(); // one extra (zeroed) slot considering how terribly the code were written
    s_patches = std::move(sorted); // also gives back the MAX_PATCHES reservation
    g_patches = s_patches.data();
    qsort((void *)g_patches, (std::size_t)g_num_patches, sizeof(patch_t), patch_sorter);

    // Fixup g_face_patches & Fixup patch->next
    std::memset(g_face_patches, 0, sizeof(g_face_patches));
    {
        patch_t *patch = g_patches + 1;
        patch_t *prev = g_patches;

        g_face_patches[prev->faceNumber] = prev;

        for (unsigned x = 1; x < g_num_patches; x++, patch++)
        {
            if (patch->faceNumber != prev->faceNumber)
            {
                prev->next = nullptr;
                g_face_patches[patch->faceNumber] = patch;
            }
            else
            {
                prev->next = patch;
            }
            prev = patch;
        }
    }
    for (unsigned x = 0; x < g_num_patches; x++)
    {
        patch_t *patch = &g_patches[x];
        patch->leafnum = PointInLeaf(patch->origin) - g_dleafs;
    }
}

static void FreePatches()
{
    s_patch_windings = std::vector<std::unique_ptr<Winding>>();
    s_patches = std::vector<patch_t>();
    g_patches = nullptr;
}

static void CollectLight()
{
    unsigned i;
    patch_t *patch;

    for (i = 0, patch = g_patches; i < g_num_patches; i++, patch++)
    {
        vec3_t newtotallight[MAXLIGHTMAPS];
        for (unsigned j = 0; j < MAXLIGHTMAPS && newstyles[i][j] != 255; j++)
        {
            VectorClear(newtotallight[j]);
            for (int k = 0; k < MAXLIGHTMAPS && patch->totalstyle[k] != 255; k++)
            {
                if (patch->totalstyle[k] == newstyles[i][j])
                {
                    VectorCopy(patch->totallight[k], newtotallight[j]);
                    break;
                }
            }
        }
        for (unsigned j = 0; j < MAXLIGHTMAPS; j++)
        {
            if (newstyles[i][j] != 255)
            {
                patch->totalstyle[j] = newstyles[i][j];
                VectorCopy(newtotallight[j], patch->totallight[j]);
                VectorCopy(addlight[i][j], emitlight[i][j]);
            }
            else
            {
                patch->totalstyle[j] = 255;
            }
        }
    }
}

//      Get light from other g_patches
//      Run multi-threaded


static void GatherLight(int /*threadnum*/)
{
    float f = 0; // set by float_decompress for every transfer
    vec3_t adds[ALLSTYLES];
    unsigned int fastfind_index;

    while (true)
    {
        int j = GetThreadWork();
        if (j == -1)
        {
            break;
        }
        std::memset(adds, 0, ALLSTYLES * sizeof(vec3_t));
        fastfind_index = StyleSearchStart(j);

        patch_t *patch = &g_patches[j];

        const transfer_data_t *tData = g_patch_transfers[j].data.get();
        const transfer_index_t *tIndex = g_patch_transfers[j].index.get();
        unsigned iIndex = g_patch_transfers[j].iIndex;

        for (unsigned m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++)
        {
            VectorAdd(adds[patch->totalstyle[m]], patch->totallight[m], adds[patch->totalstyle[m]]);
        }

        for (unsigned k = 0; k < iIndex; k++, tIndex++)
        {
            unsigned size = (tIndex->size + 1);
            unsigned patchnum = tIndex->index;

            for (unsigned l = 0; l < size; l++, tData += float_size[g_transfer_compress_type], patchnum++)
            {
                vec3_t v;
                //LRC:
                patch_t *emitpatch = &g_patches[patchnum];
                int opaquestyle = -1;
                GetStyle(j, patchnum, opaquestyle, fastfind_index);
                float_decompress(g_transfer_compress_type, tData, &f);

                // for each style on the emitting patch
                for (unsigned emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->directstyle[emitstyle] != 255; emitstyle++)
                {
                    VectorScale(emitpatch->directlight[emitstyle], f, v);
                    VectorMultiply(v, emitpatch->bouncereflectivity, v);
                    if (isPointFinite(v))
                    {
                        int addstyle = emitpatch->directstyle[emitstyle];
                        if (emitpatch->bouncestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
                                addstyle = emitpatch->bouncestyle;
                            else
                                continue;
                        }
                        if (opaquestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == opaquestyle)
                                addstyle = opaquestyle;
                            else
                                continue;
                        }
                        VectorAdd(adds[addstyle], v, adds[addstyle]);
                    }
                }
                for (unsigned emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->totalstyle[emitstyle] != 255; emitstyle++)
                {
                    VectorScale(emitlight[patchnum][emitstyle], f, v);
                    VectorMultiply(v, emitpatch->bouncereflectivity, v);
                    if (isPointFinite(v))
                    {
                        int addstyle = emitpatch->totalstyle[emitstyle];
                        if (emitpatch->bouncestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
                                addstyle = emitpatch->bouncestyle;
                            else
                                continue;
                        }
                        if (opaquestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == opaquestyle)
                                addstyle = opaquestyle;
                            else
                                continue;
                        }
                        VectorAdd(adds[addstyle], v, adds[addstyle]);
                    }
                }
                //LRC (ends)
            }
        }

        vec_t maxlights[ALLSTYLES];
        for (int style = 0; style < ALLSTYLES; style++)
        {
            maxlights[style] = VectorMaximum(adds[style]);
        }
        for (unsigned m = 0; m < MAXLIGHTMAPS; m++)
        {
            unsigned char beststyle = 255;
            if (m == 0)
            {
                beststyle = 0;
            }
            else
            {
                vec_t bestmaxlight = 0;
                for (int style = 1; style < ALLSTYLES; style++)
                {
                    if (maxlights[style] > bestmaxlight + NORMAL_EPSILON)
                    {
                        bestmaxlight = maxlights[style];
                        beststyle = style;
                    }
                }
            }
            if (beststyle != 255)
            {
                maxlights[beststyle] = 0;
                newstyles[j][m] = beststyle;
                VectorCopy(adds[beststyle], addlight[j][m]);
            }
            else
            {
                newstyles[j][m] = 255;
            }
        }
        for (int style = 1; style < ALLSTYLES; style++)
        {
            NoteDiscardedLight(maxlights[style], patch->origin);
        }
    }
}

// RGB Transfer version
static void GatherRGBLight(int /*threadnum*/)
{
    float f[3] = {0, 0, 0}; // set by vector_decompress for every transfer
    vec3_t adds[ALLSTYLES];
    unsigned int fastfind_index;

    while (true)
    {
        int j = GetThreadWork();
        if (j == -1)
        {
            break;
        }
        std::memset(adds, 0, ALLSTYLES * sizeof(vec3_t));
        fastfind_index = StyleSearchStart(j);

        patch_t *patch = &g_patches[j];

        const rgb_transfer_data_t *tRGBData = g_patch_transfers[j].data.get();
        const transfer_index_t *tIndex = g_patch_transfers[j].index.get();
        unsigned iIndex = g_patch_transfers[j].iIndex;

        for (unsigned m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++)
        {
            VectorAdd(adds[patch->totalstyle[m]], patch->totallight[m], adds[patch->totalstyle[m]]);
        }

        for (unsigned k = 0; k < iIndex; k++, tIndex++)
        {
            unsigned size = (tIndex->size + 1);
            unsigned patchnum = tIndex->index;
            for (unsigned l = 0; l < size; l++, tRGBData += vector_size[g_rgbtransfer_compress_type], patchnum++)
            {
                vec3_t v;
                //LRC:
                patch_t *emitpatch = &g_patches[patchnum];
                int opaquestyle = -1;
                GetStyle(j, patchnum, opaquestyle, fastfind_index);
                vector_decompress(g_rgbtransfer_compress_type, tRGBData, &f[0], &f[1], &f[2]);

                // for each style on the emitting patch
                for (unsigned emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->directstyle[emitstyle] != 255; emitstyle++)
                {
                    VectorMultiply(emitpatch->directlight[emitstyle], f, v);
                    VectorMultiply(v, emitpatch->bouncereflectivity, v);
                    if (isPointFinite(v))
                    {
                        int addstyle = emitpatch->directstyle[emitstyle];
                        if (emitpatch->bouncestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
                                addstyle = emitpatch->bouncestyle;
                            else
                                continue;
                        }
                        if (opaquestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == opaquestyle)
                                addstyle = opaquestyle;
                            else
                                continue;
                        }
                        VectorAdd(adds[addstyle], v, adds[addstyle]);
                    }
                }
                for (unsigned emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->totalstyle[emitstyle] != 255; emitstyle++)
                {
                    VectorMultiply(emitlight[patchnum][emitstyle], f, v);
                    VectorMultiply(v, emitpatch->bouncereflectivity, v);
                    if (isPointFinite(v))
                    {
                        int addstyle = emitpatch->totalstyle[emitstyle];
                        if (emitpatch->bouncestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
                                addstyle = emitpatch->bouncestyle;
                            else
                                continue;
                        }
                        if (opaquestyle != -1)
                        {
                            if (addstyle == 0 || addstyle == opaquestyle)
                                addstyle = opaquestyle;
                            else
                                continue;
                        }
                        VectorAdd(adds[addstyle], v, adds[addstyle]);
                    }
                }
                //LRC (ends)
            }
        }

        vec_t maxlights[ALLSTYLES];
        for (int style = 0; style < ALLSTYLES; style++)
        {
            maxlights[style] = VectorMaximum(adds[style]);
        }
        for (unsigned m = 0; m < MAXLIGHTMAPS; m++)
        {
            unsigned char beststyle = 255;
            if (m == 0)
            {
                beststyle = 0;
            }
            else
            {
                vec_t bestmaxlight = 0;
                for (int style = 1; style < ALLSTYLES; style++)
                {
                    if (maxlights[style] > bestmaxlight + NORMAL_EPSILON)
                    {
                        bestmaxlight = maxlights[style];
                        beststyle = style;
                    }
                }
            }
            if (beststyle != 255)
            {
                maxlights[beststyle] = 0;
                newstyles[j][m] = beststyle;
                VectorCopy(adds[beststyle], addlight[j][m]);
            }
            else
            {
                newstyles[j][m] = 255;
            }
        }
        for (int style = 1; style < ALLSTYLES; style++)
        {
            NoteDiscardedLight(maxlights[style], patch->origin);
        }
    }
}

static void BounceLight()
{
    for (unsigned i = 0; i < g_num_patches; i++)
    {
        patch_t *patch = &g_patches[i];
        for (unsigned j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++)
        {
            VectorCopy(patch->totallight[j], emitlight[i][j]);
        }
    }

    for (unsigned i = 0; i < g_numbounce; i++)
    {
        Log("Bounce %u ", i + 1);
        if (g_rgb_transfers)
        {
            NamedRunThreadsOn(g_num_patches, g_estimate, GatherRGBLight);
        }
        else
        {
            NamedRunThreadsOn(g_num_patches, g_estimate, GatherLight);
        }
        CollectLight();
    }
    for (unsigned i = 0; i < g_num_patches; i++)
    {
        patch_t *patch = &g_patches[i];
        for (unsigned j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++)
        {
            VectorCopy(emitlight[i][j], patch->totallight[j]);
        }
    }
}

static void CheckMaxPatches()
{
    switch (g_method)
    {
    case eMethodVismatrix:
        hlassume(g_num_patches < MAX_VISMATRIX_PATCHES, assume_MAX_PATCHES); // should use "<=" instead. --vluzacn
        break;
    case eMethodSparseVismatrix:
        hlassume(g_num_patches < MAX_SPARSE_VISMATRIX_PATCHES, assume_MAX_PATCHES);
        break;
    case eMethodNoVismatrix:
        hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
        break;
    }
}

static void MakeScalesStub()
{
    g_patch_transfers = std::vector<patch_transfers_t>(g_num_patches); // each thread fills its patches' entries
    switch (g_method)
    {
    case eMethodVismatrix:
        MakeScalesVismatrix();
        break;
    case eMethodSparseVismatrix:
        MakeScalesSparseVismatrix();
        break;
    case eMethodNoVismatrix:
        MakeScalesNoVismatrix();
        break;
    }
}

static void FreeTransfers()
{
    g_patch_transfers = std::vector<patch_transfers_t>();
}

static void ExtendLightmapBuffer()
{
    int maxsize = 0;
    for (int i = 0; i < g_numfaces; i++)
    {
        dface_t *f = &g_dfaces[i];
        if (f->lightofs >= 0)
        {
            int ofs = f->lightofs;
            for (int j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++)
            {
                ofs += (MAX_SURFACE_EXTENT + 1) * (MAX_SURFACE_EXTENT + 1) * 3;
            }
            if (ofs > maxsize)
            {
                maxsize = ofs;
            }
        }
    }
    if (maxsize >= g_lightdatasize)
    {
        hlassume(maxsize <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);
        std::memset(&g_dlightdata[g_lightdatasize], 0, maxsize - g_lightdatasize);
        g_lightdatasize = maxsize;
    }
}

static void RadWorld()
{
    MakeBackplanes();
    MakeParents(0, -1);
    MakeTnodes(&g_dmodels[0]);
    CreateOpaqueNodes();
    LoadOpaqueEntities();

    // turn each face into a single patch
    MakePatches();
    CheckMaxPatches(); // Check here for exceeding max patches, to prevent a lot of work from occuring before an error occurs
    SortPatches();     // Makes the runs in the Transfer Compression really good
    PairEdges();

    BuildDiffuseNormals();
    // create directlights out of g_patches and lights
    CreateDirectLights();

    Log("\n");

    // generate a position map for each face
    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FindFacePositions);

    // build initial facelights
    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, BuildFacelights);

    FreePositionMaps();

    // free up the direct lights now that we have facelights
    DeleteDirectLights();

    if (g_numbounce > 0)
    {
        // build transfer lists
        MakeScalesStub();

        // these arrays are only used in CollectLight, GatherLight and BounceLight
        emitlight = std::make_unique<vec3_t[][MAXLIGHTMAPS]>(g_num_patches + 1);
        addlight = std::make_unique<vec3_t[][MAXLIGHTMAPS]>(g_num_patches + 1);
        newstyles = std::make_unique<unsigned char[][MAXLIGHTMAPS]>(g_num_patches + 1);
        // spread light around
        BounceLight();

        emitlight.reset();
        addlight.reset();
        newstyles.reset();
    }

    FreeTransfers();
    FreeStyleArrays();

    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, CreateTriangulations);

    // blend bounced light into direct light and save
    PrecompLightmapOffsets();

    ScaleDirectLights();

    {

        CreateFacelightDependencyList();

        NamedRunThreadsOnIndividual(g_numfaces, g_estimate, AddPatchLights);

        FreeFacelightDependencyList();
    }

    FreeTriangulations();

    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FinalLightFace);
    // s_maxdiscardedlight > 0.01 Maximum brightness loss (too many light styles on a face)

    MdlLightHack();
    ReduceLightmap();
    if (g_lightdatasize == 0)
    {
        g_lightdatasize = 1;
        g_dlightdata[0] = 0;
    }
    ExtendLightmapBuffer(); // expand the size of lightdata array (for a few KB) to ensure that game engine reads within its valid range
}

[[noreturn]] static void Usage()
{
    Banner();

    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -waddir folder  : Search this folder for wad files.\n");
    Log("    -fast           : Fast rad\n");
    Log("    -vismatrix value: Set vismatrix method to normal, sparse or off .\n");
    Log("    -extra          : Improve lighting quality by doing 9 point oversampling\n");
    Log("    -bounce #       : Set number of radiosity bounces\n");
    Log("    -ambient r g b  : Set ambient world light (0.0 to 1.0, r g b)\n");
    Log("    -limiter #      : Set light clipping threshold (-1=None)\n");
    Log("    -circus         : Enable 'circus' mode for locating unlit lightmaps\n");
    Log("    -nospread       : Disable sunlight spread angles for this compile\n");
    Log("    -smooth #       : Set smoothing threshold for blending (in degrees)\n");
    Log("    -smooth2 #      : Set smoothing threshold between different textures\n");
    Log("    -chop #         : Set radiosity patch size for normal textures\n");
    Log("    -texchop #      : Set radiosity patch size for texture light faces\n\n");
    Log("    -notexscale     : Do not scale radiosity patches with texture scale\n");
    Log("    -coring #       : Set lighting threshold before blackness\n");
    Log("    -dlight #       : Set direct lighting threshold\n");
    Log("    -fade #         : Set global fade (larger values = shorter lights)\n");
    Log("    -texlightgap #  : Set global gap distance for texlights\n");
    Log("    -scale #        : Set global light scaling value\n");
    Log("    -gamma #        : Set global gamma value\n\n");
    Log("    -sky #          : Set ambient sunlight contribution in the shade outside\n");
    Log("    -noskyfix       : Disable light_environment being global\n");
    Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
    Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n"); //lightdata
    Log("    -chart          : display bsp statitics\n");
    Log("    -low | -high    : run program an altered priority level\n");
    Log("    -threads #      : manually specify the number of threads to run\n");
    Log("    -estimate       : display estimated time during compile\n");
    Log("   -colourgamma r g b  : Sets different gamma values for r, g, b\n");
    Log("   -colourscale r g b  : Sets different lightscale values for r, g ,b\n");
    Log("   -customshadowwithbounce : Enables custom shadows with bounce light\n");
    Log("   -rgbtransfers           : Enables RGB Transfers (for custom shadows)\n\n");
    Log("   -minlight #    : Minimum final light (integer from 0 to 255)\n");
    {
        Log("   -compress #    : compress tranfer (");
        for (int i = 0; i < float_type_count; i++)
            Log(" %d=%s", i, float_type_string[i]);
        Log(" )\n");
        Log("   -rgbcompress # : compress rgbtranfer (");
        for (int i = 0; i < vector_type_count; i++)
            Log(" %d=%s", i, vector_type_string[i]);
        Log(" )\n");
    }
    Log("   -softsky #     : Smooth skylight.(0=off 1=on)\n");
    Log("   -depth #       : Thickness of translucent objects.\n");
    Log("   -blockopaque # : Remove the black areas around opaque entities.(0=off 1=on)\n");
    Log("   -notextures    : Don't load textures.\n");
    Log("   -texreflectgamma # : Gamma that relates reflectivity to texture color bits.\n");
    Log("   -texreflectscale # : Reflectivity for 255-white texture.\n");
    Log("   -blur #        : Enlarge lightmap sample to blur the lightmap.\n");
    Log("    mapfile       : The mapfile to compile\n\n");

    std::exit(1);
}

static void Settings()
{
    char buf1[1024];
    char buf2[1024];

    Log("\n-= Current %s Settings =-\n", g_Program);
    Log("Name                | Setting             | Default\n"
        "--------------------|---------------------|-------------------------\n");

    // ZHLT Common Settings
    if (DEFAULT_NUMTHREADS == -1)
    {
        Log("threads              [ %17d ] [            Varies ]\n", g_numthreads);
    }
    else
    {
        Log("threads              [ %17d ] [ %17d ]\n", g_numthreads, DEFAULT_NUMTHREADS);
    }
    Log("chart                [ %17s ] [ %17s ]\n", g_chart ? "on" : "off", DEFAULT_CHART ? "on" : "off");
    Log("estimate             [ %17s ] [ %17s ]\n", g_estimate ? "on" : "off", DEFAULT_ESTIMATE ? "on" : "off");
    Log("max texture memory   [ %17d ] [ %17d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);
    Log("max lighting memory  [ %17d ] [ %17d ]\n", g_max_map_lightdata, DEFAULT_MAX_MAP_LIGHTDATA); //lightdata

    Log("fast rad             [ %17s ] [ %17s ]\n", g_fastmode ? "on" : "off", DEFAULT_FASTMODE ? "on" : "off");
    Log("vismatrix algorithm  [ %17s ] [ %17s ]\n",
        g_method == eMethodVismatrix ? "Original" : g_method == eMethodSparseVismatrix ? "Sparse"
                                                : g_method == eMethodNoVismatrix       ? "NoMatrix"
                                                                                       : "Unknown",
        DEFAULT_METHOD == eMethodVismatrix ? "Original" : DEFAULT_METHOD == eMethodSparseVismatrix ? "Sparse"
                                                      : DEFAULT_METHOD == eMethodNoVismatrix       ? "NoMatrix"
                                                                                                   : "Unknown");
    Log("oversampling (-extra)[ %17s ] [ %17s ]\n", g_extra ? "on" : "off", DEFAULT_EXTRA ? "on" : "off");
    Log("bounces              [ %17d ] [ %17d ]\n", g_numbounce, DEFAULT_BOUNCE);

    safe_snprintf(buf1, sizeof(buf1), "%1.3f %1.3f %1.3f", g_ambient[0], g_ambient[1], g_ambient[2]);
    safe_snprintf(buf2, sizeof(buf2), "%1.3f %1.3f %1.3f", DEFAULT_AMBIENT_RED, DEFAULT_AMBIENT_GREEN, DEFAULT_AMBIENT_BLUE);
    Log("ambient light        [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_limitthreshold);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIMITTHRESHOLD);
    Log("light limit threshold[ %17s ] [ %17s ]\n", g_limitthreshold >= 0 ? buf1 : "None", buf2);
    Log("circus mode          [ %17s ] [ %17s ]\n", g_circus ? "on" : "off", DEFAULT_CIRCUS ? "on" : "off");

    Log("\n");

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_smoothing_value);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_SMOOTHING_VALUE);
    Log("smoothing threshold  [ %17s ] [ %17s ]\n", buf1, buf2);
    const auto smoothing2 = [](char *buf, std::size_t size, float value)
    {
        if (value < 0)
            safe_snprintf(buf, size, "no change");
        else
            safe_snprintf(buf, size, "%3.3f", value);
    };
    smoothing2(buf1, sizeof(buf1), g_smoothing_value_2);
    smoothing2(buf2, sizeof(buf2), DEFAULT_SMOOTHING2_VALUE);
    Log("smoothing threshold 2[ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_dlight_threshold);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_DLIGHT_THRESHOLD);
    Log("direct threshold     [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_direct_scale);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_DLIGHT_SCALE);
    Log("direct light scale   [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_coring);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_CORING);
    Log("coring threshold     [ %17s ] [ %17s ]\n", buf1, buf2);

    Log("\n");

    Log("texscale             [ %17s ] [ %17s ]\n", g_texscale ? "on" : "off", DEFAULT_TEXSCALE ? "on" : "off");
    Log("patch subdividing    [ %17s ] [ %17s ]\n", g_subdivide ? "on" : "off", DEFAULT_SUBDIVIDE ? "on" : "off");
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_chop);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_CHOP);
    Log("chop value           [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texchop);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXCHOP);
    Log("texchop value        [ %17s ] [ %17s ]\n", buf1, buf2);
    Log("\n");

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_fade);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_FADE);
    Log("global fade          [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texlightgap);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXLIGHTGAP);
    Log("global texlight gap  [ %17s ] [ %17s ]\n", buf1, buf2);

    safe_snprintf(buf1, sizeof(buf1), "%1.3f %1.3f %1.3f", g_colour_lightscale[0], g_colour_lightscale[1], g_colour_lightscale[2]);
    safe_snprintf(buf2, sizeof(buf2), "%1.3f %1.3f %1.3f", DEFAULT_COLOUR_LIGHTSCALE_RED, DEFAULT_COLOUR_LIGHTSCALE_GREEN, DEFAULT_COLOUR_LIGHTSCALE_BLUE);
    Log("global light scale   [ %17s ] [ %17s ]\n", buf1, buf2);

    safe_snprintf(buf1, sizeof(buf1), "%1.3f %1.3f %1.3f", g_colour_qgamma[0], g_colour_qgamma[1], g_colour_qgamma[2]);
    safe_snprintf(buf2, sizeof(buf2), "%1.3f %1.3f %1.3f", DEFAULT_COLOUR_GAMMA_RED, DEFAULT_COLOUR_GAMMA_GREEN, DEFAULT_COLOUR_GAMMA_BLUE);
    Log("global gamma         [ %17s ] [ %17s ]\n", buf1, buf2);

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_lightscale);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIGHTSCALE);
    Log("global light scale   [ %17s ] [ %17s ]\n", buf1, buf2);

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_indirect_sun);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_INDIRECT_SUN);
    Log("global sky diffusion [ %17s ] [ %17s ]\n", buf1, buf2);

    Log("\n");
    Log("spread angles        [ %17s ] [ %17s ]\n", g_allow_spread ? "on" : "off", DEFAULT_ALLOW_SPREAD ? "on" : "off");
    Log("sky lighting fix     [ %17s ] [ %17s ]\n", g_sky_lighting_fix ? "on" : "off", DEFAULT_SKY_LIGHTING_FIX ? "on" : "off");

    Log("\n");
    Log("custom shadows with bounce light\n"
        "                     [ %17s ] [ %17s ]\n",
        g_customshadow_with_bouncelight ? "on" : "off", DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT ? "on" : "off");
    Log("rgb transfers        [ %17s ] [ %17s ]\n", g_rgb_transfers ? "on" : "off", DEFAULT_RGB_TRANSFERS ? "on" : "off");

    Log("minimum final light  [ %17d ] [ %17d ]\n", (int)g_minlight, (int)DEFAULT_MINLIGHT);
    std::sprintf(buf1, "%d (%s)", g_transfer_compress_type, float_type_string[g_transfer_compress_type]);
    std::sprintf(buf2, "%d (%s)", DEFAULT_TRANSFER_COMPRESS_TYPE, float_type_string[DEFAULT_TRANSFER_COMPRESS_TYPE]);
    Log("size of transfer     [ %17s ] [ %17s ]\n", buf1, buf2);
    std::sprintf(buf1, "%d (%s)", g_rgbtransfer_compress_type, vector_type_string[g_rgbtransfer_compress_type]);
    std::sprintf(buf2, "%d (%s)", DEFAULT_RGBTRANSFER_COMPRESS_TYPE, vector_type_string[DEFAULT_RGBTRANSFER_COMPRESS_TYPE]);
    Log("size of rgbtransfer  [ %17s ] [ %17s ]\n", buf1, buf2);
    Log("soft sky             [ %17s ] [ %17s ]\n", g_softsky ? "on" : "off", DEFAULT_SOFTSKY ? "on" : "off");
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_translucentdepth);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TRANSLUCENTDEPTH);
    Log("translucent depth    [ %17s ] [ %17s ]\n", buf1, buf2);
    Log("block opaque         [ %17s ] [ %17s ]\n", g_blockopaque ? "on" : "off", DEFAULT_BLOCKOPAQUE ? "on" : "off");
    Log("ignore textures      [ %17s ] [ %17s ]\n", g_notextures ? "on" : "off", DEFAULT_NOTEXTURES ? "on" : "off");
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texreflectgamma);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXREFLECTGAMMA);
    Log("reflectivity gamma   [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texreflectscale);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXREFLECTSCALE);
    Log("reflectivity scale   [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_blur);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_BLUR);
    Log("blur size            [ %17s ] [ %17s ]\n", buf1, buf2);

    Log("\n\n");
}

// try and parse texlight info from the info_texlights entity
static void ReadInfoTexlights()
{
    float r, g, b, i;
    texlight_t texlight;

    for (int k = 0; k < g_numentities; k++)
    {
        entity_t *mapent = &g_entities[k];

        if (std::strcmp(ValueForKey(mapent, "classname"), "info_texlights"))
            continue;

        Log("Reading texlights from info_texlights map entity\n");

        for (const EntityKeyValue &ep : mapent->keys)
        {
            if (!std::strcmp(ep.key.c_str(), "classname") || !std::strcmp(ep.key.c_str(), "origin"))
                continue; // we dont care about these keyvalues

            int values = std::sscanf(ep.value.c_str(), "%f %f %f %f", &r, &g, &b, &i);

            if (values == 1)
            {
                g = b = r;
            }
            else if (values == 4) // use brightness value.
            {
                r *= i / 255.0;
                g *= i / 255.0;
                b *= i / 255.0;
            }
            else if (values != 3)
            {
                Warning("ignoring bad texlight '%s' in info_texlights entity", ep.key.c_str());
                continue;
            }

            texlight.name = ep.key;
            texlight.value[0] = r;
            texlight.value[1] = g;
            texlight.value[2] = b;
            texlight.filename = "info_texlights";
            s_texlights.push_back(texlight);
        }
    }
}

int main(const int argc, char **argv)
{
    const char *mapname_from_arg = nullptr;

    g_Program = "hlrad";
    SetAssumeHowto(assume_NoMemory, sizeof(intptr_t) <= 4 ? "The map is too complex for the compile tools to handle. Switch to the 64-bit version of hlrad if possible." : "Likely causes are (in order of likeliness) : the partition holding the swapfile is full; swapfile size is smaller than required; memory fragmentation; heap corruption");

    if (argc == 1)
        Usage();

    // stops with an Error() when fewer than three values follow
    const auto colour = [](const char *name, vec3_t &target, float scale) {
        return CommandLineOption{name, nullptr, false, nullptr, 3,
                                 [&target, scale](char *const *values) {
                                     for (int k = 0; k < 3; k++)
                                     {
                                         target[k] = (float)std::atof(values[k]) * scale;
                                     }
                                 },
                                 [name]() { Error("expected three color values after '%s'\n", name); }};
    };
    const auto number = [](const char *name, auto &target) {
        return CommandLineOption{name, [&target](const char *value) { target = std::atof(value); }, true};
    };
    const CommandLineOption options[] = {
        {"-bounce", [](const char *value) {
             g_numbounce = std::atoi(value);
             if (g_numbounce > 1000)
             {
                 Log("Unexpectedly large value (>1000) for '-bounce'\n");
                 Usage();
             }
         },
         true},
        {"-threads", [](const char *value) {
             g_numthreads = std::atoi(value);
             if (g_numthreads < 1)
             {
                 Log("Expected value of at least 1 for '-threads'\n");
                 Usage();
             }
         },
         true},
        CommandLineFlag("-estimate", g_estimate, true),
        CommandLineFlag("-fast", g_fastmode, true),
        {"-chop", [](const char *value) {
             g_chop = std::atof(value);
             if (g_chop < 1)
             {
                 Log("expected value greater than 1 for '-chop'\n");
                 Usage();
             }
             if (g_chop < 32)
             {
                 Log("Warning: Chop values below 32 are not recommended."); // compat: no newline
             }
         },
         true},
        {"-texchop", [](const char *value) {
             g_texchop = std::atof(value);
             if (g_texchop < 1)
             {
                 Log("expected value greater than 1 for '-texchop'\n");
                 Usage();
             }
             if (g_texchop < 32)
             {
                 Log("Warning: texchop values below 16 are not recommended."); // compat: no newline, and 16 for 32
             }
         },
         true},
        CommandLineFlag("-notexscale", g_texscale, false),
        CommandLineFlag("-nosubdivide", g_subdivide, false),
        {"-scale", [](const char *value) {
             // Changes by Adam Foster - afoster@compsoc.man.ac.uk: munge monochrome lightscale into colour one
             VectorFill(g_colour_lightscale, (float)std::atof(value));
         },
         true},
        {"-fade", [](const char *value) {
             g_fade = (float)std::atof(value);
             if (g_fade < 0.0)
             {
                 Log("-fade must be a positive number\n");
                 Usage();
             }
         },
         true},
        colour("-ambient", g_ambient, 128),
        number("-limiter", g_limitthreshold),
        CommandLineFlag("-circus", g_circus, true),
        CommandLineFlag("-noskyfix", g_sky_lighting_fix, false),
        CommandLineFlag("-chart", g_chart, true),
        {"-gamma", [](const char *value) {
             // Changes by Adam Foster - afoster@compsoc.man.ac.uk: munge monochrome gamma into colour gamma
             VectorFill(g_colour_qgamma, (float)std::atof(value));
         },
         true},
        number("-dlight", g_dlight_threshold),
        CommandLineFlag("-extra", g_extra, true),
        number("-sky", g_indirect_sun),
        number("-smooth", g_smoothing_value),
        number("-smooth2", g_smoothing_value_2),
        number("-coring", g_coring),
        {"-texdata", [](const char *value) { g_max_map_miptex = KilobytesToBytes(value); }, true},
        {"-lightdata", [](const char *value) { g_max_map_lightdata = KilobytesToBytes(value); }, true},
        {"-vismatrix", [](const char *value) {
             if (!strcasecmp(value, "normal"))
             {
                 g_method = eMethodVismatrix;
             }
             else if (!strcasecmp(value, "sparse"))
             {
                 g_method = eMethodSparseVismatrix;
             }
             else if (!strcasecmp(value, "off"))
             {
                 g_method = eMethodNoVismatrix;
             }
             else
             {
                 Error("Unknown vismatrix type: '%s'", value);
             }
         },
         true},
        CommandLineFlag("-nospread", g_allow_spread, false),
        number("-dscale", g_direct_scale),
        // Changes by Adam Foster - afoster@compsoc.man.ac.uk
        colour("-colourgamma", g_colour_qgamma, 1),
        colour("-colourscale", g_colour_lightscale, 1),
        CommandLineFlag("-customshadowwithbounce", g_customshadow_with_bouncelight, true),
        CommandLineFlag("-rgbtransfers", g_rgb_transfers, true),
        {"-minlight", [](const char *value) { g_minlight = (unsigned char)qmax(0, qmin(std::atoi(value), 255)); }, true},
        {"-softsky", [](const char *value) { g_softsky = (bool)std::atoi(value); }, true},
        {"-compress", [](const char *value) {
             const int type = std::atoi(value); // checked before the cast: an enum cannot hold any int
             if (type < 0 || type >= float_type_count)
                 Usage();
             g_transfer_compress_type = (float_type)type;
         },
         true},
        {"-rgbcompress", [](const char *value) {
             const int type = std::atoi(value);
             if (type < 0 || type >= vector_type_count)
                 Usage();
             g_rgbtransfer_compress_type = (vector_type)type;
         },
         true},
        number("-depth", g_translucentdepth),
        {"-blockopaque", [](const char *value) { g_blockopaque = std::atoi(value); }, true},
        {"-waddir", [](const char *value) { AddWadFolder(value); }, true},
        CommandLineFlag("-notextures", g_notextures, true),
        number("-texreflectgamma", g_texreflectgamma),
        number("-texreflectscale", g_texreflectscale),
        number("-blur", g_blur),
        number("-texlightgap", g_texlightgap),
    };
    mapname_from_arg = ParseCommandLine(argc, argv, options, Usage);

    if (!mapname_from_arg)
    {
        Log("No mapname specified\n");
        Usage();
    }

    g_smoothing_threshold = (float)std::cos(g_smoothing_value * (Q_PI / 180.0));

    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
    FlipSlashes(g_Mapname);
    StripExtension(g_Mapname);
    OpenLog();
    std::atexit(CloseLog);
    ThreadSetDefault();
    ThreadSetOrderedLog(true); // the log of any thread count is the -threads 1 log
    LogStart(argc, argv);
    {
        Log("Arguments: ");
        for (int i = 1; i < argc; i++)
        {
            if (strchr(argv[i], ' '))
            {
                Log("\"%s\" ", argv[i]);
            }
            else
            {
                Log("%s ", argv[i]);
            }
        }
        Log("\n");
    }

    CheckForErrorLog();

    hlassume(CalcFaceExtents_test(), assume_first);

    dtexdata_init();
    std::atexit(dtexdata_free);
    // END INIT

    // BEGIN RAD
    double start = I_FloatTime();

    // normalise maxlight

    safe_snprintf(g_source, _MAX_PATH, "%s.bsp", g_Mapname);
    LoadBSPFile(g_source);
    ParseEntities(GetParamsFromEnt);
    dtexdata_grow(); // info_compile_parameters may have raised g_max_map_lightdata
    if (g_fastmode)
    {
        g_numbounce = 0;
        g_softsky = false;
    }
    Settings();
    DeleteEmbeddedLightmaps();
    LoadTextures();

    ReadInfoTexlights();

    ReadCustomChopValue();
    ReadCustomSmoothValue();
    ReadTranslucentTextures();
    ReadLightingCone();
    g_smoothing_threshold_2 = g_smoothing_value_2 < 0 ? g_smoothing_threshold : (float)std::cos(g_smoothing_value_2 * (Q_PI / 180.0));
    {
        for (int style = 0; style < ALLSTYLES; ++style)
        {
            g_corings[style] = style ? g_coring : 0;
        }
    }
    if (g_direct_scale != 1.0)
    {
        Warning("dscale value should be 1.0 for final compile.\nIf you need to adjust the bounced light, use the '-texreflectscale' and '-texreflectgamma' options instead.");
    }
    if (g_colour_lightscale[0] != 2.0 || g_colour_lightscale[1] != 2.0 || g_colour_lightscale[2] != 2.0)
    {
        Warning("light scale value should be 2.0 for final compile.\nValues other than 2.0 will result in incorrect interpretation of light_environment's brightness when the engine loads the map.");
    }

    if (!g_visdatasize)
    {
        Warning("No vis information.");
    }
    if (g_blur < 1.0)
    {
        g_blur = 1.0;
    }

    RadWorld();

    FreeOpaqueFaceList();
    FreePatches();
    DeleteOpaqueNodes();

    EmbedLightmapInTextures();
    if (g_chart)
        PrintBSPFileSizes();

    WriteBSPFile(g_source);

    double end = I_FloatTime();
    LogTimeElapsed(end - start);
    // END RAD

    return 0;
}