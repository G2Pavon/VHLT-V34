#pragma once

#include "common/bspfile.h"

constexpr int MAX_POINTS_ON_FIXED_WINDING = 32;

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
} portalplane_t;

typedef enum
{
    stat_none,
    stat_working,
    stat_done
} vstatus_t;

typedef struct
{
    portalplane_t plane; // normal pointing into neighbor
    int leaf;            // neighbor
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
    portalplane_t plane; // from portal is on positive side
} sep_t;

typedef struct passage_s
{
    struct passage_s *next;
    int from, to; // leaf numbers
    sep_t *planes;
} passage_t;

constexpr int MAX_PORTALS_ON_LEAF = 256;
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

    const portalplane_t *portalplane;

    int clipPlaneCount;
    portalplane_t *clipPlane;
} pstack_t;

typedef struct
{
    byte *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t *base;
    pstack_t pstack_head;
} threaddata_t;

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

typedef struct
{
    bool isoverviewpoint;
    bool isskyboxpoint;
} leafinfo_t;

extern portal_t *g_portals;
extern leaf_t *g_leafs;

extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

void BasePortalVis(int threadnum);

void MaxDistVis(int threadnum);
//extern void		PostMaxDistVis(int threadnum);

void PortalFlow(portal_t *p);
void CalcAmbientSounds();