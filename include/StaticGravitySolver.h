#pragma once

#include "NodeTypes.h"
#include <vector>

namespace blast_demo
{
struct StaticGravityNodeResult
{
    bool supported = false;
    float carriedMass = 0.0f;
    float carriedComX = 0.0f;
    float carriedComZ = 0.0f;
    float compressionUtilization = 0.0f;
    float bendingUtilization = 0.0f;
    float utilization = 0.0f;
};

struct StaticGravityResult
{
    std::vector<StaticGravityNodeResult> nodes;
    std::vector<int> distanceToGround;
    std::vector<float> edgeTransferredMass;
    std::vector<float> plateOverhang;   // per-node horizontal arm to nearest stable plate
    std::vector<int> unsupportedNodes;
    std::vector<int> overloadedNodes;
};

class StaticGravitySolver
{
public:
    StaticGravityResult solve(const std::vector<NodeState>& nodes,
                              const std::vector<EdgeState>& edges) const;
};
}
