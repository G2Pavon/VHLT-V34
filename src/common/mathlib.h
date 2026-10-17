#pragma once

#include "common/mathtypes.h"

#include <cmath>
#include <cfloat>
#include <cstdint>

constexpr double Q_PI = 3.14159265358979323846;

// HLCSG_HLBSP_DOUBLEPLANE: We could use smaller epsilon for hlcsg and hlbsp (hlcsg and hlbsp use double as vec_t), which will totally eliminate all epsilon errors. But we choose this big epsilon to tolerate the imprecision caused by Hammer. Basically, this is a balance between precision and flexibility.
constexpr float NORMAL_EPSILON = 0.00001;
constexpr float ON_EPSILON = 0.04; // we should ensure that (float)BOGUS_RANGE < (float)(BOGUA_RANGE + 0.2 * ON_EPSILON)
constexpr float EQUAL_EPSILON = 0.004;

constexpr vec3_t vec3_origin = {0, 0, 0};
constexpr vec3_t vec3_one = {1.0, 1.0, 1.0};
// Vector Math
//
// These were macros. Each template evaluates exactly the macro's expression, component by
// component in the same order, so results are bit-identical: the operand types (float, double,
// mixed) and the usual arithmetic conversions are those of the arguments, nothing is widened,
// and a destination that aliases an input sees the same partial updates as before.
// Arguments are now evaluated once; no call site relied on repeated evaluation.
// qmax/qmin/VectorMaximum/VectorMinimum return a value where the macros yielded an lvalue;
// no call site took the address of, or held a reference to, the result.

// compat: ((a) > (b)) ? (a) : (b) returns b on ties and NaN, unlike std::max (sign of zero).
template <typename A, typename B>
constexpr auto qmax(const A &a, const B &b) // changed 'max' to 'qmax'. --vluzacn
{
    return (a > b) ? a : b;
}

template <typename A, typename B>
constexpr auto qmin(const A &a, const B &b) // changed 'min' to 'qmin'. --vluzacn
{
    return (a < b) ? a : b;
}

template <typename A, typename B, typename C>
constexpr void VectorAdd(const A &a, const B &b, C &&c)
{
    c[0] = a[0] + b[0];
    c[1] = a[1] + b[1];
    c[2] = a[2] + b[2];
}

template <typename A, typename B, typename C>
constexpr void VectorSubtract(const A &a, const B &b, C &&c)
{
    c[0] = a[0] - b[0];
    c[1] = a[1] - b[1];
    c[2] = a[2] - b[2];
}

template <typename X, typename Y>
constexpr auto DotProduct(const X &x, const Y &y)
{
    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

// dest may alias a or b: dest[0] is written before dest[1] and dest[2] are computed.
template <typename A, typename B, typename D>
constexpr void CrossProduct(const A &a, const B &b, D &&dest)
{
    dest[0] = a[1] * b[2] - a[2] * b[1];
    dest[1] = a[2] * b[0] - a[0] * b[2];
    dest[2] = a[0] * b[1] - a[1] * b[0];
}

template <typename A, typename B>
constexpr void VectorFill(A &&a, const B &b)
{
    a[0] = b;
    a[1] = b;
    a[2] = b;
}

template <typename A>
constexpr auto VectorAvg(const A &a)
{
    return (a[0] + a[1] + a[2]) / 3;
}

template <typename A, typename B, typename C>
constexpr void VectorMultiply(const A &a, const B &b, C &&c)
{
    c[0] = a[0] * b[0];
    c[1] = a[1] * b[1];
    c[2] = a[2] * b[2];
}

// c = a + scalar b
template <typename A, typename B, typename C>
constexpr void VectorAddVec(const A &a, const B &b, C &&c)
{
    c[0] = a[0] + b;
    c[1] = a[1] + b;
    c[2] = a[2] + b;
}

// c = scalar a - b
template <typename A, typename B, typename C>
constexpr void VecSubtractVector(const A &a, const B &b, C &&c)
{
    c[0] = a - b[0];
    c[1] = a - b[1];
    c[2] = a - b[2];
}

template <typename A, typename B, typename C>
constexpr void VectorScale(const A &a, const B &b, C &&c)
{
    c[0] = a[0] * b;
    c[1] = a[1] * b;
    c[2] = a[2] * b;
}

template <typename A, typename B>
constexpr void VectorCopy(const A &a, B &&b)
{
    b[0] = a[0];
    b[1] = a[1];
    b[2] = a[2];
}

template <typename A>
constexpr void VectorClear(A &&a)
{
    a[0] = a[1] = a[2] = 0.0;
}

template <typename A>
constexpr auto VectorMaximum(const A &a)
{
    return qmax(a[0], qmax(a[1], a[2]));
}

template <typename A>
constexpr auto VectorMinimum(const A &a)
{
    return qmin(a[0], qmin(a[1], a[2]));
}

template <typename A>
constexpr void VectorInverse(A &&a)
{
    a[0] = -(a[0]);
    a[1] = -(a[1]);
    a[2] = -(a[2]);
}

// Rounds a scalar (despite the name).
template <typename T>
auto VectorRound(const T &a)
{
    return std::floor(a + 0.5);
}

template <typename A, typename S, typename B, typename D>
constexpr void VectorMA(const A &a, const S &scale, const B &b, D &&dest)
{
    dest[0] = a[0] + scale * b[0];
    dest[1] = a[1] + scale * b[1];
    dest[2] = a[2] + scale * b[2];
}

// compat: each square is computed in the element type, then widened to double.
template <typename A>
double VectorLength(const A &a)
{
    return std::sqrt((double)((double)(a[0] * a[0]) + (double)(a[1] * a[1]) + (double)(a[2] * a[2])));
}

template <typename A, typename B, typename C>
constexpr void VectorCompareMinimum(const A &a, const B &b, C &&c)
{
    c[0] = qmin(a[0], b[0]);
    c[1] = qmin(a[1], b[1]);
    c[2] = qmin(a[2], b[2]);
}

template <typename A, typename B, typename C>
constexpr void VectorCompareMaximum(const A &a, const B &b, C &&c)
{
    c[0] = qmax(a[0], b[0]);
    c[1] = qmax(a[1], b[1]);
    c[2] = qmax(a[2], b[2]);
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
// Misc
inline bool isPointFinite(const vec_t *p)
{
    if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]))
    {
        return true;
    }
    return false;
}
// Planetype Math
enum planetypes : std::int32_t // stored in dplane_t on disk
{
    plane_x = 0,
    plane_y,
    plane_z,
    plane_anyx,
    plane_anyy,
    plane_anyz
};

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