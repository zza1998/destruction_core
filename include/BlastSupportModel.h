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
class StaticGravitySolver;
struct StaticGravityResult;

struct StructuralConfig
{
    float lowerColumnCapacity = 800.0f;
    float upperColumnCapacity = 290.0f;
    // Upper-floor columns are sized as a multiple of their nominal vertical load,
    // so a lower storey has a larger absolute margin than an upper one and
    // survives a partial load redistribution (e.g. two destroyed columns) instead
    // of collapsing floor-by-floor from the top.
float upperColumnSafetyFactor = 2.5f;
    float overloadFailureRatio = 1.0f;
    // How far a floor plate may overhang past its last support before the
    // resulting cantilever moment fails it, expressed as a multiple of its own
    // half-width. Small values snap a free end quickly; large values let a plate
    // span a longer gap. This is the designer-facing "cantilever tolerance".
    float plateOverhangFactor = 1.0f;
    // Maximum number of failure waves allowed per tickAnalysis before the
    // unresolved supported component is forcibly released. Clamped to at least 1.
    unsigned int maxCascadeWaves = 64;
};

enum class StructuralPreset
{
    Floors5Columns4,
    Grid4x4Floors4,
    ShearPair       // 单层双板对：用于测试横向悬挑 (cantilever overhang)
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
    // Sets how far a floor plate may overhang its last support before snapping,
    // as a multiple of its own half-width. Re-derives every slab's maxOverhang.
    void setPlateOverhangFactor(float factor);
    // Convenience presets for the designer-facing cantilever tolerance.
    enum class PlateOverhang { Small, Medium, Large };
    void setPlateOverhang(PlateOverhang level);
    // Sets the vertical-bearing failure threshold: a column fails when its axial
    // utilization (carried mass / capacity) reaches this fraction.
    void setColumnFailureRatio(float ratio);
    // Convenience presets: Small=0.7 (fails at 70% usage, fragile), Medium=0.85,
    // Large=1.0 (fails only at full capacity, sturdy).
    enum class ColumnStrength { Small, Medium, Large };
    void setColumnStrength(ColumnStrength level);

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
    // Per-storey vertical bearing capacity for the linear (non-grid, 5x4)
    // preset: the ground storey keeps lowerColumnCapacity, and every storey
    // above it is a multiple (upperColumnSafetyFactor) of its nominal vertical
    // load so lower storeys carry a larger absolute margin.
    float columnCapacityFor(int floor) const;
    // Applies one StaticGravityResult to model state: copies supported/load and
    // diagnostic fields onto live nodes, then schedules/executes reported
    // failures (unsupported immediate, overloaded as a delayed wave).
    void applyStaticGravityResult(const StaticGravityResult& result);
    void markDirty(int nodeId);
    void markIncidentNeighborsDirty(int nodeId);
    void addEvent(const std::string& text);
    void saveSnapshot();
    void scheduleFail(int nodeId, NodeStatus status, const std::string& reason = "",
                      float snapN = 0.0f, float snapV = 0.0f, float snapM = 0.0f);
    void executePendingFail(int nodeId, NodeStatus status, const std::string& reason = "",
                            float snapN = 0.0f, float snapV = 0.0f, float snapM = 0.0f,
                            bool spawnFragments = true);

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
        bool spawnFragments = true;
    };

    std::vector<NodeState> m_nodes;
    std::vector<EdgeState> m_edges;
    std::vector<std::string> m_events;
    std::vector<FragmentSpawnInfo> m_pendingFragments;
    std::unique_ptr<BlastRuntime> m_blastRuntime;
    std::unique_ptr<SupportGraphSolver> m_graphSolver;
    std::unique_ptr<StaticGravitySolver> m_staticGravitySolver;
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
