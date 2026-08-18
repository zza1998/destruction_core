#include "StaticGravitySolver.h"

#include "GeometryDerived.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <utility>

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

    // Precompute plate cantilever (overhang) results. A floor is modelled as a
    // set of connected plate components (joined by live horizontal contacts);
    // each component's support is the set of live vertical bearings whose top
    // touches a plate in that component. A plate overhangs when its own centre
    // lies horizontally outside the convex hull of that support set by more than
    // its maxOverhang. This handles one-dimensional chains (ShearPair) and 2D
    // grids uniformly: an intact floor's hull encloses every plate, a removed
    // corner shrinks the hull so the gap-edge plates overhang and fail, and the
    // cascade advances inward plate by plate.
    for (int i = 1; i < n; ++i)
    {
        result.nodes[static_cast<std::size_t>(i)].bendingUtilization = 0.0f;
        result.nodes[static_cast<std::size_t>(i)].utilization = 0.0f;
    }
    {
        std::vector<char> compVisited(static_cast<std::size_t>(n), 0);
        for (int seed = 1; seed < n; ++seed)
        {
            const NodeState& sn = nodes[static_cast<std::size_t>(seed)];
            if (!sn.alive || compVisited[static_cast<std::size_t>(seed)]) continue;
            if (deriveRole(sn.box) != MemberRole::HorizontalPlate) continue;
            if (sn.maxOverhang <= 0.0f) continue;

            // BFS the horizontal-contact plate component.
            std::vector<int> comp;
            std::deque<int> q;
            q.push_back(seed);
            compVisited[static_cast<std::size_t>(seed)] = 1;
            while (!q.empty())
            {
                const int cur = q.front(); q.pop_front();
                comp.push_back(cur);
                for (std::size_t ei = 0; ei < edges.size(); ++ei)
                {
                    const EdgeState& e = edges[ei];
                    if (!e.alive || e.shareWeight <= 0.0f) continue;
                    int other = -1;
                    if (e.from == cur) other = e.to;
                    else if (e.to == cur) other = e.from;
                    else continue;
                    if (other <= 0 || other >= n) continue;
                    if (!nodes[static_cast<std::size_t>(other)].alive) continue;
                    if (deriveRole(nodes[static_cast<std::size_t>(other)].box) != MemberRole::HorizontalPlate) continue;
                    if (!horizontalContact(nodes[static_cast<std::size_t>(cur)].box,
                                           nodes[static_cast<std::size_t>(other)].box)) continue;
                    if (compVisited[static_cast<std::size_t>(other)]) continue;
                    compVisited[static_cast<std::size_t>(other)] = 1;
                    q.push_back(other);
                }
            }

            // Collect live vertical bearings touching a plate in this component.
            std::vector<int> supports;
            for (int pid : comp)
            {
                for (std::size_t ei = 0; ei < edges.size(); ++ei)
                {
                    const EdgeState& e = edges[ei];
                    if (!e.alive || e.shareWeight <= 0.0f) continue;
                    int other = -1;
                    if (e.from == pid) other = e.to;
                    else if (e.to == pid) other = e.from;
                    else continue;
                    if (other <= 0 || other >= n) continue;
                    if (!nodes[static_cast<std::size_t>(other)].alive) continue;
                    if (deriveRole(nodes[static_cast<std::size_t>(other)].box) != MemberRole::VerticalBearing) continue;
                    if (!verticalContact(nodes[static_cast<std::size_t>(pid)].box,
                                         nodes[static_cast<std::size_t>(other)].box) &&
                        !verticalContact(nodes[static_cast<std::size_t>(other)].box,
                                         nodes[static_cast<std::size_t>(pid)].box)) continue;
                    supports.push_back(other);
                }
            }
            // Build the convex hull of the support points (x,z). Use the true 2D
            // hull rather than an axis-aligned box so a missing corner column
            // leaves a real gap: the hull is a polygon whose far corner is cut off,
            // and the plate at that corner then lies outside it.
            std::vector<std::pair<float,float>> pts;
            for (int sid : supports)
            {
                const BoxLayout& s = nodes[static_cast<std::size_t>(sid)].box;
                pts.push_back(std::make_pair(s.cx, s.cz));
            }
            std::sort(pts.begin(), pts.end());
            pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
            std::vector<std::pair<float,float>> hull;
            auto cross = [](const std::pair<float,float>& o,
                            const std::pair<float,float>& a,
                            const std::pair<float,float>& b) {
                return (a.first - o.first) * (b.second - o.second) -
                       (a.second - o.second) * (b.first - o.first);
            };
            if (pts.size() >= 3u)
            {
                for (int pass = 0; pass < 2; ++pass)
                {
                    std::size_t start = hull.size();
                    for (const auto& p : pts)
                    {
                        while (hull.size() >= start + 2 &&
                               cross(hull[hull.size()-2], hull.back(), p) <= 1e-9f)
                            hull.pop_back();
                        hull.push_back(p);
                    }
                    hull.pop_back();
                    std::reverse(pts.begin(), pts.end());
                }
            }
            else
            {
                hull = pts;
            }

            // Distance (or 0 if inside) from a query point to the hull polygon.
            auto pointHullDistance = [&](float cx, float cz) -> float {
                if (hull.empty()) return 0.0f;
                auto segDist = [](float px, float pz, float ax, float az, float bx, float bz) -> float {
                    const float abx = bx - ax, abz = bz - az;
                    const float len2 = abx * abx + abz * abz;
                    float t = len2 > 0.0f ? ((px - ax) * abx + (pz - az) * abz) / len2 : 0.0f;
                    t = std::max(0.0f, std::min(1.0f, t));
                    const float dx = px - (ax + t * abx);
                    const float dz = pz - (az + t * abz);
                    return std::sqrt(dx * dx + dz * dz);
                };
                // A single point (or a segment) yields the distance to that set.
                if (hull.size() == 1u)
                {
                    const float dx = cx - hull[0].first;
                    const float dz = cz - hull[0].second;
                    return std::sqrt(dx * dx + dz * dz);
                }
                if (hull.size() == 2u)
                {
                    return segDist(cx, cz, hull[0].first, hull[0].second, hull[1].first, hull[1].second);
                }
                // True polygon: point-inside test (ray cast), else min edge distance.
                bool inside = false;
                const std::size_t m = hull.size();
                for (std::size_t j = 0; j < m; ++j)
                {
                    const auto& a = hull[j];
                    const auto& b = hull[(j + 1) % m];
                    if ((a.second > cz) != (b.second > cz))
                    {
                        const float xcross = a.first + (cz - a.second) / (b.second - a.second) * (b.first - a.first);
                        if (cx < xcross) inside = !inside;
                    }
                }
                if (inside) return 0.0f;
                float best = std::numeric_limits<float>::max();
                for (std::size_t j = 0; j < m; ++j)
                {
                    const auto& a = hull[j];
                    const auto& b = hull[(j + 1) % m];
                    best = std::min(best, segDist(cx, cz, a.first, a.second, b.first, b.second));
                }
                return best;
            };

            // For each plate, compute eccentricity of its own centre outside the hull.
            for (int pid : comp)
            {
                const NodeState& p = nodes[static_cast<std::size_t>(pid)];
                if (p.maxOverhang <= 0.0f) continue;
                const float overhang = hull.empty() ? 0.0f : pointHullDistance(p.box.cx, p.box.cz);
                const float bending = overhang / p.maxOverhang;
                result.nodes[static_cast<std::size_t>(pid)].bendingUtilization = bending;
                result.nodes[static_cast<std::size_t>(pid)].utilization = bending;
            }
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
            // Cantilever (overhang) failure via component support hull, resolved
            // pre-pass below (see computeComponentOverhang). This per-node branch
            // just records the precomputed utilization onto the result.
            const float bending = result.nodes[static_cast<std::size_t>(i)].bendingUtilization;
            if (bending >= 1.0f)
                result.overloadedNodes.push_back(i);
        }
    }

    return result;
}
}