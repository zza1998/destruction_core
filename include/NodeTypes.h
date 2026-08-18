#pragma once

#include <string>
#include <vector>

namespace blast_demo
{
// Shared enums and the structural node/edge model, defined here so both the
// structure model and the support solvers can use them without a circular
// include.
enum class NodeStatus { Safe, Warning, Overloaded, Broken, Unsupported, Falling };

// Axis-aligned bounding box of a structural member. This is the single source
// of truth for geometry: roles, support edges, capacity, fracture and
// rendering are all derived from it, not from a member-type enum.
struct BoxLayout
{
    float cx = 0.0f;   // centre x
    float cy = 0.0f;   // centre y (up)
    float cz = 0.0f;   // centre z
    float hx = 1.0f;   // half-extent x
    float hy = 1.0f;   // half-extent y
    float hz = 1.0f;   // half-extent z
    float minY() const { return cy - hy; }
    float maxY() const { return cy + hy; }
};

struct NodeState
{
    int id = 0;
    std::string name;
    int floor = 0;
    int slot = 0;
    float health = 100.0f;
    float mass = 0.0f;
    float load = 0.0f;
    float capacity = 0.0f;
    bool alive = true;
    bool supported = true;
    NodeStatus status = NodeStatus::Safe;
    float releasedLoad = 0.0f;
    // Horizontal (lateral) force accumulated on this member from relaying the
    // released load of a dead support sideways to its same-storey neighbours.
    // Diagnostic: only plates are *failed* on this; bearings get a nonzero
    // shearCapacity so their UI/lateral reads are meaningful.
    float lateralShear = 0.0f;
    float shearCapacity = 0.0f;   // horizontal shear limit for a plate
BoxLayout box;                  // geometric bounds (single source of truth)
    float maxOverhang = 0.0f;       // 0 means ignore bending in v1.
    // Rest-state overhang arm (distance to the nearest stable plate) captured
    // at reset. A plate only overhang-fails when its current arm exceeds this
    // baseline by maxOverhang, so an intact continuous floor never fails and
    // only a plate at the edge of a freshly opened gap does.
    float baselineOverhang = 0.0f;
    float carriedMass = 0.0f;       // Solver diagnostic, includes own mass.
    float carriedComX = 0.0f;       // Solver diagnostic.
    float carriedComZ = 0.0f;
    float compressionUtilization = 0.0f;
    float bendingUtilization = 0.0f;
    float utilization = 0.0f;
};

struct EdgeState
{
    int from = 0;
    int to = 0;
    float capacity = 0.0f;
    float load = 0.0f;
    bool alive = true;
    float shareWeight = 1.0f;
};
}

