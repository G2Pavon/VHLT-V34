/*
 
    VISIBLE INFORMATION SET    -aka-    V I S

    Code based on original code from Valve Software, 
    Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
    Modified by Tony "Merl" Moore (merlinis@bigpond.net.au)
    Contains code by Skyler "Zipster" York (zipster89134@hotmail.com) - Included with permission.
    
*/

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "vis.h"
#include "filelib.h"
#include "cmdlinecfg.h"
#include "threads.h"
#include "log.h"
#include "cmdlib.h"
#include "mathtypes.h"
#include "mathlib.h"
#include "bspfile.h"
#include "winding.h"

/*

 NOTES

*/

int g_numportals = 0;
unsigned g_portalleafs = 0;

portal_t *g_portals;

leaf_t *g_leafs;
int *g_leafstarts;
int *g_leafcounts;
int g_leafcount_all;

// AJM: MVD
//

static byte *vismap;
static byte *vismap_p;
static byte *vismap_end; // past visfile
static int originalvismapsize;

byte *g_uncompressed; // [bitbytes*portalleafs]

unsigned g_bitbytes; // (portalleafs+63)>>3
unsigned g_bitlongs;

bool g_fastvis = DEFAULT_FASTVIS;
bool g_fullvis = DEFAULT_FULLVIS;
bool g_estimate = DEFAULT_ESTIMATE;
bool g_chart = DEFAULT_CHART;
bool g_info = DEFAULT_INFO;

// AJM: MVD
unsigned int g_maxdistance = DEFAULT_MAXDISTANCE_RANGE;
//bool			g_postcompile = DEFAULT_POST_COMPILE;
//
const int g_overview_max = MAX_MAP_ENTITIES;
overview_t g_overview[g_overview_max];
int g_overview_count = 0;
leafinfo_t *g_leafinfos = NULL;

static int totalvis = 0;

// AJM: addded in
// =====================================================================================
//  GetParamsFromEnt
//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own version of this
//      to handle its own specific settings.
// =====================================================================================
void GetParamsFromEnt(entity_t *mapent)
{
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
    if (!strcmp(ValueForKey(mapent, "priority"), "1"))
    {
        g_threadpriority = eThreadPriorityHigh;
        Log("%30s [ %-9s ]\n", "Thread Priority", "high");
    }
    else if (!strcmp(ValueForKey(mapent, "priority"), "-1"))
    {
        g_threadpriority = eThreadPriorityLow;
        Log("%30s [ %-9s ]\n", "Thread Priority", "low");
    }

    /*
    hlvis(choices) : "HLVIS" : 2 = 
    [ 
        0 : "Off"
        1 : "Fast"
        2 : "Normal" 
        3 : "Full"
    ]
    */
    iTmp = IntForKey(mapent, "hlvis");
    if (iTmp == 0)
    {
        Fatal(assume_TOOL_CANCEL,
              "%s flag was not checked in info_compile_parameters entity, execution of %s cancelled", g_Program, g_Program);
        CheckFatal();
    }
    else if (iTmp == 1)
    {
        g_fastvis = true;
        g_fullvis = false;
    }
    else if (iTmp == 2)
    {
        g_fastvis = false;
        g_fullvis = false;
    }
    else if (iTmp == 3)
    {
        g_fullvis = true;
        g_fastvis = false;
    }
    Log("%30s [ %-9s ]\n", "Fast VIS", g_fastvis ? "on" : "off");
    Log("%30s [ %-9s ]\n", "Full VIS", g_fullvis ? "on" : "off");

    ///////////////////
    Log("\n");
}

// =====================================================================================
//  PlaneFromWinding
// =====================================================================================
static void PlaneFromWinding(winding_t *w, plane_t *plane)
{
    vec3_t v1;
    vec3_t v2;

    // calc plane
    VectorSubtract(w->points[2], w->points[1], v1);
    VectorSubtract(w->points[0], w->points[1], v2);
    CrossProduct(v2, v1, plane->normal);
    VectorNormalize(plane->normal);
    plane->dist = DotProduct(w->points[0], plane->normal);
}

// =====================================================================================
//  NewWinding
// =====================================================================================
static winding_t *NewWinding(const int points)
{
    if (points > MAX_POINTS_ON_WINDING)
    {
        Error("NewWinding: %i points > MAX_POINTS_ON_WINDING", points);
    }

    int size = (int)(intptr_t)((winding_t *)0)->points[points];
    winding_t *w = (winding_t *)calloc(1, size);

    return w;
}

// =====================================================================================
//  GetNextPortal
//      Returns the next portal for a thread to work on
//      Returns the portals from the least complex, so the later ones can reuse the earlier information.
// =====================================================================================
static portal_t *GetNextPortal()
{
    int j;
    portal_t *tp;

    {
        if (GetThreadWork() == -1)
        {
            return NULL;
        }

        ThreadLock();

        int min = 99999;
        portal_t *p = NULL;

        for (j = 0, tp = g_portals; j < g_numportals * 2; j++, tp++)
        {
            if (tp->nummightsee < min && tp->status == stat_none)
            {
                min = tp->nummightsee;
                p = tp;
            }
        }

        if (p)
        {
            p->status = stat_working;
        }

        ThreadUnlock();

        return p;
    }
}

// =====================================================================================
//  LeafThread
// =====================================================================================
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter

static void LeafThread(int unused)
{
    portal_t *p;

    while (1)
    {
        if (!(p = GetNextPortal()))
        {
            return;
        }

        PortalFlow(p);

        Verbose("portal:%4i  mightsee:%4i  cansee:%4i\n", (int)(p - g_portals), p->nummightsee, p->numcansee);
    }
}

#pragma warning(pop)

// =====================================================================================
//  LeafFlow
//      Builds the entire visibility list for a leaf
// =====================================================================================
static void LeafFlow(const int leafnum)
{
    byte compressed[MAX_MAP_LEAFS / 8];

    //
    // flow through all portals, collecting visible bits
    //
    memset(compressed, 0, sizeof(compressed));
    byte *outbuffer = g_uncompressed + leafnum * g_bitbytes;
    leaf_t *leaf = &g_leafs[leafnum];
    int tmp = 0;

    const unsigned offset = leafnum >> 3;
    const unsigned bit = (1 << (leafnum & 7));

    for (unsigned i = 0; i < leaf->numportals; i++)
    {
        portal_t *p = leaf->portals[i];
        if (p->status != stat_done)
        {
            Error("portal not done (leaf %d)", leafnum);
        }

        {
            byte *dst = outbuffer;
            byte *src = p->visbits;
            for (unsigned j = 0; j < g_bitbytes; j++, dst++, src++)
            {
                *dst |= *src;
            }
        }

        if ((tmp == 0) && (outbuffer[offset] & bit))
        {
            tmp = 1;
            Warning("Leaf portals saw into leaf");
            Log("    Problem at portal between leaves %i and %i:\n   ", leafnum, p->leaf);
            for (int k = 0; k < p->winding->numpoints; k++)
            {
                Log("    (%4.3f %4.3f %4.3f)\n", p->winding->points[k][0], p->winding->points[k][1], p->winding->points[k][2]);
            }
            Log("\n");
        }
    }

    outbuffer[offset] |= bit;

    if (g_leafinfos[leafnum].isoverviewpoint)
    {
        for (unsigned i = 0; i < g_portalleafs; i++)
        {
            outbuffer[i >> 3] |= (1 << (i & 7));
        }
    }
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        if (g_leafinfos[i].isskyboxpoint)
        {
            outbuffer[i >> 3] |= (1 << (i & 7));
        }
    }
    int numvis = 0;
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        if (outbuffer[i >> 3] & (1 << (i & 7)))
        {
            numvis++;
        }
    }

    //
    // compress the bit string
    //
    Verbose("leaf %4i : %4i visible\n", leafnum, numvis);
    totalvis += numvis;

    byte buffer2[MAX_MAP_LEAFS / 8];
    int diskbytes = (g_leafcount_all + 7) >> 3;
    memset(buffer2, 0, diskbytes);
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        for (unsigned j = 0; j < g_leafcounts[i]; j++)
        {
            int srcofs = i >> 3;
            int srcbit = 1 << (i & 7);
            int dstofs = (g_leafstarts[i] + j) >> 3;
            int dstbit = 1 << ((g_leafstarts[i] + j) & 7);
            if (outbuffer[srcofs] & srcbit)
            {
                buffer2[dstofs] |= dstbit;
            }
        }
    }
    unsigned i = CompressVis(buffer2, diskbytes, compressed, sizeof(compressed));

    byte *dest = vismap_p;
    vismap_p += i;

    if (vismap_p > vismap_end)
    {
        Error("Vismap expansion overflow");
    }

    for (unsigned j = 0; j < g_leafcounts[leafnum]; j++)
    {
        g_dleafs[g_leafstarts[leafnum] + j + 1].visofs = dest - vismap;
    }

    memcpy(dest, compressed, i);
}

// =====================================================================================
//  CalcPortalVis
// =====================================================================================
static void CalcPortalVis()
{
    // g_fastvis just uses mightsee for a very loose bound
    if (g_fastvis)
    {
        for (int i = 0; i < g_numportals * 2; i++)
        {
            g_portals[i].visbits = g_portals[i].mightsee;
            g_portals[i].status = stat_done;
        }
        return;
    }
    NamedRunThreadsOn(g_numportals * 2, g_estimate, LeafThread);
}

// AJM: MVD
// =====================================================================================
//  SaveVisData
// =====================================================================================
void SaveVisData(const char *filename)
{
    std::FILE *fp = std::fopen(filename, "wb");

    if (!fp)
        return;

    SafeWrite(fp, g_dvisdata, (vismap_p - g_dvisdata));

    // BUG BUG BUG!
    // Leaf offsets need to be saved too!!!!
    for (int i = 0; i < g_numleafs; i++)
    {
        SafeWrite(fp, &g_dleafs[i].visofs, sizeof(int));
    }

    std::fclose(fp);
}

// AJM UNDONE HLVIS_MAXDIST THIS!!!!!!!!!!!!!

// AJM: MVD modified
// =====================================================================================
//  CalcVis
// =====================================================================================
static void CalcVis()
{
    char visdatafile[_MAX_PATH];

    safe_snprintf(visdatafile, _MAX_PATH, "%s.vdt", g_Mapname);

    // Remove this file
    unlink(visdatafile);

    /*    if(g_postcompile)
	{
		if(!g_maxdistance)
		{
			Error("Must use -maxdistance parameter with -postcompile");
		}

		// Decompress everything so we can edit it
		DecompressAll();
		
		NamedRunThreadsOn(g_portalleafs, g_estimate, PostMaxDistVis);

		// Recompress it
		CompressAll();
	}
	else
	{*/
    //		InitVisBlock();
    //		SetupVisBlockLeafs();

    NamedRunThreadsOn(g_numportals * 2, g_estimate, BasePortalVis);

    //		if(g_numvisblockers)
    //			NamedRunThreadsOn(g_numvisblockers, g_estimate, BlockVis);

    // First do a normal VIS, save to file, then redo MaxDistVis

    CalcPortalVis();

    //
    // assemble the leaf vis lists by oring and compressing the portal lists
    //
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        LeafFlow(i);
    }

    Log("average leafs visible: %i\n", totalvis / g_portalleafs);

    if (g_maxdistance)
    {
        totalvis = 0;

        Log("saving visdata to %s...\n", visdatafile);
        SaveVisData(visdatafile);

        // We need to reset the uncompressed variable and portal visbits
        free(g_uncompressed);
        g_uncompressed = (byte *)calloc(g_portalleafs, g_bitbytes);

        vismap_p = g_dvisdata;

        // We don't need to run BasePortalVis again
        NamedRunThreadsOn(g_portalleafs, g_estimate, MaxDistVis);

        // No need to run this - MaxDistVis now writes directly to visbits after the initial VIS
        //CalcPortalVis();

        for (unsigned i = 0; i < g_portalleafs; i++)
        {
            LeafFlow(i);
        }

        Log("average maxdistance leafs visible: %i\n", totalvis / g_portalleafs);
    }
    //	}
}

// =====================================================================================
//  CheckNullToken
// =====================================================================================
static INLINE void FASTCALL CheckNullToken(const char *const token)
{
    if (token == NULL)
    {
        Error("LoadPortals: Damaged or invalid .prt file\n");
    }
}

// =====================================================================================
//  LoadPortals
// =====================================================================================
static void LoadPortals(char *portal_image)
{
    int i;
    portal_t *p;
    int numpoints;
    int leafnums[2];
    plane_t plane;
    const char *const seperators = " ()\r\n\t";

    char *token = strtok(portal_image, seperators);
    CheckNullToken(token);
    if (!std::sscanf(token, "%u", &g_portalleafs))
    {
        Error("LoadPortals: failed to read header: number of leafs");
    }

    token = strtok(NULL, seperators);
    CheckNullToken(token);
    if (!std::sscanf(token, "%i", &g_numportals))
    {
        Error("LoadPortals: failed to read header: number of portals");
    }

    Log("%4i portalleafs\n", g_portalleafs);
    Log("%4i numportals\n", g_numportals);

    g_bitbytes = ((g_portalleafs + 63) & ~63) >> 3;
    g_bitlongs = g_bitbytes / sizeof(long);

    // each file portal is split into two memory portals
    g_portals = (portal_t *)calloc(2 * g_numportals, sizeof(portal_t));
    g_leafs = (leaf_t *)calloc(g_portalleafs, sizeof(leaf_t));
    g_leafinfos = (leafinfo_t *)calloc(g_portalleafs, sizeof(leafinfo_t));
    g_leafcounts = (int *)calloc(g_portalleafs, sizeof(int));
    g_leafstarts = (int *)calloc(g_portalleafs, sizeof(int));

    originalvismapsize = g_portalleafs * ((g_portalleafs + 7) / 8);

    vismap = vismap_p = g_dvisdata;
    vismap_end = vismap + MAX_MAP_VISIBILITY;

    if (g_portalleafs > MAX_MAP_LEAFS)
    { // this may cause hlvis to overflow, because numportalleafs can be larger than g_numleafs in some special cases
        Error("Too many portalleafs (g_portalleafs(%d) > MAX_MAP_LEAFS(%d)).", g_portalleafs, MAX_MAP_LEAFS);
    }
    g_leafcount_all = 0;
    for (i = 0; i < g_portalleafs; i++)
    {
        unsigned rval = 0;
        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &g_leafcounts[i]);
        if (rval != 1)
        {
            Error("LoadPortals: read leaf %i failed", i);
        }
        g_leafstarts[i] = g_leafcount_all;
        g_leafcount_all += g_leafcounts[i];
    }
    if (g_leafcount_all != g_dmodels[0].visleafs)
    { // internal error (this should never happen)
        Error("Corrupted leaf mapping (g_leafcount_all(%d) != g_dmodels[0].visleafs(%d)).", g_leafcount_all, g_dmodels[0].visleafs);
    }
    for (i = 0; i < g_portalleafs; i++)
    {
        for (int j = 0; j < g_overview_count; j++)
        {
            int d = g_overview[j].visleafnum - g_leafstarts[i];
            if (0 <= d && d < g_leafcounts[i])
            {
                if (g_overview[j].reverse)
                {
                    g_leafinfos[i].isskyboxpoint = true;
                }
                else
                {
                    g_leafinfos[i].isoverviewpoint = true;
                }
            }
        }
    }
    for (i = 0, p = g_portals; i < g_numportals; i++)
    {
        unsigned rval = 0;

        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &numpoints);
        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &leafnums[0]);
        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &leafnums[1]);

        if (rval != 3)
        {
            Error("LoadPortals: reading portal %i", i);
        }
        if (numpoints > MAX_POINTS_ON_WINDING)
        {
            Error("LoadPortals: portal %i has too many points", i);
        }
        if (((unsigned)leafnums[0] > g_portalleafs) || ((unsigned)leafnums[1] > g_portalleafs))
        {
            Error("LoadPortals: reading portal %i", i);
        }

        winding_t *w = p->winding = NewWinding(numpoints);
        w->original = true;
        w->numpoints = numpoints;

        for (int j = 0; j < numpoints; j++)
        {
            double v[3];
            unsigned rval = 0;

            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += std::sscanf(token, "%lf", &v[0]);
            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += std::sscanf(token, "%lf", &v[1]);
            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += std::sscanf(token, "%lf", &v[2]);

            // scanf into double, then assign to vec_t
            if (rval != 3)
            {
                Error("LoadPortals: reading portal %i", i);
            }
            for (int k = 0; k < 3; k++)
            {
                w->points[j][k] = v[k];
            }
        }

        // calc plane
        PlaneFromWinding(w, &plane);

        // create forward portal
        leaf_t *l = &g_leafs[leafnums[0]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        p->winding = w;
        VectorSubtract(vec3_origin, plane.normal, p->plane.normal);
        p->plane.dist = -plane.dist;
        p->leaf = leafnums[1];
        p++;

        // create backwards portal
        l = &g_leafs[leafnums[1]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        p->winding = NewWinding(w->numpoints);
        p->winding->numpoints = w->numpoints;
        for (int j = 0; j < w->numpoints; j++)
        {
            VectorCopy(w->points[w->numpoints - 1 - j], p->winding->points[j]);
        }

        p->plane = plane;
        p->leaf = leafnums[0];
        p++;
    }
}

// =====================================================================================
//  LoadPortalsByFilename
// =====================================================================================
static void LoadPortalsByFilename(const char *const filename)
{
    char *file_image;

    if (!q_exists(filename))
    {
        Error("Portal file '%s' does not exist, cannot vis the map\n", filename);
    }
    LoadFile(filename, &file_image);
    LoadPortals(file_image);
    free(file_image);
}

// =====================================================================================
//  Usage
// =====================================================================================
static void Usage()
{
    Banner();

    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -console #      : Set to 0 to turn off the pop-up console (default is 1)\n");
    Log("    -lang file      : localization file\n");
    Log("    -full           : Full vis\n");
    Log("    -fast           : Fast vis\n\n");
    Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
    Log("    -lightdata #      : Alter maximum lighting memory limit (in kb)\n"); //lightdata //--vluzacn
    Log("    -chart          : display bsp statitics\n");
    Log("    -low | -high    : run program an altered priority level\n");
    Log("    -nolog          : don't generate the compile logfiles\n");
    Log("    -threads #      : manually specify the number of threads to run\n");
    Log("    -estimate       : display estimated time during compile\n");
    Log("    -maxdistance #  : Alter the maximum distance for visibility\n");
    Log("    -verbose        : compile with verbose messages\n");
    Log("    -noinfo         : Do not show tool configuration information\n");
    Log("    -dev #          : compile with developer message\n\n");
    Log("    mapfile         : The mapfile to compile\n\n");
    exit(1);
}

// =====================================================================================
//  Settings
// =====================================================================================
static void Settings()
{
    char *tmp;

    if (!g_info)
    {
        return;
    }

    Log("\n-= Current %s Settings =-\n", g_Program);
    Log("Name               |  Setting  |  Default\n"
        "-------------------|-----------|-------------------------\n");

    // ZHLT Common Settings
    if (DEFAULT_NUMTHREADS == -1)
    {
        Log("threads             [ %7d ] [  Varies ]\n", g_numthreads);
    }
    else
    {
        Log("threads             [ %7d ] [ %7d ]\n", g_numthreads, DEFAULT_NUMTHREADS);
    }

    Log("verbose             [ %7s ] [ %7s ]\n", g_verbose ? "on" : "off", DEFAULT_VERBOSE ? "on" : "off");
    Log("log                 [ %7s ] [ %7s ]\n", g_log ? "on" : "off", DEFAULT_LOG ? "on" : "off");
    Log("developer           [ %7d ] [ %7d ]\n", g_developer, DEFAULT_DEVELOPER);
    Log("chart               [ %7s ] [ %7s ]\n", g_chart ? "on" : "off", DEFAULT_CHART ? "on" : "off");
    Log("estimate            [ %7s ] [ %7s ]\n", g_estimate ? "on" : "off", DEFAULT_ESTIMATE ? "on" : "off");
    Log("max texture memory  [ %7d ] [ %7d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);

    Log("max vis distance    [ %7d ] [ %7d ]\n", g_maxdistance, DEFAULT_MAXDISTANCE_RANGE);
    //Log("max dist only       [ %7s ] [ %7s ]\n", g_postcompile ? "on" : "off", DEFAULT_POST_COMPILE ? "on" : "off");

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
    Log("priority            [ %7s ] [ %7s ]\n", tmp, "Normal");
    Log("\n");

    // HLVIS Specific Settings
    Log("fast vis            [ %7s ] [ %7s ]\n", g_fastvis ? "on" : "off", DEFAULT_FASTVIS ? "on" : "off");
    Log("full vis            [ %7s ] [ %7s ]\n", g_fullvis ? "on" : "off", DEFAULT_FULLVIS ? "on" : "off");
    Log("\n\n");
}

int VisLeafnumForPoint(const vec3_t point)
{
    int nodenum = 0;
    while (nodenum >= 0)
    {
        dnode_t *node = &g_dnodes[nodenum];
        dplane_t *plane = &g_dplanes[node->planenum];
        vec_t dist = DotProduct(point, plane->normal) - plane->dist;
        if (dist >= 0.0)
        {
            nodenum = node->children[0];
        }
        else
        {
            nodenum = node->children[1];
        }
    }

    return -nodenum - 2;
}
// =====================================================================================
//  main
// =====================================================================================
int main(const int argc, char **argv)
{
    char portalfile[_MAX_PATH];
    char source[_MAX_PATH];
    const char *mapname_from_arg = NULL;

    g_Program = "hlvis";

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
            {
                Usage();
            }

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
                else if (!strcasecmp(argv[i], "-fast"))
                {
                    Log("g_fastvis = true\n");
                    g_fastvis = true;
                }
                else if (!strcasecmp(argv[i], "-full"))
                {
                    g_fullvis = true;
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

                else if (!strcasecmp(argv[i], "-noinfo"))
                {
                    g_info = false;
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
                else if (!strcasecmp(argv[i], "-nolog"))
                {
                    g_log = false;
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
                else if (!strcasecmp(argv[i], "-lightdata")) //lightdata
                {
                    if (i + 1 < argc) //--vluzacn
                    {
                        int x = std::atoi(argv[++i]) * 1024;

                        //if (x > g_max_map_lightdata) //--vluzacn
                        {
                            g_max_map_lightdata = x; //--vluzacn
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }

                // AJM: MVD
                else if (!strcasecmp(argv[i], "-maxdistance"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        g_maxdistance = std::abs(std::atoi(argv[++i]));
                    }
                    else
                    {
                        Usage();
                    }
                }
                /*		else if(!strcasecmp(argv[i], "-postcompile"))
		{
			g_postcompile = true;
		}*/
                else if (!strcasecmp(argv[i], "-lang"))
                {
                    if (i + 1 < argc)
                    {
                        char tmp[_MAX_PATH];

                        GetModuleFileName(NULL, tmp, _MAX_PATH);
                        LoadLangFile(argv[++i], tmp);
                    }
                    else
                    {
                        Usage();
                    }
                }

                else if (argv[i][0] == '-')
                {
                    Log("Unknown option \"%s\"", argv[i]);
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

            if (!mapname_from_arg)
            {
                Log("No mapfile specified\n");
                Usage();
            }

            safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
            FlipSlashes(g_Mapname);
            StripExtension(g_Mapname);
            OpenLog(g_clientid);
            atexit(CloseLog);
            ThreadSetDefault();
            ThreadSetPriority(g_threadpriority);
            LogStart(argcold, argvold);
            {
                int i;
                Log("Arguments: ");
                for (i = 1; i < argc; i++)
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
            atexit(dtexdata_free);
            // END INIT

            // BEGIN VIS
            double start = I_FloatTime();

            safe_strncpy(source, g_Mapname, _MAX_PATH);
            safe_strncat(source, ".bsp", _MAX_PATH);
            safe_strncpy(portalfile, g_Mapname, _MAX_PATH);
            safe_strncat(portalfile, ".prt", _MAX_PATH);

            LoadBSPFile(source);
            ParseEntities();
            {
                int i;
                for (i = 0; i < g_numentities; i++)
                {
                    if (!strcmp(ValueForKey(&g_entities[i], "classname"), "info_overview_point"))
                    {
                        if (g_overview_count < g_overview_max)
                        {
                            vec3_t p;
                            GetVectorForKey(&g_entities[i], "origin", p);
                            VectorCopy(p, g_overview[g_overview_count].origin);
                            g_overview[g_overview_count].visleafnum = VisLeafnumForPoint(p);
                            g_overview[g_overview_count].reverse = IntForKey(&g_entities[i], "reverse");
                            g_overview_count++;
                        }
                    }
                }
            }
            LoadPortalsByFilename(portalfile);

            Settings();
            g_uncompressed = (byte *)calloc(g_portalleafs, g_bitbytes);

            CalcVis();
            g_visdatasize = vismap_p - g_dvisdata;
            Log("g_visdatasize:%i  compressed from %i\n", g_visdatasize, originalvismapsize);

            if (g_chart)
            {
                PrintBSPFileSizes();
            }

            WriteBSPFile(source);

            double end = I_FloatTime();
            LogTimeElapsed(end - start);

            free(g_uncompressed);
            // END VIS
        }
    }

    return 0;
}
