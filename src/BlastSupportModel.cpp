#include "BlastSupportModel.h"
#include "BlastRuntime.h"
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
    case StructuralPreset::Floors3Columns4: floors = 3; columns = 4; blocks = 4; break;
    case StructuralPreset::Floors2Columns4: floors = 2; columns = 4; blocks = 4; break;
    case StructuralPreset::Floors5Columns2: floors = 5; columns = 2; blocks = 2; break;
    case StructuralPreset::House3Floors: floors = 3; columns = HouseWallsPerFloor; blocks = HouseSlabsPerFloor; break;
    case StructuralPreset::Grid4x4Floors4: floors = 4; columns = 4; blocks = 16; walls = 8; break;
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

    m_nodes.push_back({0, "Ground", NodeType::Ground, -1, 0, 100, 0, 0, 100000, true, true, NodeStatus::Safe});
    // IDs are also vector indices: Ground, then every block, then every
    // column/wall. Keep this order aligned with blockId()/columnId().
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeBlocks; ++slot)
        {
            const std::string slabName = isHouse()
                ? "F" + std::to_string(floor + 1) + "-S" + std::to_string(slot + 1)
                : "F" + std::to_string(floor + 1) + "-B" + std::to_string(slot + 1);
            m_nodes.push_back({blockId(floor, slot), slabName,
                               NodeType::Slab, floor, slot, 100, 25, 0, 290, true, true, NodeStatus::Safe});
        }
    }
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeColumns; ++slot)
        {
            const float capacity = isGrid() ? gridCapacityFor(floor)
                                            : (floor == 0 ? m_config.lowerColumnCapacity : m_config.upperColumnCapacity);
            if (isHouse())
            {
                m_nodes.push_back({columnId(floor, slot), "F" + std::to_string(floor + 1) + "-W" + std::to_string(slot + 1),
                                   NodeType::Wall, floor, slot, 100, floor == 0 ? 50.0f : 25.0f, 0, capacity, true, true, NodeStatus::Safe});
            }
            else
            {
                m_nodes.push_back({columnId(floor, slot), "F" + std::to_string(floor + 1) + "-C" + std::to_string(slot + 1),
                                   NodeType::Column, floor, slot, 100, floor == 0 ? 50.0f : 25.0f, 0, capacity, true, true, NodeStatus::Safe});
            }
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
            m_nodes.push_back({wallId(floor, slot), wallName,
                               NodeType::Wall, floor, slot, 100, floor == 0 ? 50.0f : 25.0f, 0, capacity, true, true, NodeStatus::Safe});
        }
    }
    for (size_t index = 0; index < m_nodes.size(); ++index)
    {
        if (m_nodes[index].id != static_cast<int>(index))
            addEvent("ERROR: node ID/index mismatch at " + std::to_string(index));
    }
    rebuildEdges();
    std::string blastError;
    if (!m_blastRuntime->initialize(m_nodes, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, blastError))
        addEvent(blastError);
    m_loadPathSolver->route(m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls);
    if (isHouse())
        addEvent("Reset: " + std::to_string(m_activeFloors) + " floors, " +
                 std::to_string(m_activeFloors * m_activeColumns) + " walls (house), " +
                 std::to_string(m_activeColumns) + " load paths per floor.");
    else if (isGrid())
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
    if (node.type == NodeType::Ground || !node.alive)
        return false;

    saveSnapshot();
    m_hasDamage = true;
    node.health = std::max(0.0f, node.health - amount);
    addEvent(node.name + " damage -> " + std::to_string(static_cast<int>(node.health)) + " HP");
    if (node.health == 0.0f)
    {
        if (node.type == NodeType::Slab)
        {
            node.releasedLoad = node.mass;
            if (node.floor + 1 < m_activeFloors && node.slot < m_activeColumns)
            {
                const int upperColumn = columnId(node.floor + 1, node.slot);
                if (upperColumn < static_cast<int>(m_nodes.size()) && m_nodes[upperColumn].alive)
                    node.releasedLoad += m_nodes[upperColumn].load;
            }
        }
        else if (node.type == NodeType::Column || node.type == NodeType::Wall)
        {
            node.releasedLoad = std::max(node.mass, node.load);
            node.releasedLoad += node.mass;
        }
        node.alive = false;
        addEvent(node.name + " failed; load path removed.");
        std::vector<FragmentSpawnInfo> fragments;
        m_blastRuntime->fractureMember(node, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
        m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
        for (EdgeState& edge : m_edges)
        {
            const bool incident = edge.from == nodeId || edge.to == nodeId;
            const bool horizontal = edge.from > 0 && edge.to > 0 &&
                m_nodes[edge.from].type == NodeType::Slab && m_nodes[edge.to].type == NodeType::Slab;
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
    if (!m_blastRuntime->initialize(m_nodes, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, blastError))
        addEvent(blastError);
    addEvent("Undo: restored previous structural state.");
    return true;
}


void BlastSupportModel::rebuildEdges()
{
    m_edges.clear();
    if (isHouse())
    {
        rebuildHouseEdges();
        return;
    }
    if (isGrid())
    {
        rebuildGridEdges();
        return;
    }
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeColumns; ++slot)
        {
            const int block = blockId(floor, slot);
            const int column = columnId(floor, slot);
            m_edges.push_back({block, column, 160.0f, 0, m_nodes[column].alive});
            if (floor == 0)
                m_edges.push_back({column, 0, 300.0f, 0, m_nodes[column].alive});
            else
            {
                // Columns do not connect directly to columns across floors.
                // Their support is transferred through the lower floor block,
                // then through that block's neighboring block connections.
                m_edges.push_back({column, blockId(floor - 1, slot), 160.0f, 0,
                                   m_nodes[column].alive && m_nodes[blockId(floor - 1, slot)].alive});
            }
            if (slot > 0)
            {
                const int left = blockId(floor, slot - 1);
                // Horizontal bonds are bidirectional: a block can route its
                // released load to either neighbor, and a support path can
                // detour through neighboring blocks in both directions.
                m_edges.push_back({left, block, 100.0f, 0, m_nodes[block].alive});
                m_edges.push_back({block, left, 100.0f, 0, m_nodes[left].alive});
            }
        }
    }
}

void BlastSupportModel::rebuildHouseEdges()
{
    constexpr float kContactTol = 0.1f;
    const int total = static_cast<int>(m_nodes.size());

    // Resolve every node's 3D box from its geometric layout. Connectivity is
    // derived purely from these boxes, so a new house arrangement only needs
    // a matching nodeLayout() (no per-slot edge table to keep in sync).
    std::vector<BoxLayout> layouts(total);
    for (int i = 0; i < total; ++i)
        layouts[i] = nodeLayout(m_nodes[i], m_activeColumns, m_activeBlocks, true);

    const auto overlap = [](float a0, float a1, float b0, float b1)
    {
        return std::min(a1, b1) - std::max(a0, b0);
    };
    const auto xOverlap = [&overlap](const BoxLayout& a, const BoxLayout& b)
    {
        return overlap(a.cx - a.hx, a.cx + a.hx, b.cx - b.hx, b.cx + b.hx);
    };
    const auto zOverlap = [&overlap](const BoxLayout& a, const BoxLayout& b)
    {
        return overlap(a.cz - a.hz, a.cz + a.hz, b.cz - b.hz, b.cz + b.hz);
    };
    const auto xzContact = [&](const BoxLayout& a, const BoxLayout& b)
    {
        return xOverlap(a, b) > -kContactTol && zOverlap(a, b) > -kContactTol;
    };
    const auto minY = [](const BoxLayout& l) { return l.cy - l.hy; };
    const auto maxY = [](const BoxLayout& l) { return l.cy + l.hy; };
    const auto addEdge = [this](int from, int to, float capacity, bool alive)
    {
        m_edges.push_back({from, to, capacity, 0.0f, alive});
    };

    for (int i = 1; i < total; ++i)
    {
        const NodeState& nodeI = m_nodes[i];
        if (nodeI.type != NodeType::Wall && nodeI.type != NodeType::Slab) continue;
        const BoxLayout& boxI = layouts[i];

        // Ground-story walls stand on the ground plane (y == 0).
        if (nodeI.type == NodeType::Wall && nodeI.floor == 0 && minY(boxI) <= kContactTol)
            addEdge(i, 0, 300.0f, m_nodes[i].alive);

        for (int j = i + 1; j < total; ++j)
        {
            const NodeState& nodeJ = m_nodes[j];
            if (nodeJ.type != NodeType::Wall && nodeJ.type != NodeType::Slab) continue;
            const BoxLayout& boxJ = layouts[j];

            if (nodeI.type == NodeType::Wall && nodeJ.type == NodeType::Wall)
            {
                if (!xzContact(boxI, boxJ)) continue;
                if (std::fabs(boxI.cy - boxJ.cy) < 0.5f)
                {
                    // Same-story wall segments that meet in the XZ plane
                    // (adjacent segment or a corner) are bound both ways, so a
                    // wall can route its load around to a live neighbor.
                    const bool bothAlive = m_nodes[i].alive && m_nodes[j].alive;
                    addEdge(i, j, 160.0f, bothAlive);
                    addEdge(j, i, 160.0f, bothAlive);
                }
                else if (std::fabs(boxI.cy - boxJ.cy) < kFloorHeight + 0.5f &&
                         std::fabs(boxI.cx - boxJ.cx) < kContactTol &&
                         std::fabs(boxI.cz - boxJ.cz) < kContactTol)
                {
                    // Upper-story wall stacks on the wall below it in the same
                    // lane: its bottom face lands on the lower wall's top face.
                    const bool upperIsI = maxY(boxI) > maxY(boxJ);
                    const int upper = upperIsI ? i : j;
                    const int lower = upperIsI ? j : i;
                    addEdge(upper, lower, 160.0f, m_nodes[upper].alive && m_nodes[lower].alive);
                }
            }
            else if ((nodeI.type == NodeType::Wall && nodeJ.type == NodeType::Slab) ||
                     (nodeI.type == NodeType::Slab && nodeJ.type == NodeType::Wall))
            {
                const bool iIsWall = nodeI.type == NodeType::Wall;
                const int wallId = iIsWall ? i : j;
                const int slabId = iIsWall ? j : i;
                const BoxLayout& wallBox = iIsWall ? boxI : boxJ;
                const BoxLayout& slabBox = iIsWall ? boxJ : boxI;
                if (!xzContact(wallBox, slabBox)) continue;
                // The floor plate rests on the top face of its own story's
                // walls: the slab bottom reaches the wall top (slab sits
                // inside the wall enclosure).
                if (maxY(slabBox) >= maxY(wallBox) - kContactTol &&
                    minY(slabBox) <= maxY(wallBox) + kContactTol)
                    addEdge(slabId, wallId, 160.0f, m_nodes[wallId].alive);
            }
            else if (nodeI.type == NodeType::Slab && nodeJ.type == NodeType::Slab)
            {
                if (std::fabs(boxI.cy - boxJ.cy) >= 0.5f) continue;
                // Neighboring slabs share a face; two slabs that only meet at
                // the single center point of the tile grid are not bonded.
                if (!xzContact(boxI, boxJ)) continue;
                if (xOverlap(boxI, boxJ) <= 0.001f && zOverlap(boxI, boxJ) <= 0.001f) continue;
                addEdge(i, j, 100.0f, m_nodes[i].alive);
                addEdge(j, i, 100.0f, m_nodes[j].alive);
            }
        }
    }
}

void BlastSupportModel::rebuildGridEdges()
{
    // Grid floor connectivity is derived from physical adjacency only:
    //
    // * A corner column bonds to the corner block at its corner plus the two
    //   edge blocks immediately beside it (the 2x2 corner quadrant minus the
    //   diagonal block). Interior blocks never bond to a column directly; they
    //   reach a column only through the block-block bonds and the edge walls.
    // * An upper column rests on the physically adjacent corner blocks of the
    //   floor below, which route back to the lower column, so every column has
    //   a Ground path: column -> lower corner blocks -> lower column -> Ground.
    //   A single column failure therefore does not break the lane above it.
    // * Ground-story columns stand on Ground.
    // * Neighbouring blocks are bonded both ways so a block can route its load
    //   (and its support path) around a dead bond through the live ones.
    for (int floor = 0; floor < m_activeFloors; ++floor)
    {
        for (int slot = 0; slot < m_activeColumns; ++slot)
        {
            const int column = columnId(floor, slot);
            if (floor == 0)
                m_edges.push_back({column, 0, 300.0f, 0, m_nodes[column].alive});
            else
            {
                // An upper column stands on the corner blocks of the floor
                // plate below it. It is not directly supported by the
                // same-lane lower column: its load reaches the lower plate,
                // which redistributes through the lower corner blocks back to
                // the lower column, so a single column failure does not break
                // the lane above it.
                for (int b = 0; b < m_activeBlocks; ++b)
                {
                    if (!gridBlockAdjacentToColumn(gridCol(b), gridRow(b), slot)) continue;
                    const int lowerBlock = blockId(floor - 1, b);
                    m_edges.push_back({column, lowerBlock, 160.0f, 0,
                                       m_nodes[column].alive && m_nodes[lowerBlock].alive});
                }
            }
        }
        // Block-to-column: only the edge blocks physically adjacent to a
        // corner column bond to it (their 2x2 corner quadrant minus the
        // diagonal block). Interior blocks carry no direct column bond and
        // must transmit their load through the block-block bonds to an edge
        // block or to a north/west block that bonds to a wall.
        for (int slot = 0; slot < m_activeBlocks; ++slot)
        {
            const int block = blockId(floor, slot);
            const int col = gridCol(slot);
            const int row = gridRow(slot);
            for (int c = 0; c < m_activeColumns; ++c)
            {
                if (!gridBlockAdjacentToColumn(col, row, c)) continue;
                const int column = columnId(floor, c);
                m_edges.push_back({block, column, 160.0f, 0, m_nodes[column].alive});
            }
        }
        for (int slot = 0; slot < m_activeBlocks; ++slot)
        {
            const int block = blockId(floor, slot);
            const int col = gridCol(slot);
            const int row = gridRow(slot);
            if (col > 0)
            {
                const int left = blockId(floor, slot - 1);
                m_edges.push_back({left, block, 100.0f, 0, m_nodes[block].alive});
                m_edges.push_back({block, left, 100.0f, 0, m_nodes[left].alive});
            }
            if (row > 0)
            {
                const int back = blockId(floor, slot - kGridSize);
                m_edges.push_back({back, block, 100.0f, 0, m_nodes[block].alive});
                m_edges.push_back({block, back, 100.0f, 0, m_nodes[back].alive});
            }
        }
        // Load-bearing walls rise through the story like a column: the
        // ground-story wall stands on Ground and an upper wall stacks on the
        // wall below it (vertical chain). The wall also bonds to the floor
        // plate below it so a destroyed lower wall does not take the wall
        // above it down. Each floor has 8 wall segments: slot 0..3 split the
        // north wall into four pieces (one per grid column, along the north
        // edge) and slot 4..7 split the west wall into four pieces (one per
        // grid row, along the west edge).
        for (int slot = 0; slot < m_activeWalls; ++slot)
        {
            const int wall = wallId(floor, slot);
            if (floor == 0)
                m_edges.push_back({wall, 0, 300.0f, 0, m_nodes[wall].alive});
            else
            {
                const int lowerWall = wallId(floor - 1, slot);
                m_edges.push_back({wall, lowerWall, 160.0f, 0,
                                   m_nodes[wall].alive && m_nodes[lowerWall].alive});
                // Directly below the wall sits the plate band it follows: a
                // north segment at grid column c rests on the north-row block
                // (c,0); a west segment at grid row r rests on the west-column
                // block (0,r).
                const bool north = slot < 4;
                const int segment = north ? slot : slot - 4;
                for (int b = 0; b < m_activeBlocks; ++b)
                {
                    const bool beneath = north ? (gridCol(b) == segment && gridRow(b) == 0)
                                               : (gridCol(b) == 0 && gridRow(b) == segment);
                    if (!beneath) continue;
                    const int lowerBlock = blockId(floor - 1, b);
                    m_edges.push_back({wall, lowerBlock, 160.0f, 0,
                                       m_nodes[wall].alive && m_nodes[lowerBlock].alive});
                }
            }
            // Same-story wall bonds: adjacent north segments connect both ways,
            // adjacent west segments connect both ways, and the north wall's
            // west segment meets the west wall's north segment at the
            // north-west corner of the slab.
            const bool north = slot < 4;
            if (north)
            {
                if (slot < 3)
                {
                    const int next = wallId(floor, slot + 1);
                    const bool bothAlive = m_nodes[wall].alive && m_nodes[next].alive;
                    m_edges.push_back({wall, next, 160.0f, 0, bothAlive});
                    m_edges.push_back({next, wall, 160.0f, 0, bothAlive});
                }
                if (slot == 0)
                {
                    const int corner = wallId(floor, 4);
                    const bool bothAlive = m_nodes[wall].alive && m_nodes[corner].alive;
                    m_edges.push_back({wall, corner, 160.0f, 0, bothAlive});
                    m_edges.push_back({corner, wall, 160.0f, 0, bothAlive});
                }
            }
            else if (slot < 7)
            {
                const int next = wallId(floor, slot + 1);
                const bool bothAlive = m_nodes[wall].alive && m_nodes[next].alive;
                m_edges.push_back({wall, next, 160.0f, 0, bothAlive});
                m_edges.push_back({next, wall, 160.0f, 0, bothAlive});
            }
        }
        // Blocks on the north row can detour their load through the north wall
        // segment above their column, and blocks on the west column through the
        // west wall segment beside their row.
        for (int slot = 0; slot < m_activeBlocks; ++slot)
        {
            const int block = blockId(floor, slot);
            const int col = gridCol(slot);
            const int row = gridRow(slot);
            if (row == 0)
            {
                const int wall = wallId(floor, col);
                m_edges.push_back({block, wall, 100.0f, 0, m_nodes[wall].alive});
            }
            if (col == 0)
            {
                const int wall = wallId(floor, 4 + row);
                m_edges.push_back({block, wall, 100.0f, 0, m_nodes[wall].alive});
            }
        }
    }
}

float BlastSupportModel::gridCapacityFor(int floor) const
{
    // Whole-floor load sharing accumulates downward: each floor contributes its
    // live block, column and wall masses, so a lower member carries the
    // stories above it. With 12 bearers per floor (4 columns + 8 walls),
    // losing a single member drops the shared load by less than one member
    // mass, while losing two raises every survivor's share above the capacity.
    // The factor sits between per-bearer shares of 11 and 10 survivors so ten
    // or more live members hold the floor and losing two overloads it.
    float total = 0.0f;
    for (int f = m_activeFloors - 1; f >= floor; --f)
    {
        const float memberMass = f == 0 ? 50.0f : 25.0f;
        total += static_cast<float>(m_activeBlocks) * 25.0f +
                 static_cast<float>(m_activeColumns) * memberMass +
                 static_cast<float>(m_activeWalls) * memberMass;
    }
    return total * 0.0915f;
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

void BlastSupportModel::scheduleFail(int nodeId, NodeStatus status)
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
    m_pending.push_back(pending);
    addEvent("Scheduled " + m_nodes[nodeId].name +
             (status == NodeStatus::Overloaded ? " overload" : " unsupported") +
             " in " + std::to_string(m_cascadeDelay) + " s");
}

void BlastSupportModel::executePendingFail(int nodeId, NodeStatus status)
{
    NodeState& node = m_nodes[nodeId];
    if (node.type == NodeType::Slab) node.releasedLoad = node.mass;
    node.health = 0.0f;
    node.alive = false;
    node.supported = false;
    node.status = status;
    addEvent(node.name + " failed (" +
             (status == NodeStatus::Overloaded ? "overloaded" : "unsupported") +
             "); load path removed.");
    std::vector<FragmentSpawnInfo> fragments;
    m_blastRuntime->fractureMember(node, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, fragments);
    m_pendingFragments.insert(m_pendingFragments.end(), fragments.begin(), fragments.end());
    for (EdgeState& edge : m_edges)
    {
        const bool incident = edge.from == nodeId || edge.to == nodeId;
        if (!incident) continue;
        const bool horizontal = edge.from > 0 && edge.to > 0 &&
            m_nodes[edge.from].type == NodeType::Slab && m_nodes[edge.to].type == NodeType::Slab;
        if (status == NodeStatus::Overloaded || !horizontal) edge.alive = false;
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
                    executePendingFail(it->nodeId, it->status);
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
            if (node.type != NodeType::Ground && node.alive &&
                !m_graphSolver->hasGroundPath(id, m_nodes, m_edges))
            {
                if (node.type == NodeType::Slab) node.releasedLoad = node.mass;
                scheduleFail(id, NodeStatus::Unsupported);
            }
        }
        const std::vector<int> overloaded = m_loadPathSolver->route(
            m_nodes, m_edges, m_activeFloors, m_activeColumns, m_activeBlocks, m_activeWalls, false);
        for (int id : overloaded) scheduleFail(id, NodeStatus::Overloaded);
        for (EdgeState& edge : m_edges)
        {
            const bool horizontal = edge.from > 0 && edge.to > 0 &&
                m_nodes[edge.from].type == NodeType::Slab && m_nodes[edge.to].type == NodeType::Slab;
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
