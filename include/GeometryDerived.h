#pragma once

#include "NodeTypes.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

// Windows headers define max/min as macros, which break std::max/std::min.
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace blast_demo
{
// Member role derived from geometry, not from a stored type enum. A vertical
// load-bearing member (column/wall) is tall relative to its plan; a horizontal
// floor plate is wide and flat. Column vs wall are NOT distinguished here —
// both carry vertical load and the solver treats them identically.
enum class MemberRole
{
    VerticalBearing,   // tall: carries load down (column, wall, brace)
    HorizontalPlate,   // flat: floor plate / beam spanning between supports
    Other
};

// Height-to-plan ratio that separates a tall bearing member from a flat plate.
// Columns (hy/hx ~3.7) and walls (hy/hx ~0.86) are both vertical bearings;
// floor plates (hy/hx ~0.16) are horizontal. The wall aspect (~0.86) is what
// separates the two families, so the tall threshold must sit below it.
constexpr float kTallAspect = 0.5f;
constexpr float kFlatAspect = 0.3f;

inline MemberRole deriveRole(const BoxLayout& b)
{
    const float plan = std::max(b.hx, b.hz);
    if (plan <= 1e-6f) return MemberRole::Other;
    const float aspect = b.hy / plan;
    if (aspect >= kTallAspect) return MemberRole::VerticalBearing;
    if (aspect <= kFlatAspect) return MemberRole::HorizontalPlate;
    return MemberRole::Other;
}

// A thin bearing member (wall) has one horizontal extent much larger than the
// other; a column's plan is roughly square. This is a presentational/geometric
// distinction only — the solver treats both as a vertical bearing.
inline bool isThinBearing(const BoxLayout& b)
{
    const float w = std::max(b.hx, b.hz);
    const float t = std::min(b.hx, b.hz);
    return w > t * 2.0f;
}

// Volume of the box (unit^3), used for mass and capacity derivation.
inline float boxVolume(const BoxLayout& b)
{
    return (2.0f * b.hx) * (2.0f * b.hy) * (2.0f * b.hz);
}

// True when the box's bottom face is at (or within tolerance of) the ground
// plane y == 0.
inline bool boxGrounded(const BoxLayout& b, float tolerance = 0.1f)
{
    return b.minY() <= tolerance;
}

// Vertical overlap of two boxes along y (>=0 when they share a span).
inline float yOverlap(const BoxLayout& a, const BoxLayout& b)
{
    return std::min(a.maxY(), b.maxY()) - std::max(a.minY(), b.minY());
}

// Horizontal (x-z) overlap of two boxes (>=0 when they share area).
inline float xzOverlap(const BoxLayout& a, const BoxLayout& b)
{
    const float ox = std::min(a.cx + a.hx, b.cx + b.hx) - std::max(a.cx - a.hx, b.cx - b.hx);
    const float oz = std::min(a.cz + a.hz, b.cz + b.hz) - std::max(a.cz - a.hz, b.cz - b.hz);
    return std::min(ox, oz);
}

// True when two boxes are in face contact (one sits on top of the other): their
// xz footprints overlap and the upper box's bottom touches the lower box's top.
inline bool verticalContact(const BoxLayout& upper, const BoxLayout& lower, float tolerance = 0.1f)
{
    if (xzOverlap(upper, lower) <= -tolerance) return false;
    return std::fabs(upper.minY() - lower.maxY()) <= tolerance;
}

// True when two boxes share a horizontal edge contact (same story, side by
// side, corner-to-corner, or partially interlocking). Two boxes touch along an
// edge when their y ranges overlap AND one horizontal axis genuinely overlaps
// (a flush face or a small gap counts), while the other axis must overlap by a
// positive amount. This rejects diagonal-only neighbours, whose two projections
// are both mere gaps.
inline bool horizontalContact(const BoxLayout& a, const BoxLayout& b, float tolerance = 0.1f)
{
    const float oy = yOverlap(a, b);
    if (oy <= 1e-4f) return false;
    const float ox = std::min(a.cx + a.hx, b.cx + b.hx) - std::max(a.cx - a.hx, b.cx - b.hx);
    const float oz = std::min(a.cz + a.hz, b.cz + b.hz) - std::max(a.cz - a.hz, b.cz - b.hz);
    const float flush = 0.05f;  // absorb the small inter-block gap
    const float eps = 1e-3f;
    const bool xEdge = ox >= -flush && oz > eps;
    const bool zEdge = oz >= -flush && ox > eps;
    return xEdge || zEdge;
}
}
