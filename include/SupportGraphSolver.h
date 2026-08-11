#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace blast_demo
{
struct EdgeState;
struct NodeState;

class SupportGraphSolver
{
public:
    void markDirty(int nodeId, const std::vector<NodeState>& nodes,
                   std::deque<int>& dirtyNodes, std::vector<bool>& dirtyFlags) const;
    void markIncidentNeighborsDirty(int nodeId, const std::vector<NodeState>& nodes,
                                    const std::vector<EdgeState>& edges,
                                    std::deque<int>& dirtyNodes, std::vector<bool>& dirtyFlags) const;
    bool hasGroundPath(int nodeId, const std::vector<NodeState>& nodes,
                       const std::vector<EdgeState>& edges) const;
    std::vector<int> collectAffectedNodes(const std::vector<NodeState>& nodes,
                                          const std::vector<EdgeState>& edges,
                                          std::deque<int>& dirtyNodes,
                                          std::vector<bool>& dirtyFlags,
                                          std::size_t budget, std::size_t& bfsCount) const;
};
}
