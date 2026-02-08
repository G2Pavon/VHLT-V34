#pragma once

#include "common/bspfile.h"

constexpr int MAX_POINTS_ON_FIXED_WINDING = 32;

struct winding_t
{
    bool original; // don't free, it's part of the portal
    int numpoints;
    vec3_t points[MAX_POINTS_ON_FIXED_WINDING];
};

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
    portalplane_t plane; // normal pointing into neighbor
    int leaf;            // neighbor
    winding_t *winding;
    vstatus_t status;
    byte *visbits;
    byte *mightsee;
    unsigned nummightsee;
    int numcansee;
};

struct sep_t
{
    sep_t *next;
    portalplane_t plane; // from portal is on positive side
};

struct passage_t
{
    passage_t *next;
    int from, to; // leaf numbers
    sep_t *planes;
};

constexpr int MAX_PORTALS_ON_LEAF = 256;
struct leaf_t
{
    unsigned numportals;
    passage_t *passages;
    portal_t *portals[MAX_PORTALS_ON_LEAF];
};

struct pstack_t
{
    byte mightsee[MAX_MAP_LEAFS / 8]; // bit string
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
    byte *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t *base;
    pstack_t pstack_head;
};

extern bool g_fullvis;

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

extern portal_t *g_portals;
extern leaf_t *g_leafs;

extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

void BasePortalVis(int threadnum);

void MaxDistVis(int threadnum);
//extern void		PostMaxDistVis(int threadnum);

void PortalFlow(portal_t *p);
void CalcAmbientSounds();