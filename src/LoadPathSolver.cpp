#include "LoadPathSolver.h"

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
    for (NodeState& node : nodes) node.load = 0.0f;
    for (EdgeState& edge : edges) edge.load = 0.0f;

    // Whole-floor load sharing from the top floor downward. Each floor sums
    // the incoming load from above plus its own live block (slab) and bearer
    // (column or wall) masses, then divides it equally among the surviving
    // load bearers. Slab and bearer counts may differ (house walls vs slabs).
    // Walls are extra bearers appended after the columns (grid preset), so
    // they share the floor load together with the columns.
    //
    // The 1:1 column model (and the house walls) ties a bearer to its own
    // block: a bearer whose block has died no longer carries section load. The
    // grid preset has more blocks than columns (each column supports several
    // blocks), so there a bearer stays load-bearing as long as it is alive.
    const bool blockTiedColumns = activeBlocks <= activeColumns;
    const int bearerCount = activeColumns + activeWalls;
    std::vector<float> pathLoad(bearerCount, 0.0f);
    const auto bearerIdOf = [&](int floor, int b)
    {
        return b >= activeColumns
            ? wallIdOf(floor, b - activeColumns, activeFloors, activeColumns, activeBlocks, activeWalls)
            : columnIdOf(floor, b, activeFloors, activeColumns, activeBlocks);
    };
    for (int floor = activeFloors - 1; floor >= 0; --floor)
    {
        float totalSection = 0.0f;
        std::vector<int> liveBearers;
        for (int b = 0; b < bearerCount; ++b)
        {
            totalSection += pathLoad[b];
            const int bearer = bearerIdOf(floor, b);
            const bool hasBlock = b < activeBlocks;
            const int block = hasBlock ? blockIdOf(floor, b, activeBlocks) : -1;
            const bool blockAlive = hasBlock ? nodes[block].alive : true;
            const bool live = nodes[bearer].alive && (!blockTiedColumns || blockAlive);
            if (live)
            {
                totalSection += nodes[bearer].mass;
                liveBearers.push_back(b);
            }
        }
        for (int slot = 0; slot < activeBlocks; ++slot)
        {
            const int block = blockIdOf(floor, slot, activeBlocks);
            if (nodes[block].alive) totalSection += nodes[block].mass;
        }

        const int liveCount = static_cast<int>(liveBearers.size());
        if (liveCount == 0)
        {
            for (int b = 0; b < bearerCount; ++b) pathLoad[b] = 0.0f;
            continue;
        }

        const float perBearer = totalSection / static_cast<float>(liveCount);
        std::vector<bool> isLive(bearerCount, false);
        for (int b : liveBearers) isLive[b] = true;
        for (int b = 0; b < bearerCount; ++b)
        {
            const int bearer = bearerIdOf(floor, b);
            if (isLive[b])
            {
                nodes[bearer].load = perBearer;
                pathLoad[b] = perBearer;
            }
            else
            {
                nodes[bearer].load = nodes[bearer].mass;
                pathLoad[b] = 0.0f;
            }
        }
        // Display each slab with the story's shared load share, or with its
        // own weight when its bearer (column/wall) is gone. In the grid mode
        // blocks are not tied to a single bearer, so every live block shows
        // the story's shared share while any bearer remains.
        for (int slot = 0; slot < activeBlocks; ++slot)
        {
            const int block = blockIdOf(floor, slot, activeBlocks);
            if (!nodes[block].alive) continue;
            if (blockTiedColumns)
                nodes[block].load = isLive[slot] ? perBearer : nodes[block].mass;
            else
                nodes[block].load = perBearer;
        }
    }

    std::vector<int> overloaded;
    for (int floor = activeFloors - 1; floor >= 0; --floor)
    {
        for (int b = 0; b < bearerCount; ++b)
        {
            const int bearer = bearerIdOf(floor, b);
            NodeState& bearerNode = nodes[bearer];
            if (bearerNode.alive && bearerNode.load > bearerNode.capacity)
            {
                if (!applyOverloads)
                {
                    overloaded.push_back(bearer);
                    continue;
                }
                bearerNode.health = 0.0f;
                bearerNode.alive = false;
                bearerNode.supported = false;
                bearerNode.status = NodeStatus::Overloaded;
                for (EdgeState& edge : edges)
                    if (edge.from == bearer || edge.to == bearer) edge.alive = false;
                overloaded.push_back(bearer);
            }
        }
    }
    return overloaded;
}
}
