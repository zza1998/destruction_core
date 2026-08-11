#pragma once

#include "BlastRuntime.h"

#include <string>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace blast_demo
{
class SupportGraphSolver;
class LoadPathSolver;

enum class NodeType { Slab, Column, Ground, Wall };
enum class NodeStatus { Safe, Warning, Overloaded, Broken, Unsupported, Falling };

struct NodeState
{
    int id = 0;
    std::string name;
    NodeType type = NodeType::Column;
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
};

struct EdgeState
{
    int from = 0;
    int to = 0;
    float capacity = 0.0f;
    float load = 0.0f;
    bool alive = true;
};

struct StructuralConfig
{
    float lowerColumnCapacity = 800.0f;
    float upperColumnCapacity = 290.0f;
    float overloadFailureRatio = 1.0f;
};

enum class StructuralPreset
{
    Floors5Columns4,
    Floors3Columns4,
    Floors2Columns4,
    Floors5Columns2,
    House3Floors,
    Grid4x4Floors4
};

class BlastSupportModel
{
public:
    static constexpr int MaxFloors = 5;
    static constexpr int MaxColumnsPerFloor = 4;
    static constexpr int MaxWallsPerFloor = 8;
    static constexpr int MaxBlocksPerFloor = 16;
    static constexpr int HouseWallsPerFloor = 8;
    static constexpr int HouseSlabsPerFloor = 4;

    explicit BlastSupportModel(const StructuralConfig& config = StructuralConfig());
    ~BlastSupportModel();

    void reset();
    bool setPreset(StructuralPreset preset);
    StructuralPreset preset() const { return m_preset; }
    bool isHouse() const { return m_preset == StructuralPreset::House3Floors; }
    // Grid layout: every floor has more blocks than columns (blocks tile the
    // floor in a 4x4 grid and the columns stand at its corners), so a column
    // carries several blocks instead of one.
    bool isGrid() const { return m_activeBlocks > m_activeColumns; }
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
    void rebuildHouseEdges();
    void rebuildGridEdges();
    float gridCapacityFor(int floor) const;
    void markDirty(int nodeId);
    void markIncidentNeighborsDirty(int nodeId);
    void addEvent(const std::string& text);
    void saveSnapshot();
    void scheduleFail(int nodeId, NodeStatus status);
    void executePendingFail(int nodeId, NodeStatus status);

    struct PendingFail
    {
        int nodeId = -1;
        float dueTime = 0.0f;
        NodeStatus status = NodeStatus::Unsupported;
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
