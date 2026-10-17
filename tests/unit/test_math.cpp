#include "doctest/doctest.h"

#include <cmath>
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
}
