#include "ReducedStaticsSolver.h"

#include <algorithm>
#include <cmath>

namespace blast_demo
{
namespace
{
float cross(StaticsVec2 a, StaticsVec2 b) { return a.x * b.y - a.y * b.x; }
StaticsVec2 subtract(StaticsVec2 a, StaticsVec2 b) { return {a.x - b.x, a.y - b.y}; }
float dot(StaticsVec2 a, StaticsVec2 b) { return a.x * b.x + a.y * b.y; }

bool solve3x3(float matrix[3][3], float rhs[3], float result[3])
{
    for (int column = 0; column < 3; ++column)
    {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row)
            if (std::fabs(matrix[row][column]) > std::fabs(matrix[pivot][column])) pivot = row;
        if (std::fabs(matrix[pivot][column]) < 1e-8f) return false;
        for (int k = column; k < 3; ++k) std::swap(matrix[column][k], matrix[pivot][k]);
        std::swap(rhs[column], rhs[pivot]);
        const float divisor = matrix[column][column];
        for (int k = column; k < 3; ++k) matrix[column][k] /= divisor;
        rhs[column] /= divisor;
        for (int row = 0; row < 3; ++row)
        {
            if (row == column) continue;
            const float factor = matrix[row][column];
            for (int k = column; k < 3; ++k) matrix[row][k] -= factor * matrix[column][k];
            rhs[row] -= factor * rhs[column];
        }
    }
    result[0] = rhs[0]; result[1] = rhs[1]; result[2] = rhs[2];
    return true;
}
}

StaticsResult ReducedStaticsSolver::solve(const StaticsLoadCase& load,
                                          const std::vector<StaticsSupport>& supports) const
{
    StaticsResult result;
    result.totalWeight = load.mass * load.gravity;
    if (supports.empty()) { result.diagnostic = "No supports."; return result; }

    const int count = static_cast<int>(supports.size());
    std::vector<StaticsVec2> directions(count);
    std::vector<float> coefficients(count * 3, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        const float length = std::sqrt(dot(supports[i].direction, supports[i].direction));
        if (length < 1e-6f) { result.diagnostic = "Support has zero direction."; return result; }
        directions[i] = {supports[i].direction.x / length, supports[i].direction.y / length};
        const StaticsVec2 arm = subtract(supports[i].position, load.centerOfMass);
        coefficients[i * 3 + 0] = directions[i].x;
        coefficients[i * 3 + 1] = directions[i].y;
        coefficients[i * 3 + 2] = cross(arm, directions[i]);
    }

    const StaticsVec2 totalForce = {load.externalForce.x, load.externalForce.y - result.totalWeight};
    const float target[3] = {-totalForce.x, -totalForce.y, -load.externalMoment};
    float gram[3][3] = {};
    float gramRhs[3] = {};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            for (int i = 0; i < count; ++i)
                gram[row][column] += coefficients[i * 3 + row] * coefficients[i * 3 + column];
    for (int row = 0; row < 3; ++row)
        gramRhs[row] = target[row];
    for (int diagonal = 0; diagonal < 3; ++diagonal) gram[diagonal][diagonal] += 1e-5f;

    float multipliers[3] = {};
    if (!solve3x3(gram, gramRhs, multipliers))
    {
        result.diagnostic = "Support directions cannot satisfy static equilibrium.";
        return result;
    }

    float residual[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < count; ++i)
    {
        float reaction = 0.0f;
        for (int row = 0; row < 3; ++row) reaction += coefficients[i * 3 + row] * multipliers[row];
        StaticsReaction output;
        output.supportId = supports[i].id;
        output.scalarForce = reaction;
        output.force = {directions[i].x * reaction, directions[i].y * reaction};
        const float capacity = reaction >= 0.0f ? supports[i].compressionCapacity : supports[i].tensionCapacity;
        output.utilization = capacity > 0.0f ? std::fabs(reaction) / capacity : 1e9f;
        output.exceedsCapacity = output.utilization > 1.0f;
        result.reactions.push_back(output);
        for (int row = 0; row < 3; ++row) residual[row] += coefficients[i * 3 + row] * reaction;
    }

    result.equilibriumResidual = std::sqrt((residual[0] - target[0]) * (residual[0] - target[0]) +
                                            (residual[1] - target[1]) * (residual[1] - target[1]) +
                                            (residual[2] - target[2]) * (residual[2] - target[2]));
    result.solvable = true;
    result.stable = result.equilibriumResidual < 0.01f;
    for (const StaticsReaction& reaction : result.reactions)
        result.stable = result.stable && !reaction.exceedsCapacity;
    result.diagnostic = result.stable ? "Static equilibrium is stable." : "Support reactions indicate instability or failure.";
    return result;
}
}
