#include <cstring>
#include <cmath>

#include "hlbsp/hlbsp.h"
#include "common/log.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"

static constexpr int PLANENUM_LEAF = -1;

typedef struct wvert_s
{
    vec_t t;
    struct wvert_s *prev;
    struct wvert_s *next;
} wvert_t;

typedef struct wedge_s
{
    struct wedge_s *next;
    vec3_t dir;
    vec3_t origin;
    wvert_t head;
} wedge_t;

static int numwedges;
static int numwverts;
static int tjuncs;
static int tjuncfaces;

constexpr int MAX_WVERTS = 0x40000;
constexpr int MAX_WEDGES = 0x20000;

static wvert_t wverts[MAX_WVERTS];
static wedge_t wedges[MAX_WEDGES];

//============================================================================

constexpr int NUM_HASH = 4096;

wedge_t *wedge_hash[NUM_HASH];

static vec3_t hash_min;
static vec3_t hash_scale;
// It's okay if the coordinates go under hash_min, because they are hashed in a cyclic way (modulus by hash_numslots)
// So please don't change the hardcoded hash_min and scale
static int hash_numslots[3];
constexpr int MAX_HASH_NEIGHBORS = 4;

static void InitHash()
{
    vec3_t size;

    // Let's ignore the parameters and make things more predictable, so there won't be strange cases such as division by 0 or extreme scaling values.
    VectorFill(hash_min, -8000);
    VectorFill(size, 16000);
    std::memset(wedge_hash, 0, sizeof(wedge_hash));

    vec_t volume = size[0] * size[1];

    vec_t scale = std::sqrt(volume / NUM_HASH);

    hash_numslots[0] = (int)std::floor(size[0] / scale);
    hash_numslots[1] = (int)std::floor(size[1] / scale);
    while (hash_numslots[0] * hash_numslots[1] > NUM_HASH)
    {
        hash_numslots[0]--;
        hash_numslots[1]--;
    }

    hash_scale[0] = hash_numslots[0] / size[0];
    hash_scale[1] = hash_numslots[1] / size[1];
}

static int HashVec(const vec3_t vec, int *num_hashneighbors, int *hashneighbors)
{
    int slot[2];
    vec_t normalized[2];
    vec_t slotdiff[2];

    for (int i = 0; i < 2; i++)
    {
        normalized[i] = hash_scale[i] * (vec[i] - hash_min[i]);
        slot[i] = (int)std::floor(normalized[i]);
        slotdiff[i] = normalized[i] - (vec_t)slot[i];

        slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i];
        slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i]; // do it twice to handle negative values
    }

    int h = slot[0] * hash_numslots[1] + slot[1];

    *num_hashneighbors = 0;
    for (int x = -1; x <= 1; x++)
    {
        if ((x == -1 && slotdiff[0] > hash_scale[0] * (2 * ON_EPSILON)) ||
            (x == 1 && slotdiff[0] < 1 - hash_scale[0] * (2 * ON_EPSILON)))
        {
            continue;
        }
        for (int y = -1; y <= 1; y++)
        {
            if ((y == -1 && slotdiff[1] > hash_scale[1] * (2 * ON_EPSILON)) ||
                (y == 1 && slotdiff[1] < 1 - hash_scale[1] * (2 * ON_EPSILON)))
            {
                continue;
            }
            if (*num_hashneighbors >= MAX_HASH_NEIGHBORS)
            {
                Error("HashVec: internal error.");
            }
            hashneighbors[*num_hashneighbors] =
                ((slot[0] + x + hash_numslots[0]) % hash_numslots[0]) * hash_numslots[1] +
                (slot[1] + y + hash_numslots[1]) % hash_numslots[1];
            (*num_hashneighbors)++;
        }
    }

    return h;
}

//============================================================================

static bool CanonicalVector(vec3_t vec)
{
    if (VectorNormalize(vec))
    {
        if (vec[0] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[0] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[0] = 0;
        }

        if (vec[1] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[1] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[1] = 0;
        }

        if (vec[2] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[2] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[2] = 0;
        }
        //        hlassert(false);
        return false;
    }
    //    hlassert(false);
    return false;
}

static wedge_t *FindEdge(const vec3_t p1, const vec3_t p2, vec_t *t1, vec_t *t2)
{
    vec3_t origin;
    vec3_t dir;
    wedge_t *w;
    int num_hashneighbors;
    int hashneighbors[MAX_HASH_NEIGHBORS];

    VectorSubtract(p2, p1, dir);
    if (!CanonicalVector(dir))
    {
    }

    *t1 = DotProduct(p1, dir);
    *t2 = DotProduct(p2, dir);

    VectorMA(p1, -*t1, dir, origin);

    if (*t1 > *t2)
    {
        vec_t temp = *t1;
        *t1 = *t2;
        *t2 = temp;
    }

    int h = HashVec(origin, &num_hashneighbors, hashneighbors);

    for (int i = 0; i < num_hashneighbors; ++i)
        for (w = wedge_hash[hashneighbors[i]]; w; w = w->next)
        {
            if (std::abs(w->origin[0] - origin[0]) > EQUAL_EPSILON ||
                std::abs(w->origin[1] - origin[1]) > EQUAL_EPSILON ||
                std::abs(w->origin[2] - origin[2]) > EQUAL_EPSILON)
            {
                continue;
            }
            if (std::abs(w->dir[0] - dir[0]) > NORMAL_EPSILON ||
                std::abs(w->dir[1] - dir[1]) > NORMAL_EPSILON ||
                std::abs(w->dir[2] - dir[2]) > NORMAL_EPSILON)
            {
                continue;
            }

            return w;
        }

    hlassume(numwedges < MAX_WEDGES, assume_MAX_WEDGES);
    w = &wedges[numwedges];
    numwedges++;

    w->next = wedge_hash[h];
    wedge_hash[h] = w;

    VectorCopy(origin, w->origin);
    VectorCopy(dir, w->dir);
    w->head.next = w->head.prev = &w->head;
    w->head.t = 99999;
    return w;
}

static constexpr vec_t T_EPSILON = ON_EPSILON;
static void AddVert(const wedge_t *const w, const vec_t t)
{
    wvert_t *v = w->head.next;
    do
    {
        if (std::abs(v->t - t) < T_EPSILON)
        {
            return;
        }
        if (v->t > t)
        {
            break;
        }
        v = v->next;
    } while (true);

    // insert a new wvert before v
    hlassume(numwverts < MAX_WVERTS, assume_MAX_WVERTS);

    wvert_t *newv = &wverts[numwverts];
    numwverts++;

    newv->t = t;
    newv->next = v;
    newv->prev = v->prev;
    v->prev->next = newv;
    v->prev = newv;
}

static void AddEdge(const vec3_t p1, const vec3_t p2)
{
    vec_t t1;
    vec_t t2;

    wedge_t *w = FindEdge(p1, p2, &t1, &t2);
    AddVert(w, t1);
    AddVert(w, t2);
}

static void AddFaceEdges(const face_t *const f)
{
    for (int i = 0; i < f->numpoints; i++)
    {
        int j = (i + 1) % f->numpoints;
        AddEdge(f->pts[i], f->pts[j]);
    }
}

//============================================================================

// The face FixFaceEdges works on, with room for the points it inserts. It used to be a 16 KB byte
// buffer cast to face_t whose pts[] was indexed past MAXEDGES (misaligned, out of bounds).
// compat: the same capacity, so MAX_SUPERFACEEDGES is reached at the same point: (16 KB - sizeof(face_t)
// + sizeof(pts)) / sizeof(vec3_t) with the oracle's face_t (1208 bytes; outputedges was an int*).
constexpr int MAX_SUPERFACEEDGES = 680;
static_assert(sizeof(face_t::pts) == 1152 && (1024 * 16 - 1208 + sizeof(face_t::pts)) / sizeof(vec3_t) == MAX_SUPERFACEEDGES);
struct SuperFace
{
    face_t face; // everything but the points: face.numpoints counts pts below
    vec3_t pts[MAX_SUPERFACEEDGES];
};
static SuperFace superface;

// The face's fields and its first MAXEDGES points (all a face_t holds), as `*dst = *src` did.
static void CopyFace(face_t *dst, const SuperFace &src)
{
    *dst = src.face;
    std::memcpy(dst->pts, src.pts, sizeof(dst->pts));
}
static face_t *newlist;

static void SplitFaceForTjunc(SuperFace &sf, face_t *original)
{
    vec3_t dir, test;
    int firstcorner, lastcorner;

    face_t *chain = nullptr;
    do
    {
        hlassume(sf.face.original == nullptr, assume_ValidPointer); // "SplitFaceForTjunc: sf.face.original"

        if (sf.face.numpoints <= MAXPOINTS)
        { // the face is now small enough without more cutting
            // so copy it back to the original
            CopyFace(original, sf);
            original->original = chain;
            original->next = newlist;
            newlist = original;
            return;
        }

        tjuncfaces++;

        for (;;) // rotate the winding until the first corner leaves room for a piece
        {
        // find the last corner
        VectorSubtract(sf.pts[sf.face.numpoints - 1], sf.pts[0], dir);
        VectorNormalize(dir);
        for (lastcorner = sf.face.numpoints - 1; lastcorner > 0; lastcorner--)
        {
            VectorSubtract(sf.pts[lastcorner - 1], sf.pts[lastcorner], test);
            VectorNormalize(test);
            vec_t v = DotProduct(test, dir);
            if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON)
            {
                break;
            }
        }

        // find the first corner
        VectorSubtract(sf.pts[1], sf.pts[0], dir);
        VectorNormalize(dir);
        for (firstcorner = 1; firstcorner < sf.face.numpoints - 1; firstcorner++)
        {
            VectorSubtract(sf.pts[firstcorner + 1], sf.pts[firstcorner], test);
            VectorNormalize(test);
            vec_t v = DotProduct(test, dir);
            if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON)
            {
                break;
            }
        }

        if (firstcorner + 2 >= MAXPOINTS)
        {
            // rotate the point winding
            VectorCopy(sf.pts[0], test);
            for (int i = 1; i < sf.face.numpoints; i++)
            {
                VectorCopy(sf.pts[i], sf.pts[i - 1]);
            }
            VectorCopy(test, sf.pts[sf.face.numpoints - 1]);
            continue;
        }
        break;
        }

        // cut off as big a piece as possible, less than MAXPOINTS, and not
        // past lastcorner

        face_t *newface = NewFaceFromFace(&sf.face);

        hlassume(sf.face.original == nullptr, assume_ValidPointer); // "SplitFaceForTjunc: sf.face.original"

        newface->original = chain;
        chain = newface;
        newface->next = newlist;
        newlist = newface;
        if (sf.face.numpoints - firstcorner <= MAXPOINTS)
        {
            newface->numpoints = firstcorner + 2;
        }
        else if (lastcorner + 2 < MAXPOINTS && sf.face.numpoints - lastcorner <= MAXPOINTS)
        {
            newface->numpoints = lastcorner + 2;
        }
        else
        {
            newface->numpoints = MAXPOINTS;
        }

        for (int i = 0; i < newface->numpoints; i++)
        {
            VectorCopy(sf.pts[i], newface->pts[i]);
        }

        for (int i = newface->numpoints - 1; i < sf.face.numpoints; i++)
        {
            VectorCopy(sf.pts[i], sf.pts[i - (newface->numpoints - 2)]);
        }
        sf.face.numpoints -= (newface->numpoints - 2);
    } while (true);
}

static void FixFaceEdges(face_t *f)
{
    wvert_t *v;
    vec_t t1;
    vec_t t2;

    superface.face = *f;
    std::memcpy(superface.pts, f->pts, sizeof(f->pts));

    for (bool inserted = true; inserted;) // until no edge needs a new vertex
    {
    inserted = false;
    for (int i = 0; i < superface.face.numpoints; i++)
    {
        int j = (i + 1) % superface.face.numpoints;

        wedge_t *w = FindEdge(superface.pts[i], superface.pts[j], &t1, &t2);

        for (v = w->head.next; v->t < t1 + T_EPSILON; v = v->next)
        {
        }

        if (v->t < t2 - T_EPSILON)
        {
            tjuncs++;
            // insert a new vertex here
            for (int k = superface.face.numpoints; k > j; k--)
            {
                VectorCopy(superface.pts[k - 1], superface.pts[k]);
            }
            VectorMA(w->origin, v->t, w->dir, superface.pts[j]);
            superface.face.numpoints++;
            hlassume(superface.face.numpoints < MAX_SUPERFACEEDGES, assume_MAX_SUPERFACEEDGES);
            inserted = true; // start again from the first edge
            break;
        }
    }
    }

    if (superface.face.numpoints <= MAXPOINTS)
    {
        CopyFace(f, superface);
        f->next = newlist;
        newlist = f;
        return;
    }

    // the face needs to be split into multiple faces because of too many edges

    SplitFaceForTjunc(superface, f);
}

//============================================================================

static void tjunc_find_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
    {
        return;
    }

    for (face_t *f = node->faces; f; f = f->next)
    {
        AddFaceEdges(f);
    }

    tjunc_find_r(node->children[0]);
    tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t *node)
{
    face_t *next;

    if (node->planenum == PLANENUM_LEAF)
    {
        return;
    }

    newlist = nullptr;

    for (face_t *f = node->faces; f; f = next)
    {
        next = f->next;
        FixFaceEdges(f);
    }

    node->faces = newlist;

    tjunc_fix_r(node->children[0]);
    tjunc_fix_r(node->children[1]);
}

void tjunc(node_t *headnode)
{
    // identify all points on common edges
    // origin points won't allways be inside the map, so extend the hash area
    InitHash();

    numwedges = numwverts = 0;

    tjunc_find_r(headnode);
    // add extra vertexes on edges where needed
    tjuncs = tjuncfaces = 0;

    tjunc_fix_r(headnode);
}