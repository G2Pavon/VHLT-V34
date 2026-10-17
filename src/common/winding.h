#pragma once

#include <cstdint>
#include <memory>

#include "common/boundingbox.h"
#include "common/bspfile.h"
#include "common/mathlib.h"
#include "common/mathtypes.h"

constexpr int MAX_POINTS_ON_WINDING = 128;
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

constexpr int BASE_WINDING_DISTANCE = 9000;

constexpr int SIDE_FRONT = 0;
constexpr int SIDE_ON = 2;
constexpr int SIDE_BACK = 1;
constexpr int SIDE_CROSS = -2;

// Anything with a normal[3] and a dist: the disk dplane_t, hlbsp's plane_t, ...
template <typename P>
concept PlaneLike = requires(const P &p) {
    p.normal[0];
    p.dist;
};

class Winding;

// Result of Winding::Split: either side may be empty (nullptr).
struct WindingSplit
{
    std::unique_ptr<Winding> front;
    std::unique_ptr<Winding> back;
};

// Result of Winding::DivideBy: when the winding is not split, side says where it lies and
// front/back are empty; when it is split, side is SIDE_CROSS and front/back hold the pieces.
struct WindingDivision
{
    int side; // SIDE_FRONT, SIDE_BACK or SIDE_CROSS
    std::unique_ptr<Winding> front;
    std::unique_ptr<Winding> back;
};

class Winding
{
  public:
    // General Functions
    void Print() const;
    // compat: the normal is stored in the plane's own type first, and dist is computed from that.
    template <PlaneLike P>
    void getPlane(P &plane) const;
    void getPlane(vec3_t &normal, vec_t &dist) const;
    vec_t getArea() const;
    void getBounds(BoundingBox &bounds) const;
    void getBounds(vec3_t &mins, vec3_t &maxs) const;
    void getCenter(vec3_t &center) const;
    Winding *Copy() const;
    void Check(vec_t epsilon = ON_EPSILON) const; // Developer check for validity
    bool Valid() const;                           // Runtime/user/normal check for validity
    void addPoint(const vec3_t newpoint);
    void insertPoint(const vec3_t newpoint, const unsigned int offset);

    // Specialized Functions
    void RemoveColinearPoints(vec_t epsilon = ON_EPSILON);

    // Splits into new windings; this one is unchanged. Points on the plane go to both sides;
    // a side with no points is empty. If the winding lies on the plane it goes to the back.
    WindingSplit Split(const vec3_t normal, vec_t dist, vec_t epsilon = ON_EPSILON) const;
    // Keeps only the front part, in place. With keepon, a winding lying on the plane is kept.
    // Returns false (and empties the winding) if nothing is left.
    bool ClipInPlace(const vec3_t normal, vec_t dist, bool keepon, vec_t epsilon = ON_EPSILON);
    // Like Split, but a winding that is not split is reported by side instead of being copied,
    // and one lying on the plane goes to the side given by the sign of its summed distances.
    WindingDivision DivideBy(const vec3_t normal, vec_t dist, vec_t epsilon = ON_EPSILON) const;

    // Older interfaces, kept for the tools.
    template <PlaneLike P>
    bool Clip(const P &split, bool keepon, vec_t epsilon = ON_EPSILON); // ClipInPlace
    template <PlaneLike P>
    void Clip(const P &split, Winding **front, Winding **back, vec_t epsilon = ON_EPSILON); // Split; caller owns both
    void Clip(const vec3_t normal, const vec_t dist, Winding **front, Winding **back, vec_t epsilon = ON_EPSILON);
    // compat: a side that is not split is returned as this winding itself, not a copy.
    template <PlaneLike P>
    void Divide(const P &split, Winding **front, Winding **back, vec_t epsilon = ON_EPSILON);
    bool Chop(const vec3_t normal, const vec_t dist, vec_t epsilon = ON_EPSILON); // keeps the front, in place
    int WindingOnPlaneSide(const vec3_t normal, const vec_t dist, vec_t epsilon = ON_EPSILON);
    void CopyPoints(vec3_t *points, int &numpoints);

    void initFromPoints(vec3_t *points, std::uint32_t numpoints);
    void Reset(); // Resets the structure

  protected:
    void resize(std::uint32_t newsize);

  public:
    // Construction
    Winding();                                        // Do nothing :)
    Winding(vec3_t *points, std::uint32_t numpoints); // Create from raw points
    Winding(const dface_t &face, vec_t epsilon = ON_EPSILON);
    template <PlaneLike P>
    explicit Winding(const P &plane);
    Winding(const vec3_t normal, const vec_t dist);
    Winding(std::uint32_t points);
    Winding(const Winding &other);
    Winding(Winding &&other) noexcept;
    ~Winding();
    Winding &operator=(const Winding &other);
    Winding &operator=(Winding &&other) noexcept;

    // Misc
  private:
    void initFromPlane(const vec3_t normal, const vec_t dist);
    void allocate(std::uint32_t maxpoints); // uninitialized storage for maxpoints points

  public:
    // Data
    std::uint32_t m_NumPoints;
    std::unique_ptr<vec3_t[]> m_Points;

  protected:
    std::uint32_t m_MaxPoints; // allocated points
};

template <PlaneLike P>
void Winding::getPlane(P &plane) const
{
    vec3_t v1, v2;
    vec3_t plane_normal;

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[2], m_Points[1], v1);
        VectorSubtract(m_Points[0], m_Points[1], v2);

        CrossProduct(v2, v1, plane_normal);
        VectorNormalize(plane_normal);
        VectorCopy(plane_normal, plane.normal); // change from vec_t
        plane.dist = DotProduct(m_Points[0], plane.normal);
    }
    else
    {
        VectorClear(plane.normal);
        plane.dist = 0.0;
    }
}

// The plane's normal and dist are copied into vec_t first. That is exact (float to double, or
// same type), and the arithmetic below promoted mixed operands to vec_t anyway.
template <PlaneLike P>
bool Winding::Clip(const P &split, bool keepon, vec_t epsilon)
{
    vec3_t normal;
    VectorCopy(split.normal, normal);
    return ClipInPlace(normal, split.dist, keepon, epsilon);
}

template <PlaneLike P>
void Winding::Clip(const P &split, Winding **front, Winding **back, vec_t epsilon)
{
    vec3_t normal;
    VectorCopy(split.normal, normal);
    Clip(normal, split.dist, front, back, epsilon);
}

template <PlaneLike P>
void Winding::Divide(const P &split, Winding **front, Winding **back, vec_t epsilon)
{
    vec3_t normal;
    VectorCopy(split.normal, normal);
    WindingDivision d = DivideBy(normal, split.dist, epsilon);
    if (d.side == SIDE_CROSS)
    {
        *front = d.front.release();
        *back = d.back.release();
    }
    else
    {
        *front = d.side == SIDE_FRONT ? this : nullptr;
        *back = d.side == SIDE_BACK ? this : nullptr;
    }
}

template <PlaneLike P>
Winding::Winding(const P &plane) : m_NumPoints(0), m_MaxPoints(0)
{
    vec3_t normal;

    VectorCopy(plane.normal, normal);
    vec_t dist = plane.dist;
    initFromPlane(normal, dist);
}
