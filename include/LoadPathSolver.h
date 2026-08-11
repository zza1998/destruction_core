#pragma once

#include "BlastSupportModel.h"

#include <vector>

namespace blast_demo
{
class LoadPathSolver
{
public:
    // Routes every active block's load toward Ground over live edges.
    // Returns the ids of columns that overloaded and were destroyed so the
    // caller can propagate the failure to their neighbors. When
    // applyOverloads is false the overloaded columns are reported but left
    // alive so the caller can defer the actual failure (cascade delay).
    std::vector<int> route(std::vector<NodeState>& nodes,
                           std::vector<EdgeState>& edges,
                           int activeFloors,
                           int activeColumns,
                           int activeBlocks,
                           int activeWalls,
                           bool applyOverloads = true) const;

private:
    static int blockIdOf(int floor, int slot, int blocks);
    static int columnIdOf(int floor, int slot, int floors, int columns, int blocks);
    static int wallIdOf(int floor, int slot, int floors, int columns, int blocks, int walls);
};
}
