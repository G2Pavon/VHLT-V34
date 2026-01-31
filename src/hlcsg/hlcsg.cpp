//#pragma warning(disable: 4018) // '<' : signed/unsigned mismatch

/*
 
    CONSTRUCTIVE SOLID GEOMETRY    -aka-    C S G 

    Code based on original code from Valve Software, 
    Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
    Modified by Tony "Merl" Moore (merlinis@bigpond.net.au) [AJM]
    
*/
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h> //--vluzacn

#include "hlcsg.h"
#include "wadpath.h"
#include "common/cmdlib.h"
#include "common/cmdlinecfg.h"
#include "common/filelib.h"
#include "common/blockmem.h"
#include "common/threads.h"
#include "common/scriplib.h"
#include "common/bspfile.h"

/*

 NOTES

 - check map size for +/- 4k limit at load time
 - allow for multiple wad.cfg configurations per compile

*/

static constexpr bool DEFAULT_ONLYENTS = false;
static constexpr bool DEFAULT_WADTEXTURES = true;
static constexpr bool DEFAULT_SKYCLIP = true;
static constexpr bool DEFAULT_CHART = false;
static constexpr bool DEFAULT_CLIPNAZI = false;
static constexpr bool DEFAULT_WADAUTODETECT = false;
static constexpr bool DEFAULT_NOLIGHTOPT = false;
static constexpr bool DEFAULT_NOUTF8 = false;
static constexpr bool DEFAULT_ESTIMATE = false;
static constexpr cliptype DEFAULT_CLIPTYPE = clip_simple; //clip_legacy //--vluzacn

static std::FILE *out[NUM_HULLS]; // pointer to each of the hull out files (.p0, .p1, ect.)
static std::FILE *out_view[NUM_HULLS];
static std::FILE *out_detailbrush[NUM_HULLS];
static int c_outfaces;
static int c_csgfaces;
static BoundingBox world_bounds;

static bool g_chart = DEFAULT_CHART;        // show chart "-chart"
static bool g_estimate = DEFAULT_ESTIMATE;  // progress estimates "-estimate"
static bool g_bClipNazi = DEFAULT_CLIPNAZI; // "-noclipeconomy"
static bool g_noutf8 = DEFAULT_NOUTF8;

bool g_onlyents = DEFAULT_ONLYENTS;            // onlyents mode "-onlyents"
bool g_wadtextures = DEFAULT_WADTEXTURES;      // "-nowadtextures"
bool g_skyclip = DEFAULT_SKYCLIP;              // no sky clipping "-noskyclip"
cliptype g_cliptype = DEFAULT_CLIPTYPE;        // "-cliptype <value>"
bool g_bWadAutoDetect = DEFAULT_WADAUTODETECT; // "-wadautodetect"
bool g_nolightopt = DEFAULT_NOLIGHTOPT;

// =====================================================================================
//  GetParamsFromEnt
//      parses entity keyvalues for setting information
// =====================================================================================
void GetParamsFromEnt(entity_t *mapent)
{
    char szTmp[256];

    Log("\nCompile Settings detected from info_compile_parameters entity\n");

    // verbose(choices) : "Verbose compile messages" : 0 = [ 0 : "Off" 1 : "On" ]
    int iTmp = IntForKey(mapent, "verbose");
    if (iTmp == 1)
    {
        g_verbose = true;
    }
    else if (iTmp == 0)
    {
        g_verbose = false;
    }
    Log("%30s [ %-9s ]\n", "Compile Option", "setting");
    Log("%30s [ %-9s ]\n", "Verbose Compile Messages", g_verbose ? "on" : "off");

    // estimate(choices) :"Estimate Compile Times?" : 0 = [ 0: "Yes" 1: "No" ]
    if (IntForKey(mapent, "estimate"))
    {
        g_estimate = true;
    }
    else
    {
        g_estimate = false;
    }
    Log("%30s [ %-9s ]\n", "Estimate Compile Times", g_estimate ? "on" : "off");

    // priority(choices) : "Priority Level" : 0 = [	0 : "Normal" 1 : "High"	-1 : "Low" ]
    if (!std::strcmp(ValueForKey(mapent, "priority"), "1"))
    {
        g_threadpriority = eThreadPriorityHigh;
        Log("%30s [ %-9s ]\n", "Thread Priority", "high");
    }
    else if (!std::strcmp(ValueForKey(mapent, "priority"), "-1"))
    {
        g_threadpriority = eThreadPriorityLow;
        Log("%30s [ %-9s ]\n", "Thread Priority", "low");
    }

    // texdata(string) : "Texture Data Memory" : "4096"
    iTmp = IntForKey(mapent, "texdata") * 1024;
    if (iTmp > g_max_map_miptex)
    {
        g_max_map_miptex = iTmp;
    }
    sprintf_s(szTmp, "%i", g_max_map_miptex);
    Log("%30s [ %-9s ]\n", "Texture Data Memory", szTmp);

    // wadautodetect(choices) : "Wad Auto Detect" : 0 =	[ 0 : "Off" 1 : "On" ]
    if (!std::strcmp(ValueForKey(mapent, "wadautodetect"), "1"))
    {
        g_bWadAutoDetect = true;
    }
    else
    {
        g_bWadAutoDetect = false;
    }
    Log("%30s [ %-9s ]\n", "Wad Auto Detect", g_bWadAutoDetect ? "on" : "off");

    // noclipeconomy(choices) : "Strip Uneeded Clipnodes?" : 1 = [ 1 : "Yes" 0 : "No" ]
    iTmp = IntForKey(mapent, "noclipeconomy");
    if (iTmp == 1)
    {
        g_bClipNazi = true;
    }
    else if (iTmp == 0)
    {
        g_bClipNazi = false;
    }
    Log("%30s [ %-9s ]\n", "Clipnode Economy Mode", g_bClipNazi ? "on" : "off");

    /*
    hlcsg(choices) : "HLCSG" : 1 =
    [
        1 : "Normal"
        2 : "Onlyents"
        0 : "Off"
    ]
    */
    iTmp = IntForKey(mapent, "hlcsg");
    g_onlyents = false;
    if (iTmp == 2)
    {
        g_onlyents = true;
    }
    else if (iTmp == 0)
    {
        Fatal(assume_TOOL_CANCEL,
              "%s was set to \"Off\" (0) in info_compile_parameters entity, execution cancelled", g_Program);
        CheckFatal();
    }
    Log("%30s [ %-9s ]\n", "Onlyents", g_onlyents ? "on" : "off");

    /*
    nocliphull(choices) : "Generate clipping hulls" : 0 =
    [
        0 : "Yes"
        1 : "No"
    ]
    */
    // cliptype(choices) : "Clip Hull Type" : 4 = [ 0 : "Smallest" 1 : "Normalized" 2: "Simple" 3 : "Precise" 4 : "Legacy" ]
    iTmp = IntForKey(mapent, "cliptype");
    switch (iTmp)
    {
    case 0:
        g_cliptype = clip_smallest;
        break;
    case 1:
        g_cliptype = clip_normalized;
        break;
    case 2:
        g_cliptype = clip_simple;
        break;
    case 3:
        g_cliptype = clip_precise;
        break;
    default:
        g_cliptype = clip_legacy;
        break;
    }
    Log("%30s [ %-9s ]\n", "Clip Hull Type", GetClipTypeString(g_cliptype));
    /*
    noskyclip(choices) : "No Sky Clip" : 0 =
    [
        1 : "On"
        0 : "Off"
    ]
    */
    iTmp = IntForKey(mapent, "noskyclip");
    if (iTmp == 1)
    {
        g_skyclip = false;
    }
    else
    {
        g_skyclip = true;
    }
    Log("%30s [ %-9s ]\n", "Sky brush clip generation", g_skyclip ? "on" : "off");

    ///////////////
    Log("\n");
}

// =====================================================================================
//  NewFaceFromFace
//      Duplicates the non point information of a face, used by SplitFace
// =====================================================================================
static bface_t *NewFaceFromFace(const bface_t *const in)
{
    bface_t *newf = (bface_t *)std::calloc(1, sizeof(bface_t));

    newf->contents = in->contents;
    newf->texinfo = in->texinfo;
    newf->planenum = in->planenum;
    newf->plane = in->plane;
    newf->backcontents = in->backcontents;

    return newf;
}

// =====================================================================================
//  FreeFace
// =====================================================================================
static void FreeFace(bface_t *f)
{
    delete f->w;
    std::free(f);
}

// =====================================================================================
//  WriteFace
// =====================================================================================
static void WriteFace(const int hull, const bface_t *const f, int detaillevel)
{

    ThreadLock();
    if (!hull)
        c_csgfaces++;

    // .p0 format
    Winding *w = f->w;

    // plane summary
    std::fprintf(out[hull], "%i %i %i %i %u\n", detaillevel, f->planenum, f->texinfo, f->contents, w->m_NumPoints);

    // for each of the points on the face
    for (unsigned int i = 0; i < w->m_NumPoints; i++)
    {
        // write the co-ords
        std::fprintf(out[hull], "%5.8f %5.8f %5.8f\n", w->m_Points[i][0], w->m_Points[i][1], w->m_Points[i][2]);
    }

    // put in an extra line break
    std::fprintf(out[hull], "\n");

    ThreadUnlock();
}
void WriteDetailBrush(int hull, const bface_t *faces)
{
    ThreadLock();
    std::fprintf(out_detailbrush[hull], "0\n");
    for (const bface_t *f = faces; f; f = f->next)
    {
        Winding *w = f->w;
        std::fprintf(out_detailbrush[hull], "%i %u\n", f->planenum, w->m_NumPoints);
        for (int i = 0; i < w->m_NumPoints; i++)
        {
            std::fprintf(out_detailbrush[hull], "%5.8f %5.8f %5.8f\n", w->m_Points[i][0], w->m_Points[i][1], w->m_Points[i][2]);
        }
    }
    std::fprintf(out_detailbrush[hull], "-1 -1\n");
    ThreadUnlock();
}

// =====================================================================================
//  SaveOutside
//      The faces remaining on the outside list are final polygons.  Write them to the
//      output file.
//      Passable contents (water, lava, etc) will generate a mirrored copy of the face
//      to be seen from the inside.
// =====================================================================================
static void SaveOutside(const brush_t *const b, const int hull, bface_t *outside, const int mirrorcontents)
{
    bface_t *next;
    vec3_t temp;

    for (bface_t *f = outside; f; f = next)
    {
        next = f->next;

        int backcontents;
        int texinfo = f->texinfo;
        const char *texname = GetTextureByNumber_CSG(texinfo);
        int frontcontents = f->contents;
        if (mirrorcontents == CONTENTS_TOEMPTY)
        {
            backcontents = f->backcontents;
        }
        else
        {
            backcontents = mirrorcontents;
        }
        if (frontcontents == CONTENTS_TOEMPTY)
        {
            frontcontents = CONTENTS_EMPTY;
        }
        if (backcontents == CONTENTS_TOEMPTY)
        {
            backcontents = CONTENTS_EMPTY;
        }

        bool frontnull = false;
        bool backnull = false;
        if (mirrorcontents == CONTENTS_TOEMPTY)
        {
            if (strncasecmp(texname, "SKIP", 4) && strncasecmp(texname, "HINT", 4) && strncasecmp(texname, "SOLIDHINT", 9))
            // SKIP and HINT are special textures for hlbsp
            {
                backnull = true;
            }
        }
        if (!strncasecmp(texname, "SOLIDHINT", 9))
        {
            if (frontcontents != backcontents)
            {
                frontnull = backnull = true; // not discardable, so remove "SOLIDHINT" texture name and behave like NULL
            }
        }
        if (b->entitynum != 0 && !strncasecmp(texname, "!", 1))
        {
            backnull = true; // strip water face on one side
        }

        f->contents = frontcontents;
        f->texinfo = frontnull ? -1 : texinfo;

        // count unique faces
        if (!hull)
        {
            for (bface_t *f2 = b->hulls[hull].faces; f2; f2 = f2->next)
            {
                if (f2->planenum == f->planenum)
                {
                    if (!f2->used)
                    {
                        f2->used = true;
                        c_outfaces++;
                    }
                    break;
                }
            }
        }

        // check the texture alignment of this face
        if (!hull)
        {
            int texinfo = f->texinfo;
            const char *texname = GetTextureByNumber_CSG(texinfo);
            texinfo_t *tex = &g_texinfo[texinfo];

            if (texinfo != -1                                                         // nullified textures (NULL, BEVEL, aaatrigger, etc.)
                && !(tex->flags & TEX_SPECIAL)                                        // sky
                && strncasecmp(texname, "SKIP", 4) && strncasecmp(texname, "HINT", 4) // HINT and SKIP will be nullified only after hlbsp
                && strncasecmp(texname, "SOLIDHINT", 9))
            {
                // check for "Malformed face (%d) normal"
                vec3_t texnormal;
                CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
                VectorNormalize(texnormal);
                if (std::abs(DotProduct(texnormal, f->plane->normal)) <= NORMAL_EPSILON)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Texture axis perpendicular to face.",
                            b->originalentitynum, b->originalbrushnum,
                            texname);
                }

                // check for "Bad surface extents"

                bool bad = false;
                for (int i = 0; i < f->w->m_NumPoints; i++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        vec_t val = DotProduct(f->w->m_Points[i], tex->vecs[j]) + tex->vecs[j][3];
                        if (val < -99999 || val > 999999)
                        {
                            bad = true;
                        }
                    }
                }
                if (bad)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Bad surface extents.",
                            b->originalentitynum, b->originalbrushnum,
                            texname);
                }
            }
        }

        WriteFace(hull, f,
                  (hull ? b->clipnodedetaillevel : b->detaillevel));

        //              if (mirrorcontents != CONTENTS_SOLID)
        {
            f->planenum ^= 1;
            f->plane = &g_mapplanes[f->planenum];
            f->contents = backcontents;
            f->texinfo = backnull ? -1 : texinfo;

            // swap point orders
            for (int i = 0; i < f->w->m_NumPoints / 2; i++) // add points backwards
            {
                VectorCopy(f->w->m_Points[i], temp);
                VectorCopy(f->w->m_Points[f->w->m_NumPoints - 1 - i], f->w->m_Points[i]);
                VectorCopy(temp, f->w->m_Points[f->w->m_NumPoints - 1 - i]);
            }
            WriteFace(hull, f,
                      (hull ? b->clipnodedetaillevel : b->detaillevel));
        }

        FreeFace(f);
    }
}

// =====================================================================================
//  CopyFace
// =====================================================================================
static bface_t *CopyFace(const bface_t *const f)
{
    bface_t *n = NewFaceFromFace(f);
    n->w = f->w->Copy();
    n->bounds = f->bounds;
    return n;
}

// =====================================================================================
//  CopyFaceList
// =====================================================================================
static bface_t *CopyFaceList(bface_t *f)
{
    if (f)
    {
        bface_t *head = CopyFace(f);
        bface_t *n = head;
        f = f->next;

        while (f)
        {
            n->next = CopyFace(f);

            n = n->next;
            f = f->next;
        }

        return head;
    }
    else
    {
        return nullptr;
    }
}

// =====================================================================================
//  FreeFaceList
// =====================================================================================
static void FreeFaceList(bface_t *f)
{
    if (f)
    {
        if (f->next)
        {
            FreeFaceList(f->next);
        }
        FreeFace(f);
    }
}

// =====================================================================================
//  CopyFacesToOutside
//      Make a copy of all the faces of the brush, so they can be chewed up by other
//      brushes.
//      All of the faces start on the outside list.
//      As other brushes take bites out of the faces, the fragments are moved to the
//      inside list, so they can be freed when they are determined to be completely
//      enclosed in solid.
// =====================================================================================
static bface_t *CopyFacesToOutside(brushhull_t *bh)
{
    bface_t *outside = nullptr;

    for (bface_t *f = bh->faces; f; f = f->next)
    {
        bface_t *newf = CopyFace(f);
        newf->w->getBounds(newf->bounds);
        newf->next = outside;
        outside = newf;
    }

    return outside;
}

// =====================================================================================
//  CSGBrush
// =====================================================================================
extern const char *ContentsToString(const contents_t type);
static void CSGBrush(int brushnum)
{
    bface_t *next;

    // get entity and brush info from the given brushnum that we can work with
    brush_t *b1 = &g_mapbrushes[brushnum];
    entity_t *e = &g_entities[b1->entitynum];

    // for each of the hulls
    for (int hull = 0; hull < NUM_HULLS; hull++)
    {
        brushhull_t *bh1 = &b1->hulls[hull];
        if (bh1->faces &&
            (hull ? b1->clipnodedetaillevel : b1->detaillevel))
        {
            switch (b1->contents)
            {
            case CONTENTS_ORIGIN:
            case CONTENTS_BOUNDINGBOX:
            case CONTENTS_HINT:
            case CONTENTS_TOEMPTY:
                break;
            default:
                Error("Entity %i, Brush %i: %s brushes not allowed in detail\n",
                      b1->originalentitynum, b1->originalbrushnum,
                      ContentsToString((contents_t)b1->contents));
                break;
            case CONTENTS_SOLID:
                WriteDetailBrush(hull, bh1->faces);
                break;
            }
        }

        // set outside to a copy of the brush's faces
        bface_t *outside = CopyFacesToOutside(bh1);
        bool overwrite = false;
        if (b1->contents == CONTENTS_TOEMPTY)
        {
            for (bface_t *f = outside; f; f = f->next)
            {
                f->contents = CONTENTS_TOEMPTY;
                f->backcontents = CONTENTS_TOEMPTY;
            }
        }

        // for each brush in entity e
        for (int bn = 0; bn < e->numbrushes; bn++)
        {
            // see if b2 needs to clip a chunk out of b1
            if (e->firstbrush + bn == brushnum)
            {
                continue;
            }
            overwrite = e->firstbrush + bn > brushnum;

            brush_t *b2 = &g_mapbrushes[e->firstbrush + bn];
            brushhull_t *bh2 = &b2->hulls[hull];
            if (b2->contents == CONTENTS_TOEMPTY)
                continue;
            if (
                (hull ? (b2->clipnodedetaillevel - 0 > b1->clipnodedetaillevel + 0) : (b2->detaillevel - b2->chopdown > b1->detaillevel + b1->chopup)))
                continue; // you can't chop
            if (b2->contents == b1->contents &&
                (hull ? (b2->clipnodedetaillevel != b1->clipnodedetaillevel) : (b2->detaillevel != b1->detaillevel)))
            {
                overwrite =
                    (hull ? (b2->clipnodedetaillevel < b1->clipnodedetaillevel) : (b2->detaillevel < b1->detaillevel));
            }
            if (b2->contents == b1->contents && hull == 0 && b2->detaillevel == b1->detaillevel && b2->coplanarpriority != b1->coplanarpriority)
            {
                overwrite = b2->coplanarpriority > b1->coplanarpriority;
            }

            if (!bh2->faces)
                continue; // brush isn't in this hull

            // check brush bounding box first
            // TODO: use boundingbox method instead
            if (bh1->bounds.testDisjoint(bh2->bounds))
            {
                continue;
            }

            // divide faces by the planes of the b2 to find which
            // fragments are inside

            bface_t *f = outside;
            outside = nullptr;
            for (; f; f = next)
            {
                next = f->next;

                // check face bounding box first
                if (bh2->bounds.testDisjoint(f->bounds))
                { // this face doesn't intersect brush2's bbox
                    f->next = outside;
                    outside = f;
                    continue;
                }
                if (
                    (hull ? (b2->clipnodedetaillevel > b1->clipnodedetaillevel) : (b2->detaillevel > b1->detaillevel)))
                {
                    const char *texname = GetTextureByNumber_CSG(f->texinfo);
                    if (f->texinfo == -1 || !strncasecmp(texname, "SKIP", 4) || !strncasecmp(texname, "HINT", 4) || !strncasecmp(texname, "SOLIDHINT", 9))
                    {
                        // should not nullify the fragment inside detail brush
                        f->next = outside;
                        outside = f;
                        continue;
                    }
                }

                // throw pieces on the front sides of the planes
                // into the outside list, return the remains on the inside
                // find the fragment inside brush2
                Winding *w = new Winding(*f->w);
                for (bface_t *f2 = bh2->faces; f2; f2 = f2->next)
                {
                    if (f->planenum == f2->planenum)
                    {
                        if (!overwrite)
                        {
                            // face plane is outside brush2
                            w->m_NumPoints = 0;
                            break;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    if (f->planenum == (f2->planenum ^ 1))
                    {
                        continue;
                    }
                    Winding *fw;
                    Winding *bw;
                    w->Clip(f2->plane->normal, f2->plane->dist, &fw, &bw);
                    if (fw)
                    {
                        delete fw;
                    }
                    if (bw)
                    {
                        delete w;
                        w = bw;
                    }
                    else
                    {
                        w->m_NumPoints = 0;
                        break;
                    }
                }
                // do real split
                if (w->m_NumPoints)
                {
                    for (bface_t *f2 = bh2->faces; f2; f2 = f2->next)
                    {
                        if (f->planenum == f2->planenum || f->planenum == (f2->planenum ^ 1))
                        {
                            continue;
                        }
                        int valid = 0;
                        for (int x = 0; x < w->m_NumPoints; x++)
                        {
                            vec_t dist = DotProduct(w->m_Points[x], f2->plane->normal) - f2->plane->dist;
                            if (dist >= -ON_EPSILON * 4) // only estimate
                            {
                                valid++;
                            }
                        }
                        if (valid >= 2)
                        { // this splitplane forms an edge
                            Winding *fw;
                            Winding *bw;
                            f->w->Clip(f2->plane->normal, f2->plane->dist, &fw, &bw);
                            if (fw)
                            {
                                bface_t *front = NewFaceFromFace(f);
                                front->w = fw;
                                fw->getBounds(front->bounds);
                                front->next = outside;
                                outside = front;
                            }
                            if (bw)
                            {
                                delete f->w;
                                f->w = bw;
                                bw->getBounds(f->bounds);
                            }
                            else
                            {
                                FreeFace(f);
                                f = nullptr;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    f->next = outside;
                    outside = f;
                    f = nullptr;
                }
                delete w;

                if (f)
                {
                    // there is one convex fragment of the original
                    // face left inside brush2

                    if (
                        (hull ? (b2->clipnodedetaillevel > b1->clipnodedetaillevel) : (b2->detaillevel > b1->detaillevel)))
                    { // don't chop or set contents, only nullify
                        f->next = outside;
                        outside = f;
                        f->texinfo = -1;
                        continue;
                    }
                    if (
                        (hull ? b2->clipnodedetaillevel < b1->clipnodedetaillevel : b2->detaillevel < b1->detaillevel) && b2->contents == CONTENTS_SOLID)
                    { // real solid
                        FreeFace(f);
                        continue;
                    }
                    if (b1->contents == CONTENTS_TOEMPTY)
                    {
                        bool onfront = true;
                        bool onback = true;
                        for (bface_t *f2 = bh2->faces; f2; f2 = f2->next)
                        {
                            if (f->planenum == (f2->planenum ^ 1))
                                onback = false;
                            if (f->planenum == f2->planenum)
                                onfront = false;
                        }
                        if (onfront && f->contents < b2->contents)
                            f->contents = b2->contents;
                        if (onback && f->backcontents < b2->contents)
                            f->backcontents = b2->contents;
                        if (f->contents == CONTENTS_SOLID && f->backcontents == CONTENTS_SOLID && strncasecmp(GetTextureByNumber_CSG(f->texinfo), "SOLIDHINT", 9))
                        {
                            FreeFace(f);
                        }
                        else
                        {
                            f->next = outside;
                            outside = f;
                        }
                        continue;
                    }
                    if (b1->contents > b2->contents || b1->contents == b2->contents && !strncasecmp(GetTextureByNumber_CSG(f->texinfo), "SOLIDHINT", 9))
                    { // inside a water brush
                        f->contents = b2->contents;
                        f->next = outside;
                        outside = f;
                    }
                    else // inside a solid brush
                    {
                        FreeFace(f); // throw it away
                    }
                }
            }
        }

        // all of the faces left in outside are real surface faces
        SaveOutside(b1, hull, outside, b1->contents);
    }
}

//
// =====================================================================================
//

// =====================================================================================
//  EmitPlanes
// =====================================================================================
static void EmitPlanes()
{
    g_numplanes = g_nummapplanes;
    plane_t *mp = g_mapplanes;
    dplane_t *dp = g_dplanes;
    {
        char name[_MAX_PATH];
        safe_snprintf(name, _MAX_PATH, "%s.pln", g_Mapname);
        std::FILE *planeout = std::fopen(name, "wb");
        if (!planeout)
            Error("Couldn't open %s", name);
        SafeWrite(planeout, g_mapplanes, g_nummapplanes * sizeof(plane_t));
        std::fclose(planeout);
    }
    for (int i = 0; i < g_nummapplanes; i++, mp++, dp++)
    {
        //if (!(mp->redundant))
        //{
        //    Log("EmitPlanes: plane %i non redundant\n", i);
        VectorCopy(mp->normal, dp->normal);
        dp->dist = mp->dist;
        dp->type = mp->type;
        // }
        //else
        // {
        //     Log("EmitPlanes: plane %i redundant\n", i);
        // }
    }
}

// =====================================================================================
//  SetModelNumbers
//      blah
// =====================================================================================
static void SetModelNumbers()
{
    char value[10];

    int models = 1;
    for (int i = 1; i < g_numentities; i++)
    {
        if (g_entities[i].numbrushes)
        {
            safe_snprintf(value, sizeof(value), "*%i", models);
            models++;
            SetKeyValue(&g_entities[i], "model", value);
        }
    }
}

static void ReuseModel()
{
    for (int i = g_numentities - 1; i >= 1; i--) // so it won't affect the remaining entities in the loop when we move this entity backward
    {
        const char *name = ValueForKey(&g_entities[i], "zhlt_usemodel");
        if (!*name)
        {
            continue;
        }
        int j;
        for (j = 1; j < g_numentities; j++)
        {
            if (*ValueForKey(&g_entities[j], "zhlt_usemodel"))
            {
                continue;
            }
            if (!std::strcmp(name, ValueForKey(&g_entities[j], "targetname")))
            {
                break;
            }
        }
        if (j == g_numentities)
        {
            if (!strcasecmp(name, "null"))
            {
                SetKeyValue(&g_entities[i], "model", "");
                continue;
            }
            Error("zhlt_usemodel: can not find target entity '%s', or that entity is also using 'zhlt_usemodel'.\n", name);
        }
        SetKeyValue(&g_entities[i], "model", ValueForKey(&g_entities[j], "model"));
        if (j > i)
        {
            // move this entity backward
            // to prevent precache error in case of .mdl/.spr and wrong result of EntityForModel in case of map model
            entity_t tmp = g_entities[i];
            std::memmove(&g_entities[i], &g_entities[i + 1], ((j + 1) - (i + 1)) * sizeof(entity_t));
            g_entities[j] = tmp;
        }
    }
}

// =====================================================================================
//  SetLightStyles
// =====================================================================================
constexpr int MAX_SWITCHED_LIGHTS = 32;
constexpr int MAX_LIGHTTARGETS_NAME = 64;

static void SetLightStyles()
{
    int j;
    char value[10];
    char lighttargets[MAX_SWITCHED_LIGHTS][MAX_LIGHTTARGETS_NAME];

    bool newtexlight = false;

    // any light that is controlled (has a targetname)
    // must have a unique style number generated for it

    int stylenum = 0;
    for (int i = 1; i < g_numentities; i++)
    {
        entity_t *e = &g_entities[i];

        const char *t = ValueForKey(e, "classname");
        if (strncasecmp(t, "light", 5))
        {
            //LRC:
            // if it's not a normal light entity, allocate it a new style if necessary.
            t = ValueForKey(e, "style");
            switch (std::atoi(t))
            {
            case 0: // not a light, no style, generally pretty boring
                continue;
            case -1: // normal switchable texlight
                safe_snprintf(value, sizeof(value), "%i", 32 + stylenum);
                SetKeyValue(e, "style", value);
                stylenum++;
                continue;
            case -2: // backwards switchable texlight
                safe_snprintf(value, sizeof(value), "%i", -(32 + stylenum));
                SetKeyValue(e, "style", value);
                stylenum++;
                continue;
            case -3:                          // (HACK) a piggyback texlight: switched on and off by triggering a real light that has the same name
                SetKeyValue(e, "style", "0"); // just in case the level designer didn't give it a name
                newtexlight = true;
                // don't 'continue', fall out
            }
            //LRC (ends)
        }
        t = ValueForKey(e, "targetname");
        if (*ValueForKey(e, "zhlt_usestyle"))
        {
            t = ValueForKey(e, "zhlt_usestyle");
            if (!strcasecmp(t, "null"))
            {
                t = "";
            }
        }
        if (!t[0])
        {
            continue;
        }

        // find this targetname
        for (j = 0; j < stylenum; j++)
        {
            if (!std::strcmp(lighttargets[j], t))
            {
                break;
            }
        }
        if (j == stylenum)
        {
            hlassume(stylenum < MAX_SWITCHED_LIGHTS, assume_MAX_SWITCHED_LIGHTS);
            safe_strncpy(lighttargets[j], t, MAX_LIGHTTARGETS_NAME);
            stylenum++;
        }
        safe_snprintf(value, sizeof(value), "%i", 32 + j);
        SetKeyValue(e, "style", value);
    }
}

// =====================================================================================
//  ConvertHintToEmtpy
// =====================================================================================
static void ConvertHintToEmpty()
{
    // Convert HINT brushes to EMPTY after they have been carved by csg
    for (int i = 0; i < MAX_MAP_BRUSHES; i++)
    {
        if (g_mapbrushes[i].contents == CONTENTS_HINT)
        {
            g_mapbrushes[i].contents = CONTENTS_EMPTY;
        }
    }
}

// =====================================================================================
//  WriteBSP
// =====================================================================================
static void LoadWadValue()
{
    char *wadvalue;
    ParseFromMemory(g_dentdata, g_entdatasize);
    epair_t *e;
    entity_t ent0;
    entity_t *mapent = &ent0;
    std::memset(mapent, 0, sizeof(entity_t));
    if (!GetToken(true))
    {
        wadvalue = strdup("");
    }
    else
    {
        if (std::strcmp(g_token, "{"))
        {
            Error("ParseEntity: { not found");
        }
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
            e = ParseEpair();
            e->next = mapent->epairs;
            mapent->epairs = e;
        }
        wadvalue = strdup(ValueForKey(mapent, "wad"));
        epair_t *next;
        for (e = mapent->epairs; e; e = next)
        {
            next = e->next;
            std::free(e->key);
            std::free(e->value);
            std::free(e);
        }
    }
    if (*wadvalue)
    {
        Log("Wad files required to run the map: \"%s\"\n", wadvalue);
    }
    else
    {
        Log("Wad files required to run the map: (None)\n");
    }
    SetKeyValue(&g_entities[0], "wad", wadvalue);
    std::free(wadvalue);
}

static void WriteBSP(const char *const name)
{
    char path[_MAX_PATH];

    safe_snprintf(path, _MAX_PATH, "%s.bsp", name);

    SetModelNumbers();
    ReuseModel();
    SetLightStyles();

    if (!g_onlyents)
        WriteMiptex();
    if (g_onlyents)
    {
        LoadWadValue();
    }

    UnparseEntities();
    ConvertHintToEmpty(); // this is ridiculous. --vluzacn
    if (g_chart)
        PrintBSPFileSizes();
    WriteBSPFile(path);
}

// AJM: added in
unsigned int BrushClipHullsDiscarded = 0;
unsigned int ClipNodesDiscarded = 0;

//AJM: added in function
static void MarkEntForNoclip(entity_t *ent)
{
    for (int i = ent->firstbrush; i < ent->firstbrush + ent->numbrushes; i++)
    {
        brush_t *b = &g_mapbrushes[i];
        b->noclip = 1;

        BrushClipHullsDiscarded++;
        ClipNodesDiscarded += b->numsides;
    }
}

// AJM
// =====================================================================================
//  CheckForNoClip
//      marks the noclip flag on any brushes that dont need clipnode generation, eg. func_illusionaries
// =====================================================================================
static void CheckForNoClip()
{
    char entclassname[MAX_KEY];
    int count = 0;

    if (!g_bClipNazi)
        return; // NO CLIP FOR YOU!!!

    for (int i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes)
            continue; // not a model

        if (!i)
            continue; // dont waste our time with worldspawn

        entity_t *ent = &g_entities[i];

        strcpy_s(entclassname, ValueForKey(ent, "classname"));
        int spawnflags = std::atoi(ValueForKey(ent, "spawnflags"));
        int skin = IntForKey(ent, "skin"); //vluzacn

        if ((skin != -16) &&
            (!std::strcmp(entclassname, "env_bubbles") || !std::strcmp(entclassname, "func_illusionary") || (spawnflags & 8) && (/* NOTE: func_doors as far as i can tell may need clipnodes for their
							player collision detection, so for now, they stay out of it. */
                                                                                                                                 !std::strcmp(entclassname, "func_train") || !std::strcmp(entclassname, "func_door") || !std::strcmp(entclassname, "func_water") || !std::strcmp(entclassname, "func_door_rotating") || !std::strcmp(entclassname, "func_pendulum") || !std::strcmp(entclassname, "func_train") || !std::strcmp(entclassname, "func_tracktrain") || !std::strcmp(entclassname, "func_vehicle")) ||
             (skin != 0) && (!std::strcmp(entclassname, "func_door") || !std::strcmp(entclassname, "func_water")) || (spawnflags & 2) && (!std::strcmp(entclassname, "func_conveyor")) || (spawnflags & 1) && (!std::strcmp(entclassname, "func_rot_button")) || (spawnflags & 64) && (!std::strcmp(entclassname, "func_rotating"))))
        {
            MarkEntForNoclip(ent);
            count++;
        }
    }

    Log("%i entities discarded from clipping hulls\n", count);
}

// =====================================================================================
//  ProcessModels
// =====================================================================================

static void ProcessModels()
{
    int contents;

    for (int i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes) // only models
            continue;

        // sort the contents down so stone bites water, etc
        int first = g_entities[i].firstbrush;
        brush_t *temps = (brush_t *)std::malloc(g_entities[i].numbrushes * sizeof(brush_t));
        hlassume(temps, assume_NoMemory);
        for (int j = 0; j < g_entities[i].numbrushes; j++)
        {
            temps[j] = g_mapbrushes[first + j];
        }
        int placedcontents;
        bool b_placedcontents = false;
        for (int placed = 0; placed < g_entities[i].numbrushes;)
        {
            bool b_contents = false;
            for (int j = 0; j < g_entities[i].numbrushes; j++)
            {
                brush_t *brush = &temps[j];
                if (b_placedcontents && brush->contents <= placedcontents)
                    continue;
                if (b_contents && brush->contents >= contents)
                    continue;
                b_contents = true;
                contents = brush->contents;
            }
            for (int j = 0; j < g_entities[i].numbrushes; j++)
            {
                brush_t *brush = &temps[j];
                if (brush->contents == contents)
                {
                    g_mapbrushes[first + placed] = *brush;
                    placed++;
                }
            }
            b_placedcontents = true;
            placedcontents = contents;
        }
        std::free(temps);

        // csg them in order
        if (i == 0) // if its worldspawn....
        {
            NamedRunThreadsOnIndividual(g_entities[i].numbrushes, g_estimate, CSGBrush);
            CheckFatal();
        }
        else
        {
            for (int j = 0; j < g_entities[i].numbrushes; j++)
            {
                CSGBrush(first + j);
            }
        }

        // write end of model marker
        for (int j = 0; j < NUM_HULLS; j++)
        {
            std::fprintf(out[j], "-1 -1 -1 -1 -1\n");
            std::fprintf(out_detailbrush[j], "-1\n");
        }
    }
}

// =====================================================================================
//  SetModelCenters
// =====================================================================================
static void SetModelCenters(int entitynum)
{
    char string[MAXTOKEN];
    entity_t *e = &g_entities[entitynum];
    BoundingBox bounds;
    vec3_t center;

    if ((entitynum == 0) || (e->numbrushes == 0)) // skip worldspawn and point entities
        return;

    if (!*ValueForKey(e, "light_origin")) // skip if its not a zhlt_flags light_origin
        return;

    for (int i = e->firstbrush, last = e->firstbrush + e->numbrushes; i < last; i++)
    {
        if (g_mapbrushes[i].contents != CONTENTS_ORIGIN && g_mapbrushes[i].contents != CONTENTS_BOUNDINGBOX)
        {
            bounds.add(g_mapbrushes[i].hulls->bounds);
        }
    }

    VectorAdd(bounds.m_Mins, bounds.m_Maxs, center);
    VectorScale(center, 0.5, center);

    safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)center[0], (int)center[1], (int)center[2]);
    SetKeyValue(e, "model_center", string);
}

//
// =====================================================================================
//

// =====================================================================================
//  BoundWorld
// =====================================================================================
static void BoundWorld()
{
    world_bounds.reset();

    for (int i = 0; i < g_nummapbrushes; i++)
    {
        brushhull_t *h = &g_mapbrushes[i].hulls[0];
        if (!h->faces)
        {
            continue;
        }
        world_bounds.add(h->bounds);
    }

    Verbose("World bounds: (%i %i %i) to (%i %i %i)\n",
            (int)world_bounds.m_Mins[0], (int)world_bounds.m_Mins[1], (int)world_bounds.m_Mins[2],
            (int)world_bounds.m_Maxs[0], (int)world_bounds.m_Maxs[1], (int)world_bounds.m_Maxs[2]);
}

// =====================================================================================
//  Usage
//      prints out usage sheet
// =====================================================================================
static void Usage()
{
    Banner(); // TODO: Call banner from main CSG process?

    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -console #       : Set to 0 to turn off the pop-up console (default is 1)\n");
    Log("    -nowadtextures   : include all used textures into bsp\n");
    Log("    -wadinclude file : place textures used from wad specified into bsp\n");
    Log("    -clipeconomy     : turn clipnode economy mode on\n");
    Log("    -cliptype value  : set to smallest, normalized, simple, precise, or legacy (default)\n");
    Log("    -onlyents        : do an entity update from .map to .bsp\n");
    Log("    -noskyclip       : disable automatic clipping of SKY brushes\n");
    Log("    -texdata #       : Alter maximum texture memory limit (in kb)\n");
    Log("    -lightdata #     : Alter maximum lighting memory limit (in kb)\n");
    Log("    -chart           : display bsp statitics\n");
    Log("    -low | -high     : run program an altered priority level\n");
    Log("    -threads #       : manually specify the number of threads to run\n");
    Log("    -estimate        : display estimated time during compile\n");
    Log("    -verbose         : compile with verbose messages\n");
    Log("    -nolightopt      : don't optimize engine light entities\n");
    Log("    -notextconvert   : don't convert game_text message from Windows ANSI to UTF8 format\n");
    Log("    -dev #           : compile with developer message\n\n");
    Log("    -wadautodetect   : Force auto-detection of wadfiles\n");
    Log("    mapfile          : The mapfile to compile\n\n");

    std::exit(1);
}

// =====================================================================================
//  DumpWadinclude
//      prints out the wadinclude list
// =====================================================================================
static void DumpWadinclude()
{
    Log("Wadinclude list :\n");
    for (WadInclude_i it = g_WadInclude.begin(); it != g_WadInclude.end(); it++)
    {
        Log("[%s]\n", it->c_str());
    }
}

// =====================================================================================
//  Settings
//      prints out settings sheet
// =====================================================================================
static void Settings()
{
    char *tmp;

    Log("\nCurrent %s Settings\n", g_Program);
    Log("Name                 |  Setting  |  Default\n"
        "---------------------|-----------|-------------------------\n");

    // ZHLT Common Settings
    if (DEFAULT_NUMTHREADS == -1)
    {
        Log("threads               [ %7d ] [  Varies ]\n", g_numthreads);
    }
    else
    {
        Log("threads               [ %7d ] [ %7d ]\n", g_numthreads, DEFAULT_NUMTHREADS);
    }

    Log("verbose               [ %7s ] [ %7s ]\n", g_verbose ? "on" : "off", DEFAULT_VERBOSE ? "on" : "off");

    Log("developer             [ %7d ] [ %7d ]\n", g_developer, DEFAULT_DEVELOPER);
    Log("chart                 [ %7s ] [ %7s ]\n", g_chart ? "on" : "off", DEFAULT_CHART ? "on" : "off");
    Log("estimate              [ %7s ] [ %7s ]\n", g_estimate ? "on" : "off", DEFAULT_ESTIMATE ? "on" : "off");
    Log("max texture memory    [ %7d ] [ %7d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);
    Log("max lighting memory   [ %7d ] [ %7d ]\n", g_max_map_lightdata, DEFAULT_MAX_MAP_LIGHTDATA);

    switch (g_threadpriority)
    {
    case eThreadPriorityNormal:
    default:
        tmp = "Normal";
        break;
    case eThreadPriorityLow:
        tmp = "Low";
        break;
    case eThreadPriorityHigh:
        tmp = "High";
        break;
    }
    Log("priority              [ %7s ] [ %7s ]\n", tmp, "Normal");
    Log("\n");
    // HLCSG Specific Settings
    Log("clipnode economy mode [ %7s ] [ %7s ]\n", g_bClipNazi ? "on" : "off", DEFAULT_CLIPNAZI ? "on" : "off");
    Log("clip hull type        [ %7s ] [ %7s ]\n", GetClipTypeString(g_cliptype), GetClipTypeString(DEFAULT_CLIPTYPE));
    Log("onlyents              [ %7s ] [ %7s ]\n", g_onlyents ? "on" : "off", DEFAULT_ONLYENTS ? "on" : "off");
    Log("wadtextures           [ %7s ] [ %7s ]\n", g_wadtextures ? "on" : "off", DEFAULT_WADTEXTURES ? "on" : "off");
    Log("skyclip               [ %7s ] [ %7s ]\n", g_skyclip ? "on" : "off", DEFAULT_SKYCLIP ? "on" : "off");
    Log("light name optimize   [ %7s ] [ %7s ]\n", !g_nolightopt ? "on" : "off", !DEFAULT_NOLIGHTOPT ? "on" : "off");
    Log("convert game_text     [ %7s ] [ %7s ]\n", !g_noutf8 ? "on" : "off", !DEFAULT_NOUTF8 ? "on" : "off");
    Log("\n");
}

// AJM: added in
// =====================================================================================
//  CSGCleanup
// =====================================================================================
static void CSGCleanup()
{
    //Log("CSGCleanup\n");
    FreeWadPaths();
}

// =====================================================================================
//  Main
//      Oh, come on.
// =====================================================================================
int main(const int argc, char **argv)
{
    char name[_MAX_PATH];                   // mapanme
    const char *mapname_from_arg = nullptr; // mapname path from passed argvar

    g_Program = "hlcsg";

    int argcold = argc;
    char **argvold = argv;
    {
        int argc;
        char **argv;
        ParseParamFile(argcold, argvold, argc, argv);
        {
            if (InitConsole(argc, argv) < 0)
                Usage();
            if (argc == 1)
                Usage();

            // Hard coded list of -wadinclude files, used for HINT texture brushes so lazy
            // mapmakers wont cause beta testers (or possibly end users) to get a wad
            // error on zhlt.wad etc
            g_WadInclude.push_back("zhlt.wad");

            InitDefaultHulls();

            // detect argv
            for (int i = 1; i < argc; i++)
            {
                if (!strcasecmp(argv[i], "-threads"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        g_numthreads = std::atoi(argv[++i]);
                        if (g_numthreads < 1)
                        {
                            Log("Expected value of at least 1 for '-threads'\n");
                            Usage();
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }

                else if (!strcasecmp(argv[i], "-console"))
                {
                    if (i + 1 < argc)
                        ++i;
                    else
                        Usage();
                }
                else if (!strcasecmp(argv[i], "-estimate"))
                {
                    g_estimate = true;
                }

                else if (!strcasecmp(argv[i], "-dev"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        g_developer = (developer_level_t)std::atoi(argv[++i]);
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-verbose"))
                {
                    g_verbose = true;
                }
                else if (!strcasecmp(argv[i], "-chart"))
                {
                    g_chart = true;
                }
                else if (!strcasecmp(argv[i], "-low"))
                {
                    g_threadpriority = eThreadPriorityLow;
                }
                else if (!strcasecmp(argv[i], "-high"))
                {
                    g_threadpriority = eThreadPriorityHigh;
                }
                else if (!strcasecmp(argv[i], "-noskyclip"))
                {
                    g_skyclip = false;
                }
                else if (!strcasecmp(argv[i], "-onlyents"))
                {
                    g_onlyents = true;
                }

                else if (!strcasecmp(argv[i], "-clipeconomy"))
                {
                    g_bClipNazi = true;
                }

                else if (!strcasecmp(argv[i], "-cliptype"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        ++i;
                        if (!strcasecmp(argv[i], "smallest"))
                        {
                            g_cliptype = clip_smallest;
                        }
                        else if (!strcasecmp(argv[i], "normalized"))
                        {
                            g_cliptype = clip_normalized;
                        }
                        else if (!strcasecmp(argv[i], "simple"))
                        {
                            g_cliptype = clip_simple;
                        }
                        else if (!strcasecmp(argv[i], "precise"))
                        {
                            g_cliptype = clip_precise;
                        }
                        else if (!strcasecmp(argv[i], "legacy"))
                        {
                            g_cliptype = clip_legacy;
                        }
                    }
                    else
                    {
                        Log("Error: -cliptype: incorrect usage of parameter\n");
                        Usage();
                    }
                }

                else if (!strcasecmp(argv[i], "-wadautodetect"))
                {
                    g_bWadAutoDetect = true;
                }

                else if (!strcasecmp(argv[i], "-nowadtextures"))
                {
                    g_wadtextures = false;
                }
                else if (!strcasecmp(argv[i], "-wadinclude"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        g_WadInclude.push_back(argv[++i]);
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-texdata"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        int x = std::atoi(argv[++i]) * 1024;

                        //if (x > g_max_map_miptex) //--vluzacn
                        {
                            g_max_map_miptex = x;
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-lightdata"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        int x = std::atoi(argv[++i]) * 1024;

                        //if (x > g_max_map_lightdata) //--vluzacn
                        {
                            g_max_map_lightdata = x;
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-nolightopt"))
                {
                    g_nolightopt = true;
                }
                else if (!strcasecmp(argv[i], "-notextconvert"))
                {
                    g_noutf8 = true;
                }
                else if (argv[i][0] == '-')
                {
                    Log("Unknown option \"%s\"\n", argv[i]);
                    Usage();
                }
                else if (!mapname_from_arg)
                {
                    mapname_from_arg = argv[i];
                }
                else
                {
                    Log("Unknown option \"%s\"\n", argv[i]);
                    Usage();
                }
            }

            // no mapfile?
            if (!mapname_from_arg)
            {
                // what a shame.
                Log("No mapfile specified\n");
                Usage();
            }

            // handle mapname
            safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
            FlipSlashes(g_Mapname);
            StripExtension(g_Mapname);

            // onlyents
            if (!g_onlyents)
                ResetTmpFiles();

            // other stuff
            ResetErrorLog();
            if (!g_onlyents)
                ResetLog();
            OpenLog(g_clientid);
            std::atexit(CloseLog);
            LogStart(argcold, argvold);
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

            hlassume(CalcFaceExtents_test(), assume_first);

            std::atexit(CSGCleanup); // AJM
            dtexdata_init();
            std::atexit(dtexdata_free);

            // START CSG
            // AJM: re-arranged some stuff up here so that the mapfile is loaded
            //  before settings are finalised and printed out, so that the info_compile_parameters
            //  entity can be dealt with effectively
            double start = I_FloatTime();

            safe_strncpy(name, mapname_from_arg, _MAX_PATH); // make a copy of the nap name
            FlipSlashes(name);
            DefaultExtension(name, ".map"); // might be .reg

            LoadMapFile(name);
            ThreadSetDefault();
            ThreadSetPriority(g_threadpriority);
            Settings();

            if (!g_noutf8)
            {
                int count = 0;

                for (int i = 0; i < g_numentities; i++)
                {
                    entity_t *ent = &g_entities[i];

                    if (std::strcmp(ValueForKey(ent, "classname"), "game_text"))
                    {
                        continue;
                    }

                    const char *value = ValueForKey(ent, "message");
                    if (*value)
                    {
                        char *newvalue = ANSItoUTF8(value);
                        if (std::strcmp(newvalue, value))
                        {
                            SetKeyValue(ent, "message", newvalue);
                            count++;
                        }
                        std::free(newvalue);
                    }
                }

                if (count)
                {
                    Log("%d game_text messages converted from Windows ANSI(CP_ACP) to UTF-8 encoding\n", count);
                }
            }

            if (!g_onlyents)
            {
                Log("Using mapfile wad configuration\n");
                GetUsedWads();

                if (g_bWadAutoDetect)
                {
                    Log("Wadfiles not in use by the map will be excluded\n");
                }

                DumpWadinclude();
                Log("\n");
            }

            // if onlyents, just grab the entites and resave
            if (g_onlyents)
            {
                char out[_MAX_PATH];

                safe_snprintf(out, _MAX_PATH, "%s.bsp", g_Mapname);
                LoadBSPFile(out);

                // Write it all back out again.
                WriteBSP(g_Mapname);

                double end = I_FloatTime();
                LogTimeElapsed(end - start);
                return 0;
            }

            CheckForNoClip();

            // createbrush
            NamedRunThreadsOnIndividual(g_nummapbrushes, g_estimate, CreateBrush);
            CheckFatal();

            // boundworld
            BoundWorld();

            Verbose("%5i map planes\n", g_nummapplanes);

            // Set model centers
            for (int i = 0; i < g_numentities; i++)
                SetModelCenters(i); //NamedRunThreadsOnIndividual(g_numentities, g_estimate, SetModelCenters); //--vluzacn

            // open hull files
            for (int i = 0; i < NUM_HULLS; i++)
            {
                char name[_MAX_PATH];

                safe_snprintf(name, _MAX_PATH, "%s.p%i", g_Mapname, i);

                out[i] = std::fopen(name, "w");

                if (!out[i])
                    Error("Couldn't open %s", name);
                safe_snprintf(name, _MAX_PATH, "%s.b%i", g_Mapname, i);
                out_detailbrush[i] = std::fopen(name, "w");
                if (!out_detailbrush[i])
                    Error("Couldn't open %s", name);
            }
            {
                char name[_MAX_PATH];
                safe_snprintf(name, _MAX_PATH, "%s.hsz", g_Mapname);
                std::FILE *f = std::fopen(name, "w");
                if (!f)
                    Error("Couldn't open %s", name);
                for (int i = 0; i < NUM_HULLS; i++)
                {
                    float x1 = g_hull_size[i][0][0];
                    float y1 = g_hull_size[i][0][1];
                    float z1 = g_hull_size[i][0][2];
                    float x2 = g_hull_size[i][1][0];
                    float y2 = g_hull_size[i][1][1];
                    float z2 = g_hull_size[i][1][2];
                    std::fprintf(f, "%g %g %g %g %g %g\n", x1, y1, z1, x2, y2, z2);
                }
                std::fclose(f);
            }

            ProcessModels();

            Verbose("%5i csg faces\n", c_csgfaces);
            Verbose("%5i used faces\n", c_outfaces);

            // close hull files
            for (int i = 0; i < NUM_HULLS; i++)
            {
                std::fclose(out[i]);
                std::fclose(out_detailbrush[i]);
            }

            EmitPlanes();

            WriteBSP(g_Mapname);

            // elapsed time
            double end = I_FloatTime();
            LogTimeElapsed(end - start);
        }
    }
    return 0;
}
