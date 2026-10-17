#pragma once

// Exact-result index for hlcsg's plane table.
//
// FindIntPlane used to scan every plane and return the first (lowest-index) one accepted by
// PlaneMatches. PlaneIndex returns that same plane: it only narrows down which planes are
// tested, and tests them with the same PlaneMatches.
//
// Narrowing:
//  * normals: planes are bucketed by the cell of each normal component, cell size
//    2 * DIR_EPSILON. A plane that matches has every component within DIR_EPSILON of the query,
//    so it lies in a cell between CellOf(q - DIR_EPSILON - margin) and CellOf(q + DIR_EPSILON +
//    margin) (CellOf is monotonic; the margin covers rounding in the comparison).
//  * distance: within a cell, planes are ordered by dist. If plane i matches,
//    |dist_i - dot(origin, n_i)| < DIST_EPSILON and |dot(origin, n_i) - dot(origin, q)| <
//    DIR_EPSILON * sum|origin_k|, so dist_i lies in a window around dot(origin, q).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>

#include "common/mathlib.h"

// Tolerance FindIntPlane allows between a plane's distance and the origin it is looked up with.
constexpr vec_t PLANE_DIST_EPSILON = 0.04;

// The test the linear scan applied (unchanged, including its evaluation order).
template <typename Plane>
inline bool PlaneMatches(const vec_t *const normal, const vec_t *const origin, const Plane &plane)
{
    vec_t t;
    if (-DIR_EPSILON < (t = normal[0] - plane.normal[0]) && t < DIR_EPSILON &&
        -DIR_EPSILON < (t = normal[1] - plane.normal[1]) && t < DIR_EPSILON &&
        -DIR_EPSILON < (t = normal[2] - plane.normal[2]) && t < DIR_EPSILON)
    {
        t = DotProduct(origin, plane.normal) - plane.dist;

        if (-PLANE_DIST_EPSILON < t && t < PLANE_DIST_EPSILON)
        {
            return true;
        }
    }
    return false;
}

class PlaneIndex
{
  public:
    void Clear() { m_cells.clear(); }

    void Insert(int index, const vec_t *const normal, vec_t dist)
    {
        m_cells[Key{CellOf(normal[0]), CellOf(normal[1]), CellOf(normal[2])}].emplace(dist, index);
    }

    // Lowest index i such that PlaneMatches(normal, origin, planes[i]), or -1.
    template <typename Planes>
    int Find(const vec_t *const normal, const vec_t *const origin, const Planes &planes) const
    {
        constexpr double margin = 1e-9; // far above any rounding in PlaneMatches' differences
        std::int64_t lo[3], hi[3];
        for (int k = 0; k < 3; k++)
        {
            lo[k] = CellOf(normal[k] - DIR_EPSILON - margin);
            hi[k] = CellOf(normal[k] + DIR_EPSILON + margin);
        }
        const double center = DotProduct(origin, normal);
        const double spread = std::abs(origin[0]) + std::abs(origin[1]) + std::abs(origin[2]);
        const double slack = PLANE_DIST_EPSILON + DIR_EPSILON * spread + 1e-6 * (1.0 + std::abs(center) + spread);

        int best = -1;
        for (std::int64_t x = lo[0]; x <= hi[0]; x++)
        {
            for (std::int64_t y = lo[1]; y <= hi[1]; y++)
            {
                for (std::int64_t z = lo[2]; z <= hi[2]; z++)
                {
                    const auto cell = m_cells.find(Key{x, y, z});
                    if (cell == m_cells.end())
                    {
                        continue;
                    }
                    const auto end = cell->second.upper_bound(center + slack);
                    for (auto it = cell->second.lower_bound(center - slack); it != end; ++it)
                    {
                        const int i = it->second;
                        if ((best == -1 || i < best) && PlaneMatches(normal, origin, planes[i]))
                        {
                            best = i;
                        }
                    }
                }
            }
        }
        return best;
    }

  private:
    static std::int64_t CellOf(double component)
    {
        return static_cast<std::int64_t>(std::floor(component / (2.0 * DIR_EPSILON)));
    }

    struct Key
    {
        std::int64_t x, y, z;
        bool operator==(const Key &) const = default;
    };
    struct KeyHash
    {
        std::size_t operator()(const Key &k) const
        {
            std::size_t h = std::hash<std::int64_t>{}(k.x);
            h = h * 1000003u ^ std::hash<std::int64_t>{}(k.y);
            return h * 1000003u ^ std::hash<std::int64_t>{}(k.z);
        }
    };

    std::unordered_map<Key, std::multimap<double, int>, KeyHash> m_cells;
};
