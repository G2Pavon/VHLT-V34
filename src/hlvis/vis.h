#pragma once

#include "bspfile.h"

#define DEFAULT_MAXDISTANCE_RANGE 0

#define DEFAULT_FULLVIS false
#define DEFAULT_CHART false
#define DEFAULT_INFO true
#define DEFAULT_ESTIMATE false

#define DEFAULT_FASTVIS false
#define DEFAULT_NETVIS_PORT 21212
#define DEFAULT_NETVIS_RATE 60

#define MAX_PORTALS 32768

#define PORTALFILE "PRT1" // WTF?

#define MAX_POINTS_ON_FIXED_WINDING 32

typedef struct
{
    bool original; // don't free, it's part of the portal
    int numpoints;
    vec3_t points[MAX_POINTS_ON_FIXED_WINDING];
} winding_t;

typedef struct
{
    vec3_t normal;
    float dist;
} plane_t;

typedef enum
{
    stat_none,
    stat_working,
    stat_done
} vstatus_t;

typedef struct
{
    plane_t plane; // normal pointing into neighbor
    int leaf;      // neighbor
    winding_t *winding;
    vstatus_t status;
    byte *visbits;
    byte *mightsee;
    unsigned nummightsee;
    int numcansee;
} portal_t;

typedef struct seperating_plane_s
{
    struct seperating_plane_s *next;
    plane_t plane; // from portal is on positive side
} sep_t;

typedef struct passage_s
{
    struct passage_s *next;
    int from, to; // leaf numbers
    sep_t *planes;
} passage_t;

#define MAX_PORTALS_ON_LEAF 256
typedef struct leaf_s
{
    unsigned numportals;
    passage_t *passages;
    portal_t *portals[MAX_PORTALS_ON_LEAF];
} leaf_t;

typedef struct pstack_s
{
    byte mightsee[MAX_MAP_LEAFS / 8]; // bit string
    struct pstack_s *head;

    leaf_t *leaf;
    portal_t *portal; // portal exiting
    winding_t *source;
    winding_t *pass;

    winding_t windings[3]; // source, pass, temp in any order
    char freewindings[3];

    const plane_t *portalplane;

    int clipPlaneCount;
    plane_t *clipPlane;
} pstack_t;

typedef struct
{
    byte *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t *base;
    pstack_t pstack_head;
} threaddata_t;

extern bool g_fastvis;
extern bool g_fullvis;

extern int g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;
//extern bool		g_postcompile;
typedef struct
{
    vec3_t origin;
    int visleafnum;
    int reverse;
} overview_t;
extern const int g_overview_max;
extern overview_t g_overview[];
extern int g_overview_count;

typedef struct
{
    bool isoverviewpoint;
    bool isskyboxpoint;
} leafinfo_t;
extern leafinfo_t *g_leafinfos;

extern portal_t *g_portals;
extern leaf_t *g_leafs;

extern byte *g_uncompressed;
extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

extern volatile int g_vislocalpercent;

extern void BasePortalVis(int threadnum);

extern void MaxDistVis(int threadnum);
//extern void		PostMaxDistVis(int threadnum);

extern void PortalFlow(portal_t *p);
extern void CalcAmbientSounds();