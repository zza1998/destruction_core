#include "BlastSupportModel.h"
#include "BlastRuntime.h"
#include "ContactEdges.h"
#include "SceneLayout.h"
#include "StaticGravitySolver.h"
#include "SupportGraphSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <random>

namespace blast_demo
{

namespace
{
const char* nodeTypeName(const NodeState& node)
{
    if (node.id == 0) return "Ground";
    const MemberRole role = deriveRole(node.box);
    return role == MemberRole::VerticalBearing ? "Bearing" : "Plate";
}
}

BlastSupportModel::BlastSupportModel(const StructuralConfig& config)
    : m_config(config)
    , m_blastRuntime(new BlastRuntime())
    , m_graphSolver(new SupportGraphSolver())
    , m_staticGravitySolver(new StaticGravitySolver())
{
    reset();
}

BlastSupportModel::~BlastSupportModel()
{
}

int BlastSupportModel::blockId(int floor, int slot) const
{
    return 1 + floor * m_activeBlocks + slot;
}

int BlastSupportModel::columnId(int floor, int slot) const
{
    return 1 + m_activeFloors * m_activeBlocks + floor * m_activeColumns + slot;
}

int BlastSupportModel::wallId(int floor, int slot) const
{
    return 1 + m_activeFloors * m_activeBlocks + m_activeFloors * m_activeColumns +
        floor * m_activeWalls + slot;
}

bool BlastSupportModel::setPreset(StructuralPreset preset)
{
    int floors = 0;
    int columns = 0;
    int blocks = 0;
    int walls = 0;
    switch (preset)
    {
    case StructuralPreset::Floors5Columns4: floors = 5; columns = 4; blocks = 4; break;
    case StructuralPreset::Grid4x4Floors4: floors = 4; columns = 4; blocks = 16; walls = 8; break;
    case StructuralPreset::ShearPair: floors = 1; columns = 2; blocks = 4; break;
    default: return false;
    }
    if (floors > MaxFloors || columns > MaxWallsPerFloor || blocks > MaxBlocksPerFloor) return false;
    m_preset = preset;
    m_activeFloors = floors;
    m_activeColumns = columns;
    m_activeBlocks = blocks;
    m_activeWalls = walls;
    reset();
    return true;
}

void BlastSupportModel::reset()
{
    m_blastRuntime->destroy();
    m_nodes.clear();
    m_edges.clear();
    m_events.clear();
    m_history.clear();
    m_pendingFragments.clear();
    m_progressiveCollapse = false;
    m_hasDamage = false;
    m_dirtyNodes.clear();
    m_analysisTick = 0;
    m_pending.clear();

    m_nodes.push_back({0, "Ground", -1, 0, 100, 0, 0, 100000, true, true, NodeStatus::Safe});
    // IDs are also vector indices: Ground, then every block, then every
    // column/wall. Keep this order aligned with blockId()/columnId().
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeBlocks; ++slot)
        {
            const std::string slabName = "F" + std::to_string(floor + 1) + "-B" + std::to_string(slot + 1);
            NodeState slab;
            slab.id = blockId(floor, slot);
            slab.name = slabName;
            slab.floor = floor;
            slab.slot = slot;
            slab.health = 100;
            slab.mass = 25;
            slab.capacity = 290;
            slab.shearCapacity = m_config.plateShearCapacity;
            slab.alive = true;
            slab.supported = true;
            slab.status = NodeStatus::Safe;
            slab.box = nodeLayout(floor, slot, LayoutKind::Slab, m_activeColumns, m_activeBlocks, false);
            // A slab may overhang past its last support (an end support removed)
            // by up to plateOverhangFactor times its own half-width before the
            // resulting cantilever moment fails it. This lets a ShearPair row drop
            // its free end as an L-shape after one end column is destroyed, with
            // the tolerance designers tune via Small/Medium/Large.
            slab.maxOverhang = std::max(slab.box.hx, slab.box.hz) * m_config.plateOverhangFactor;
            m_nodes.push_back(slab);
        }
    }
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeColumns; ++slot)
        {
const float capacity = isGrid() ? gridCapacityFor(floor)
                                            : columnCapacityFor(floor);
            NodeState col;
            col.id = columnId(floor, slot);
            col.name = "F" + std::to_string(floor + 1) + "-C" + std::to_string(slot + 1);
            col.floor = floor;
            col.slot = slot;
            col.health = 100;
            col.mass = floor == 0 ? 50.0f : 25.0f;
            col.alive = true;
            col.supported = true;
            col.status = NodeStatus::Safe;
            col.box = nodeLayout(floor, slot, LayoutKind::Column,
                                 m_activeColumns, m_activeBlocks, false);
            // Capacity scales with the member's cross-section area relative to
            // the reference column section (the 0.3x0.3 1:1 column that the
            // capacity values are tuned for), so a thicker member both carries
            // more of the storey (area-weighted load) and can hold more.
            const float refArea = (2.0f * 0.3f) * (2.0f * 0.3f);
            const float memberArea = (2.0f * col.box.hx) * (2.0f * col.box.hz);
            col.capacity = capacity * (memberArea / refArea);
            // Lateral capacity scales with the member's own section like axial
            // capacity, from the single plateShearCapacity source. Necessary so
            // a vertical bearing does not shear on any trivial positive value.
            col.shearCapacity = m_config.plateShearCapacity * (memberArea / refArea);
            m_nodes.push_back(col);
        }
    }
    // Grid load-bearing walls, appended after every column: the north wall is
    // split into 4 segments (slot 0..3, one per grid column along the north
    // edge of the slab) and the west wall into 4 segments (slot 4..7, one per
    // grid row along the west edge). They are extra nodes, so they never reuse
    // a column slot.
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeWalls; ++slot)
        {
            const float capacity = isGrid() ? gridCapacityFor(floor) : 0.0f;
            const bool north = slot < 4;
            const std::string wallName = "F" + std::to_string(floor + 1) +
                (north ? "-WN" : "-WW") + std::to_string(north ? slot : slot - 4);
            NodeState wall;
            wall.id = wallId(floor, slot);
            wall.name = wallName;
            wall.floor = floor;
            wall.slot = slot;
            wall.health = 100;
            wall.mass = floor == 0 ? 50.0f : 25.0f;
            wall.alive = true;
            wall.supported = true;
            wall.status = NodeStatus::Safe;
            wall.box = nodeLayout(floor, slot, LayoutKind::Wall, m_activeColumns, m_activeBlocks, false);
            const float refArea = (2.0f * 0.3f) * (2.0f * 0.3f);
            const float memberArea = (2.0f * wall.box.hx) * (2.0f * wall.box.hz);
            wall.capacity = capacity * (memberArea / refArea);
            wall.shearCapacity = m_config.plateShearCapacity * (memberArea / refArea);
            m_nodes.push_back(wall);
        }
    }
    for (size_t index = 0; index < m_nodes.size(); ++index)
    {
        if (m_nodes[index].id != static_cast<int>(index))
            addEvent("ERROR: node ID/index mismatch at " + std::to_string(index));
    }
    rebuildEdges();
    std::string blastError;
    if (!m_blastRuntime->initialize(m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, blastError))
        addEvent(blastError);
    applyStaticGravityResult(m_staticGravitySolver->solve(m_nodes, m_edges));
    if (isGrid())
        addEvent("Reset: " + std::to_string(m_activeFloors) + " floors, " +
                 std::to_string(m_activeBlocks) + " blocks + " +
                 std::to_string(m_activeColumns) + " columns + " +
                 std::to_string(m_activeWalls) + " walls per floor (grid 4x4), " +
                 std::to_string(m_activeColumns + m_activeWalls) + " load paths per floor.");
    else
        addEvent("Reset: " + std::to_string(m_activeFloors) + " floors, " +
                 std::to_string(m_activeFloors * m_activeColumns) + " columns, " +
                 std::to_string(m_activeColumns) + " load paths per floor.");
    m_dirtyFlags.assign(m_nodes.size(), false);
}

bool BlastSupportModel::damageNode(int nodeId, float amount)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_nodes.size()) ||
        !std::isfinite(amount) || amount <= 0.0f)
        return false;

    NodeState& node = m_nodes[static_cast<size_t>(nodeId)];
    if (node.id == 0 || !node.alive)
        return false;

    saveSnapshot();
    m_hasDamage = true;
    node.health = std::max(0.0f, node.health - amount);
    addEvent(node.name + " damage -> " + std::to_string(static_cast<int>(node.health)) + " HP");
    if (node.health == 0.0f)
    {
        node.releasedLoad = node.mass;
        node.alive = false;
        addEvent(node.name + " failed; load path removed.");
        std::vector<FragmentSpawnInfo> fragments;
        m_blastRuntime->fractureMember(node, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
        m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
        // A dead node is not a valid relay in the static-gravity model: cut every
        // incident edge. Live neighbors keep their own edges and may reroute.
        for (EdgeState& edge : m_edges)
            if (edge.from == nodeId || edge.to == nodeId)
                edge.alive = false;
    }
    if (node.health == 0.0f)
        markIncidentNeighborsDirty(nodeId);
    else
        markDirty(nodeId);
    return true;
}

void BlastSupportModel::markFalling(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_nodes.size()))
        return;
    NodeState& node = m_nodes[static_cast<size_t>(nodeId)];
    // Mark a structurally "supported" member that is physically collapsing.
    if (node.alive && (node.status == NodeStatus::Safe || node.status == NodeStatus::Warning))
        node.status = NodeStatus::Falling;
}

void BlastSupportModel::takePendingFragments(std::vector<FragmentSpawnInfo>& out)
{
    out.assign(m_pendingFragments.begin(), m_pendingFragments.end());
    m_pendingFragments.clear();
}

void BlastSupportModel::saveSnapshot()
{
    Snapshot snapshot;
    snapshot.nodes = m_nodes;
    snapshot.edges = m_edges;
    snapshot.events = m_events;
    snapshot.seed = m_seed;
    snapshot.progressiveCollapse = m_progressiveCollapse;
    snapshot.dirtyNodes = m_dirtyNodes;
    snapshot.dirtyFlags = m_dirtyFlags;
    snapshot.analysisTick = m_analysisTick;
    m_history.push_back(std::move(snapshot));
    if (m_history.size() > 100)
        m_history.erase(m_history.begin());
}

bool BlastSupportModel::undoLast()
{
    if (m_history.empty())
        return false;
    const Snapshot snapshot = std::move(m_history.back());
    m_history.pop_back();
    m_nodes = snapshot.nodes;
    m_edges = snapshot.edges;
    m_events = snapshot.events;
    m_seed = snapshot.seed;
    m_progressiveCollapse = snapshot.progressiveCollapse;
    m_dirtyNodes = snapshot.dirtyNodes;
    m_dirtyFlags = snapshot.dirtyFlags;
    m_analysisTick = snapshot.analysisTick;
    m_pending.clear();
    m_pendingStagger = 0.0f;
    m_pendingFragments.clear();
    m_blastRuntime->destroy();
    std::string blastError;
    if (!m_blastRuntime->initialize(m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, blastError))
        addEvent(blastError);
    addEvent("Undo: restored previous structural state.");
    return true;
}


void BlastSupportModel::rebuildEdges()
{
    m_edges.clear();
    m_edges = rebuildEdgesFromContacts(m_nodes);
}

float BlastSupportModel::gridCapacityFor(int floor) const
{
    // The static-gravity solver accumulates load downward along graph paths, so
    // a lower member carries the mass of every story above it. Capacity is a
    // fraction of that accumulated whole-building total; the 0.10 factor keeps
    // the most-loaded corner member comfortably under capacity at rest while
    // still allowing concentrated redistribution to overload survivors.
    float total = 0.0f;
    for (int f = m_activeFloors - 1; f >= floor; --f)
    {
        const float memberMass = f == 0 ? 50.0f : 25.0f;
        total += static_cast<float>(m_activeBlocks) * 25.0f +
                 static_cast<float>(m_activeColumns) * memberMass +
                 static_cast<float>(m_activeWalls) * memberMass;
    }
    return total * 0.10f;
}

float BlastSupportModel::columnCapacityFor(int floor) const
{
    // Linear (non-grid) preset: each slot is an independent vertical chain, and
    // a column at `floor` carries every slab and column of the storeys at or
    // above it. That nominal vertical load is 25*(stories) for the slabs plus
    // 25*(stories-1) for the upper columns (the top storey column carries only
    // its own slab). The ground storey column additionally carries its own
    // 50-mass section, so it keeps the explicit lowerColumnCapacity instead.
    if (floor == 0)
        return m_config.lowerColumnCapacity;
    // Each upper storey's column carries its own 25-mass section, the 25-mass
    // slab directly above it, and (for storeys above it) one more slab and
    // column pair, so the nominal vertical load is 50 * (storeys at/below it):
    // e.g. the top column carries 50, the next 100, then 150, 200.
    const int stories = m_activeFloors - floor;
    const float nominalLoad = 50.0f * static_cast<float>(stories);
    return nominalLoad * m_config.upperColumnSafetyFactor;
}

void BlastSupportModel::setPlateShearCapacity(float loadUnits)
{
    m_config.plateShearCapacity = std::max(loadUnits, 0.0f);
    // Re-derive every member's lateral (shear) capacity, mirroring reset():
    // a plate keeps the nominal config value; a vertical bearing scales with
    // its own section like its axial capacity, so a thicker member resists
    // more lateral load.
    static constexpr float kRefArea = (2.0f * 0.3f) * (2.0f * 0.3f);
    for (NodeState& node : m_nodes)
    {
        if (node.id == 0) continue;
        if (deriveRole(node.box) == MemberRole::VerticalBearing)
        {
            const float memberArea = (2.0f * node.box.hx) * (2.0f * node.box.hz);
            node.shearCapacity = m_config.plateShearCapacity * (memberArea / kRefArea);
        }
        else
        {
            node.shearCapacity = m_config.plateShearCapacity;
        }
    }
}

void BlastSupportModel::setPlateOverhangFactor(float factor)
{
    m_config.plateOverhangFactor = std::max(factor, 0.0f);
    for (NodeState& node : m_nodes)
    {
        if (node.id == 0 || deriveRole(node.box) != MemberRole::HorizontalPlate)
            continue;
        node.maxOverhang = std::max(node.box.hx, node.box.hz) * m_config.plateOverhangFactor;
    }
    // Re-run the static solve so overloads are recomputed with the new
    // tolerance and no member keeps a stale pending failure from the old one.
    m_pending.clear();
    applyStaticGravityResult(m_staticGravitySolver->solve(m_nodes, m_edges));
}

void BlastSupportModel::setPlateOverhang(PlateOverhang level)
{
    switch (level)
    {
    case PlateOverhang::Small:  setPlateOverhangFactor(1.0f); break;
    case PlateOverhang::Medium: setPlateOverhangFactor(2.5f); break;
    case PlateOverhang::Large:  setPlateOverhangFactor(5.0f); break;
    }
}

void BlastSupportModel::damageColumn(int floor, int slot, float amount)
{
    if (floor < 0 || floor >= m_activeFloors || slot < 0 || slot >= m_activeColumns)
        return;
    damageNode(columnId(floor, slot), amount);
}

void BlastSupportModel::randomDamageTwo(uint32_t seed)
{
    m_seed = seed;
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> floorDist(0, m_activeFloors - 1);
    std::uniform_int_distribution<int> slotDist(0, m_activeColumns - 1);
    const int firstFloor = floorDist(generator);
    const int firstSlot = slotDist(generator);
    int secondFloor = floorDist(generator);
    int secondSlot = slotDist(generator);
    while (firstFloor == secondFloor && firstSlot == secondSlot)
    {
        secondFloor = floorDist(generator);
        secondSlot = slotDist(generator);
    }
    addEvent("Random seed " + std::to_string(seed) + ": two columns selected.");
    damageColumn(firstFloor, firstSlot, 100.0f);
    damageColumn(secondFloor, secondSlot, 100.0f);
}

void BlastSupportModel::stepAnalysis()
{
    tickAnalysis();
}

void BlastSupportModel::markDirty(int nodeId)
{
    m_graphSolver->markDirty(nodeId, m_nodes, m_dirtyNodes, m_dirtyFlags);
}

void BlastSupportModel::markIncidentNeighborsDirty(int nodeId)
{
    m_graphSolver->markIncidentNeighborsDirty(nodeId, m_nodes, m_edges, m_dirtyNodes, m_dirtyFlags);
}

void BlastSupportModel::setCascadeDelay(float seconds)
{
    m_cascadeDelay = seconds < 0.0f ? 0.0f : seconds;
}

void BlastSupportModel::scheduleFail(int nodeId, NodeStatus status, const std::string& reason,
                                    float snapN, float snapV, float snapM)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_nodes.size()) || !m_nodes[nodeId].alive)
        return;
    for (const PendingFail& pending : m_pending)
        if (pending.nodeId == nodeId) return;
    PendingFail pending;
    pending.nodeId = nodeId;
    // All overloads reported by one solver result share one failure wave: the
    // same due time, no intra-wave stagger. delay<=0 keeps the effect immediate.
    pending.dueTime = m_currentTime + m_cascadeDelay;
    pending.status = status;
    pending.reason = reason;
    pending.spawnFragments = true;
    if (status == NodeStatus::Overloaded)
    {
        if (snapN != 0.0f || snapV != 0.0f || snapM != 0.0f)
        {
            pending.snapN = snapN;
            pending.snapV = snapV;
            pending.snapM = snapM;
        }
    }
    m_pending.push_back(pending);
    addEvent("Scheduled " + m_nodes[nodeId].name +
             " [" + nodeTypeName(m_nodes[nodeId]) +
             (status == NodeStatus::Overloaded ? "] overload" : "] unsupported") +
             (reason.empty() ? "" : " (" + reason + ")") +
             " in " + std::to_string(m_cascadeDelay) + " s");
}

void BlastSupportModel::executePendingFail(int nodeId, NodeStatus status, const std::string& reason,
                                           float snapN, float snapV, float snapM, bool spawnFragments)
{
    NodeState& node = m_nodes[nodeId];
    node.releasedLoad = node.mass;
    node.health = 0.0f;
    node.alive = false;
    node.supported = false;
    node.status = status;
    const std::string detail = reason.empty()
        ? std::string(status == NodeStatus::Overloaded ? "overloaded" : "unsupported")
        : (status == NodeStatus::Unsupported ? "unsupported" : reason + " overloaded");
    // Attach the measured force and its threshold from the snapshot taken when
    // the member was scheduled, so the log shows how far it exceeded the limit
    // (e.g. M=1912.95/900.00 N*m) instead of a later solve's value.
    std::string values;
    if (status == NodeStatus::Overloaded)
    {
        char buf[128];
        if (reason == "axial")
        {
            const float cap = node.capacity * 9.81f;
            std::snprintf(buf, sizeof(buf), "  N=%.2f/%.2f N", snapN, cap);
        }
        else if (reason == "bending")
        {
            const float momentLimit = node.capacity * 9.81f * node.maxOverhang;
            std::snprintf(buf, sizeof(buf), "  M=%.2f/%.2f N*m", snapM, momentLimit);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "  N=%.2f V=%.2f M=%.2f", snapN, snapV, snapM);
        }
        values = buf;
    }
    addEvent(node.name + " [" + nodeTypeName(node) + "] failed (" + detail +
             "); load path removed." + values);
    if (spawnFragments)
    {
        std::vector<FragmentSpawnInfo> fragments;
        m_blastRuntime->fractureMember(node, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
        m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
    }
    // A dead node is not a valid relay in the static-gravity model: cut every
    // incident edge. Live neighbors keep their own edges and may reroute.
    for (EdgeState& edge : m_edges)
        if (edge.from == nodeId || edge.to == nodeId)
            edge.alive = false;
    markIncidentNeighborsDirty(nodeId);
}

void BlastSupportModel::applyStaticGravityResult(const StaticGravityResult& result)
{
    // Copy diagnostics and the confirmed supported state onto live nodes, then
    // fail unsupported nodes immediately and schedule overloads as one wave.
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
    {
        NodeState& node = m_nodes[static_cast<size_t>(i)];
        node.lateralShear = 0.0f;
        if (i == 0 || !node.alive) continue;
        const StaticGravityNodeResult& r = result.nodes[static_cast<size_t>(i)];
        node.supported = r.supported;
        node.load = r.supported ? r.carriedMass : 0.0f;
        node.carriedMass = r.carriedMass;
        node.carriedComX = r.carriedComX;
        node.carriedComZ = r.carriedComZ;
        node.compressionUtilization = r.compressionUtilization;
        node.bendingUtilization = r.bendingUtilization;
        node.utilization = r.utilization;
    }
    for (int id : result.unsupportedNodes)
    {
        if (id <= 0 || id >= static_cast<int>(m_nodes.size()) || !m_nodes[static_cast<size_t>(id)].alive)
            continue;
        executePendingFail(id, NodeStatus::Unsupported, "", 0.0f, 0.0f, 0.0f, false);
    }
    for (int id : result.overloadedNodes)
    {
        if (id <= 0 || id >= static_cast<int>(m_nodes.size()) || !m_nodes[static_cast<size_t>(id)].alive)
            continue;
        const NodeState& node = m_nodes[static_cast<size_t>(id)];
        const bool isPlate = deriveRole(node.box) == MemberRole::HorizontalPlate;
        const bool bendingDominant = isPlate || node.bendingUtilization > node.compressionUtilization;
        const std::string reason = bendingDominant ? "bending" : "axial";
        if (bendingDominant && !isPlate)
        {
            const float eccentricity = std::sqrt(
                (node.carriedComX - node.box.cx) * (node.carriedComX - node.box.cx) +
                (node.carriedComZ - node.box.cz) * (node.carriedComZ - node.box.cz));
            const float moment = node.carriedMass * 9.81f * eccentricity;
            scheduleFail(id, NodeStatus::Overloaded, reason, 0.0f, 0.0f, moment);
        }
        else if (bendingDominant)
        {
            scheduleFail(id, NodeStatus::Overloaded, reason, 0.0f, 0.0f, 0.0f);
        }
        else
        {
            const float normalForce = node.carriedMass * 9.81f;
            scheduleFail(id, NodeStatus::Overloaded, reason, normalForce, 0.0f, 0.0f);
        }
    }
}

void BlastSupportModel::update(float time)
{
    m_currentTime = time;
    int guard = 0;
    while (guard++ < 64)
    {
        bool executed = false;
        auto it = m_pending.begin();
        while (it != m_pending.end())
        {
            if (it->dueTime <= time)
            {
                if (m_nodes[it->nodeId].alive)
                {
                    executePendingFail(it->nodeId, it->status, it->reason,
                                       it->snapN, it->snapV, it->snapM,
                                       it->spawnFragments);
                    executed = true;
                }
                it = m_pending.erase(it);
            }
            else ++it;
        }
        if (!executed) break;
        tickAnalysis();
    }
}

void BlastSupportModel::tickAnalysis()
{
    ++m_analysisTick;
    if (m_dirtyNodes.empty()) return;
    // Clear the dirty queue up-front: the static solver re-analyzes the whole
    // live graph, and any immediate unsupported release re-marks neighbors.
    m_dirtyNodes.clear();
    std::fill(m_dirtyFlags.begin(), m_dirtyFlags.end(), false);

    const unsigned int maxWaves = m_config.maxCascadeWaves < 1u ? 1u : m_config.maxCascadeWaves;
    unsigned int wave = 0;
    for (; wave < maxWaves; ++wave)
    {
        const StaticGravityResult result = m_staticGravitySolver->solve(m_nodes, m_edges);
        applyStaticGravityResult(result);
        if (result.unsupportedNodes.empty())
            break;
    }

    if (wave == maxWaves)
    {
        // Safety fallback: no stable solution within the wave budget. Release any
        // still-live node that currently has no Ground path and report it.
        StaticGravityResult result = m_staticGravitySolver->solve(m_nodes, m_edges);
        for (int id : result.unsupportedNodes)
        {
            if (id <= 0 || id >= static_cast<int>(m_nodes.size()) || !m_nodes[static_cast<size_t>(id)].alive)
                continue;
            executePendingFail(id, NodeStatus::Unsupported, "", 0.0f, 0.0f, 0.0f, false);
        }
        addEvent("Cascade guard reached; released unresolved components.");
    }

    addEvent("Tick " + std::to_string(m_analysisTick) + ": waves=" + std::to_string(wave) +
             " remaining=" + std::to_string(m_dirtyNodes.size()) +
             " pending=" + std::to_string(m_pending.size()));

    // A full live-graph re-analysis has run; any dirt re-marked during the
    // release cascade has already been consumed by the re-solve loop above.
    m_dirtyNodes.clear();
    std::fill(m_dirtyFlags.begin(), m_dirtyFlags.end(), false);
}

void BlastSupportModel::addEvent(const std::string& text)
{
    // Timestamp each event with the wall-clock date and time (local).
    const std::time_t now = std::time(nullptr);
    char stamp[32];
#if defined(_MSC_VER)
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &localTime);
#else
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
#endif
    m_events.push_back(std::string("[") + stamp + "] " + text);
}
}
