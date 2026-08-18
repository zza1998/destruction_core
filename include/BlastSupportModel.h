#pragma once

#include "BlastRuntime.h"
#include "NodeTypes.h"

#include <string>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace blast_demo
{
class SupportGraphSolver;
class LoadPathSolver;

struct StructuralConfig
{
    float lowerColumnCapacity = 800.0f;
    float upperColumnCapacity = 290.0f;
    float overloadFailureRatio = 1.0f;
    // Horizontal shear capacity of a floor plate, in load units (mass). A plate
    // fails when the vertical load released by dead columns above it must be
    // transferred sideways through the plate (shear) and exceeds this limit.
    // Sized larger than a single support's released share so one destroyed
    // support does NOT shear the plate; two or more simultaneous failures
    // accumulate and snap it.
    float plateShearCapacity = 300.0f;
};

enum class StructuralPreset
{
    Floors5Columns4,
    Grid4x4Floors4,
    ShearPair       // 单层双板对：用于测试横向剪切中继 (detectLateralShear)
};

class BlastSupportModel
{
public:
    static constexpr int MaxFloors = 5;
    static constexpr int MaxColumnsPerFloor = 4;
    static constexpr int MaxWallsPerFloor = 8;
    static constexpr int MaxBlocksPerFloor = 16;

    explicit BlastSupportModel(const StructuralConfig& config = StructuralConfig());
    ~BlastSupportModel();

    void reset();
    bool setPreset(StructuralPreset preset);
    StructuralPreset preset() const { return m_preset; }
    // Grid layout: every floor has more blocks than columns (blocks tile the
    // floor in a 4x4 grid and the columns stand at its corners), so a column
    // carries several blocks instead of one. Only the actual grid preset counts:
    // ShearPair also uses blocks>columns (4 mid-span plates over 2 end columns)
    // but is NOT a 4x4 corner-column grid, so it must not inherit the grid
    // whole-floor capacity scheme.
    bool isGrid() const { return m_preset == StructuralPreset::Grid4x4Floors4; }
    int activeFloors() const { return m_activeFloors; }
    int activeColumns() const { return m_activeColumns; }
    int activeBlocks() const { return m_activeBlocks; }
    int activeWalls() const { return m_activeWalls; }
    int blockId(int floor, int slot) const;
    int columnId(int floor, int slot) const;
    int wallId(int floor, int slot) const;
    bool damageNode(int nodeId, float amount);
    void markFalling(int nodeId);
    bool undoLast();
    bool canUndo() const { return !m_history.empty(); }
    void damageColumn(int floor, int slot, float amount = 35.0f);
    void randomDamageTwo(uint32_t seed);
    void stepAnalysis();
    void tickAnalysis();
    void setCascadeDelay(float seconds);
    float cascadeDelay() const { return m_cascadeDelay; }
    void update(float time);

    const std::vector<NodeState>& nodes() const { return m_nodes; }
    const std::vector<EdgeState>& edges() const { return m_edges; }
    const std::vector<std::string>& events() const { return m_events; }
    uint32_t seed() const { return m_seed; }
    bool progressiveCollapse() const { return m_progressiveCollapse; }
    const StructuralConfig& config() const { return m_config; }
    // Single source for the lateral (shear) failure threshold. Updates the
    // config and re-derives every member's shearCapacity from its geometry.
    void setPlateShearCapacity(float loadUnits);

    // Fragments spawned by destroyed members since the last call. PhysicsWorld
    // consumes this to create visual debris bodies.
    void takePendingFragments(std::vector<FragmentSpawnInfo>& out);

private:
    struct Snapshot
    {
        std::vector<NodeState> nodes;
        std::vector<EdgeState> edges;
        std::vector<std::string> events;
        uint32_t seed = 0;
        bool progressiveCollapse = false;
        std::deque<int> dirtyNodes;
        std::vector<bool> dirtyFlags;
        uint32_t analysisTick = 0;
    };

    void rebuildEdges();
    float gridCapacityFor(int floor) const;
    // Lateral-shear detector: finds plates that must relay a dead support's
    // released load sideways to same-storey neighbours (area-weighted). Only
    // plates accumulate and can shear-fail. Accumulates per-node lateral into
    // NodeState::lateralShear (diagnostic) and writes the pass-local output
    // vectors (overloaded ids + lateral force in load units).
    void detectLateralShear(std::vector<int>& overloaded, std::vector<float>& lateralValues);
    void markDirty(int nodeId);
    void markIncidentNeighborsDirty(int nodeId);
    void addEvent(const std::string& text);
    void saveSnapshot();
    void scheduleFail(int nodeId, NodeStatus status, const std::string& reason = "",
                      float snapN = 0.0f, float snapV = 0.0f, float snapM = 0.0f);
    void executePendingFail(int nodeId, NodeStatus status, const std::string& reason = "",
                            float snapN = 0.0f, float snapV = 0.0f, float snapM = 0.0f);

    struct PendingFail
    {
        int nodeId = -1;
        float dueTime = 0.0f;
        NodeStatus status = NodeStatus::Unsupported;
        std::string reason;
        // Snapshot of the forces at scheduling time, so the failure log shows
        // the actual values that triggered the overload (not the latest solve,
        // which may have changed by the time the failure executes).
        float snapN = 0.0f;
        float snapV = 0.0f;
        float snapM = 0.0f;
    };

    std::vector<NodeState> m_nodes;
    std::vector<EdgeState> m_edges;
    std::vector<std::string> m_events;
    std::vector<FragmentSpawnInfo> m_pendingFragments;
    std::unique_ptr<BlastRuntime> m_blastRuntime;
    std::unique_ptr<SupportGraphSolver> m_graphSolver;
    std::unique_ptr<LoadPathSolver> m_loadPathSolver;
    uint32_t m_seed = 0;
    bool m_progressiveCollapse = false;
    bool m_hasDamage = false;
    std::deque<int> m_dirtyNodes;
    std::vector<bool> m_dirtyFlags;
    uint32_t m_analysisTick = 0;
    StructuralConfig m_config;
    StructuralPreset m_preset = StructuralPreset::Floors5Columns4;
    int m_activeFloors = 5;
    int m_activeColumns = 4;
    int m_activeBlocks = 4;
    int m_activeWalls = 0;
    std::vector<Snapshot> m_history;
    float m_cascadeDelay = 0.5f;
    float m_currentTime = 0.0f;
    float m_pendingStagger = 0.0f;
    std::vector<PendingFail> m_pending;
};
}
