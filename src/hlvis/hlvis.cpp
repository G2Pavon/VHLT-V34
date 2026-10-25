#include "hlvis/hlvis.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "common/bspfile.h"
#include "common/cmdlib.h"
#include "common/cmdline.h"
#include "common/filelib.h"
#include "common/log.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/threads.h"
#include "common/winding.h"


static constexpr bool DEFAULT_FASTVIS = false;
static constexpr bool DEFAULT_ESTIMATE = false;
static constexpr bool DEFAULT_FULLVIS = false;
static constexpr bool DEFAULT_CHART = false;
static constexpr unsigned int DEFAULT_MAXDISTANCE_RANGE = 0;

static bool fastvis = DEFAULT_FASTVIS;
static const int overview_max = MAX_MAP_ENTITIES;
static int overview_count = 0;
static std::vector<leafinfo_t> leafinfos;
static int totalvis = 0;

static std::vector<int> leafstarts;
static std::vector<int> leafcounts;
static int leafcount_all;
static byte *vismap;
static byte *vismap_p;
static byte *vismap_end; // past visfile
static int originalvismapsize;
static std::vector<byte> uncompressed; // [bitbytes*portalleafs]
static overview_t overview[overview_max];

std::vector<portal_t> g_portals;
std::vector<leaf_t> g_leafs;
unsigned g_bitbytes; // (portalleafs+63)>>3
unsigned g_bitlongs;

int g_numportals = 0;
unsigned g_portalleafs = 0;
bool g_fullvis = DEFAULT_FULLVIS;
bool g_fastthreads = false;
bool g_estimate = DEFAULT_ESTIMATE;
bool g_chart = DEFAULT_CHART;
unsigned int g_maxdistance = DEFAULT_MAXDISTANCE_RANGE;

//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own version of this
//      to handle its own specific settings.

void GetParamsFromEnt(entity_t *mapent)
{
    Log("\nCompile Settings detected from info_compile_parameters entity\n");
    Log("%30s [ %-9s ]\n", "Compile Option", "setting");

    if (IntForKey(mapent, "estimate"))
    {
        g_estimate = true;
    }
    else
    {
        g_estimate = false;
    }
    Log("%30s [ %-9s ]\n", "Estimate Compile Times", g_estimate ? "on" : "off");

    int iTmp = IntForKey(mapent, "hlvis");
    if (iTmp == 0)
    {
        Fatal(assume_TOOL_CANCEL,
              "%s flag was not checked in info_compile_parameters entity, execution of %s cancelled", g_Program, g_Program);
        CheckFatal();
    }
    else if (iTmp == 1)
    {
        fastvis = true;
        g_fullvis = false;
    }
    else if (iTmp == 2)
    {
        fastvis = false;
        g_fullvis = false;
    }
    else if (iTmp == 3)
    {
        g_fullvis = true;
        fastvis = false;
    }
    Log("%30s [ %-9s ]\n", "Fast VIS", fastvis ? "on" : "off");
    Log("%30s [ %-9s ]\n", "Full VIS", g_fullvis ? "on" : "off");
    Log("\n");
}

static void PlaneFromWinding(winding_t *w, portalplane_t *plane)
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

std::vector<bitword_t> NewBitString()
{
    return std::vector<bitword_t>(g_bitlongs);
}

// Gives p a winding of `points` points, owned by p.
// (the size was computed as &((winding_t *)0)->points[points]: a null pointer dereference)
static winding_t *NewWinding(portal_t *p, const int points)
{
    if (points > MAX_POINTS_ON_WINDING)
    {
        Error("NewWinding: %i points > MAX_POINTS_ON_WINDING", points);
    }
    p->windingpoints = std::make_unique<vec3_t[]>(points > 0 ? points : 1);
    p->winding = std::make_unique<winding_t>();
    p->winding->points = p->windingpoints.get();
    return p->winding.get();
}

//      Returns the next portal for a thread to work on
//      Returns the portals from the least complex, so the later ones can reuse the earlier information.

static std::vector<portal_t *> s_order; // processing order: smallest nummightsee first, then index

// The order the portals were picked in: each pick took the unprocessed portal with the smallest
// nummightsee, the lowest index among equals (nummightsee is fixed once BasePortalVis has run).
// compat: the pick only considered nummightsee < 99999, which every portal has (<= MAX_MAP_LEAFS).
static void SortPortals()
{
    s_order.clear();
    for (portal_t &p : g_portals)
    {
        s_order.push_back(&p);
    }
    std::stable_sort(s_order.begin(), s_order.end(),
                     [](const portal_t *a, const portal_t *b) { return a->nummightsee < b->nummightsee; });
    for (std::size_t k = 0; k < s_order.size(); k++)
    {
        s_order[k]->order = static_cast<int>(k);
    }
}

// The next portal in s_order (GetThreadWork hands out 0, 1, 2, ... one at a time). This used to scan
// every portal under the lock for the smallest nummightsee still unprocessed: the same sequence.
static portal_t *GetNextPortal()
{
    const int k = GetThreadWork();
    if (k == -1)
    {
        return nullptr;
    }
    portal_t *p = s_order[k];
    p->status = stat_working;
    return p;
}

static void LeafThread(int /*threadnum*/)
{
    portal_t *p;

    while (true)
    {
        if (!(p = GetNextPortal()))
        {
            return;
        }

        PortalFlow(p);
    }
}

// Builds the entire visibility list for a leaf
static void LeafFlow(const int leafnum)
{
    byte compressed[MAX_MAP_LEAFS / 8];
    // flow through all portals, collecting visible bits
    std::memset(compressed, 0, sizeof(compressed));
    byte *outbuffer = uncompressed.data() + leafnum * g_bitbytes;
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
            const byte *src = BitBytes(p->visbits.data());
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

    if (leafinfos[leafnum].isoverviewpoint)
    {
        for (unsigned i = 0; i < g_portalleafs; i++)
        {
            outbuffer[i >> 3] |= (1 << (i & 7));
        }
    }
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        if (leafinfos[i].isskyboxpoint)
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
    // compress the bit string
    totalvis += numvis;

    byte buffer2[MAX_MAP_LEAFS / 8];
    int diskbytes = (leafcount_all + 7) >> 3;
    std::memset(buffer2, 0, diskbytes);
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        for (int j = 0; j < leafcounts[i]; j++)
        {
            int srcofs = i >> 3;
            int srcbit = 1 << (i & 7);
            int dstofs = (leafstarts[i] + j) >> 3;
            int dstbit = 1 << ((leafstarts[i] + j) & 7);
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

    for (int j = 0; j < leafcounts[leafnum]; j++)
    {
        g_dleafs[leafstarts[leafnum] + j + 1].visofs = dest - vismap;
    }

    std::memcpy(dest, compressed, i);
}

static void CalcPortalVis()
{
    // fastvis just uses mightsee for a very loose bound
    if (fastvis)
    {
        for (int i = 0; i < g_numportals * 2; i++)
        {
            g_portals[i].visbits = std::move(g_portals[i].mightsee); // only visbits is used from here on
            g_portals[i].status = stat_done;
        }
        return;
    }
    SortPortals();
    NamedRunThreadsOn(g_numportals * 2, g_estimate, LeafThread);
}

static void SaveVisData(const char *filename)
{
    UniqueFile file(std::fopen(filename, "wb"));
    std::FILE *fp = file.get();

    if (!fp)
        return;

    SafeWrite(fp, g_dvisdata, (vismap_p - g_dvisdata));

    // BUG BUG BUG!
    // Leaf offsets need to be saved too!!!!
    for (int i = 0; i < g_numleafs; i++)
    {
        SafeWrite(fp, &g_dleafs[i].visofs, sizeof(int));
    }
}

static void CalcVis()
{
    const std::string visdatafile = g_Mapname + ".vdt";

    // Remove this file
    RemoveFile(visdatafile);

    NamedRunThreadsOn(g_numportals * 2, g_estimate, BasePortalVis);

    // First do a normal VIS, save to file, then redo MaxDistVis

    CalcPortalVis();
    // assemble the leaf vis lists by oring and compressing the portal lists
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        LeafFlow(i);
    }

    // A .prt and BSP that both declare 0 leafs are consistent; the average used to divide by zero (SIGFPE).
    Log("average leafs visible: %i\n", g_portalleafs ? totalvis / g_portalleafs : 0u);

    if (g_maxdistance)
    {
        totalvis = 0;

        Log("saving visdata to %s...\n", visdatafile.c_str());
        SaveVisData(visdatafile.c_str());

        // We need to reset the uncompressed variable and portal visbits
        uncompressed.assign(uncompressed.size(), 0);

        vismap_p = g_dvisdata;

        // We don't need to run BasePortalVis again
        NamedRunThreadsOn(g_portalleafs, g_estimate, MaxDistVis);

        // No need to run this - MaxDistVis now writes directly to visbits after the initial VIS
        //CalcPortalVis();

        for (unsigned i = 0; i < g_portalleafs; i++)
        {
            LeafFlow(i);
        }

        Log("average maxdistance leafs visible: %i\n", g_portalleafs ? totalvis / g_portalleafs : 0u);
    }
}

// The next token of the .prt text (separators " ()\r\n\t", as strtok used them), or nullptr.
static const char *NextToken(char *&cursor)
{
    static constexpr const char *separators = " ()\r\n\t";
    cursor += std::strspn(cursor, separators);
    if (!*cursor)
    {
        return nullptr;
    }
    char *token = cursor;
    cursor += std::strcspn(cursor, separators);
    if (*cursor)
    {
        *cursor++ = '\0';
    }
    return token;
}

static void CheckNullToken(const char *const token)
{
    if (token == nullptr)
    {
        Error("LoadPortals: Damaged or invalid .prt file\n");
    }
}

static void LoadPortals(char *portal_image)
{
    char *cursor = portal_image;
    const char *token = NextToken(cursor);
    CheckNullToken(token);
    if (!std::sscanf(token, "%u", &g_portalleafs))
    {
        Error("LoadPortals: failed to read header: number of leafs");
    }

    token = NextToken(cursor);
    CheckNullToken(token);
    if (!std::sscanf(token, "%i", &g_numportals))
    {
        Error("LoadPortals: failed to read header: number of portals");
    }
    if (g_numportals < 0) // was used as an allocation size
    {
        Error("LoadPortals: %i portals", g_numportals);
    }

    Log("%4i portalleafs\n", g_portalleafs);
    Log("%4i numportals\n", g_numportals);

    g_bitbytes = ((g_portalleafs + 63) & ~63) >> 3;
    g_bitlongs = g_bitbytes / sizeof(bitword_t); // g_bitbytes is a multiple of 8

    // each file portal is split into two memory portals
    g_portals = std::vector<portal_t>(2 * static_cast<std::size_t>(g_numportals));
    g_leafs.assign(g_portalleafs, leaf_t{});
    leafinfos.assign(g_portalleafs, leafinfo_t{});
    leafcounts.assign(g_portalleafs, 0);
    leafstarts.assign(g_portalleafs, 0);

    originalvismapsize = g_portalleafs * ((g_portalleafs + 7) / 8);

    vismap = vismap_p = g_dvisdata;
    vismap_end = vismap + MAX_MAP_VISIBILITY;

    if (g_portalleafs > MAX_MAP_LEAFS)
    { // this may cause hlvis to overflow, because numportalleafs can be larger than g_numleafs in some special cases
        Error("Too many portalleafs (g_portalleafs(%d) > MAX_MAP_LEAFS(%d)).", g_portalleafs, MAX_MAP_LEAFS);
    }
    leafcount_all = 0;
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        unsigned rval = 0;
        token = NextToken(cursor);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &leafcounts[i]);
        if (rval != 1)
        {
            Error("LoadPortals: read leaf %i failed", i);
        }
        if (leafcounts[i] < 0) // a negative count made LeafFlow loop over unsigned wrap-around
        {
            Error("LoadPortals: leaf %i has %i leafs", i, leafcounts[i]);
        }
        leafstarts[i] = leafcount_all;
        leafcount_all += leafcounts[i];
    }
    if (leafcount_all != g_dmodels[0].visleafs)
    { // internal error (this should never happen)
        Error("Corrupted leaf mapping (leafcount_all(%d) != g_dmodels[0].visleafs(%d)).", leafcount_all, g_dmodels[0].visleafs);
    }
    for (unsigned i = 0; i < g_portalleafs; i++)
    {
        for (int j = 0; j < overview_count; j++)
        {
            int d = overview[j].visleafnum - leafstarts[i];
            if (0 <= d && d < leafcounts[i])
            {
                if (overview[j].reverse)
                {
                    leafinfos[i].isskyboxpoint = true;
                }
                else
                {
                    leafinfos[i].isoverviewpoint = true;
                }
            }
        }
    }

    int i;
    portal_t *p;
    int numpoints;
    int leafnums[2];
    for (i = 0, p = g_portals.data(); i < g_numportals; i++)
    {
        unsigned rval = 0;

        token = NextToken(cursor);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &numpoints);
        token = NextToken(cursor);
        CheckNullToken(token);
        rval += std::sscanf(token, "%i", &leafnums[0]);
        token = NextToken(cursor);
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
        if (numpoints < 3) // PlaneFromWinding reads three points; a negative count was an allocation size
        {
            Error("LoadPortals: portal %i has %i points", i, numpoints);
        }
        // compat: the message; the check was `>`, letting leaf g_portalleafs index past g_leafs
        if (((unsigned)leafnums[0] >= g_portalleafs) || ((unsigned)leafnums[1] >= g_portalleafs))
        {
            Error("LoadPortals: reading portal %i", i);
        }

        winding_t *w = NewWinding(p, numpoints);
        w->original = true;
        w->numpoints = numpoints;

        for (int j = 0; j < numpoints; j++)
        {
            double v[3];
            unsigned rval = 0;

            token = NextToken(cursor);
            CheckNullToken(token);
            rval += std::sscanf(token, "%lf", &v[0]);
            token = NextToken(cursor);
            CheckNullToken(token);
            rval += std::sscanf(token, "%lf", &v[1]);
            token = NextToken(cursor);
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

        portalplane_t plane;
        PlaneFromWinding(w, &plane);

        // create forward portal
        leaf_t *l = &g_leafs[leafnums[0]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        VectorSubtract(vec3_origin, plane.normal, p->plane.normal);
        p->plane.dist = -plane.dist;
        p->leaf = leafnums[1];
        p++;

        // create backwards portal
        l = &g_leafs[leafnums[1]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        NewWinding(p, w->numpoints);
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

static void LoadPortalsByFilename(const char *const filename)
{
    if (!q_exists(filename))
    {
        Error("Portal file '%s' does not exist, cannot vis the map\n", filename);
    }
    std::vector<char> file_image = ReadFile(filename);
    file_image.push_back('\0'); // LoadPortals tokenizes it in place, as a C string
    LoadPortals(file_image.data());
}

[[noreturn]] static void Usage()
{
    Banner();

    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -full           : Full vis\n");
    Log("    -fast           : Fast vis\n\n");
    Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
    Log("    -lightdata #      : Alter maximum lighting memory limit (in kb)\n"); //lightdata //--vluzacn
    Log("    -chart          : display bsp statitics\n");
    Log("    -low | -high    : run program an altered priority level\n");
    Log("    -threads #      : manually specify the number of threads to run\n");
    Log("    -estimate       : display estimated time during compile\n");
    Log("    -maxdistance #  : Alter the maximum distance for visibility\n");
    Log("    -fastthreads    : Faster with many threads, but the result can differ from run to run\n");
    Log("    mapfile         : The mapfile to compile\n\n");
    std::exit(1);
}

static void Settings()
{
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
    Log("chart               [ %7s ] [ %7s ]\n", g_chart ? "on" : "off", DEFAULT_CHART ? "on" : "off");
    Log("estimate            [ %7s ] [ %7s ]\n", g_estimate ? "on" : "off", DEFAULT_ESTIMATE ? "on" : "off");
    Log("max texture memory  [ %7d ] [ %7d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);

    // HLVIS Specific Settings
    Log("max vis distance    [ %7d ] [ %7d ]\n", g_maxdistance, DEFAULT_MAXDISTANCE_RANGE);
    Log("fast vis            [ %7s ] [ %7s ]\n", fastvis ? "on" : "off", DEFAULT_FASTVIS ? "on" : "off");
    Log("full vis            [ %7s ] [ %7s ]\n", g_fullvis ? "on" : "off", DEFAULT_FULLVIS ? "on" : "off");
    if (g_fastthreads) // only shown when set: the default settings table is unchanged
    {
        Log("fast threads        [ %7s ] [ %7s ]\n", "on", "off");
    }
    Log("\n\n");
}

static int VisLeafnumForPoint(const vec3_t point)
{
    if (g_numnodes == 0)
    {
        return -1; // no world nodes: the walk below never ended (node 0 is all zeros)
    }
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

int main(const int argc, char **argv)
{
    const char *mapname_from_arg = nullptr;

    g_Program = "hlvis";

    if (argc == 1)
    {
        Usage();
    }

    const CommandLineOption options[] = {
        {"-threads", [](const char *value) {
             g_numthreads = ParseInt(value);
             if (g_numthreads < 1)
             {
                 Log("Expected value of at least 1 for '-threads'\n");
                 Usage();
             }
         },
         true},
        CommandLineFlag("-estimate", g_estimate, true),
        {"-fast", [](const char *) {
             Log("fastvis = true\n");
             fastvis = true;
         }},
        CommandLineFlag("-full", g_fullvis, true),
        CommandLineFlag("-chart", g_chart, true),
        CommandLineFlag("-fastthreads", g_fastthreads, true),
        {"-texdata", [](const char *value) { g_max_map_miptex = KilobytesToBytes(value); }, true},
        {"-lightdata", [](const char *value) { g_max_map_lightdata = KilobytesToBytes(value); }, true},
        {"-maxdistance", [](const char *value) {
             // compat: abs of the int (std::abs(INT_MIN) was undefined; it gave 2^31 in practice)
             g_maxdistance = static_cast<unsigned int>(std::llabs(ParseInt(value)));
         },
         true},
    };
    // compat: an unknown '-' option is logged without a newline (a second map name with one)
    mapname_from_arg = ParseCommandLine(argc, argv, options, Usage, "Unknown option \"%s\"");

    if (!mapname_from_arg)
    {
        Log("No mapfile specified\n");
        Usage();
    }

    SetMapname(mapname_from_arg);
    OpenLog();
    std::atexit(CloseLog);
    ThreadSetDefault();
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

    // BEGIN VIS
    double start = I_FloatTime();

    const std::string source = g_Mapname + ".bsp";
    const std::string portalfile = g_Mapname + ".prt";

    LoadBSPFile(source.c_str());
    ParseEntities(GetParamsFromEnt);
    {
        for (int i = 0; i < g_numentities; i++)
        {
            if (!std::strcmp(ValueForKey(&g_entities[i], "classname"), "info_overview_point"))
            {
                if (overview_count < overview_max)
                {
                    vec3_t p;
                    GetVectorForKey(&g_entities[i], "origin", p);
                    VectorCopy(p, overview[overview_count].origin);
                    overview[overview_count].visleafnum = VisLeafnumForPoint(p);
                    overview[overview_count].reverse = IntForKey(&g_entities[i], "reverse");
                    overview_count++;
                }
            }
        }
    }
    LoadPortalsByFilename(portalfile.c_str());

    Settings();
    uncompressed.assign(static_cast<std::size_t>(g_portalleafs) * g_bitbytes, 0);

    CalcVis();
    g_visdatasize = vismap_p - g_dvisdata;
    Log("g_visdatasize:%i  compressed from %i\n", g_visdatasize, originalvismapsize);

    if (g_chart)
    {
        PrintBSPFileSizes();
    }

    WriteBSPFile(source.c_str());

    double end = I_FloatTime();
    LogTimeElapsed(end - start);

    // END VIS

    return 0;
}