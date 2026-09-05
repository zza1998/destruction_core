#include "StaticGravitySolver.h"

#include "GeometryDerived.h"

#include <cmath>
#include <iostream>
#include <vector>

using blast_demo::BoxLayout;
using blast_demo::EdgeState;
using blast_demo::NodeState;
using blast_demo::StaticGravityResult;
using blast_demo::StaticGravitySolver;

namespace
{
bool nearlyEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << "\n";
    return condition;
}

bool contains(const std::vector<int>& ids, int target)
{
    for (int id : ids)
        if (id == target)
            return true;
    return false;
}

NodeState ground()
{
    NodeState n;
    n.id = 0;
    n.name = "Ground";
    n.alive = true;
    return n;
}

// A vertical bearing (column): tall, roughly square plan, centered at (x,z).
NodeState column(int id, float x, float z, float mass, float capacity)
{
    NodeState n;
    n.id = id;
    n.name = "Column";
    n.alive = true;
    n.mass = mass;
    n.capacity = capacity;
    n.box.cx = x;
    n.box.cy = 2.0f;
    n.box.cz = z;
    n.box.hx = 0.5f;
    n.box.hy = 2.0f;
    n.box.hz = 0.5f;
    return n;
}

// A horizontal plate (floor slab): flat and wide, centered at (x,z).
NodeState plate(int id, float x, float z, float mass)
{
    NodeState n;
    n.id = id;
    n.name = "Plate";
    n.alive = true;
    n.mass = mass;
    n.capacity = 0.0f;
    n.box.cx = x;
    n.box.cy = 4.1f;
    n.box.cz = z;
    n.box.hx = 1.0f;
    n.box.hy = 0.1f;
    n.box.hz = 1.0f;
    return n;
}

EdgeState edge(int from, int to, bool alive = true, float shareWeight = 1.0f)
{
    EdgeState e;
    e.from = from;
    e.to = to;
    e.alive = alive;
    e.shareWeight = shareWeight;
    return e;
}
}

int main()
{
    // Simple column grounded, carrying one platform of mass 10 plus its own 5.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, 0.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(2, 0.0f, 0.0f, 10.0f));
        std::vector<EdgeState> edges;
        edges.push_back(edge(2, 1));
        edges.push_back(edge(1, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(result.nodes[1].supported, "grounded support is not supported")) return 1;
        if (!check(nearlyEqual(result.nodes[1].carriedMass, 15.0f),
                   "column did not receive its own and platform mass")) return 1;
        if (!check(result.overloadedNodes.empty(), "under-capacity column overloaded")) return 1;
    }
    // Symmetric two-column platform: each column carries its own mass plus half
    // the platform mass.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, -1.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(column(2, 1.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(3, 0.0f, 0.0f, 10.0f));
        std::vector<EdgeState> edges;
        edges.push_back(edge(3, 1));
        edges.push_back(edge(3, 2));
        edges.push_back(edge(1, 0));
        edges.push_back(edge(2, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(nearlyEqual(result.nodes[1].carriedMass, 10.0f),
                   "left column did not receive half platform plus own mass")) return 1;
        if (!check(nearlyEqual(result.nodes[2].carriedMass, 10.0f),
                   "right column did not receive half platform plus own mass")) return 1;
    }
    // Disconnected component: every non-Ground node unreachable is reported
    // unsupported in ascending ID order.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, 0.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(2, 3.0f, 0.0f, 10.0f));
        nodes.push_back(column(3, 6.0f, 0.0f, 5.0f, 100.0f));
        // Only node 1 grounds; 2 and 3 form a floating island.
        std::vector<EdgeState> edges;
        edges.push_back(edge(3, 2));
        edges.push_back(edge(1, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(result.unsupportedNodes.size() == 2u,
                   "disconnected component did not report two unsupported nodes")) return 1;
        if (!check(contains(result.unsupportedNodes, 2) &&
                   contains(result.unsupportedNodes, 3),
                   "unsupported nodes do not match the disconnected island")) return 1;
        if (!check(result.nodes[2].supported == false && result.nodes[3].supported == false,
                   "disconnected nodes were marked supported")) return 1;
    }
    // Invalid graph: broken edges and non-positive share weights are ignored.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, 0.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(2, 0.0f, 0.0f, 10.0f));
        std::vector<EdgeState> edges;
        EdgeState broken = edge(2, 1);
        broken.alive = false;
        edges.push_back(broken);
        EdgeState zeroWeight = edge(1, 0);
        zeroWeight.shareWeight = 0.0f;
        edges.push_back(zeroWeight);

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(contains(result.unsupportedNodes, 1) &&
                   contains(result.unsupportedNodes, 2),
                   "invalid edges did not leave nodes unsupported")) return 1;
    }
    // Inverted-L: 5 mass on a platform at x=3 carried by a root at x=0 with
    // capacity 100 and maxOverhang 2 -> compression passes, bending fails.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        NodeState root = column(1, 0.0f, 0.0f, 5.0f, 100.0f);
        root.maxOverhang = 2.0f;
        root.mass = 0.0f;   // Root own mass is negligible so the plate load is 3m off-center.
        nodes.push_back(root);
        nodes.push_back(plate(2, 3.0f, 0.0f, 5.0f));
        std::vector<EdgeState> edges;
        edges.push_back(edge(2, 1));
        edges.push_back(edge(1, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(contains(result.overloadedNodes, 1),
                   "overhang did not overload inverted-L root")) return 1;
        if (!check(result.nodes[1].compressionUtilization < 1.0f &&
                   result.nodes[1].bendingUtilization > 1.0f,
                   "inverted-L utilization components are wrong")) return 1;
    }
    // Same inverted-L with maxOverhang 4 survives: bending utilization below 1.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        NodeState root = column(1, 0.0f, 0.0f, 5.0f, 100.0f);
        root.maxOverhang = 4.0f;
        root.mass = 0.0f;
        nodes.push_back(root);
        nodes.push_back(plate(2, 3.0f, 0.0f, 5.0f));
        std::vector<EdgeState> edges;
        edges.push_back(edge(2, 1));
        edges.push_back(edge(1, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(!contains(result.overloadedNodes, 1),
                   "inverted-L with generous overhang overloaded")) return 1;
        if (!check(result.nodes[1].bendingUtilization > 0.0f &&
                   result.nodes[1].bendingUtilization < 1.0f,
                   "inverted-L bending utilization should be between 0 and 1")) return 1;
    }
    // Reroute: top plate relays through right plate/column after left branch
    // breaks. topPlate has two outbound edges (left and right plates); right
    // side still reaches Ground.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, -2.0f, 0.0f, 5.0f, 100.0f)); // leftColumn
        nodes.push_back(column(2, 2.0f, 0.0f, 5.0f, 100.0f));  // rightColumn
        nodes.push_back(plate(3, -2.0f, 0.0f, 5.0f));          // leftPlate
        nodes.push_back(plate(4, 2.0f, 0.0f, 5.0f));           // rightPlate
        nodes.push_back(plate(5, 0.0f, 0.0f, 8.0f));           // topPlate
        std::vector<EdgeState> edges;
        edges.push_back(edge(5, 3));
        edges.push_back(edge(5, 4));
        edges.push_back(edge(3, 1));
        edges.push_back(edge(4, 2));
        // leftColumn -> Ground is dead.
        edges.push_back(edge(1, 0, false));
        edges.push_back(edge(2, 0));

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(result.nodes[4].supported, "right plate lost support")) return 1;
        if (!check(result.nodes[5].supported, "top plate lost support")) return 1;
        if (!check(contains(result.unsupportedNodes, 1) &&
                   contains(result.unsupportedNodes, 3),
                   "left branch was not reported unsupported after its support died")) return 1;
        // topPlate mass 8 + rightPlate mass 5 lands on rightColumn (mass 5).
        if (!check(nearlyEqual(result.nodes[2].carriedMass, 18.0f),
                   "reroute did not send top-plate mass through the right column")) return 1;
    }
    // Horizontal bidirectional edge: A <-> B, B -> column -> Ground live, A's
    // direct groundward branch broken. A must route through B exactly once.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, 0.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(2, 0.0f, 0.0f, 4.0f));           // B
        nodes.push_back(plate(3, 3.0f, 0.0f, 6.0f));           // A
        std::vector<EdgeState> edges;
        edges.push_back(edge(2, 3));   // B -> A (higher distance, carries 0)
        edges.push_back(edge(3, 2));   // A -> B (carries A's mass)
        edges.push_back(edge(2, 1));   // B -> column
        edges.push_back(edge(1, 0));   // column -> Ground

        StaticGravitySolver solver;
        StaticGravityResult result = solver.solve(nodes, edges);

        if (!check(result.distanceToGround[3] == result.distanceToGround[2] + 1,
                   "A is not one level farther from ground than B")) return 1;
        // B receives B's own mass (4) plus A's mass (6) = 10; column receives
        // that plus its own 5 = 15.
        if (!check(nearlyEqual(result.nodes[2].carriedMass, 10.0f),
                   "B did not receive A's full mass")) return 1;
        if (!check(nearlyEqual(result.nodes[1].carriedMass, 15.0f),
                   "column did not receive B and A masses")) return 1;
        if (!check(nearlyEqual(result.nodes[3].carriedMass, 6.0f),
                   "A received back-mass it should not have")) return 1;
    }
    // Determinism: repeated solves produce identical vectors.
    {
        std::vector<NodeState> nodes;
        nodes.push_back(ground());
        nodes.push_back(column(1, -1.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(column(2, 1.0f, 0.0f, 5.0f, 100.0f));
        nodes.push_back(plate(3, 0.0f, 0.0f, 10.0f));
        std::vector<EdgeState> edges;
        edges.push_back(edge(3, 1));
        edges.push_back(edge(3, 2));
        edges.push_back(edge(1, 0));
        edges.push_back(edge(2, 0));

        StaticGravitySolver solver;
        StaticGravityResult a = solver.solve(nodes, edges);
        StaticGravityResult b = solver.solve(nodes, edges);

        bool same = true;
        if (a.overloadedNodes != b.overloadedNodes) same = false;
        if (a.unsupportedNodes != b.unsupportedNodes) same = false;
        if (a.distanceToGround != b.distanceToGround) same = false;
        if (a.nodes.size() != b.nodes.size()) same = false;
        else
        {
            for (size_t i = 0; i < a.nodes.size(); ++i)
                if (!nearlyEqual(a.nodes[i].carriedMass, b.nodes[i].carriedMass) ||
                    !nearlyEqual(a.nodes[i].utilization, b.nodes[i].utilization))
                    same = false;
        }
        if (!check(same, "repeated solve produced different results")) return 1;
    }
    std::cout << "PASS: static gravity routing, overhang, rerouting, and determinism checks\n";
    return 0;
}
