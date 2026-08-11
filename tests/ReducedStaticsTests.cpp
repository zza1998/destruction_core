#include "ReducedStaticsSolver.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace blast_demo;
    ReducedStaticsSolver solver;
    StaticsLoadCase load;
    load.mass = 10.0f;
    load.centerOfMass = {2.0f, 0.0f};
    std::vector<StaticsSupport> supports = {
        {1, {0.0f, 0.0f}, {0.0f, 1.0f}, 1000.0f, 1000.0f},
        {2, {10.0f, 0.0f}, {0.0f, 1.0f}, 1000.0f, 1000.0f}};
    const StaticsResult result = solver.solve(load, supports);
    if (!result.solvable || !result.stable || result.reactions.size() != 2 ||
        std::fabs(result.reactions[0].scalarForce - 78.48f) > 0.1f ||
        std::fabs(result.reactions[1].scalarForce - 19.62f) > 0.1f)
    {
        std::cerr << "FAIL: eccentric load reaction distribution\n";
        return 1;
    }

    supports[1].compressionCapacity = 10.0f;
    if (solver.solve(load, supports).stable)
    {
        std::cerr << "FAIL: overloaded support was reported stable\n";
        return 1;
    }
    std::cout << "PASS: reduced statics solver\n";
    return 0;
}
