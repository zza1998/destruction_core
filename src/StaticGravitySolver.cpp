#include "StaticGravitySolver.h"

#include "GeometryDerived.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>

namespace blast_demo
{
namespace
{
bool containsUnsupported(const StaticGravityResult& result, int id)
{
    return std::find(result.unsupportedNodes.begin(), result.unsupportedNodes.end(), id) !=
           result.unsupportedNodes.end();
}
}

StaticGravityResult StaticGravitySolver::solve(const std::vector<NodeState>& nodes,
                                               const std::vector<EdgeState>& edges) const
{
    const int n = static_cast<int>(nodes.size());
    StaticGravityResult result;
    result.nodes.assign(static_cast<std::size_t>(n), StaticGravityNodeResult());
    result.distanceToGround.assign(static_cast<std::size_t>(n), -1);
    result.edgeTransferredMass.assign(edges.size(), 0.0f);

    if (n <= 0) return result;

    // Reverse adjacency from live, positively-weighted edges whose endpoints are
    // both alive. This reverse graph lets us BFS from Ground toward supported
    // nodes deterministically.
    std::vector<std::vector<int>> reverse(n);
    for (std::size_t ei = 0; ei < edges.size(); ++ei)
    {
        const EdgeState& e = edges[ei];
        if (!e.alive || e.shareWeight <= 0.0f) continue;
        if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n) continue;
        if (!nodes[static_cast<std::size_t>(e.from)].alive) continue;
        if (!nodes[static_cast<std::size_t>(e.to)].alive) continue;
        reverse[static_cast<std::size_t>(e.to)].push_back(e.from);
    }

    for (int i = 0; i < n; ++i)
        std::sort(reverse[static_cast<std::size_t>(i)].begin(),
                  reverse[static_cast<std::size_t>(i)].end());

    // BFS from Ground (id 0) to assign shortest directed distances.
    result.distanceToGround[0] = 0;
    std::deque<int> queue;
    queue.push_back(0);
    while (!queue.empty())
    {
        const int current = queue.front();
        queue.pop_front();
        for (int pred : reverse[static_cast<std::size_t>(current)])
        {
            if (result.distanceToGround[static_cast<std::size_t>(pred)] == -1)
            {
                result.distanceToGround[static_cast<std::size_t>(pred)] =
                    result.distanceToGround[static_cast<std::size_t>(current)] + 1;
                queue.push_back(pred);
            }
        }
    }

    // Accumulated mass and weighted COM moments per node (incoming transfers).
    std::vector<double> accMass(static_cast<std::size_t>(n), 0.0);
    std::vector<double> accComX(static_cast<std::size_t>(n), 0.0);
    std::vector<double> accComZ(static_cast<std::size_t>(n), 0.0);

    // Seed each supported non-ground node with its own mass at its own center.
    std::vector<std::size_t> order;
    for (int i = 1; i < n; ++i)
    {
        if (!nodes[static_cast<std::size_t>(i)].alive) continue;
        if (result.distanceToGround[static_cast<std::size_t>(i)] == -1)
        {
            result.unsupportedNodes.push_back(i);
            continue;
        }
        accMass[static_cast<std::size_t>(i)] = static_cast<double>(nodes[static_cast<std::size_t>(i)].mass);
        accComX[static_cast<std::size_t>(i)] = static_cast<double>(nodes[static_cast<std::size_t>(i)].box.cx) * accMass[static_cast<std::size_t>(i)];
        accComZ[static_cast<std::size_t>(i)] = static_cast<double>(nodes[static_cast<std::size_t>(i)].box.cz) * accMass[static_cast<std::size_t>(i)];
        order.push_back(static_cast<std::size_t>(i));
    }

    // Process supported nodes from furthest-to-nearest Ground, then by
    // descending ID as a deterministic tie-breaker. For each node, split its
    // accumulated mass among its strictly-closer support edges by share weight.
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const int da = result.distanceToGround[a];
        const int db = result.distanceToGround[b];
        if (da != db) return da > db;
        return a > b;
    });

    for (std::size_t node : order)
    {
        const int dist = result.distanceToGround[node];
        std::vector<std::size_t> outgoing;
        double weightSum = 0.0;
        for (std::size_t ei = 0; ei < edges.size(); ++ei)
        {
            const EdgeState& e = edges[ei];
            if (!e.alive || e.shareWeight <= 0.0f) continue;
            if (e.from != static_cast<int>(node)) continue;
            if (e.to < 0 || e.to >= n) continue;
            if (!nodes[static_cast<std::size_t>(e.to)].alive) continue;
            const int targetDist = result.distanceToGround[static_cast<std::size_t>(e.to)];
            if (targetDist < 0 || targetDist >= dist) continue;
            outgoing.push_back(ei);
            weightSum += static_cast<double>(e.shareWeight);
        }

        if (weightSum <= 0.0 || outgoing.empty())
        {
            // A supported node with no valid downward outlet becomes an
            // unsupported relay: it cannot actually transfer its load.
            if (!containsUnsupported(result, static_cast<int>(node)))
            {
                result.unsupportedNodes.push_back(static_cast<int>(node));
                std::sort(result.unsupportedNodes.begin(), result.unsupportedNodes.end());
            }
            continue;
        }

        const double mass = accMass[node];
        const double comX = accComX[node];
        const double comZ = accComZ[node];
        for (std::size_t ei : outgoing)
        {
            const EdgeState& e = edges[ei];
            const std::size_t target = static_cast<std::size_t>(e.to);
            const double share = static_cast<double>(e.shareWeight) / weightSum;
            const double transferred = mass * share;
            result.edgeTransferredMass[ei] = static_cast<float>(transferred);
            accMass[target] += transferred;
            accComX[target] += comX * share;
            accComZ[target] += comZ * share;
        }
    }

    // Finalize carried mass / COM and evaluate vertical bearings.
    for (int i = 1; i < n; ++i)
    {
        const NodeState& node = nodes[static_cast<std::size_t>(i)];
        if (!node.alive) continue;
        const bool supported = result.distanceToGround[static_cast<std::size_t>(i)] != -1;
        result.nodes[static_cast<std::size_t>(i)].supported = supported;
        if (!supported) continue;

        const double mass = accMass[static_cast<std::size_t>(i)];
        result.nodes[static_cast<std::size_t>(i)].carriedMass = static_cast<float>(mass);
        if (mass > 0.0)
        {
            result.nodes[static_cast<std::size_t>(i)].carriedComX =
                static_cast<float>(accComX[static_cast<std::size_t>(i)] / mass);
            result.nodes[static_cast<std::size_t>(i)].carriedComZ =
                static_cast<float>(accComZ[static_cast<std::size_t>(i)] / mass);
        }
        else
        {
            result.nodes[static_cast<std::size_t>(i)].carriedComX = node.box.cx;
            result.nodes[static_cast<std::size_t>(i)].carriedComZ = node.box.cz;
        }

        if (deriveRole(node.box) == MemberRole::VerticalBearing)
        {
            const float clampedCapacity = node.capacity > 0.0f ? node.capacity : 0.001f;
            const float compression = static_cast<float>(mass) / clampedCapacity;
            float bending = 0.0f;
            if (node.maxOverhang > 0.0f)
            {
                const float dx = result.nodes[static_cast<std::size_t>(i)].carriedComX - node.box.cx;
                const float dz = result.nodes[static_cast<std::size_t>(i)].carriedComZ - node.box.cz;
                const float eccentricity = std::sqrt(dx * dx + dz * dz);
                bending = eccentricity / node.maxOverhang;
            }
            result.nodes[static_cast<std::size_t>(i)].compressionUtilization = compression;
            result.nodes[static_cast<std::size_t>(i)].bendingUtilization = bending;
            result.nodes[static_cast<std::size_t>(i)].utilization = compression + bending;

            if (compression + bending >= 1.0f)
                result.overloadedNodes.push_back(i);
        }
        else if (deriveRole(node.box) == MemberRole::HorizontalPlate && node.maxOverhang > 0.0f)
        {
            // Cantilever (overhang) failure for a floor plate. A plate overhangs
            // only when it is the free end of a broken one-dimensional slab chain:
            // exactly one live horizontal neighbour and no live vertical bearing
            // directly beneath. A plate with two or more neighbours belongs to a
            // continuous floor/beam and never overhangs this way. The overhang arm
            // is the plate's horizontal distance to the nearest live vertical
            // bearing, so a longer tolerance lets plates farther from the remaining
            // support hold on (Small/Medium/Large tune how many drop).
            std::vector<int> hNeighbours;
            bool hasVerticalSupport = false;
            for (std::size_t ei = 0; ei < edges.size(); ++ei)
            {
                const EdgeState& e = edges[ei];
                if (!e.alive || e.shareWeight <= 0.0f) continue;
                if (e.from != i && e.to != i) continue;
                if (e.to < 0 || e.to >= n || e.from < 0 || e.from >= n) continue;
                const int other = e.from == i ? e.to : e.from;
                if (!nodes[static_cast<std::size_t>(other)].alive) continue;
                const BoxLayout& o = nodes[static_cast<std::size_t>(other)].box;
                if (verticalContact(node.box, o) || verticalContact(o, node.box))
                {
                    hasVerticalSupport = true;
                    continue;
                }
                if (!horizontalContact(node.box, o)) continue;
                if (std::find(hNeighbours.begin(), hNeighbours.end(), other) == hNeighbours.end())
                    hNeighbours.push_back(other);
            }

            float overhang = 0.0f;
            if (!hasVerticalSupport && hNeighbours.size() == 1u)
            {
                float nearest = std::numeric_limits<float>::max();
                for (int b = 1; b < n; ++b)
                {
                    const NodeState& other = nodes[static_cast<std::size_t>(b)];
                    if (!other.alive || b == i) continue;
                    if (deriveRole(other.box) != MemberRole::VerticalBearing) continue;
                    const float dx = other.box.cx - node.box.cx;
                    const float dz = other.box.cz - node.box.cz;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    if (d < nearest) nearest = d;
                }
                overhang = nearest == std::numeric_limits<float>::max() ? 0.0f : nearest;
            }
            const float bending = overhang / node.maxOverhang;
            result.nodes[static_cast<std::size_t>(i)].bendingUtilization = bending;
            result.nodes[static_cast<std::size_t>(i)].utilization = bending;

            if (bending >= 1.0f)
                result.overloadedNodes.push_back(i);
        }
    }

    return result;
}
}