#include "common/winding.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

#include "common/hlassert.h"
#include "common/log.h"
#include "common/mathlib.h"
#include "common/mathtypes.h"

namespace
{
// winding.cpp's own extent for "bogus" coordinates and the size of the initial plane winding
// (the tools' BOGUS_RANGE macros are different values).
constexpr double WINDING_BOGUS_RANGE = 80000.0;

// Per-point scratch space for the clipping functions: on the stack for ordinary windings, on
// the heap for oversized ones (the old fixed MAX_POINTS_ON_WINDING arrays overflowed there).
template <typename T>
class PointScratch
{
  public:
    explicit PointScratch(std::size_t n)
        : m_data(n <= std::size(m_stack) ? m_stack : (m_heap = std::make_unique_for_overwrite<T[]>(n)).get())
    {
    }
    T &operator[](std::size_t i) { return m_data[i]; }

  private:
    T m_stack[MAX_POINTS_ON_WINDING + 4];
    std::unique_ptr<T[]> m_heap;
    T *m_data;
};
} // namespace

// Winding Public Methods
void Winding::Print() const
{
    for (std::uint32_t x = 0; x < m_NumPoints; x++)
    {
        Log("(%5.2f, %5.2f, %5.2f)\n", m_Points[x][0], m_Points[x][1], m_Points[x][2]);
    }
}

void Winding::getPlane(vec3_t &normal, vec_t &dist) const
{
    vec3_t v1, v2;

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[1], m_Points[0], v1);
        VectorSubtract(m_Points[2], m_Points[0], v2);
        CrossProduct(v2, v1, normal);
        VectorNormalize(normal);
        dist = DotProduct(m_Points[0], normal);
    }
    else
    {
        VectorClear(normal);
        dist = 0.0;
    }
}

vec_t Winding::getArea() const
{
    vec3_t d1, d2, cross;

    vec_t total = 0.0;
    if (m_NumPoints >= 3)
    {
        for (unsigned int i = 2; i < m_NumPoints; i++)
        {
            VectorSubtract(m_Points[i - 1], m_Points[0], d1);
            VectorSubtract(m_Points[i], m_Points[0], d2);
            CrossProduct(d1, d2, cross);
            total += 0.5 * VectorLength(cross);
        }
    }
    return total;
}

void Winding::getBounds(vec3_t &mins, vec3_t &maxs) const
{
    BoundingBox bounds;

    for (unsigned x = 0; x < m_NumPoints; x++)
    {
        bounds.add(m_Points[x]);
    }
    VectorCopy(bounds.m_Mins, mins);
    VectorCopy(bounds.m_Maxs, maxs);
}

void Winding::getBounds(BoundingBox &bounds) const
{
    bounds.reset();

    for (unsigned x = 0; x < m_NumPoints; x++)
    {
        bounds.add(m_Points[x]);
    }
}

void Winding::getCenter(vec3_t &center) const
{
    if (m_NumPoints > 0)
    {
        VectorCopy(vec3_origin, center);
        for (unsigned int i = 0; i < m_NumPoints; i++)
        {
            VectorAdd(m_Points[i], center, center);
        }

        vec_t scale = 1.0 / m_NumPoints;
        VectorScale(center, scale, center);
    }
    else
    {
        VectorClear(center);
    }
}

Winding *Winding::Copy() const
{
    return new Winding(*this);
}

void Winding::Check(vec_t epsilon) const
{
    unsigned int j;
    vec3_t dir, edgenormal, facenormal;
    vec_t facedist;

    if (m_NumPoints < 3)
    {
        Error("Winding::Check : %i points", m_NumPoints);
    }

    vec_t area = getArea();
    if (area < 1)
    {
        Error("Winding::Check : %f area", area);
    }

    getPlane(facenormal, facedist);

    for (unsigned int i = 0; i < m_NumPoints; i++)
    {
        vec_t *p1 = m_Points[i];

        for (j = 0; j < 3; j++)
        {
            if (p1[j] > WINDING_BOGUS_RANGE || p1[j] < -WINDING_BOGUS_RANGE)
            {
                Error("Winding::Check : BOGUS_RANGE: %f", p1[j]);
            }
        }

        j = i + 1 == m_NumPoints ? 0 : i + 1;

        // check the point is on the face plane
        vec_t d = DotProduct(p1, facenormal) - facedist;
        if (d < -epsilon || d > epsilon)
        {
            Error("Winding::Check : point off plane");
        }

        // check the edge isn't degenerate
        vec_t *p2 = m_Points[j];
        VectorSubtract(p2, p1, dir);

        if (VectorLength(dir) < epsilon)
        {
            Error("Winding::Check : degenerate edge");
        }

        CrossProduct(facenormal, dir, edgenormal);
        VectorNormalize(edgenormal);
        vec_t edgedist = DotProduct(p1, edgenormal);
        edgedist += epsilon;

        // all other points must be on front side
        for (j = 0; j < m_NumPoints; j++)
        {
            if (j == i)
            {
                continue;
            }
            d = DotProduct(m_Points[j], edgenormal);
            if (d > edgedist)
            {
                Error("Winding::Check : non-convex");
            }
        }
    }
}

bool Winding::Valid() const
{
    // TODO: Check to ensure there are 3 non-colinear points
    if ((m_NumPoints < 3) || (!m_Points))
    {
        return false;
    }
    return true;
}
// Construction
void Winding::allocate(std::uint32_t maxpoints)
{
    m_Points = std::make_unique_for_overwrite<vec3_t[]>(maxpoints);
    m_MaxPoints = maxpoints;
}

Winding::Winding() : m_NumPoints(0), m_MaxPoints(0)
{
}

Winding::Winding(vec3_t *points, std::uint32_t numpoints) : m_NumPoints(numpoints)
{
    hlassert(numpoints >= 3);
    allocate((m_NumPoints + 3) & ~3); // groups of 4
    std::memcpy(m_Points.get(), points, sizeof(vec3_t) * m_NumPoints);
}

void Winding::initFromPoints(vec3_t *points, std::uint32_t numpoints)
{
    hlassert(numpoints >= 3);

    Reset();

    m_NumPoints = numpoints;
    allocate((m_NumPoints + 3) & ~3); // groups of 4
    std::memcpy(m_Points.get(), points, sizeof(vec3_t) * m_NumPoints);
}

Winding &Winding::operator=(const Winding &other)
{
    if (this != &other)
    {
        m_NumPoints = other.m_NumPoints;
        allocate((m_NumPoints + 3) & ~3); // groups of 4
        std::memcpy(m_Points.get(), other.m_Points.get(), sizeof(vec3_t) * m_NumPoints);
    }
    return *this;
}

Winding &Winding::operator=(Winding &&other) noexcept
{
    m_NumPoints = std::exchange(other.m_NumPoints, 0);
    m_Points = std::move(other.m_Points);
    m_MaxPoints = std::exchange(other.m_MaxPoints, 0);
    return *this;
}

Winding::Winding(std::uint32_t numpoints) : m_NumPoints(numpoints)
{
    hlassert(numpoints >= 3);
    allocate((m_NumPoints + 3) & ~3); // groups of 4
    std::memset(m_Points.get(), 0, sizeof(vec3_t) * m_NumPoints);
}

Winding::Winding(const Winding &other) : m_NumPoints(other.m_NumPoints)
{
    allocate((m_NumPoints + 3) & ~3); // groups of 4
    std::memcpy(m_Points.get(), other.m_Points.get(), sizeof(vec3_t) * m_NumPoints);
}

Winding::Winding(Winding &&other) noexcept
    : m_NumPoints(std::exchange(other.m_NumPoints, 0)), m_Points(std::move(other.m_Points)),
      m_MaxPoints(std::exchange(other.m_MaxPoints, 0))
{
}

Winding::~Winding() = default;

void Winding::initFromPlane(const vec3_t normal, const vec_t dist)
{
    vec_t v;
    vec3_t org, vright, vup;

    // find the major axis

    vec_t max = -WINDING_BOGUS_RANGE;
    int x = -1;
    for (int i = 0; i < 3; i++)
    {
        v = std::abs(normal[i]);
        if (v > max)
        {
            max = v;
            x = i;
        }
    }
    if (x == -1)
    {
        Error("Winding::initFromPlane no major axis found\n");
    }

    VectorCopy(vec3_origin, vup);
    switch (x)
    {
    case 0:
    case 1:
        vup[2] = 1;
        break;
    case 2:
        vup[0] = 1;
        break;
    }

    v = DotProduct(vup, normal);
    VectorMA(vup, -v, normal, vup);
    VectorNormalize(vup);

    VectorScale(normal, dist, org);

    CrossProduct(vup, normal, vright);

    VectorScale(vup, WINDING_BOGUS_RANGE, vup);
    VectorScale(vright, WINDING_BOGUS_RANGE, vright);

    // project a really big     axis aligned box onto the plane
    m_NumPoints = 4;
    allocate(m_NumPoints);

    VectorSubtract(org, vright, m_Points[0]);
    VectorAdd(m_Points[0], vup, m_Points[0]);

    VectorAdd(org, vright, m_Points[1]);
    VectorAdd(m_Points[1], vup, m_Points[1]);

    VectorAdd(org, vright, m_Points[2]);
    VectorSubtract(m_Points[2], vup, m_Points[2]);

    VectorSubtract(org, vright, m_Points[3]);
    VectorSubtract(m_Points[3], vup, m_Points[3]);
}

Winding::Winding(const vec3_t normal, const vec_t dist) : m_NumPoints(0), m_MaxPoints(0)
{
    initFromPlane(normal, dist);
}

Winding::Winding(const dface_t &face, vec_t epsilon) : m_NumPoints(face.numedges > 0 ? face.numedges : 0)
{
    allocate(m_NumPoints);

    for (unsigned i = 0; i < m_NumPoints; i++)
    {
        int v;
        int se = g_dsurfedges[face.firstedge + i];
        if (se < 0)
        {
            v = g_dedges[-se].v[1];
        }
        else
        {
            v = g_dedges[se].v[0];
        }

        dvertex_t *dv = &g_dvertexes[v];
        VectorCopy(dv->point, m_Points[i]);
    }

    RemoveColinearPoints(epsilon);
}

// Specialized Functions
// Remove the colinear point of any three points that forms a triangle which is thinner than epsilon
void Winding::RemoveColinearPoints(vec_t epsilon)
{
    vec3_t v1, v2;
    for (unsigned int i = 0; i < m_NumPoints;)
    {
        vec_t *p1 = m_Points[(i + m_NumPoints - 1) % m_NumPoints];
        vec_t *p2 = m_Points[i];
        vec_t *p3 = m_Points[(i + 1) % m_NumPoints];
        VectorSubtract(p2, p1, v1);
        VectorSubtract(p3, p2, v2);
        // v1 or v2 might be close to 0
        if (DotProduct(v1, v2) * DotProduct(v1, v2) >= DotProduct(v1, v1) * DotProduct(v2, v2) - epsilon * epsilon * (DotProduct(v1, v1) + DotProduct(v2, v2) + epsilon * epsilon))
        // v2 == k * v1 + v3 && abs (v3) < epsilon || v1 == k * v2 + v3 && abs (v3) < epsilon
        {
            m_NumPoints--;
            for (; i < m_NumPoints; i++)
            {
                VectorCopy(m_Points[i + 1], m_Points[i]);
            }
            i = 0; // start over
            continue;
        }
        i++;
    }
}

WindingSplit Winding::Split(const vec3_t normal, const vec_t dist, vec_t epsilon) const
{
    PointScratch<vec_t> dists(m_NumPoints + 1);
    PointScratch<int> sides(m_NumPoints + 1);
    int counts[3] = {0, 0, 0};
    vec_t dot;
    unsigned int i;

    // determine sides for each point
    for (i = 0; i < m_NumPoints; i++)
    {
        dot = DotProduct(m_Points[i], normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    if (!counts[0])
    {
        return {nullptr, std::make_unique<Winding>(*this)};
    }
    if (!counts[1])
    {
        return {std::make_unique<Winding>(*this), nullptr};
    }

    unsigned int maxpts = m_NumPoints + 4; // can't use counts[0]+2 because
    // of fp grouping errors

    auto f = std::make_unique<Winding>(maxpts);
    auto b = std::make_unique<Winding>(maxpts);

    f->m_NumPoints = 0;
    b->m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        vec_t *p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            f->m_NumPoints++;
            b->m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            f->m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            b->m_NumPoints++;
        }

        if ((sides[i + 1] == SIDE_ON) | (sides[i + 1] == sides[i])) // | instead of || for branch optimization
        {
            continue;
        }

        // generate a split point
        vec3_t mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
        {
            tmp = 0;
        }
        vec_t *p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);

        for (unsigned int j = 0; j < 3; j++)
        { // avoid round off error when possible
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, f->m_Points[f->m_NumPoints]);
        VectorCopy(mid, b->m_Points[b->m_NumPoints]);
        f->m_NumPoints++;
        b->m_NumPoints++;
    }

    if ((f->m_NumPoints > maxpts) | (b->m_NumPoints > maxpts)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : points exceeded estimate");
    }
    if ((f->m_NumPoints > MAX_POINTS_ON_WINDING) | (b->m_NumPoints > MAX_POINTS_ON_WINDING)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : MAX_POINTS_ON_WINDING");
    }
    f->RemoveColinearPoints(epsilon);
    b->RemoveColinearPoints(epsilon);
    if (f->m_NumPoints == 0)
    {
        f.reset();
    }
    if (b->m_NumPoints == 0)
    {
        b.reset();
    }
    return {std::move(f), std::move(b)};
}

void Winding::Clip(const vec3_t normal, const vec_t dist, Winding **front, Winding **back, vec_t epsilon)
{
    WindingSplit s = Split(normal, dist, epsilon);
    *front = s.front.release();
    *back = s.back.release();
}

bool Winding::Chop(const vec3_t normal, const vec_t dist, vec_t epsilon)
{
    WindingSplit s = Split(normal, dist, epsilon);

    if (s.front)
    {
        *this = std::move(*s.front);
        return true;
    }
    else
    {
        Reset();
        return false;
    }
}

int Winding::WindingOnPlaneSide(const vec3_t normal, const vec_t dist, vec_t epsilon)
{
    bool front = false;
    bool back = false;
    for (unsigned int i = 0; i < m_NumPoints; i++)
    {
        vec_t d = DotProduct(m_Points[i], normal) - dist;
        if (d < -epsilon)
        {
            if (front)
            {
                return SIDE_CROSS;
            }
            back = true;
            continue;
        }
        if (d > epsilon)
        {
            if (back)
            {
                return SIDE_CROSS;
            }
            front = true;
            continue;
        }
    }

    if (back)
    {
        return SIDE_BACK;
    }
    if (front)
    {
        return SIDE_FRONT;
    }
    return SIDE_ON;
}

bool Winding::ClipInPlace(const vec3_t normal, const vec_t dist, bool keepon, vec_t epsilon)
{
    PointScratch<vec_t> dists(m_NumPoints + 1);
    PointScratch<int> sides(m_NumPoints + 1);
    int counts[3] = {0, 0, 0};
    vec_t dot;
    unsigned int i;

    // determine sides for each point
    // do this exactly, with no epsilon so tiny portals still work
    for (i = 0; i < m_NumPoints; i++)
    {
        dot = DotProduct(m_Points[i], normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    if (keepon && !counts[0] && !counts[1])
    {
        return true;
    }

    if (!counts[0])
    {
        Reset();
        return false;
    }

    if (!counts[1])
    {
        return true;
    }

    unsigned maxpts = m_NumPoints + 4; // can't use counts[0]+2 because of fp grouping errors
    unsigned newNumPoints = 0;
    auto newPoints = std::make_unique<vec3_t[]>(maxpts); // zeroed

    for (i = 0; i < m_NumPoints; i++)
    {
        vec_t *p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, newPoints[newNumPoints]);
            newNumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, newPoints[newNumPoints]);
            newNumPoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_t mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
        {
            tmp = 0;
        }
        vec_t *p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (int j = 0; j < 3; j++)
        { // avoid round off error when possible
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, newPoints[newNumPoints]);
        newNumPoints++;
    }

    if (newNumPoints > maxpts)
    {
        Error("Winding::Clip : points exceeded estimate");
    }

    m_Points = std::move(newPoints);
    m_MaxPoints = maxpts;
    m_NumPoints = newNumPoints;

    RemoveColinearPoints(epsilon);
    if (m_NumPoints == 0)
    {
        Reset();
        return false;
    }

    return true;
}

// Divides a winding by a plane, producing one or two windings. The original winding is not
// damaged. If it lies on only one side, that side is reported and no winding is created.
WindingDivision Winding::DivideBy(const vec3_t normal, const vec_t dist, vec_t epsilon) const
{
    PointScratch<vec_t> dists(m_NumPoints + 1);
    PointScratch<int> sides(m_NumPoints + 1);
    int counts[3] = {0, 0, 0};
    vec_t dot;
    unsigned int i;

    // determine sides for each point
    for (i = 0; i < m_NumPoints; i++)
    {
        dot = DotProduct(m_Points[i], normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    if (!counts[0] && !counts[1])
    {
        vec_t sum = 0.0;
        for (i = 0; i < m_NumPoints; i++)
        {
            dot = DotProduct(m_Points[i], normal);
            dot -= dist;
            sum += dot;
        }
        return {sum > NORMAL_EPSILON ? SIDE_FRONT : SIDE_BACK, nullptr, nullptr};
    }
    if (!counts[0])
    {
        return {SIDE_BACK, nullptr, nullptr};
    }
    if (!counts[1])
    {
        return {SIDE_FRONT, nullptr, nullptr};
    }

    unsigned int maxpts = m_NumPoints + 4; // can't use counts[0]+2 because
    // of fp grouping errors

    auto f = std::make_unique<Winding>(maxpts);
    auto b = std::make_unique<Winding>(maxpts);

    f->m_NumPoints = 0;
    b->m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        vec_t *p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            f->m_NumPoints++;
            b->m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            f->m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            b->m_NumPoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_t mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
        {
            tmp = 0;
        }
        vec_t *p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (int j = 0; j < 3; j++)
        { // avoid round off error when possible
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, f->m_Points[f->m_NumPoints]);
        VectorCopy(mid, b->m_Points[b->m_NumPoints]);
        f->m_NumPoints++;
        b->m_NumPoints++;
    }

    if (f->m_NumPoints > maxpts || b->m_NumPoints > maxpts)
    {
        Error("Winding::Divide : points exceeded estimate");
    }

    f->RemoveColinearPoints(epsilon);
    b->RemoveColinearPoints(epsilon);
    if (f->m_NumPoints == 0)
    {
        return {SIDE_BACK, nullptr, nullptr};
    }
    if (b->m_NumPoints == 0)
    {
        return {SIDE_FRONT, nullptr, nullptr};
    }
    return {SIDE_CROSS, std::move(f), std::move(b)};
}

void Winding::addPoint(const vec3_t newpoint)
{
    if (m_NumPoints >= m_MaxPoints)
    {
        resize(m_NumPoints + 1);
    }
    VectorCopy(newpoint, m_Points[m_NumPoints]);
    m_NumPoints++;
}

void Winding::insertPoint(const vec3_t newpoint, const unsigned int offset)
{
    if (offset >= m_NumPoints)
    {
        addPoint(newpoint);
    }
    else
    {
        if (m_NumPoints >= m_MaxPoints)
        {
            resize(m_NumPoints + 1);
        }

        unsigned x;
        for (x = m_NumPoints; x > offset; x--)
        {
            VectorCopy(m_Points[x - 1], m_Points[x]);
        }
        VectorCopy(newpoint, m_Points[x]);

        m_NumPoints++;
    }
}

void Winding::resize(std::uint32_t newsize)
{
    newsize = (newsize + 3) & ~3; // groups of 4

    auto newpoints = std::make_unique_for_overwrite<vec3_t[]>(newsize);
    m_NumPoints = qmin(newsize, m_NumPoints);
    if (m_NumPoints)
    {
        std::memcpy(newpoints.get(), m_Points.get(), sizeof(vec3_t) * m_NumPoints); // was m_NumPoints bytes
    }
    m_Points = std::move(newpoints);
    m_MaxPoints = newsize;
}

void Winding::CopyPoints(vec3_t *points, int &numpoints)
{
    if (!points)
    {
        numpoints = 0;
        return;
    }

    std::memcpy(points, m_Points.get(), sizeof(vec3_t) * m_NumPoints);

    numpoints = m_NumPoints;
}

void Winding::Reset()
{
    m_Points.reset();
    m_NumPoints = m_MaxPoints = 0;
}
