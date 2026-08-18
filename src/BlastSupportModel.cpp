#include "BlastSupportModel.h"
#include "BlastRuntime.h"
#include "ContactEdges.h"
#include "LoadPathSolver.h"
#include "SceneLayout.h"
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
    , m_loadPathSolver(new LoadPathSolver())
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
            m_nodes.push_back(slab);
        }
    }
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeColumns; ++slot)
        {
            const float capacity = isGrid() ? gridCapacityFor(floor)
                                            : (floor == 0 ? m_config.lowerColumnCapacity : m_config.upperColumnCapacity);
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
    m_loadPathSolver->route(m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls);
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
        // The dead member's own weight is what a block above must now relay
        // sideways (its lateral duty); this is NOT added to the storey vertical
        // total (LoadPathSolver drops dead members), so use its own mass, not a
        // pre-failure load share.
        node.releasedLoad = node.mass;
        node.alive = false;
        addEvent(node.name + " failed; load path removed.");
        std::vector<FragmentSpawnInfo> fragments;
        m_blastRuntime->fractureMember(node, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
        m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
        for (EdgeState& edge : m_edges)
        {
            const bool incident = edge.from == nodeId || edge.to == nodeId;
            const bool horizontal = edge.from > 0 && edge.to > 0 &&
                horizontalContact(m_nodes[edge.from].box, m_nodes[edge.to].box, kContactTol);
            // A destroyed block must keep its horizontal bonds alive so its
            // released load can route to neighboring blocks. Vertical support
            // edges are removed because the block can no longer carry load.
            if (incident && !horizontal) edge.alive = false;
        }
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
    // Whole-floor load sharing accumulates downward: each floor contributes its
    // live block, column and wall masses, so a lower member carries the
    // stories above it. The capacity is the reference column's share of that
    // total: with area-weighted load distribution every bearer's utilisation
    // is the same, and the 0.051 factor sits between "one column destroyed
    // stays under capacity" and "two columns destroyed overload the survivors".
    float total = 0.0f;
    for (int f = m_activeFloors - 1; f >= floor; --f)
    {
        const float memberMass = f == 0 ? 50.0f : 25.0f;
        total += static_cast<float>(m_activeBlocks) * 25.0f +
                 static_cast<float>(m_activeColumns) * memberMass +
                 static_cast<float>(m_activeWalls) * memberMass;
    }
    return total * 0.040f;
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

// Correct lateral-shear model (continuous-beam / diaphragm): destroying a support
// makes its released load travel sideways along the plate band toward the nearest
// SURVIVING bearing. A floor band acts like a simply-supported continuous member,
// so its shear force is largest near the surviving support and smallest at the
// dead / free end — the surviving column's plate must carry the accumulated load
// of every unsupported plate between it and the failure. We walk each source
// plate toward the nearest surviving bearing, accumulating a "transported" load
// that grows as we approach the support; each plate on that path is credited the
// load it must pass sideways, so the plate next to the surviving column sees the
// peak shear and the dead column's plate sees the least.
void BlastSupportModel::detectLateralShear(std::vector<int>& overloaded,
                                           std::vector<float>& lateralValues)
{
    overloaded.clear();
    lateralValues.clear();
    std::vector<float> lateral(m_nodes.size(), 0.0f);
    const int n = static_cast<int>(m_nodes.size());

    auto isAliveBearing = [&](int id) {
        return id > 0 && id < n && m_nodes[id].alive &&
               deriveRole(m_nodes[id].box) == MemberRole::VerticalBearing;
    };
    auto isPlate = [&](int id) {
        return id > 0 && id < n && m_nodes[id].alive &&
               deriveRole(m_nodes[id].box) == MemberRole::HorizontalPlate;
    };
    auto sameStorey = [&](const NodeState& a, const NodeState& b) {
        return std::fabs(a.box.cy - b.box.cy) <= 1e-3f;
    };

    // Source: for each alive plate, the released load of the dead supports directly
    // beneath it (its own weight no longer has a vertical path) plus its own mass,
    // which it must push sideways.
    std::vector<float> sourceLoad(m_nodes.size(), 0.0f);
    std::vector<int> sourcePlate;
    for (const NodeState& node : m_nodes)
    {
        if (!isPlate(node.id)) continue;
        float duty = 0.0f;
        for (const EdgeState& edge : m_edges)
        {
            if (edge.from != node.id) continue;
            if (edge.to <= 0 || edge.to >= n) continue;
            const NodeState& support = m_nodes[edge.to];
            if (support.alive) continue;
            if (std::fabs(node.box.minY() - support.box.maxY()) > kContactTol) continue;
            duty += support.releasedLoad;
        }
        if (duty > 0.0f)
        {
            sourceLoad[node.id] = duty;
            sourcePlate.push_back(node.id);
        }
    }
    if (sourcePlate.empty()) goto done;

    // For each source plate, walk toward the nearest surviving bearing along the
    // horizontal plate path, accumulating the transported load. We try both the X
    // and Z axes; each is a candidate "run" direction. The load splits evenly
    // among the directions that lead to a surviving bearing.
    for (int sid : sourcePlate)
    {
        const NodeState& S = m_nodes[sid];
        const float load = sourceLoad[sid];
        if (load <= 0.0f) continue;

        // Neighbours of S that are same-storey plates (horizontal contact).
        auto plateNeighbours = [&](int id) {
            std::vector<int> out;
            const NodeState& a = m_nodes[id];
            for (int j = 1; j < n; ++j)
            {
                if (!isPlate(j) || j == id) continue;
                if (!sameStorey(a, m_nodes[j])) continue;
                if (!horizontalContact(a.box, m_nodes[j].box, kContactTol)) continue;
                out.push_back(j);
            }
            return out;
        };

        // Distance from a plate to the nearest surviving bearing on its floor
        // (match by floor index, not cy — plates and the columns that support
        // them sit at different heights in the same storey).
        auto nearestSupportDistSq = [&](int id) -> float {
            const NodeState& a = m_nodes[id];
            float best = -1.0f;
            for (int j = 1; j < n; ++j)
            {
                if (!isAliveBearing(j) || m_nodes[j].floor != a.floor) continue;
                const float dx = a.box.cx - m_nodes[j].box.cx;
                const float dz = a.box.cz - m_nodes[j].box.cz;
                const float d2 = dx * dx + dz * dz;
                if (best < 0.0f || d2 < best) best = d2;
            }
            return best;
        };

        // Dedicated BFS toward the nearest surviving bearing: accumulate load on
        // the path from S outward, adding to each plate then growing transported.
        // We perform a flood where the accumulated load is carried to the support.
        // Treat the whole connected component as a set of plates; walk from S and
        // keep crediting accumulated load outward, weighting by how close to the
        // surviving support each plate is (closer => passes more).
        // Load splits evenly across the number of distinct nearest-bearing directions.
        std::vector<int> comp;
        std::vector<int> stack{sid};
        std::vector<char> seen(n, 0); seen[sid] = 1;
        while (!stack.empty()) { int c = stack.back(); stack.pop_back(); comp.push_back(c);
            for (int nb : plateNeighbours(c)) if (!seen[nb]) { seen[nb] = 1; stack.push_back(nb); } }
        if (comp.empty()) continue;

        // Distances of component plates to nearest support; the plate at the peak
        // (nearest to the surviving support) takes the largest share, dead-end the
        // smallest. Use inverse-distance weighting from the surviving support so the
        // nearest plate to the support bears the most.
        std::vector<float> d2(comp.size());
        float maxD2 = 0.0f;
        bool anySupport = false;
        for (size_t i = 0; i < comp.size(); ++i)
        {
            d2[i] = nearestSupportDistSq(comp[i]);
            if (d2[i] >= 0.0f) { anySupport = true; if (d2[i] > maxD2) maxD2 = d2[i]; }
        }
        if (!anySupport) continue;
        // Weight: closer to support (smaller d2) => larger shear pass-through.
        float wsum = 0.0f;
        for (size_t i = 0; i < comp.size(); ++i)
        {
            if (d2[i] >= 0.0f) wsum += (maxD2 + 1.0f) - d2[i];
        }
        if (wsum <= 0.0f) continue;
        for (size_t i = 0; i < comp.size(); ++i)
            if (d2[i] >= 0.0f)
                lateral[comp[i]] += load * ((maxD2 + 1.0f) - d2[i]) / wsum;
        // The plates with a surviving bearing directly beneath (d2==0) are not
        // shear-active themselves; they just pass through — but for display we keep
        // their credited share, which the user can read.

        // ALSO credit the direct source plate fully so the shown per-plate shear is
        // never zero at the failure point (it still must relay its own load) — but
        // keep the support-adjacent plates dominant. (Already covered by weights.)
    }

done:
    // Stash the accumulated lateral on each node so the inspector / 3D view and
    // tests can read it directly.
    for (int i = 0; i < n; ++i)
        m_nodes[i].lateralShear = lateral[i];

    // Pass 2: only alive plates shear-fail when their lateral share exceeds
    // their shear capacity. Values are in load units to match shearCapacity.
    for (const NodeState& node : m_nodes)
    {
        if (node.id == 0 || !node.alive ||
            deriveRole(node.box) != MemberRole::HorizontalPlate) continue;
        if (lateral[node.id] > node.shearCapacity)
        {
            overloaded.push_back(node.id);
            lateralValues.push_back(lateral[node.id]);
        }
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
    // Stagger within a wave so failures do not all fire on the same frame:
    // each newly scheduled member is offset a little later than the previous
    // one, producing a short burst of sequential collapses instead of one
    // instant pop. When the delay is 0 the effect must stay immediate.
    pending.dueTime = m_currentTime + m_cascadeDelay + (m_cascadeDelay > 0.0f ? m_pendingStagger : 0.0f);
    m_pendingStagger += m_cascadeDelay > 0.0f ? 0.05f : 0.0f;
    pending.status = status;
    pending.reason = reason;
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
                                           float snapN, float snapV, float snapM)
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
        else if (reason == "shear")
        {
            // The failure criterion uses node.shearCapacity (which is the
            // StructuralConfig::plateShearCapacity for a plate), so the log must
            // compare against that same threshold. snapV is in Newtons (convert
            // to load units on input), so convert the threshold to Newtons too,
            // matching the axial log below.
            std::snprintf(buf, sizeof(buf), "  V=%.2f/%.2f N", snapV, node.shearCapacity * 9.81f);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "  N=%.2f V=%.2f M=%.2f", snapN, snapV, snapM);
        }
        values = buf;
    }
    addEvent(node.name + " [" + nodeTypeName(node) + "] failed (" + detail +
             "); load path removed." + values);
    std::vector<FragmentSpawnInfo> fragments;
    m_blastRuntime->fractureMember(node, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
    m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
    for (EdgeState& edge : m_edges)
    {
        const bool incident = edge.from == nodeId || edge.to == nodeId;
        if (!incident) continue;
        const bool horizontal = edge.from > 0 && edge.to > 0 &&
            horizontalContact(m_nodes[edge.from].box, m_nodes[edge.to].box, kContactTol);
        // On any death path, only the vertical incident edges are cut: a
        // ruined plate must keep its horizontal bonds so its released load can
        // still detour sideways to neighbours (matches damageNode and the
        // documented "destroyed block keeps horizontal bonds" intent).
        if (!horizontal) edge.alive = false;
    }
    markIncidentNeighborsDirty(nodeId);
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
                                       it->snapN, it->snapV, it->snapM);
                    executed = true;
                }
                it = m_pending.erase(it);
            }
            else ++it;
        }
        if (!executed) break;
        // A new wave of failures starts fresh: the intra-wave stagger must not
        // accumulate across waves, otherwise the delay drifts (e.g. many waves
        // push the due time far into the future).
        m_pendingStagger = 0.0f;
        tickAnalysis();
    }
}

void BlastSupportModel::tickAnalysis()
{
    ++m_analysisTick; const size_t initialDirty = m_dirtyNodes.size();
    if (initialDirty == 0) return;
    int guard = 0;
    while (!m_dirtyNodes.empty() && guard++ < 64)
    {
        for (EdgeState& edge : m_edges) edge.load = 0.0f;
        size_t bfsCount = 0;
        std::vector<int> affected = m_graphSolver->collectAffectedNodes(
            m_nodes, m_edges, m_dirtyNodes, m_dirtyFlags, std::numeric_limits<size_t>::max(), bfsCount);
        for (int id : affected)
        {
            NodeState& node = m_nodes[id];
            if (node.id != 0 && node.alive &&
                !m_graphSolver->hasGroundPath(id, m_nodes, m_edges))
            {
                node.releasedLoad = node.mass;
                scheduleFail(id, NodeStatus::Unsupported);
            }
        }
        std::vector<int> overloaded;
        // Per-floor uniform load distribution (scheme 1): each storey gathers
        // the weight of the storeys above plus its own masses and divides it
        // equally among its surviving vertical bearings. A bearing fails when
        // its share exceeds its capacity. This is the driver of failure.
        overloaded = m_loadPathSolver->route(
            m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks,
            m_activeWalls, false);
        // Lateral (horizontal) shear detection, correct physics: when a
        // support directly beneath a plate dies, the plate must relay that
        // support's released load sideways to its same-storey neighbours (via
        // horizontalContact edges), area-weighted like the storey redistribution
        // above. The relaying plate and each neighbour accumulate a lateral
        // force; only a plate shear-fails, when its share exceeds its shear
        // capacity. This replaces the old anti-physical loop that made the
        // member *above* a dead support the shear victim and never actually
        // transferred the load sideways (see detectLateralShear).
        std::vector<int> shearOverloaded;
        std::vector<float> shearValues;
        detectLateralShear(shearOverloaded, shearValues);
        // The whole-storey redistribution couples every member, so a heavy
        // overload near the top also raises the load of the intact storeys
        // below. If we scheduled all of them at once the whole building would
        // pop on the first frame. Collapse from the top down: when several
        // storeys are overloaded only the highest is scheduled this pass; once
        // it fails the solver re-runs and the storeys below it recover. A
        // single overloaded storey (e.g. the surviving ground column after its
        // neighbours are destroyed) is always scheduled.
        std::vector<int> toSchedule = overloaded;
        if (!overloaded.empty())
        {
            int minFloor = std::numeric_limits<int>::max();
            int maxFloor = -1;
            for (int id : overloaded)
                if (id >= 0 && id < static_cast<int>(m_nodes.size()))
                {
                    minFloor = std::min(minFloor, m_nodes[id].floor);
                    maxFloor = std::max(maxFloor, m_nodes[id].floor);
                }
            if (minFloor != maxFloor)
            {
                toSchedule.clear();
                for (int id : overloaded)
                    if (id >= 0 && id < static_cast<int>(m_nodes.size()) &&
                        m_nodes[id].floor == maxFloor)
                        toSchedule.push_back(id);
            }
        }
        for (int id : toSchedule)
        {
            if (id < 0 || id >= static_cast<int>(m_nodes.size())) continue;
            scheduleFail(id, NodeStatus::Overloaded);
        }
        // Shear failures are scheduled on their own floor; a plate never
        // overloads a storey below it, so no top-down filter is needed.
        for (size_t i = 0; i < shearOverloaded.size(); ++i)
            scheduleFail(shearOverloaded[i], NodeStatus::Overloaded, "shear",
                         0.0f, shearValues[i] * 9.81f, 0.0f);
        for (EdgeState& edge : m_edges)
        {
            const bool horizontal = edge.from > 0 && edge.to > 0 &&
                horizontalContact(m_nodes[edge.from].box, m_nodes[edge.to].box, kContactTol);
            if (horizontal)
                edge.alive = edge.alive && m_nodes[edge.to].alive;
            else
                edge.alive = edge.alive && m_nodes[edge.from].alive && m_nodes[edge.to].alive;
        }
    }
    addEvent("Tick " + std::to_string(m_analysisTick) + ": passes=" + std::to_string(guard) +
             " remaining=" + std::to_string(m_dirtyNodes.size()) +
             " pending=" + std::to_string(m_pending.size()));
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
