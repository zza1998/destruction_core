#include "SupportGraphSolver.h"
#include "BlastSupportModel.h"

#include <algorithm>

namespace blast_demo
{
void SupportGraphSolver::markDirty(int nodeId, const std::vector<NodeState>& nodes,
                                   std::deque<int>& dirtyNodes, std::vector<bool>& dirtyFlags) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes.size())) return;
    if (dirtyFlags.empty()) dirtyFlags.assign(nodes.size(), false);
    if (!dirtyFlags[nodeId]) { dirtyFlags[nodeId] = true; dirtyNodes.push_back(nodeId); }
}

void SupportGraphSolver::markIncidentNeighborsDirty(int nodeId, const std::vector<NodeState>& nodes,
                                                    const std::vector<EdgeState>& edges,
                                                    std::deque<int>& dirtyNodes, std::vector<bool>& dirtyFlags) const
{
    markDirty(nodeId, nodes, dirtyNodes, dirtyFlags);
    for (const EdgeState& edge : edges)
    {
        if (edge.from == nodeId) markDirty(edge.to, nodes, dirtyNodes, dirtyFlags);
        else if (edge.to == nodeId) markDirty(edge.from, nodes, dirtyNodes, dirtyFlags);
    }
}

bool SupportGraphSolver::hasGroundPath(int nodeId, const std::vector<NodeState>& nodes,
                                       const std::vector<EdgeState>& edges) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes.size()) || !nodes[nodeId].alive) return false;
    std::vector<bool> visited(nodes.size(), false);
    std::deque<int> queue;
    queue.push_back(nodeId); visited[nodeId] = true;
    while (!queue.empty())
    {
        const int current = queue.front(); queue.pop_front();
        if (current == 0) return true;
        for (const EdgeState& edge : edges)
        {
            if (!edge.alive || edge.from != current) continue;
            const int other = edge.to;
            if (!visited[other] && nodes[other].alive) { visited[other] = true; queue.push_back(other); }
        }
    }
    return false;
}

std::vector<int> SupportGraphSolver::collectAffectedNodes(const std::vector<NodeState>& nodes,
                                                          const std::vector<EdgeState>& edges,
                                                          std::deque<int>& dirtyNodes,
                                                          std::vector<bool>& dirtyFlags,
                                                          std::size_t budget, std::size_t& bfsCount) const
{
    std::vector<int> affected;
    std::vector<bool> visited(nodes.size(), false);
    bfsCount = 0;
    while (!dirtyNodes.empty() && bfsCount < budget)
    {
        const int start = dirtyNodes.front(); dirtyNodes.pop_front(); dirtyFlags[start] = false;
        std::fill(visited.begin(), visited.end(), false);
        std::deque<int> queue; queue.push_back(start); visited[start] = true;
        while (!queue.empty() && bfsCount < budget)
        {
            const int current = queue.front(); queue.pop_front(); ++bfsCount; affected.push_back(current);
            for (const EdgeState& edge : edges)
            {
                if (!edge.alive) continue;
                const int other = edge.from == current ? edge.to : (edge.to == current ? edge.from : -1);
                if (other >= 0 && !visited[other] && nodes[other].alive)
                { visited[other] = true; queue.push_back(other); }
            }
        }
    }
    return affected;
}
}
