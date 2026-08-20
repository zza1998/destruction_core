#pragma once

#include "GeometryDerived.h"
#include "NodeTypes.h"

#include <cmath>
#include <vector>

namespace blast_demo
{
// Build support edges purely from member geometry (bounding boxes + role
// derived from aspect ratio). There is no per-preset slot table and no
// "column supports its 2x2 quadrant" rule: an edge exists exactly when two
// members physically touch.
//
// Edge semantics (direction = load flow toward Ground, so `from` is the
// supported member and `to` is its support):
//   * grounded vertical bearing -> Ground (id 0)
//   * a member whose bottom face lands on another's top face -> {upper, lower}
//   * same-story side contact -> bidirectional
constexpr float kContactTol = 0.1f;

inline std::vector<EdgeState> rebuildEdgesFromContacts(const std::vector<NodeState>& nodes)
{
    std::vector<EdgeState> edges;
    const int total = static_cast<int>(nodes.size());
    const auto alive = [&nodes](int i) { return nodes[static_cast<size_t>(i)].alive; };

    for (int i = 1; i < total; ++i)
    {
        const NodeState& ni = nodes[static_cast<size_t>(i)];
        if (ni.id == 0) continue;

        // Grounded vertical bearing stands on the ground plane.
        if (deriveRole(ni.box) == MemberRole::VerticalBearing && boxGrounded(ni.box, kContactTol))
            edges.push_back({i, 0, alive(i), 1.0f});

        for (int j = i + 1; j < total; ++j)
        {
            const NodeState& nj = nodes[static_cast<size_t>(j)];
            if (nj.id == 0) continue;

            // Vertical support: upper bottom face aligns with lower top face.
            // The vertical alignment (bottom ~ top) is the strict test; the
            // footprint only needs to be roughly under/over (a slab tucked
            // inside its wall enclosure overlaps the wall by a small negative
            // amount, which the loose tolerance absorbs).
            const float iOnJ = ni.box.minY() - nj.box.maxY();
            const float jOnI = nj.box.minY() - ni.box.maxY();
            if (std::fabs(iOnJ) <= kContactTol && xzOverlap(ni.box, nj.box) > -kContactTol)
            {
                edges.push_back({i, j, alive(i) && alive(j), 1.0f});
            }
            else if (std::fabs(jOnI) <= kContactTol && xzOverlap(ni.box, nj.box) > -kContactTol)
            {
                edges.push_back({j, i, alive(i) && alive(j), 1.0f});
            }
            else if (horizontalContact(ni.box, nj.box, kContactTol))
            {
                edges.push_back({i, j, alive(i) && alive(j), 1.0f});
                edges.push_back({j, i, alive(i) && alive(j), 1.0f});
            }
        }
    }
    return edges;
}
}
