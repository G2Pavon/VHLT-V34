#include "doctest/doctest.h"

#include <cmath>
#include <array>
#include <random>
#include <vector>

#include "hlcsg/planeindex.h"

namespace
{
struct TestPlane
{
    vec_t normal[3];
    vec_t dist;
};

int LinearFind(const vec_t *normal, const vec_t *origin, const std::vector<TestPlane> &planes)
{
    for (std::size_t i = 0; i < planes.size(); i++)
    {
        if (PlaneMatches(normal, origin, planes[i]))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace

TEST_SUITE("planeindex")
{
    TEST_CASE("PlaneIndex::Find returns the same plane as the linear scan, near every epsilon")
    {
        std::mt19937_64 rng(12345);
        std::uniform_real_distribution<double> unit(-1.0, 1.0);
        std::uniform_int_distribution<int> pick(0, 99);

        // A few base directions; queries perturb them by multiples of DIR_EPSILON / 4 (straddling
        // the tolerance) plus sub-ulp-scale jitter, so many lookups sit right at the boundaries.
        std::vector<std::array<double, 3>> bases = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int i = 0; i < 10; i++)
        {
            double v[3] = {unit(rng), unit(rng), unit(rng)};
            const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            bases.push_back({v[0] / len, v[1] / len, v[2] / len});
        }

        std::vector<TestPlane> planes;
        PlaneIndex index;
        int found_count = 0, added = 0;
        for (int op = 0; op < 6000; op++)
        {
            const auto &b = bases[pick(rng) % bases.size()];
            vec_t normal[3];
            for (int k = 0; k < 3; k++)
            {
                const int steps = pick(rng) % 11 - 5; // -5..5 quarter-epsilons
                normal[k] = b[k] + steps * (DIR_EPSILON / 4.0) + (pick(rng) % 3 - 1) * 1e-12;
            }
            // a distance on a coarse grid, then an origin offset along the normal by up to about
            // 2 * PLANE_DIST_EPSILON, plus a large lateral component that stresses the dist window
            const double dist = (pick(rng) % 5 - 2) * 64.0;
            const double along = dist + (pick(rng) % 9 - 4) * (PLANE_DIST_EPSILON / 2.0) + (pick(rng) % 3 - 1) * 1e-9;
            // lateral offset perpendicular to the base direction: the perturbed normal tilts it by
            // up to 5/4 DIR_EPSILON, so at 30000 units it moves dot(origin, normal) across the
            // whole distance window
            double lateral[3] = {unit(rng), unit(rng), unit(rng)};
            const double proj = lateral[0] * b[0] + lateral[1] * b[1] + lateral[2] * b[2];
            const double scale = pick(rng) < 30 ? 30000.0 : 0.0;
            vec_t origin[3];
            for (int k = 0; k < 3; k++)
            {
                origin[k] = normal[k] * along + (lateral[k] - proj * b[k]) * scale;
            }

            const int expected = LinearFind(normal, origin, planes);
            const int actual = index.Find(normal, origin, planes);
            CAPTURE(op);
            REQUIRE(actual == expected);
            if (expected == -1)
            {
                TestPlane p{{normal[0], normal[1], normal[2]}, DotProduct(origin, normal)};
                index.Insert(static_cast<int>(planes.size()), p.normal, p.dist);
                planes.push_back(p);
                added++;
            }
            else
            {
                found_count++;
            }
        }
        MESSAGE(planes.size() << " planes; " << found_count << " lookups matched an existing plane, " << added << " added one");
        CHECK(found_count > 1000); // the test must exercise both outcomes
        CHECK(added > 500);
    }

    TEST_CASE("the lowest-index match wins when several planes match")
    {
        std::vector<TestPlane> planes = {{{0, 0, 1}, 100}, {{0, 0, 1}, 100.02}, {{0, 0, 1}, 99.99}};
        PlaneIndex index;
        for (int i = 2; i >= 0; i--) // insertion order must not matter
        {
            index.Insert(i, planes[i].normal, planes[i].dist);
        }
        const vec_t normal[3] = {0, 0, 1};
        const vec_t origin[3] = {0, 0, 100.01};
        CHECK(LinearFind(normal, origin, planes) == 0);
        CHECK(index.Find(normal, origin, planes) == 0);
        const vec_t far_origin[3] = {0, 0, 100.05}; // only plane 1 is within 0.04
        CHECK(index.Find(normal, far_origin, planes) == 1);
        const vec_t none[3] = {0, 0, 200};
        CHECK(index.Find(normal, none, planes) == -1);
    }
}
