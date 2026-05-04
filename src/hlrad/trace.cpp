#include <cstdlib>
#include <cstring>
#include <cmath>

#include "common/bspfile.h"
#include "common/cmdlib.h"
#include "hlrad/hlrad.h"
#include "common/log.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/winding.h"

// #define      ON_EPSILON      0.001

typedef struct tnode_s
{
    planetypes type;
    vec3_t normal;
    float dist;
    int children[2];
    int pad;
} tnode_t;

static tnode_t *tnodes;
static tnode_t *tnode_p;

/*
 * ==============
 * MakeTnode
 * 
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void MakeTnode(const int nodenum)
{
    tnode_t *t = tnode_p++;

    dnode_t *node = g_dnodes + nodenum;
    dplane_t *plane = g_dplanes + node->planenum;

    t->type = plane->type;
    VectorCopy(plane->normal, t->normal);
    if (plane->normal[(plane->type) % 3] < 0)
        if (plane->type < 3)
        {
            Warning("MakeTnode: negative plane");
        }
    t->dist = plane->dist;

    for (int i = 0; i < 2; i++)
    {
        if (node->children[i] < 0)
            t->children[i] = g_dleafs[-node->children[i] - 1].contents;
        else
        {
            t->children[i] = tnode_p - tnodes;
            MakeTnode(node->children[i]);
        }
    }
}

/*
 * =============
 * MakeTnodes
 * 
 * Loads the node structure out of a .bsp file to be used for light occlusion
 * =============
 */
void MakeTnodes(dmodel_t * /*bm*/)
{
    // 32 byte align the structs
    tnodes = (tnode_t *)std::calloc((g_numnodes + 1), sizeof(tnode_t));

    // The alignment doesn't have any effect at all. --vluzacn
    int ofs = 31 - (int)(((uintptr_t)tnodes + (uintptr_t)31) & (uintptr_t)31);
    tnodes = (tnode_t *)((byte *)tnodes + ofs);
    tnode_p = tnodes;

    MakeTnode(0);
}

//==========================================================

static int TestLine_r(const int node, const vec3_t start, const vec3_t stop, int &linecontent, vec_t *skyhit)
{
    float front, back;
    vec3_t mid;

    if (node < 0)
    {
        if (node == linecontent)
            return CONTENTS_EMPTY;
        if (node == CONTENTS_SOLID)
        {
            return CONTENTS_SOLID;
        }
        if (node == CONTENTS_SKY)
        {
            if (skyhit)
            {
                VectorCopy(start, skyhit);
            }
            return CONTENTS_SKY;
        }
        if (linecontent)
        {
            return CONTENTS_SOLID;
        }
        linecontent = node;
        return CONTENTS_EMPTY;
    }

    tnode_t *tnode = &tnodes[node];
    switch (tnode->type)
    {
    case plane_x:
        front = start[0] - tnode->dist;
        back = stop[0] - tnode->dist;
        break;
    case plane_y:
        front = start[1] - tnode->dist;
        back = stop[1] - tnode->dist;
        break;
    case plane_z:
        front = start[2] - tnode->dist;
        back = stop[2] - tnode->dist;
        break;
    default:
        front = (start[0] * tnode->normal[0] + start[1] * tnode->normal[1] + start[2] * tnode->normal[2]) - tnode->dist;
        back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1] + stop[2] * tnode->normal[2]) - tnode->dist;
        break;
    }

    if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
    {
        return TestLine_r(tnode->children[0], start, stop, linecontent, skyhit);
    }
    if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
    {
        return TestLine_r(tnode->children[1], start, stop, linecontent, skyhit);
    }
    if (std::abs(front) <= ON_EPSILON && std::abs(back) <= ON_EPSILON)
    {
        int r1 = TestLine_r(tnode->children[0], start, stop, linecontent, skyhit);
        if (r1 == CONTENTS_SOLID)
            return CONTENTS_SOLID;
        int r2 = TestLine_r(tnode->children[1], start, stop, linecontent, skyhit);
        if (r2 == CONTENTS_SOLID)
            return CONTENTS_SOLID;
        if (r1 == CONTENTS_SKY || r2 == CONTENTS_SKY)
            return CONTENTS_SKY;
        return CONTENTS_EMPTY;
    }
    int side = (front - back) < 0;
    float frac = front / (front - back);
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    mid[0] = start[0] + (stop[0] - start[0]) * frac;
    mid[1] = start[1] + (stop[1] - start[1]) * frac;
    mid[2] = start[2] + (stop[2] - start[2]) * frac;
    int r = TestLine_r(tnode->children[side], start, mid, linecontent, skyhit);
    if (r != CONTENTS_EMPTY)
        return r;
    return TestLine_r(tnode->children[!side], mid, stop, linecontent, skyhit);
}

int TestLine(const vec3_t start, const vec3_t stop, vec_t *skyhit)
{
    int linecontent = 0;
    return TestLine_r(0, start, stop, linecontent, skyhit);
}

typedef struct
{
    Winding *winding;
    dplane_t plane;
    int numedges;
    dplane_t *edges;
    int texinfo;
    bool tex_alphatest;
    vec_t tex_vecs[2][4];
    int tex_width;
    int tex_height;
    const byte *tex_canvas;
} opaqueface_t;
opaqueface_t *opaquefaces;

typedef struct opaquenode_s
{
    planetypes type;
    vec3_t normal;
    vec_t dist;
    int children[2];
    int firstface;
    int numfaces;
} opaquenode_t;
opaquenode_t *opaquenodes;

opaquemodel_t *opaquemodels;

static bool TryMerge(opaqueface_t *f, const opaqueface_t *f2)
{
    if (!f->winding || !f2->winding)
    {
        return false;
    }
    if (std::abs(f2->plane.dist - f->plane.dist) > ON_EPSILON || std::abs(f2->plane.normal[0] - f->plane.normal[0]) > NORMAL_EPSILON || std::abs(f2->plane.normal[1] - f->plane.normal[1]) > NORMAL_EPSILON || std::abs(f2->plane.normal[2] - f->plane.normal[2]) > NORMAL_EPSILON)
    {
        return false;
    }
    if ((f->tex_alphatest || f2->tex_alphatest) && f->texinfo != f2->texinfo)
    {
        return false;
    }

    Winding *w = f->winding;
    const Winding *w2 = f2->winding;
    const vec_t *pA, *pB, *pD, *p2A, *p2B, *p2D;
    int i, i2;

    for (i = 0; i < w->m_NumPoints; i++)
    {
        for (i2 = 0; i2 < w2->m_NumPoints; i2++)
        {
            pA = w->m_Points[(i + w->m_NumPoints - 1) % w->m_NumPoints];
            pB = w->m_Points[i];
            const vec_t *pC = w->m_Points[(i + 1) % w->m_NumPoints];
            pD = w->m_Points[(i + 2) % w->m_NumPoints];
            p2A = w2->m_Points[(i2 + w2->m_NumPoints - 1) % w2->m_NumPoints];
            p2B = w2->m_Points[i2];
            const vec_t *p2C = w2->m_Points[(i2 + 1) % w2->m_NumPoints];
            p2D = w2->m_Points[(i2 + 2) % w2->m_NumPoints];
            if (!VectorCompare(pB, p2C) || !VectorCompare(pC, p2B))
            {
                continue;
            }
            break;
        }
        if (i2 == w2->m_NumPoints)
        {
            continue;
        }
        break;
    }
    if (i == w->m_NumPoints)
    {
        return false;
    }

    const vec_t *normal = f->plane.normal;
    vec3_t e1, e2;
    dplane_t pl1, pl2;

    VectorSubtract(p2D, pA, e1);
    CrossProduct(normal, e1, pl1.normal);   // pointing outward
    if (VectorNormalize(pl1.normal) == 0.0) // Empty edge
    {
        return false;
    }
    pl1.dist = DotProduct(pA, pl1.normal);
    if (DotProduct(pB, pl1.normal) - pl1.dist < -ON_EPSILON)
    {
        return false;
    }
    int side1 = (DotProduct(pB, pl1.normal) - pl1.dist > ON_EPSILON) ? 1 : 0;

    VectorSubtract(pD, p2A, e2);
    CrossProduct(normal, e2, pl2.normal);   // pointing outward
    if (VectorNormalize(pl2.normal) == 0.0) // Emtpy edge
    {
        return false;
    }
    pl2.dist = DotProduct(p2A, pl2.normal);
    if (DotProduct(p2B, pl2.normal) - pl2.dist < -ON_EPSILON)
    {
        return false;
    }
    int side2 = (DotProduct(p2B, pl2.normal) - pl2.dist > ON_EPSILON) ? 1 : 0;

    Winding *neww = new Winding(w->m_NumPoints + w2->m_NumPoints - 4 + side1 + side2);
    int j;
    int k = 0;
    for (j = (i + 2) % w->m_NumPoints; j != i; j = (j + 1) % w->m_NumPoints)
    {
        VectorCopy(w->m_Points[j], neww->m_Points[k]);
        k++;
    }
    if (side1)
    {
        VectorCopy(w->m_Points[j], neww->m_Points[k]);
        k++;
    }
    for (j = (i2 + 2) % w2->m_NumPoints; j != i2; j = (j + 1) % w2->m_NumPoints)
    {
        VectorCopy(w2->m_Points[j], neww->m_Points[k]);
        k++;
    }
    if (side2)
    {
        VectorCopy(w2->m_Points[j], neww->m_Points[k]);
        k++;
    }
    neww->RemoveColinearPoints();
    if (neww->m_NumPoints < 3) // Empty winding
    {
        delete neww;
        neww = nullptr;
    }
    delete f->winding;
    f->winding = neww;
    return true;
}

static int MergeOpaqueFaces(int firstface, int numfaces)
{
    int i, j;
    opaqueface_t *faces = &opaquefaces[firstface];
    for (i = 0; i < numfaces; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (TryMerge(&faces[i], &faces[j]))
            {
                delete faces[j].winding;
                faces[j].winding = nullptr;
                j = -1;
                continue;
            }
        }
    }
    for (i = 0, j = 0; i < numfaces; i++)
    {
        if (faces[i].winding)
        {
            faces[j] = faces[i];
            j++;
        }
    }
    int newnum = j;
    for (; j < numfaces; j++)
    {
        std::memset(&faces[j], 0, sizeof(opaqueface_t));
    }
    return newnum;
}

static void BuildFaceEdges(opaqueface_t *f)
{
    if (!f->winding)
        return;
    f->numedges = f->winding->m_NumPoints;
    f->edges = (dplane_t *)std::calloc(f->numedges, sizeof(dplane_t));
    const vec_t *n = f->plane.normal;
    vec3_t e;
    for (int x = 0; x < f->winding->m_NumPoints; x++)
    {
        const vec_t *p1 = f->winding->m_Points[x];
        const vec_t *p2 = f->winding->m_Points[(x + 1) % f->winding->m_NumPoints];
        dplane_t *pl = &f->edges[x];
        VectorSubtract(p2, p1, e);
        CrossProduct(n, e, pl->normal);
        if (VectorNormalize(pl->normal) == 0.0) // Empty edge
        {
            VectorClear(pl->normal);
            pl->dist = -1;
            continue;
        }
        pl->dist = DotProduct(pl->normal, p1);
    }
}

void CreateOpaqueNodes()
{
    opaquemodels = (opaquemodel_t *)std::calloc(g_nummodels, sizeof(opaquemodel_t));
    opaquenodes = (opaquenode_t *)std::calloc(g_numnodes, sizeof(opaquenode_t));
    opaquefaces = (opaqueface_t *)std::calloc(g_numfaces, sizeof(opaqueface_t));
    for (int i = 0; i < g_numfaces; i++)
    {
        opaqueface_t *of = &opaquefaces[i];
        dface_t *df = &g_dfaces[i];
        of->winding = new Winding(*df);
        if (of->winding->m_NumPoints < 3)
        {
            delete of->winding;
            of->winding = nullptr;
        }
        of->plane = g_dplanes[df->planenum];
        if (df->side)
        {
            VectorInverse(of->plane.normal);
            of->plane.dist = -of->plane.dist;
        }
        of->texinfo = df->texinfo;
        texinfo_t *info = &g_texinfo[of->texinfo];
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                of->tex_vecs[j][k] = info->vecs[j][k];
            }
        }
        radtexture_t *tex = &g_textures[info->miptex];
        of->tex_alphatest = tex->name[0] == '{';
        of->tex_width = tex->width;
        of->tex_height = tex->height;
        of->tex_canvas = tex->canvas;
    }
    for (int i = 0; i < g_numnodes; i++)
    {
        opaquenode_t *on = &opaquenodes[i];
        dnode_t *dn = &g_dnodes[i];
        on->type = g_dplanes[dn->planenum].type;
        VectorCopy(g_dplanes[dn->planenum].normal, on->normal);
        on->dist = g_dplanes[dn->planenum].dist;
        on->children[0] = dn->children[0];
        on->children[1] = dn->children[1];
        on->firstface = dn->firstface;
        on->numfaces = dn->numfaces;
        on->numfaces = MergeOpaqueFaces(on->firstface, on->numfaces);
    }
    for (int i = 0; i < g_numfaces; i++)
    {
        BuildFaceEdges(&opaquefaces[i]);
    }
    for (int i = 0; i < g_nummodels; i++)
    {
        opaquemodel_t *om = &opaquemodels[i];
        dmodel_t *dm = &g_dmodels[i];
        om->headnode = dm->headnode[0];
        for (int j = 0; j < 3; j++)
        {
            om->mins[j] = dm->mins[j] - 1;
            om->maxs[j] = dm->maxs[j] + 1;
        }
    }
}

void DeleteOpaqueNodes()
{
    for (int i = 0; i < g_numfaces; i++)
    {
        opaqueface_t *of = &opaquefaces[i];
        if (of->winding)
            delete of->winding;
        if (of->edges)
            std::free(of->edges);
    }
    std::free(opaquefaces);
    std::free(opaquenodes);
    std::free(opaquemodels);
}

static int TestLineOpaque_face(int facenum, const vec3_t hit)
{
    opaqueface_t *thisface = &opaquefaces[facenum];
    if (thisface->numedges == 0) // Empty face
    {
        return 0;
    }
    for (int x = 0; x < thisface->numedges; x++)
    {
        if (DotProduct(hit, thisface->edges[x].normal) - thisface->edges[x].dist > ON_EPSILON)
        {
            return 0;
        }
    }
    if (thisface->tex_alphatest)
    {
        double x = DotProduct(hit, thisface->tex_vecs[0]) + thisface->tex_vecs[0][3];
        double y = DotProduct(hit, thisface->tex_vecs[1]) + thisface->tex_vecs[1][3];
        x = std::floor(x - thisface->tex_width * std::floor(x / thisface->tex_width));
        y = std::floor(y - thisface->tex_height * std::floor(y / thisface->tex_height));
        x = x > thisface->tex_width - 1 ? thisface->tex_width - 1 : x < 0 ? 0
                                                                          : x;
        y = y > thisface->tex_height - 1 ? thisface->tex_height - 1 : y < 0 ? 0
                                                                            : y;
        if (thisface->tex_canvas[(int)y * thisface->tex_width + (int)x] == 0xFF)
        {
            return 0;
        }
    }
    return 1;
}

static int TestLineOpaque_r(int nodenum, const vec3_t start, const vec3_t stop)
{
    vec_t front, back;
    if (nodenum < 0)
    {
        return 0;
    }
    opaquenode_t *thisnode = &opaquenodes[nodenum];
    switch (thisnode->type)
    {
    case plane_x:
        front = start[0] - thisnode->dist;
        back = stop[0] - thisnode->dist;
        break;
    case plane_y:
        front = start[1] - thisnode->dist;
        back = stop[1] - thisnode->dist;
        break;
    case plane_z:
        front = start[2] - thisnode->dist;
        back = stop[2] - thisnode->dist;
        break;
    default:
        front = DotProduct(start, thisnode->normal) - thisnode->dist;
        back = DotProduct(stop, thisnode->normal) - thisnode->dist;
    }
    if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
    {
        return TestLineOpaque_r(thisnode->children[0], start, stop);
    }
    if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
    {
        return TestLineOpaque_r(thisnode->children[1], start, stop);
    }
    if (std::abs(front) <= ON_EPSILON && std::abs(back) <= ON_EPSILON)
    {
        return TestLineOpaque_r(thisnode->children[0], start, stop) || TestLineOpaque_r(thisnode->children[1], start, stop);
    }
    {
        vec3_t mid;
        int side = (front - back) < 0;
        vec_t frac = front / (front - back);
        if (frac < 0)
            frac = 0;
        if (frac > 1)
            frac = 1;
        mid[0] = start[0] + (stop[0] - start[0]) * frac;
        mid[1] = start[1] + (stop[1] - start[1]) * frac;
        mid[2] = start[2] + (stop[2] - start[2]) * frac;
        for (int facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
        {
            if (TestLineOpaque_face(facenum, mid))
            {
                return 1;
            }
        }
        return TestLineOpaque_r(thisnode->children[side], start, mid) || TestLineOpaque_r(thisnode->children[!side], mid, stop);
    }
}

int TestLineOpaque(int modelnum, const vec3_t modelorigin, const vec3_t start, const vec3_t stop)
{
    opaquemodel_t *thismodel = &opaquemodels[modelnum];
    vec3_t p1, p2;
    VectorSubtract(start, modelorigin, p1);
    VectorSubtract(stop, modelorigin, p2);

    for (int axial = 0; axial < 3; axial++)
    {
        vec_t front = p1[axial] - thismodel->maxs[axial];
        vec_t back = p2[axial] - thismodel->maxs[axial];
        if (front >= -ON_EPSILON && back >= -ON_EPSILON)
        {
            return 0;
        }
        if (front > ON_EPSILON || back > ON_EPSILON)
        {
            vec_t frac = front / (front - back);
            if (front > back)
            {
                p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
            else
            {
                p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
        }
        front = thismodel->mins[axial] - p1[axial];
        back = thismodel->mins[axial] - p2[axial];
        if (front >= -ON_EPSILON && back >= -ON_EPSILON)
        {
            return 0;
        }
        if (front > ON_EPSILON || back > ON_EPSILON)
        {
            vec_t frac = front / (front - back);
            if (front > back)
            {
                p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
            else
            {
                p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
        }
    }
    return TestLineOpaque_r(thismodel->headnode, p1, p2);
}

static int CountOpaqueFaces_r(opaquenode_t *node)
{
    int count = node->numfaces;
    if (node->children[0] >= 0)
    {
        count += CountOpaqueFaces_r(&opaquenodes[node->children[0]]);
    }
    if (node->children[1] >= 0)
    {
        count += CountOpaqueFaces_r(&opaquenodes[node->children[1]]);
    }
    return count;
}

int CountOpaqueFaces(int modelnum)
{
    return CountOpaqueFaces_r(&opaquenodes[opaquemodels[modelnum].headnode]);
}

int TestPointOpaque_r(int nodenum, bool solid, const vec3_t point)
{
    opaquenode_t *thisnode;
    vec_t dist;
    while (1)
    {
        if (nodenum < 0)
        {
            if (solid && g_dleafs[-nodenum - 1].contents == CONTENTS_SOLID)
                return 1;
            else
                return 0;
        }
        thisnode = &opaquenodes[nodenum];
        switch (thisnode->type)
        {
        case plane_x:
            dist = point[0] - thisnode->dist;
            break;
        case plane_y:
            dist = point[1] - thisnode->dist;
            break;
        case plane_z:
            dist = point[2] - thisnode->dist;
            break;
        default:
            dist = DotProduct(point, thisnode->normal) - thisnode->dist;
        }
        if (dist > HUNT_WALL_EPSILON)
        {
            nodenum = thisnode->children[0];
        }
        else if (dist < -HUNT_WALL_EPSILON)
        {
            nodenum = thisnode->children[1];
        }
        else
        {
            break;
        }
    }
    {
        for (int facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
        {
            if (TestLineOpaque_face(facenum, point))
            {
                return 1;
            }
        }
    }
    return TestPointOpaque_r(thisnode->children[0], solid, point) || TestPointOpaque_r(thisnode->children[1], solid, point);
}