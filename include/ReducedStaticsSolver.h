#pragma once

#include <string>
#include <vector>

namespace blast_demo
{
struct StaticsVec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct StaticsSupport
{
    int id = 0;
    StaticsVec2 position;
    StaticsVec2 direction = {0.0f, 1.0f};
    float compressionCapacity = 0.0f;
    float tensionCapacity = 0.0f;
};

struct StaticsLoadCase
{
    StaticsVec2 centerOfMass;
    float mass = 0.0f;
    StaticsVec2 externalForce = {0.0f, 0.0f};
    float externalMoment = 0.0f;
    float gravity = 9.81f;
};

struct StaticsReaction
{
    int supportId = 0;
    float scalarForce = 0.0f;
    StaticsVec2 force;
    float utilization = 0.0f;
    bool exceedsCapacity = false;
};

struct StaticsResult
{
    bool solvable = false;
    bool stable = false;
    float equilibriumResidual = 0.0f;
    float totalWeight = 0.0f;
    std::vector<StaticsReaction> reactions;
    std::string diagnostic;
};

class ReducedStaticsSolver
{
public:
    StaticsResult solve(const StaticsLoadCase& load,
                        const std::vector<StaticsSupport>& supports) const;
};
}
