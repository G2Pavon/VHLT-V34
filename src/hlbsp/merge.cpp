#include <cmath>

#include "bsp5.h"
#include "log.h"
#include "mathtypes.h"
#include "mathlib.h"

//  TryMerge
//  MergeFaceToList
//  FreeMergeListScraps
//  MergePlaneFaces
//  MergeAll

#define CONTINUOUS_EPSILON ON_EPSILON

// =====================================================================================
//  TryMerge
//      If two polygons share a common edge and the edges that meet at the
//      common points are both inside the other polygons, merge them
//      Returns NULL if the faces couldn't be merged, or the new face.
//      The originals will NOT be freed.
// =====================================================================================
static face_t *TryMerge(face_t *f1, face_t *f2)
{
    int i;
    int k;
    vec3_t normal;
    vec3_t delta;
    vec3_t planenormal;

    if (f1->numpoints == -1 || f2->numpoints == -1)
    {
        return NULL;
    }
    if (f1->texturenum != f2->texturenum)
    {
        return NULL;
    }
    if (f1->contents != f2->contents)
    {
        return NULL;
    }
    if (f1->planenum != f2->planenum)
    {
        return NULL;
    }
    if (f1->facestyle != f2->facestyle)
    {
        return NULL;
    }
    if (f1->detaillevel != f2->detaillevel)
    {
        return NULL;
    }

    //
    // find a common edge
    //
    vec_t *p1 = NULL; // shut up the compiler
    vec_t *p2 = NULL;
    int j = 0;

    for (i = 0; i < f1->numpoints; i++)
    {
        p1 = f1->pts[i];
        p2 = f1->pts[(i + 1) % f1->numpoints];
        for (j = 0; j < f2->numpoints; j++)
        {
            vec_t *p3 = f2->pts[j];
            vec_t *p4 = f2->pts[(j + 1) % f2->numpoints];
            for (k = 0; k < 3; k++)
            {
                if (std::abs(p1[k] - p4[k]) > ON_EPSILON)
                {
                    break;
                }
                if (std::abs(p2[k] - p3[k]) > ON_EPSILON)
                {
                    break;
                }
            }
            if (k == 3)
            {
                break;
            }
        }
        if (j < f2->numpoints)
        {
            break;
        }
    }

    if (i == f1->numpoints)
    {
        return NULL; // no matching edges
    }

    //
    // check slope of connected lines
    // if the slopes are colinear, the point can be removed
    //
    dplane_t *plane = &g_dplanes[f1->planenum];
    VectorCopy(plane->normal, planenormal);

    vec_t *back = f1->pts[(i + f1->numpoints - 1) % f1->numpoints];
    VectorSubtract(p1, back, delta);
    CrossProduct(planenormal, delta, normal);
    VectorNormalize(normal);

    back = f2->pts[(j + 2) % f2->numpoints];
    VectorSubtract(back, p1, delta);
    vec_t dot = DotProduct(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
    {
        return NULL; // not a convex polygon
    }
    bool keep1 = dot < -CONTINUOUS_EPSILON;

    back = f1->pts[(i + 2) % f1->numpoints];
    VectorSubtract(back, p2, delta);
    CrossProduct(planenormal, delta, normal);
    VectorNormalize(normal);

    back = f2->pts[(j + f2->numpoints - 1) % f2->numpoints];
    VectorSubtract(back, p2, delta);
    dot = DotProduct(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
    {
        return NULL; // not a convex polygon
    }
    bool keep2 = dot < -CONTINUOUS_EPSILON;

    //
    // build the new polygon
    //
    if (f1->numpoints + f2->numpoints > MAXEDGES)
    {
        //              Error ("TryMerge: too many edges!");
        return NULL;
    }

    face_t *newf = NewFaceFromFace(f1);

    // copy first polygon
    for (k = (i + 1) % f1->numpoints; k != i; k = (k + 1) % f1->numpoints)
    {
        if (k == (i + 1) % f1->numpoints && !keep2)
        {
            continue;
        }

        VectorCopy(f1->pts[k], newf->pts[newf->numpoints]);
        newf->numpoints++;
    }

    // copy second polygon
    for (int l = (j + 1) % f2->numpoints; l != j; l = (l + 1) % f2->numpoints)
    {
        if (l == (j + 1) % f2->numpoints && !keep1)
        {
            continue;
        }
        VectorCopy(f2->pts[l], newf->pts[newf->numpoints]);
        newf->numpoints++;
    }

    return newf;
}

// =====================================================================================
//  MergeFaceToList
// =====================================================================================
static face_t *MergeFaceToList(face_t *face, face_t *list)
{

    for (face_t *f = list; f; f = f->next)
    {
        //CheckColinear (f);
        face_t *newf = TryMerge(face, f);
        if (!newf)
        {
            continue;
        }
        FreeFace(face);
        f->numpoints = -1; // merged out
        return MergeFaceToList(newf, list);
    }

    // didn't merge, so add at start
    face->next = list;
    return face;
}

// =====================================================================================
//  FreeMergeListScraps
// =====================================================================================
static face_t *FreeMergeListScraps(face_t *merged)
{
    face_t *next;

    face_t *head = NULL;
    for (; merged; merged = next)
    {
        next = merged->next;
        if (merged->numpoints == -1)
        {
            FreeFace(merged);
        }
        else
        {
            merged->next = head;
            head = merged;
        }
    }

    return head;
}

// =====================================================================================
//  MergePlaneFaces
// =====================================================================================
void MergePlaneFaces(surface_t *plane)
{
    face_t *next;
    face_t *merged = NULL;

    for (face_t *f1 = plane->faces; f1; f1 = next)
    {
        next = f1->next;
        merged = MergeFaceToList(f1, merged);
    }

    // chain all of the non-empty faces to the plane
    plane->faces = FreeMergeListScraps(merged);
}

// =====================================================================================
//  MergeAll
// =====================================================================================
void MergeAll(surface_t *surfhead)
{
    Verbose("---- MergeAll ----\n");

    int mergefaces = 0;
    for (surface_t *surf = surfhead; surf; surf = surf->next)
    {
        MergePlaneFaces(surf);
        for (face_t *f = surf->faces; f; f = f->next)
        {
            mergefaces++;
        }
    }

    Verbose("%i mergefaces\n", mergefaces);
}
