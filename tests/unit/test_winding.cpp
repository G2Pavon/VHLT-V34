#include "doctest/doctest.h"

#include <cmath>
#include <cstring>
#include <utility>

#include "common/winding.h"

namespace
{
// 2x2 square on z=0, counter-clockwise seen from -z, so getPlane gives +z.
Winding Square()
{
    vec3_t pts[4] = {{0, 0, 0}, {0, 2, 0}, {2, 2, 0}, {2, 0, 0}};
    return Winding(pts, 4);
}

dplane_t Plane(vec_t nx, vec_t ny, vec_t nz, vec_t dist)
{
    dplane_t p{};
    p.normal[0] = nx;
    p.normal[1] = ny;
    p.normal[2] = nz;
    p.dist = dist;
    return p;
}
} // namespace

TEST_SUITE("winding")
{
    TEST_CASE("plane, area, bounds, center")
    {
        Winding w = Square();
        vec3_t normal;
        vec_t dist;
        w.getPlane(normal, dist);
        CHECK(normal[0] == 0);
        CHECK(normal[1] == 0);
        CHECK(normal[2] == 1);
        CHECK(dist == 0);

        dplane_t plane{};
        w.getPlane(plane);
        CHECK(plane.normal[2] == 1);

        CHECK(w.getArea() == 4);

        vec3_t mins, maxs, center;
        w.getBounds(mins, maxs);
        CHECK(mins[0] == 0);
        CHECK(maxs[1] == 2);
        w.getCenter(center);
        CHECK(center[0] == 1);
        CHECK(center[1] == 1);
        CHECK(w.Valid());
    }

    TEST_CASE("copy is deep")
    {
        Winding w = Square();
        Winding c(w);
        c.m_Points[0][0] = 99;
        CHECK(w.m_Points[0][0] == 0);
        const std::unique_ptr<Winding> p = w.Copy();
        CHECK(p->m_NumPoints == 4);
        CHECK(p->m_Points != w.m_Points);
        Winding a;
        a = w;
        CHECK(a.m_NumPoints == 4);
        CHECK(std::memcmp(a.m_Points.get(), w.m_Points.get(), sizeof(vec3_t) * 4) == 0);
    }

    TEST_CASE("Clip splits exactly on axial planes")
    {
        Winding w = Square();
        Winding *front = nullptr, *back = nullptr;
        w.Clip(Plane(1, 0, 0, 1), &front, &back);
        REQUIRE(front != nullptr);
        REQUIRE(back != nullptr);
        CHECK(front->m_NumPoints == 4);
        CHECK(back->m_NumPoints == 4);
        CHECK(front->getArea() == 2);
        CHECK(back->getArea() == 2);
        for (unsigned i = 0; i < front->m_NumPoints; i++)
        {
            CHECK(front->m_Points[i][0] >= 1); // mid points snap to dist exactly
        }
        delete front;
        delete back;
    }

    TEST_CASE("Clip with everything on one side returns a copy and a null")
    {
        Winding w = Square();
        Winding *front = nullptr, *back = nullptr;
        w.Clip(Plane(1, 0, 0, -5), &front, &back);
        CHECK(back == nullptr);
        REQUIRE(front != nullptr);
        CHECK(front != &w);
        CHECK(front->m_NumPoints == 4);
        delete front;

        w.Clip(Plane(1, 0, 0, 5), &front, &back);
        CHECK(front == nullptr);
        REQUIRE(back != nullptr);
        delete back;
    }

    TEST_CASE("Clip by a plane through the winding's own plane keeps it on the back")
    {
        Winding w = Square();
        Winding *front = nullptr, *back = nullptr;
        w.Clip(Plane(0, 0, 1, 0), &front, &back);
        CHECK(front == nullptr);
        REQUIRE(back != nullptr);
        delete back;
    }

    TEST_CASE("Divide returns the winding itself when it is not split (compat: aliasing)")
    {
        Winding w = Square();
        Winding *front = nullptr, *back = nullptr;
        w.Divide(Plane(1, 0, 0, -5), &front, &back);
        CHECK(front == &w);
        CHECK(back == nullptr);

        w.Divide(Plane(1, 0, 0, 5), &front, &back);
        CHECK(front == nullptr);
        CHECK(back == &w);

        // On-plane: decided by the sign of the summed distances (here 0 -> back).
        w.Divide(Plane(0, 0, 1, 0), &front, &back);
        CHECK(front == nullptr);
        CHECK(back == &w);

        w.Divide(Plane(1, 0, 0, 1), &front, &back);
        REQUIRE(front != nullptr);
        REQUIRE(back != nullptr);
        CHECK(front != &w);
        CHECK(front->getArea() == 2);
        delete front;
        delete back;
    }

    TEST_CASE("Clip(keepon) clips in place and keeps the front")
    {
        Winding w = Square();
        CHECK(w.Clip(Plane(1, 0, 0, 1), false));
        CHECK(w.m_NumPoints == 4);
        CHECK(w.getArea() == 2);

        Winding on = Square();
        CHECK(on.Clip(Plane(0, 0, 1, 0), true));
        CHECK(on.m_NumPoints == 4);

        Winding gone = Square();
        CHECK(!gone.Clip(Plane(0, 0, 1, 0), false));
        CHECK(gone.m_NumPoints == 0);
        CHECK(gone.m_Points == nullptr);
    }

    TEST_CASE("Chop keeps the front side in place")
    {
        Winding w = Square();
        vec3_t n = {0, 1, 0};
        CHECK(w.Chop(n, 1));
        CHECK(w.getArea() == 2);
        vec3_t n2 = {0, 1, 0};
        CHECK(!w.Chop(n2, 10));
        CHECK(w.m_NumPoints == 0);
    }

    TEST_CASE("WindingOnPlaneSide")
    {
        Winding w = Square();
        vec3_t x = {1, 0, 0}, z = {0, 0, 1};
        CHECK(w.WindingOnPlaneSide(x, -1) == SIDE_FRONT);
        CHECK(w.WindingOnPlaneSide(x, 3) == SIDE_BACK);
        CHECK(w.WindingOnPlaneSide(x, 1) == SIDE_CROSS);
        CHECK(w.WindingOnPlaneSide(z, 0) == SIDE_ON);
        CHECK(w.WindingOnPlaneSide(z, 0.03) == SIDE_ON); // within ON_EPSILON
    }

    TEST_CASE("RemoveColinearPoints drops points on an edge")
    {
        vec3_t pts[5] = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {2, 2, 0}, {2, 0, 0}};
        Winding w(pts, 5);
        w.RemoveColinearPoints();
        CHECK(w.m_NumPoints == 4);
        CHECK(w.m_Points[1][1] == 2);
    }

    TEST_CASE("winding from a plane is a huge quad on the plane")
    {
        vec3_t n = {0, 0, 1};
        Winding w(n, 64);
        CHECK(w.m_NumPoints == 4);
        for (unsigned i = 0; i < 4; i++)
        {
            CHECK(w.m_Points[i][2] == 64);
            CHECK(std::abs(w.m_Points[i][0]) == 80000);
            CHECK(std::abs(w.m_Points[i][1]) == 80000);
        }
        vec3_t pn;
        vec_t pd;
        w.getPlane(pn, pd);
        CHECK(pn[2] == 1); // same orientation as the plane
        CHECK(pd == 64);
    }

    TEST_CASE("points can be added to windings built from a plane (capacity used to be uninitialized)")
    {
        vec3_t n = {0, 0, 1};
        Winding w(n, 64);
        vec3_t extra = {1, 2, 64};
        for (int i = 0; i < 20; i++) // grows several times
        {
            w.addPoint(extra);
        }
        CHECK(w.m_NumPoints == 24);
        CHECK(std::abs(w.m_Points[0][0]) == 80000); // the original points survive each resize
        CHECK(w.m_Points[3][2] == 64);
        CHECK(w.m_Points[23][1] == 2);
        vec3_t first = {7, 7, 64};
        w.insertPoint(first, 0);
        CHECK(w.m_Points[0][0] == 7);
        CHECK(std::abs(w.m_Points[1][0]) == 80000);
        CHECK(w.m_NumPoints == 25);
    }

    TEST_CASE("windings with more than MAX_POINTS_ON_WINDING points can be clipped and divided")
    {
        // The old fixed-size side/distance arrays overflowed here.
        constexpr int N = 200;
        vec3_t pts[N];
        for (int i = 0; i < N; i++)
        {
            const double a = 2 * Q_PI * i / N;
            pts[i][0] = 100 * std::cos(a);
            pts[i][1] = 100 * std::sin(a);
            pts[i][2] = 0;
        }
        Winding big(pts, N);
        Winding clipped(big);
        CHECK(clipped.Clip(Plane(1, 0, 0, 0), false));
        CHECK(clipped.m_NumPoints > 90);
        CHECK(clipped.m_NumPoints < 110);
        for (unsigned i = 0; i < clipped.m_NumPoints; i++)
        {
            CHECK(clipped.m_Points[i][0] >= -ON_EPSILON); // points within epsilon of the plane are kept
        }
        vec3_t extra = {0, 0, 0};
        clipped.addPoint(extra); // capacity follows the new points

        Winding *front = nullptr, *back = nullptr;
        big.Divide(Plane(0, 1, 0, 0), &front, &back);
        REQUIRE(front != nullptr);
        REQUIRE(back != nullptr);
        CHECK(front != &big);
        CHECK(front->m_NumPoints + back->m_NumPoints > N);
        delete front;
        delete back;
    }

    TEST_CASE("Chop keeps a usable capacity")
    {
        Winding w = Square();
        vec3_t n = {1, 0, 0};
        REQUIRE(w.Chop(n, 1));
        vec3_t extra = {1.5, 1, 0};
        for (int i = 0; i < 10; i++)
        {
            w.addPoint(extra);
        }
        CHECK(w.m_NumPoints == 14);
        CHECK(w.m_Points[0][0] >= 1);
    }

    TEST_CASE("self-assignment and moves")
    {
        Winding w = Square();
        Winding &alias = w;
        w = alias;
        CHECK(w.m_NumPoints == 4);
        CHECK(w.getArea() == 4);

        Winding moved(std::move(w));
        CHECK(moved.m_NumPoints == 4);
        CHECK(w.m_NumPoints == 0);
        CHECK(!w.m_Points);
        Winding target;
        target = std::move(moved);
        CHECK(target.getArea() == 4);
        CHECK(moved.m_NumPoints == 0);
    }

    TEST_CASE("Split and DivideBy own their results")
    {
        const Winding w = Square();
        vec3_t n = {1, 0, 0};
        WindingSplit s = w.Split(n, 1);
        REQUIRE(s.front);
        REQUIRE(s.back);
        CHECK(s.front->getArea() == 2);
        CHECK(s.back->getArea() == 2);

        WindingSplit outside = w.Split(n, 5);
        CHECK(!outside.front);
        REQUIRE(outside.back);
        CHECK(outside.back->m_NumPoints == 4); // a copy; w is unchanged
        CHECK(w.m_NumPoints == 4);

        WindingDivision d = w.DivideBy(n, -5);
        CHECK(d.side == SIDE_FRONT);
        CHECK(!d.front);
        WindingDivision cross = w.DivideBy(n, 1);
        CHECK(cross.side == SIDE_CROSS);
        REQUIRE(cross.front);
        CHECK(cross.front->getArea() == 2);
        vec3_t z = {0, 0, 1};
        CHECK(w.DivideBy(z, 0).side == SIDE_BACK); // on the plane: summed distance 0 -> back
    }

    TEST_CASE("plane-taking functions accept any plane type with normal and dist")
    {
        struct DoublePlane // like hlbsp's plane_t
        {
            double normal[3];
            double unused_origin[3];
            double dist;
        };
        const DoublePlane dp{{1, 0, 0}, {0, 0, 0}, 1};
        const dplane_t fp = Plane(1, 0, 0, 1);

        Winding a = Square(), b = Square();
        CHECK(a.Clip(dp, false));
        CHECK(b.Clip(fp, false));
        CHECK(a.getArea() == b.getArea());

        Winding pw(dp);
        CHECK(pw.m_NumPoints == 4);
        DoublePlane out{};
        Square().getPlane(out);
        CHECK(out.normal[2] == 1);
        CHECK(out.dist == 0);
    }

    TEST_CASE("winding from a dface_t follows surfedge signs")
    {
        const int saved_numvertexes = g_numvertexes, saved_numedges = g_numedges, saved_numsurfedges = g_numsurfedges;
        const float verts[4][3] = {{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};
        for (int i = 0; i < 4; i++)
        {
            std::memcpy(g_dvertexes[i].point, verts[i], sizeof(verts[i]));
        }
        // edge 0 unused; edges 1..4 go around the square, surfedge 3 is used backwards
        const unsigned short edges[5][2] = {{0, 0}, {0, 1}, {1, 2}, {3, 2}, {3, 0}};
        for (int i = 0; i < 5; i++)
        {
            g_dedges[i].v[0] = edges[i][0];
            g_dedges[i].v[1] = edges[i][1];
        }
        const int surfedges[4] = {1, 2, -3, 4};
        std::memcpy(g_dsurfedges, surfedges, sizeof(surfedges));

        dface_t face{};
        face.firstedge = 0;
        face.numedges = 4;
        Winding w(face);
        REQUIRE(w.m_NumPoints == 4);
        CHECK(w.m_Points[0][1] == 0);
        CHECK(w.m_Points[1][1] == 64);
        CHECK(w.m_Points[2][0] == 64); // from the reversed edge: v[1] of edge 3 = vertex 2
        CHECK(w.m_Points[2][1] == 64);
        CHECK(w.m_Points[3][0] == 64);
        CHECK(w.getArea() == 64 * 64);

        g_numvertexes = saved_numvertexes;
        g_numedges = saved_numedges;
        g_numsurfedges = saved_numsurfedges;
    }
}
