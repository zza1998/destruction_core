#include "LoadPathSolver.h"

#include "ContactEdges.h"
#include "GeometryDerived.h"

#include <algorithm>
#include <deque>

namespace blast_demo
{
namespace
{
bool reachesGround(int nodeId, const std::vector<NodeState>& nodes, const std::vector<EdgeState>& edges)
{
    if (nodeId == 0) return true;
    if (!nodes[nodeId].alive) return false;
    std::vector<bool> visited(nodes.size(), false);
    std::deque<int> queue;
    queue.push_back(nodeId);
    visited[nodeId] = true;
    while (!queue.empty())
    {
        const int current = queue.front();
        queue.pop_front();
        if (current == 0) return true;
        for (const EdgeState& edge : edges)
        {
            if (!edge.alive || edge.from != current) continue;
            const int next = edge.to;
            if (!visited[next] && nodes[next].alive)
            {
                visited[next] = true;
                queue.push_back(next);
            }
        }
    }
    return false;
}

// Pure-geometry vertical support test. The member is supported when one of its
// live support edges points at a live node directly beneath it: the lower
// node's top face is flush with this member's bottom face (Ground id 0 is
// handled by the boxGrounded test). This is the "you are below me, you support
// me" rule - no column/plate/wall role is involved.
bool hasVerticalSupport(int nodeId, const std::vector<NodeState>& nodes,
                        const std::vector<EdgeState>& edges)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes.size())) return false;
    const NodeState& node = nodes[static_cast<size_t>(nodeId)];
    for (const EdgeState& edge : edges)
    {
        if (!edge.alive || edge.from != nodeId) continue;
        if (edge.to < 0 || edge.to >= static_cast<int>(nodes.size())) continue;
        const NodeState& support = nodes[static_cast<size_t>(edge.to)];
        if (!support.alive) continue;
        if (edge.to == 0) return true;
        if (std::fabs(node.box.minY() - support.box.maxY()) <= kContactTol)
            return true;
    }
    return false;
}

// A plate is load-bearing if it can reach a direct vertical support through a
// chain of same-storey horizontal (horizontalContact) bonds: when the column
// directly beneath it dies, the plate reroutes sideways across the floor band
// to a neighbouring plate that still stands on a live column. Its weight must
// therefore still land on the surviving columns of that storey, rather than
// vanish. Breadth-first over live same-storey horizontal contacts.
bool hasReachableVerticalSupport(int plateId, const std::vector<NodeState>& nodes,
                                 const std::vector<EdgeState>& edges)
{
    const int n = static_cast<int>(nodes.size());
    if (plateId <= 0 || plateId >= n) return false;
    std::vector<bool> visited(nodes.size(), false);
    std::deque<int> stack;
    stack.push_back(plateId);
    visited[static_cast<size_t>(plateId)] = true;
    while (!stack.empty())
    {
        const int cur = stack.back();
        stack.pop_back();
        if (hasVerticalSupport(cur, nodes, edges)) return true;
        const NodeState& a = nodes[static_cast<size_t>(cur)];
        for (int j = 1; j < n; ++j)
        {
            if (visited[static_cast<size_t>(j)] || !nodes[static_cast<size_t>(j)].alive) continue;
            if (deriveRole(nodes[static_cast<size_t>(j)].box) != MemberRole::HorizontalPlate) continue;
            if (std::fabs(nodes[static_cast<size_t>(j)].box.cy - a.box.cy) > 1e-3f) continue;
            if (!horizontalContact(a.box, nodes[static_cast<size_t>(j)].box, kContactTol)) continue;
            visited[static_cast<size_t>(j)] = true;
            stack.push_back(j);
        }
    }
    return false;
}
}

int LoadPathSolver::blockIdOf(int floor, int slot, int blocks)
{
    return 1 + floor * blocks + slot;
}

int LoadPathSolver::columnIdOf(int floor, int slot, int floors, int columns, int blocks)
{
    return 1 + floors * blocks + floor * columns + slot;
}

int LoadPathSolver::wallIdOf(int floor, int slot, int floors, int columns, int blocks, int walls)
{
    return 1 + floors * blocks + floors * columns + floor * walls + slot;
}

std::vector<int> LoadPathSolver::route(std::vector<NodeState>& nodes,
                                       std::vector<EdgeState>& edges,
                                       int activeFloors,
                                       int activeColumns,
                                       int activeBlocks,
                                       int activeWalls,
                                       bool applyOverloads) const
{
    (void)activeColumns;
    (void)activeWalls;
    for (NodeState& node : nodes) node.load = 0.0f;
    for (EdgeState& edge : edges) edge.load = 0.0f;

    // Contact-graph storey level: how many vertical supports sit between a
    // member and Ground. Ground is level 0; a member that stands directly on
    // Ground (or on another level-0 member) is level 1, and so on. Levels are
    // derived purely from the vertical support edges (geometry contact), so a
    // Contact-graph storey level: how many vertical supports sit between a
    // member and Ground. Ground is level 0; a member that stands directly on
    // Ground (or on another level-0 member) is level 1, and so on. Levels are
    // derived purely from the vertical support edges (geometry contact), so a
    // "storey" is whatever is stacked at that height - there is no column vs
    // plate distinction anywhere in this pass. Dead members keep their last
    // level so their released load is attributed to the right storey.
    std::vector<int> level(nodes.size(), -1);
    level[0] = 0;
    int maxLevel = 0;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const EdgeState& edge : edges)
        {
            const int a = edge.from;
            const int b = edge.to;
            if (a <= 0 || b < 0 || b >= static_cast<int>(nodes.size())) continue;
            // Only a vertical support edge raises the level: the upper member
            // (a = from) sits with its bottom face on the lower member's (b =
            // to) top face, or stands directly on Ground. Same-storey
            // horizontal contact is not a vertical support. Dead edges are
            // still traversed so a collapsed member keeps its storey level.
            const NodeState& na = nodes[static_cast<size_t>(a)];
            const NodeState& nb = nodes[static_cast<size_t>(b)];
            if (b != 0 && std::fabs(na.box.minY() - nb.box.maxY()) > kContactTol) continue;
            if (level[b] < 0) continue;
            if (level[a] < 0 || level[a] > level[b] + 1)
            {
                level[a] = level[b] + 1;
                maxLevel = std::max(maxLevel, level[a]);
                changed = true;
            }
        }
    }

    // Whole-storey load sharing from the highest level downward. A member that
    // dies is REMOVED from its level: its weight drops out of the storey total
    // (it fell), so the surviving bearers carry less, not more. The dead member
    // does NOT dump its weight onto the survivors — the broken member no longer
    // contributes to the standing mass. Its only remaining effect is lateral: if
    // a block still rests above it, that block needs a sideways path (handled by
    // detectLateralShear), not extra vertical load. The storey total is just the
    // weight of the live, vertically supported members of that level plus
    // everything above, shared area-weighted.
    float loadFromAbove = 0.0f;
    for (int lvl = maxLevel; lvl >= 1; --lvl)
    {
        std::vector<int> bearers;
        std::vector<float> areas;
        float totalSection = loadFromAbove;
        float areaSum = 0.0f;
        for (const NodeState& node : nodes)
        {
            if (node.id == 0) continue;
            if (level[node.id] != lvl) continue;
            if (!node.alive) continue;   // dead members no longer carry or add weight
            // A vertical bearing needs a direct support; a plate remains a
            // bearer if it can reach one sideways through the floor band, so a
            // plate over a dead column reroutes its weight to the surviving
            // columns instead of vanishing.
            if (deriveRole(node.box) == MemberRole::VerticalBearing
                    ? !hasVerticalSupport(node.id, nodes, edges)
                    : !hasReachableVerticalSupport(node.id, nodes, edges))
                continue;
            const float area = std::max((2.0f * node.box.hx) * (2.0f * node.box.hz), 1e-4f);
            totalSection += node.mass;
            areaSum += area;
            bearers.push_back(node.id);
            areas.push_back(area);
        }

        const int liveCount = static_cast<int>(bearers.size());
        if (liveCount == 0 || areaSum <= 0.0f)
        {
            loadFromAbove = 0.0f;
            continue;
        }

        for (size_t i = 0; i < bearers.size(); ++i)
            nodes[bearers[i]].load = totalSection * (areas[i] / areaSum);

        loadFromAbove = totalSection;
    }

    std::vector<int> overloaded;
    for (NodeState& node : nodes)
    {
        if (node.id == 0 || !node.alive) continue;
        if (node.load > node.capacity)
        {
            if (!applyOverloads)
            {
                overloaded.push_back(node.id);
                continue;
            }
            node.health = 0.0f;
            node.alive = false;
            node.supported = false;
            node.status = NodeStatus::Overloaded;
            for (EdgeState& edge : edges)
                if (edge.from == node.id || edge.to == node.id) edge.alive = false;
            overloaded.push_back(node.id);
        }
    }
    return overloaded;
}
}
