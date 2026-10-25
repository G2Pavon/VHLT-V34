#include "doctest/doctest.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "common/boundingbox.h"
#include "common/bspfile.h"
#include "common/mathlib.h"

TEST_SUITE("math")
{
    TEST_CASE("vec_t precision matches the tool configuration")
    {
#ifdef DOUBLEVEC_T
        CHECK(std::is_same_v<vec_t, double>);
#else
        CHECK(std::is_same_v<vec_t, float>);
#endif
    }

    TEST_CASE("epsilons are float constants (widening them to double changes results)")
    {
        CHECK(std::is_same_v<decltype(NORMAL_EPSILON), const float>);
        CHECK(std::is_same_v<decltype(ON_EPSILON), const float>);
        CHECK(std::is_same_v<decltype(EQUAL_EPSILON), const float>);
        CHECK(std::is_same_v<decltype(DIR_EPSILON), const float>);
        CHECK(static_cast<double>(NORMAL_EPSILON) == static_cast<double>(0.00001f));
        CHECK(static_cast<double>(NORMAL_EPSILON) != 0.00001);
        CHECK(static_cast<double>(ON_EPSILON) == static_cast<double>(0.04f));
    }

    TEST_CASE("qmax/qmin return the second argument on ties and NaN (unlike std::max)")
    {
        // compat: qmax(a, b) is ((a) > (b)) ? (a) : (b)
        CHECK(std::signbit(qmax(0.0, -0.0)));
        CHECK(!std::signbit(qmax(-0.0, 0.0)));
        CHECK(std::signbit(qmin(0.0, -0.0)));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK(qmax(nan, 1.0) == 1.0);
        CHECK(std::isnan(qmax(1.0, nan)));
        CHECK(qmin(nan, 1.0) == 1.0);
        CHECK(std::isnan(qmin(1.0, nan)));
    }

    TEST_CASE("DotProduct uses the usual arithmetic conversions of its operands")
    {
        const float f[3] = {0.1f, 0.2f, 0.3f};
        const double d[3] = {0.7, 0.11, 0.13};
        const double expected = (static_cast<double>(f[0]) * d[0] + static_cast<double>(f[1]) * d[1]) + static_cast<double>(f[2]) * d[2];
        CHECK(DotProduct(f, d) == expected);
        CHECK(std::is_same_v<decltype(DotProduct(f, d)), double>);
        CHECK(std::is_same_v<decltype(DotProduct(f, f)), float>);
        const float ff = (f[0] * f[0] + f[1] * f[1]) + f[2] * f[2];
        CHECK(DotProduct(f, f) == ff);
    }

    TEST_CASE("VectorLength computes in double")
    {
        const float v[3] = {3.0f, 4.0f, 12.0f};
        CHECK(VectorLength(v) == 13.0);
        CHECK(std::is_same_v<decltype(VectorLength(v)), double>);
    }

    TEST_CASE("CrossProduct, VectorMA, VectorScale")
    {
        vec3_t a = {1, 0, 0}, b = {0, 1, 0}, c;
        CrossProduct(a, b, c);
        CHECK(c[0] == 0);
        CHECK(c[1] == 0);
        CHECK(c[2] == 1);
        vec3_t m;
        VectorMA(a, 2.0, b, m);
        CHECK(m[0] == 1);
        CHECK(m[1] == 2);
        CHECK(m[2] == 0);
        VectorScale(m, 0.5, m);
        CHECK(m[0] == 0.5);
        CHECK(m[1] == 1);
    }

    TEST_CASE("compat: CrossProduct into one of its inputs sees the partial update")
    {
        // dest[0] is written before dest[1] and dest[2] read a[0] and a[1].
        float a[3] = {1, 2, 3};
        const float b[3] = {4, 5, 6};
        float c[3];
        CrossProduct(a, b, c);
        CHECK(c[0] == -3);
        CHECK(c[1] == 6);
        CHECK(c[2] == -3);
        CrossProduct(a, b, a);
        CHECK(a[0] == -3);
        CHECK(a[1] == 30);
        CHECK(a[2] == -135);
    }

    TEST_CASE("compat: a scalar argument that refers into the destination is re-read per component")
    {
        vec3_t v = {2, 3, 4};
        VectorScale(v, v[0], v); // v[0] becomes 4 before v[1] and v[2] are scaled
        CHECK(v[0] == 4);
        CHECK(v[1] == 12);
        CHECK(v[2] == 16);
    }

    TEST_CASE("result types follow the operands")
    {
        const float f[3] = {1, 2, 3};
        const double d[3] = {1, 2, 3};
        const int n[3] = {1, 1, 2};
        CHECK(std::is_same_v<decltype(qmax(1.5f, 2)), float>);
        CHECK(std::is_same_v<decltype(qmax(1, 2.0)), double>);
        CHECK(std::is_same_v<decltype(VectorAvg(f)), float>);
        CHECK(VectorAvg(n) == 1); // integer division, as in the macro
        CHECK(VectorAvg(f) == 2.0f);
        // The macros yielded an lvalue (const double&) for lvalue operands; the templates
        // return a value. No call site binds to or takes the address of the result.
        CHECK(std::is_same_v<std::remove_cvref_t<decltype(VectorMaximum(d))>, double>);
        CHECK(VectorMaximum(n) == 2);
        CHECK(VectorMinimum(f) == 1.0f);
        CHECK(std::is_same_v<decltype(VectorRound(2.5f)), double>);
        CHECK(VectorRound(2.5f) == 3.0);
        CHECK(VectorRound(-2.5) == -2.0);
        const char *s = "a/b\\c";
        CHECK(qmax(s + 1, s + 3) == s + 3); // pointers, as cmdlib uses it
    }

    TEST_CASE("compat: VectorLength squares in the element type before widening")
    {
        const float big[3] = {1e20f, 0, 0};
        CHECK(std::isinf(VectorLength(big))); // 1e40 overflows float
        const double bigd[3] = {1e20, 0, 0};
        CHECK(VectorLength(bigd) == 1e20);
    }

    TEST_CASE("componentwise helpers")
    {
        vec3_t a = {1, 2, 3}, b = {4, 5, 6}, c;
        VectorMultiply(a, b, c);
        CHECK(c[2] == 18);
        VectorAddVec(a, 1, c);
        CHECK(c[0] == 2);
        VecSubtractVector(10, a, c);
        CHECK(c[1] == 8);
        VectorCompareMinimum(a, b, c);
        CHECK(c[2] == 3);
        VectorCompareMaximum(a, b, c);
        CHECK(c[0] == 4);
        VectorInverse(c);
        CHECK(c[1] == -5);
        VectorFill(c, 7);
        CHECK(c[2] == 7);
        VectorClear(c);
        CHECK(c[0] == 0);
        CHECK(!std::signbit(c[1]));
        VectorAdd(a, b, a); // destination aliases an input
        CHECK(a[2] == 9);
        VectorSubtract(a, b, a);
        CHECK(a[2] == 3);
    }

    TEST_CASE("the helpers are usable in constant expressions")
    {
        constexpr float x[3] = {1, 2, 3};
        static_assert(DotProduct(x, x) == 14);
        static_assert(qmax(1, 2) == 2 && qmin(1.0, 2.0) == 1.0);
        static_assert(VectorMaximum(x) == 3);
        CHECK(true);
    }

    TEST_CASE("VectorNormalize")
    {
        vec3_t v = {3, 4, 0};
        CHECK(VectorNormalize(v) == 5);
        CHECK(v[0] == static_cast<vec_t>(3.0 / 5.0));
        CHECK(v[1] == static_cast<vec_t>(4.0 / 5.0));
        CHECK(v[2] == 0);

        vec3_t tiny = {1e-6, 0, 0}; // below NORMAL_EPSILON: cleared, returns 0
        CHECK(VectorNormalize(tiny) == 0);
        CHECK(tiny[0] == 0);
    }

    TEST_CASE("VectorCompare tolerance is EQUAL_EPSILON")
    {
        vec3_t a = {1, 2, 3};
        vec3_t b = {1.003, 2, 3};
        vec3_t c = {1.005, 2, 3};
        CHECK(VectorCompare(a, b));
        CHECK(!VectorCompare(a, c));
    }

    TEST_CASE("PlaneTypeForNormal")
    {
        vec3_t x = {1, 0, 0}, ny = {0, -1, 0}, z = {0, 0, 1};
        CHECK(PlaneTypeForNormal(x) == plane_x);
        CHECK(PlaneTypeForNormal(ny) == plane_y);
        CHECK(PlaneTypeForNormal(z) == plane_z);
        vec3_t tie = {0.6, 0.6, 0.1}; // ties resolve towards x, then y
        CHECK(PlaneTypeForNormal(tie) == plane_anyx);
        vec3_t yz = {0.1, 0.7, 0.7};
        CHECK(PlaneTypeForNormal(yz) == plane_anyy);
        vec3_t mostly_z = {0.1, 0.2, -0.9};
        CHECK(PlaneTypeForNormal(mostly_z) == plane_anyz);
        vec3_t almost_x = {1, 0.00009, 0}; // within DIR_EPSILON
        CHECK(PlaneTypeForNormal(almost_x) == plane_x);
    }

    TEST_CASE("planetypes is int-sized (it is stored on disk in dplane_t)")
    {
        CHECK(sizeof(planetypes) == 4);
        CHECK(sizeof(dplane_t) == 20);
    }

    TEST_CASE("BoundingBox")
    {
        BoundingBox box;
        CHECK(box.m_Mins[0] == static_cast<vec_t>(999999999.999));
        CHECK(box.m_Maxs[0] == static_cast<vec_t>(-999999999.999));
        vec3_t p1 = {0, 0, 0}, p2 = {10, 10, 10};
        box.add(p1);
        box.add(p2);

        vec3_t a = {2, 2, 2}, b = {4, 4, 4};
        BoundingBox inner(a, b);
        CHECK(inner.test(box) == BoundingBox::eSubset);
        CHECK(box.test(inner) == BoundingBox::eSuperset);

        vec3_t c = {5, 5, 5}, d = {20, 20, 20};
        CHECK(box.test(BoundingBox(c, d)) == BoundingBox::eUnion);

        // Disjoint only beyond ON_EPSILON: touching boxes intersect.
        vec3_t e = {10.03, 0, 0}, f = {12, 1, 1};
        CHECK(box.test(BoundingBox(e, f)) != BoundingBox::eDisjoint);
        vec3_t g = {10.05, 0, 0};
        CHECK(box.test(BoundingBox(g, f)) == BoundingBox::eDisjoint);
    }

    TEST_CASE("compat: TruncateToInt is the x86 conversion, the smallest Int for NaN and out-of-range values")
    {
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK(TruncateToInt<int>(0.0) == 0);
        CHECK(TruncateToInt<int>(2.9) == 2);
        CHECK(TruncateToInt<int>(-2.9) == -2);
        CHECK(TruncateToInt<int>(2147483647.0) == 2147483647);
        CHECK(TruncateToInt<int>(2147483647.9) == 2147483647);
        CHECK(TruncateToInt<int>(-2147483648.0) == INT_MIN);
        CHECK(TruncateToInt<int>(2147483648.0) == INT_MIN);
        CHECK(TruncateToInt<int>(-2147483649.0) == INT_MIN);
        CHECK(TruncateToInt<int>(inf) == INT_MIN);
        CHECK(TruncateToInt<int>(-inf) == INT_MIN);
        CHECK(TruncateToInt<int>(nan) == INT_MIN);
        CHECK(TruncateToInt<std::int64_t>(4294967296.5) == 4294967296LL);
        CHECK(TruncateToInt<std::int64_t>(9.3e18) == INT64_MIN);
        CHECK(TruncateToInt<std::int64_t>(-9.3e18) == INT64_MIN);
        CHECK(TruncateToInt<std::int64_t>(-inf) == INT64_MIN);
        CHECK(TruncateToInt<std::int64_t>(nan) == INT64_MIN);
    }
}
