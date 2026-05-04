#pragma once

#include "common/mathtypes.h"

#include <cmath>
#include <cfloat>

#if !defined(qmax)
#define qmax(a, b) (((a) > (b)) ? (a) : (b)) // changed 'max' to 'qmax'. --vluzacn
#endif

#if !defined(qmin)
#define qmin(a, b) (((a) < (b)) ? (a) : (b)) // changed 'min' to 'qmin'. --vluzacn
#endif

constexpr double Q_PI = 3.14159265358979323846;

// HLCSG_HLBSP_DOUBLEPLANE: We could use smaller epsilon for hlcsg and hlbsp (hlcsg and hlbsp use double as vec_t), which will totally eliminate all epsilon errors. But we choose this big epsilon to tolerate the imprecision caused by Hammer. Basically, this is a balance between precision and flexibility.
constexpr float NORMAL_EPSILON = 0.00001;
constexpr float ON_EPSILON = 0.04; // we should ensure that (float)BOGUS_RANGE < (float)(BOGUA_RANGE + 0.2 * ON_EPSILON)
constexpr float EQUAL_EPSILON = 0.004;

constexpr vec3_t vec3_origin = {0, 0, 0};
constexpr vec3_t vec3_one = {1.0, 1.0, 1.0};
//
// Vector Math
//
#define VectorAdd(a, b, c)        \
    {                             \
        (c)[0] = (a)[0] + (b)[0]; \
        (c)[1] = (a)[1] + (b)[1]; \
        (c)[2] = (a)[2] + (b)[2]; \
    }

#define VectorSubtract(a, b, c)   \
    {                             \
        (c)[0] = (a)[0] - (b)[0]; \
        (c)[1] = (a)[1] - (b)[1]; \
        (c)[2] = (a)[2] - (b)[2]; \
    }

#define DotProduct(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])

#define CrossProduct(a, b, dest)                       \
    {                                                  \
        (dest)[0] = (a)[1] * (b)[2] - (a)[2] * (b)[1]; \
        (dest)[1] = (a)[2] * (b)[0] - (a)[0] * (b)[2]; \
        (dest)[2] = (a)[0] * (b)[1] - (a)[1] * (b)[0]; \
    }

#define VectorFill(a, b) \
    {                    \
        (a)[0] = (b);    \
        (a)[1] = (b);    \
        (a)[2] = (b);    \
    }

#define VectorAvg(a) (((a)[0] + (a)[1] + (a)[2]) / 3)

#define VectorMultiply(a, b, c)   \
    {                             \
        (c)[0] = (a)[0] * (b)[0]; \
        (c)[1] = (a)[1] * (b)[1]; \
        (c)[2] = (a)[2] * (b)[2]; \
    }

#define VectorAddVec(a, b, c)  \
    {                          \
        (c)[0] = (a)[0] + (b); \
        (c)[1] = (a)[1] + (b); \
        (c)[2] = (a)[2] + (b); \
    }

#define VecSubtractVector(a, b, c) \
    {                              \
        (c)[0] = (a) - (b)[0];     \
        (c)[1] = (a) - (b)[1];     \
        (c)[2] = (a) - (b)[2];     \
    }

#define VectorScale(a, b, c)   \
    {                          \
        (c)[0] = (a)[0] * (b); \
        (c)[1] = (a)[1] * (b); \
        (c)[2] = (a)[2] * (b); \
    }

#define VectorCopy(a, b) \
    {                    \
        (b)[0] = (a)[0]; \
        (b)[1] = (a)[1]; \
        (b)[2] = (a)[2]; \
    }
#define VectorClear(a)                  \
    {                                   \
        (a)[0] = (a)[1] = (a)[2] = 0.0; \
    }

#define VectorMaximum(a) (qmax((a)[0], qmax((a)[1], (a)[2])))

#define VectorMinimum(a) (qmin((a)[0], qmin((a)[1], (a)[2])))

#define VectorInverse(a)    \
    {                       \
        (a)[0] = -((a)[0]); \
        (a)[1] = -((a)[1]); \
        (a)[2] = -((a)[2]); \
    }
#define VectorRound(a) std::floor((a) + 0.5)

#define VectorMA(a, scale, b, dest)            \
    {                                          \
        (dest)[0] = (a)[0] + (scale) * (b)[0]; \
        (dest)[1] = (a)[1] + (scale) * (b)[1]; \
        (dest)[2] = (a)[2] + (scale) * (b)[2]; \
    }
#define VectorLength(a) std::sqrt((double)((double)((a)[0] * (a)[0]) + (double)((a)[1] * (a)[1]) + (double)((a)[2] * (a)[2])))

#define VectorCompareMinimum(a, b, c)  \
    {                                  \
        (c)[0] = qmin((a)[0], (b)[0]); \
        (c)[1] = qmin((a)[1], (b)[1]); \
        (c)[2] = qmin((a)[2], (b)[2]); \
    }
#define VectorCompareMaximum(a, b, c)  \
    {                                  \
        (c)[0] = qmax((a)[0], (b)[0]); \
        (c)[1] = qmax((a)[1], (b)[1]); \
        (c)[2] = qmax((a)[2], (b)[2]); \
    }

inline vec_t VectorNormalize(vec3_t v)
{
    double length = DotProduct(v, v);
    length = std::sqrt(length);
    if (length < NORMAL_EPSILON)
    {
        VectorClear(v);
        return 0.0;
    }

    v[0] /= length;
    v[1] /= length;
    v[2] /= length;

    return length;
}

inline bool VectorCompare(const vec3_t v1, const vec3_t v2)
{
    for (int i = 0; i < 3; i++)
    {
        if (std::abs(v1[i] - v2[i]) > EQUAL_EPSILON)
        {
            return false;
        }
    }
    return true;
}

//
// Misc
//

inline bool isPointFinite(const vec_t *p)
{
    if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]))
    {
        return true;
    }
    return false;
}

//
// Planetype Math
//

typedef enum
{
    plane_x = 0,
    plane_y,
    plane_z,
    plane_anyx,
    plane_anyy,
    plane_anyz
} planetypes;

constexpr planetypes last_axial = plane_z;
constexpr float DIR_EPSILON = 0.0001;

inline planetypes PlaneTypeForNormal(vec3_t normal)
{
    vec_t ax = std::abs(normal[0]);
    vec_t ay = std::abs(normal[1]);
    vec_t az = std::abs(normal[2]);
    if (ax > 1.0 - DIR_EPSILON && ay < DIR_EPSILON && az < DIR_EPSILON)
    {
        return plane_x;
    }

    if (ay > 1.0 - DIR_EPSILON && az < DIR_EPSILON && ax < DIR_EPSILON)
    {
        return plane_y;
    }

    if (az > 1.0 - DIR_EPSILON && ax < DIR_EPSILON && ay < DIR_EPSILON)
    {
        return plane_z;
    }

    if ((ax >= ay) && (ax >= az))
    {
        return plane_anyx;
    }
    if ((ay >= ax) && (ay >= az))
    {
        return plane_anyy;
    }
    return plane_anyz;
}