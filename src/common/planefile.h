#pragma once

// <map>.pln: the plane table hlcsg hands to hlbsp, in full double precision (the .bsp only has
// floats). One record per plane, raw little-endian, nothing else in the file.
//
// compat: hlcsg used to write its in-memory faceplane_t array and hlbsp read it into its plane_t
// array, so the layout below is exactly what those structs looked like; the padding was always
// zero (checked over every .pln the oracle wrote for the corpus).

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct PlaneFileRecord
{
    double normal[3];
    double origin[3]; // the point the plane was created from (hlbsp does not use it)
    double dist;
    std::int32_t type; // planetypes
    std::int32_t padding;
};

static_assert(sizeof(PlaneFileRecord) == 64, "a .pln record is 64 bytes");
static_assert(std::is_trivially_copyable_v<PlaneFileRecord> && std::is_standard_layout_v<PlaneFileRecord>);
static_assert(offsetof(PlaneFileRecord, normal) == 0);
static_assert(offsetof(PlaneFileRecord, origin) == 24);
static_assert(offsetof(PlaneFileRecord, dist) == 48);
static_assert(offsetof(PlaneFileRecord, type) == 56);
static_assert(offsetof(PlaneFileRecord, padding) == 60);
