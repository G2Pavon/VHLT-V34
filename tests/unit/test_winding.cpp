#include "doctest/doctest.h"

#include <cstring>

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
        Winding *p = w.Copy();
        CHECK(p->m_NumPoints == 4);
        CHECK(p->m_Points != w.m_Points);
        delete p;
        Winding a;
        a = w;
        CHECK(a.m_NumPoints == 4);
        CHECK(std::memcmp(a.m_Points, w.m_Points, sizeof(vec3_t) * 4) == 0);
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
