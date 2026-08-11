#include "BlastSupportModel.h"
#include "SupportGraphSolver.h"

#include <limits>
#include <iostream>
#include <vector>

using blast_demo::BlastSupportModel;
using blast_demo::EdgeState;
using blast_demo::NodeState;
using blast_demo::NodeType;
using blast_demo::SupportGraphSolver;
using blast_demo::StructuralPreset;

namespace
{
int columnId(const BlastSupportModel& model, int floor, int slot)
{
    return 1 + model.activeFloors() * model.activeBlocks() +
        floor * model.activeColumns() + slot;
}

int blockId(const BlastSupportModel& model, int floor, int slot)
{
    return 1 + floor * model.activeBlocks() + slot;
}

int wallId(const BlastSupportModel& model, int floor, int slot)
{
    return 1 + model.activeFloors() * model.activeBlocks() +
        model.activeFloors() * model.activeColumns() +
        floor * model.activeWalls() + slot;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << "\n";
    return condition;
}
}

int main()
{
    {
        std::vector<NodeState> nodes(2);
        nodes[0] = {0, "Ground", NodeType::Ground, -1, 0, 100, 0, 0, 100000, true, true, blast_demo::NodeStatus::Safe};
        nodes[1] = {1, "Column", NodeType::Column, 0, 0, 100, 1, 0, 10, true, true, blast_demo::NodeStatus::Safe};
        SupportGraphSolver solver;
        if (!check(!solver.hasGroundPath(1, nodes, {{0, 1, 1, 0, true}}),
                   "Ground path traversal incorrectly walked an edge backwards")) return 1;
        if (!check(solver.hasGroundPath(1, nodes, {{1, 0, 1, 0, true}}),
                   "directed column Ground path was not found")) return 1;
    }
    {
        BlastSupportModel model;
        for (const blast_demo::NodeState& node : model.nodes())
            if (node.type != blast_demo::NodeType::Ground && !node.alive)
            {
                std::cerr << "FAIL: default model removed " << node.name << " during reset\n";
                return 1;
            }
    }
    {
        BlastSupportModel model;
        if (!check(model.damageNode(blockId(model, 0, 0), 35.0f), "valid block damage was not applied") ||
            !check(model.nodes()[blockId(model, 0, 0)].health == 65.0f, "block HP changed incorrectly")) return 1;
        if (!check(!model.damageNode(0, 1.0f), "Ground accepted damage") ||
            !check(!model.damageNode(blockId(model, 0, 0), std::numeric_limits<float>::quiet_NaN()), "invalid damage accepted")) return 1;
    }
    {
        BlastSupportModel model;
        const float b1 = model.nodes()[blockId(model, 3, 0)].load;
        const float b2 = model.nodes()[blockId(model, 3, 1)].load;
        const float b3 = model.nodes()[blockId(model, 3, 2)].load;
        const float b4 = model.nodes()[blockId(model, 3, 3)].load;
        if (!check(b1 > 0.0f && std::fabs(b2 - b1) < 0.001f &&
                   std::fabs(b3 - b1) < 0.001f && std::fabs(b4 - b1) < 0.001f,
                   "symmetric initial block loads are not equal"))
        {
            std::cerr << "b1=" << b1 << " b2=" << b2 << " b3=" << b3 << " b4=" << b4 << "\n";
            return 1;
        }
        if (!check(model.nodes()[columnId(model, 3, 0)].load == b1 &&
                   model.nodes()[columnId(model, 3, 3)].load == b4,
                   "column loads do not match their block chain loads")) return 1;
        const float c1Mass = model.nodes()[columnId(model, 3, 0)].mass;
        const float c2LoadBefore = model.nodes()[columnId(model, 3, 1)].load;
        model.damageNode(blockId(model, 3, 0), 100.0f); // F4-B1, not F4-C1
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const blast_demo::NodeState& c1 = model.nodes()[columnId(model, 3, 0)];
        const float c2LoadAfter = model.nodes()[columnId(model, 3, 1)].load;
        if (!check(!c1.alive || c1.load <= c1Mass + 0.001f,
                   "F4-C1 retained released load after F4-B1 failure")) return 1;
        if (!check(c2LoadAfter > c2LoadBefore,
                   "nearest live column did not receive F4-B1 released load"))
        {
            std::cerr << "c2 before=" << c2LoadBefore << " after=" << c2LoadAfter << "\n";
            return 1;
        }
    }
    {
        // Destroying one column on a floor re-shares the section load among
        // the surviving columns: they increase equally and all remain alive.
        BlastSupportModel model;
        const float before = model.nodes()[columnId(model, 3, 1)].load;
        model.damageColumn(3, 0, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const float c2 = model.nodes()[columnId(model, 3, 1)].load;
        const float c3 = model.nodes()[columnId(model, 3, 2)].load;
        const float c4 = model.nodes()[columnId(model, 3, 3)].load;
        if (!check(c2 > before && c3 > before && c4 > before,
                   "surviving columns did not receive the destroyed column's load")) return 1;
        if (!check(std::fabs(c2 - c3) < 0.001f && std::fabs(c3 - c4) < 0.001f,
                   "surviving columns are not sharing the load equally")) return 1;
        if (!check(model.nodes()[columnId(model, 3, 1)].alive &&
                   model.nodes()[columnId(model, 3, 2)].alive &&
                   model.nodes()[columnId(model, 3, 3)].alive,
                   "surviving columns collapsed after a single column failure")) return 1;
    }
    {
        BlastSupportModel model;
        if (!check(model.activeFloors() == 5 && model.activeColumns() == 4,
                   "default preset dimensions changed") ||
            !check(model.nodes().size() == 1u + 2u * static_cast<size_t>(model.activeFloors()) *
                       static_cast<size_t>(model.activeColumns()),
                   "default preset node count changed")) return 1;
        model.setPreset(StructuralPreset::Floors3Columns4);
        if (!check(model.activeFloors() == 3 && model.activeColumns() == 4,
                   "3x4 preset dimensions were not applied") ||
            !check(model.nodes().size() == 1u + 2u * static_cast<size_t>(model.activeFloors()) *
                       static_cast<size_t>(model.activeColumns()),
                   "3x4 preset node count is incorrect")) return 1;
        model.setPreset(StructuralPreset::Floors5Columns4);
        if (!check(model.activeFloors() == 5 && model.activeColumns() == 4 &&
                   model.nodes().size() == 1u + 2u * static_cast<size_t>(model.activeFloors()) *
                       static_cast<size_t>(model.activeColumns()),
                   "switching back to 5x4 failed")) return 1;
    }
    {
        BlastSupportModel model;
        for (const auto& edge : model.edges())
        {
            const auto& from = model.nodes()[edge.from];
            const auto& to = model.nodes()[edge.to];
            if (from.type == NodeType::Column && to.type == NodeType::Column && from.floor > 0)
                return check(false, "columns connect directly across floors") ? 0 : 1;
            if (from.type == NodeType::Column && from.floor > 0 &&
                !(to.type == NodeType::Slab && to.floor == from.floor - 1))
                continue;
            if (from.type == NodeType::Column && from.floor > 0 &&
                !check(to.type == NodeType::Slab && to.floor == from.floor - 1,
                       "upper column does not point to the lower block")) return 1;
        }
    }
    {
        // Vertical accumulation: lower columns must carry strictly more load
        // than the columns above them in the same lane.
        BlastSupportModel model;
        bool strictlyIncreasing = true;
        for (int slot = 0; slot < model.activeColumns(); ++slot)
        {
            for (int floor = 1; floor < model.activeFloors(); ++floor)
            {
                if (!(model.nodes()[columnId(model, floor, slot)].load <
                      model.nodes()[columnId(model, floor - 1, slot)].load))
                    strictlyIncreasing = false;
            }
        }
        if (!check(strictlyIncreasing, "lower column load is not greater than upper column load"))
        {
            for (int slot = 0; slot < model.activeColumns(); ++slot)
                for (int floor = 0; floor < model.activeFloors(); ++floor)
                    std::cerr << "slot=" << slot << " floor=" << floor
                              << " load=" << model.nodes()[columnId(model, floor, slot)].load << "\n";
            return 1;
        }
    }
    {
        // Vertical priority: with all columns alive, no horizontal block
        // bond carries load, and every block/column has symmetric load.
        BlastSupportModel model;
        bool horizontalEdgeHasLoad = false;
        for (const EdgeState& edge : model.edges())
        {
            const NodeState& from = model.nodes()[edge.from];
            const NodeState& to = model.nodes()[edge.to];
            if (from.type == NodeType::Slab && to.type == NodeType::Slab && edge.load > 0.001f)
                horizontalEdgeHasLoad = true;
        }
        if (!check(!horizontalEdgeHasLoad, "horizontal edges carried load while vertical paths were alive")) return 1;
    }
    {
        // Column failure routes its block load horizontally to the next block.
        // F4-C1 destroyed -> F4-B1 load transfers to F4-B2.
        BlastSupportModel model;
        const float b1Mass = model.nodes()[blockId(model, 3, 0)].mass;
        const float b2Mass = model.nodes()[blockId(model, 3, 1)].mass;
        model.damageNode(columnId(model, 3, 0), 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const float b1After = model.nodes()[blockId(model, 3, 0)].load;
        const float b2After = model.nodes()[blockId(model, 3, 1)].load;
        if (!check(!model.nodes()[columnId(model, 3, 0)].alive, "F4-C1 was not destroyed")) return 1;
        if (!check(b2After > b2Mass, "horizontal transfer to F4-B2 did not add F4-B1 load")) return 1;
        if (!check(b1After <= b1Mass + 0.001f, "F4-B1 load did not leave after its column died"))
        {
            std::cerr << "b1After=" << b1After << " b1Mass=" << b1Mass << "\n";
            return 1;
        }
    }
    {
        // Released upper load transfers to the nearest surviving column:
        // F4-B1 destroyed -> F4-C2 (nearest live lane) receives it.
        BlastSupportModel model;
        const float c2Before = model.nodes()[columnId(model, 3, 1)].load;
        model.damageNode(blockId(model, 3, 0), 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const float c2After = model.nodes()[columnId(model, 3, 1)].load;
        if (!check(c2After > c2Before, "released F4-B1 load did not reach the nearest live column")) return 1;
    }
    {
        // Regression: after F4-C1/C2/C3 are destroyed, F4-C4 carries the
        // whole section, overloads, and collapses; the upper F5 layer must
        // then lose its Ground path and collapse too. Nothing may survive
        // above a collapsed support section. delay=0 forces the scheduled
        // cascade to execute immediately once update() is called.
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        model.damageColumn(3, 0, 100.0f);
        model.damageColumn(3, 1, 100.0f);
        model.damageColumn(3, 2, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[columnId(model, 3, 3)].alive,
                   "overloaded F4-C4 should collapse when it is the only support")) return 1;
        bool upperSurvived = false;
        for (const blast_demo::NodeState& node : model.nodes())
            if (node.type != blast_demo::NodeType::Ground && node.floor >= 3 && node.alive)
                upperSurvived = true;
        if (!check(!upperSurvived,
                   "F4/F5 structures survived after F4 support section collapsed")) return 1;
    }
    {
        // Regression: destroying F3-C4 must not remove the block above it.
        // F3-B4 still reaches Ground through the bidirectional horizontal
        // bond to F3-B3 -> F3-C3, and F4-B4 above stays supported too.
        BlastSupportModel model;
        model.damageColumn(2, 3, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        if (!check(!model.nodes()[columnId(model, 2, 3)].alive, "F3-C4 was not destroyed")) return 1;
        if (!check(model.nodes()[blockId(model, 2, 3)].alive,
                   "F3-B4 above the destroyed column lost its support path")) return 1;
        if (!check(model.nodes()[blockId(model, 3, 3)].alive,
                   "F4-B4 above the destroyed column lost its support path")) return 1;
        const float c1 = model.nodes()[columnId(model, 2, 0)].load;
        const float c2 = model.nodes()[columnId(model, 2, 1)].load;
        const float c3 = model.nodes()[columnId(model, 2, 2)].load;
        if (!check(std::fabs(c1 - c2) < 0.001f && std::fabs(c2 - c3) < 0.001f && c1 > 150.0f,
                   "surviving F3 columns did not share the released load equally")) return 1;
    }
    {
        // House preset: 3 stories, each with 8 wall segments (column slots) +
        // 4 slab tiles (block slots). All members survive an untouched reset.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::House3Floors);
        if (!check(model.activeFloors() == 3 && model.activeColumns() == 8 && model.activeBlocks() == 4,
                   "house preset dimensions were not applied") ||
            !check(model.nodes().size() == 1u + 3u * (8u + 4u),
                   "house preset node count is incorrect"))
            return 1;
        bool wallType = model.nodes()[columnId(model, 0, 0)].type == NodeType::Wall &&
                        model.nodes()[columnId(model, 2, 3)].type == NodeType::Wall;
        bool slabType = model.nodes()[blockId(model, 0, 0)].type == NodeType::Slab;
        if (!check(wallType, "house preset did not create Wall nodes") ||
            !check(slabType, "house preset did not create Slab nodes"))
            return 1;
        bool allAlive = true;
        for (const blast_demo::NodeState& node : model.nodes())
            if (node.type != blast_demo::NodeType::Ground && !node.alive)
                allAlive = false;
        if (!check(allAlive, "house reset left a member damaged"))
            return 1;
        const float topLoad = model.nodes()[columnId(model, 2, 0)].load;
        const float bottomLoad = model.nodes()[columnId(model, 0, 0)].load;
        if (!check(bottomLoad > topLoad,
                   "house lower wall load is not greater than upper wall load")) return 1;
    }
    {
        // Destroy one wall of the top story: the surviving walls absorb its
        // share and carry the section load equally.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::House3Floors);
        const float before = model.nodes()[columnId(model, 2, 1)].load;
        model.damageNode(columnId(model, 2, 0), 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const float w1 = model.nodes()[columnId(model, 2, 1)].load;
        const float w2 = model.nodes()[columnId(model, 2, 2)].load;
        const float w3 = model.nodes()[columnId(model, 2, 3)].load;
        if (!check(!model.nodes()[columnId(model, 2, 0)].alive, "top wall was not destroyed")) return 1;
        if (!check(w1 > before && w2 > before && w3 > before,
                   "surviving house walls did not receive the destroyed wall's load"))
        {
            std::cerr << "before=" << before << " after=" << w1 << "," << w2 << "," << w3 << "\n";
            return 1;
        }
        if (!check(std::fabs(w1 - w2) < 0.001f && std::fabs(w2 - w3) < 0.001f,
                   "surviving house walls are not sharing the load equally"))
            return 1;
        if (!check(model.nodes()[columnId(model, 2, 1)].alive &&
                   model.nodes()[columnId(model, 2, 2)].alive &&
                   model.nodes()[columnId(model, 2, 3)].alive,
                   "surviving walls collapsed after a single wall failure"))
            return 1;
    }
    {
        // Destroy all four walls of the middle story: the middle slab and the
        // whole upper story lose their Ground path and collapse, while the
        // ground story stays standing.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::House3Floors);
        model.setCascadeDelay(0.0f);
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            model.damageNode(columnId(model, 1, slot), 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        for (int slot = 0; slot < model.activeColumns(); ++slot)
        {
            if (!check(!model.nodes()[columnId(model, 1, slot)].alive,
                       "middle story wall survived destruction")) return 1;
            if (!check(!model.nodes()[columnId(model, 2, slot)].alive,
                       "upper story wall retained a Ground path")) return 1;
        }
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
        {
            if (!check(!model.nodes()[blockId(model, 1, slot)].alive,
                       "middle story slab kept its support without walls")) return 1;
            if (!check(!model.nodes()[blockId(model, 2, slot)].alive,
                       "upper story slab retained a Ground path")) return 1;
        }
        bool groundAlive = model.nodes()[columnId(model, 0, 0)].alive &&
                           model.nodes()[blockId(model, 0, 0)].alive;
        if (!check(groundAlive, "ground story collapsed after losing upper walls")) return 1;
    }
    {
        // House connections are derived from geometry (nodeLayout boxes), not
        // hardcoded slot pairs: same-story walls bind horizontally (both
        // adjacent segments and corners), slabs rest on their story's walls,
        // and an upper wall stacks on the wall below it in the same lane.
        // Destroying the ground-story slot-0 wall must NOT take out the wall
        // above it: F2-W1 stays supported through the same-story wall bonds
        // and the slab tiles that sit on the surviving perimeter walls.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::House3Floors);

        const auto hasEdge = [&model](int from, int to)
        {
            for (const EdgeState& edge : model.edges())
                if (edge.from == from && edge.to == to) return true;
            return false;
        };

        const int w10 = columnId(model, 0, 0);
        const int w11 = columnId(model, 0, 1);
        const int w16 = columnId(model, 0, 6);
        const int w20 = columnId(model, 1, 0);
        const int s10 = blockId(model, 0, 0);
        const int s11 = blockId(model, 0, 1);
        const int s20 = blockId(model, 1, 0);

        if (!check(hasEdge(w10, w11) && hasEdge(w11, w10),
                   "no wall-wall horizontal bond")) return 1;
        if (!check(hasEdge(w10, w16) && hasEdge(w16, w10),
                   "no wall-wall corner bond")) return 1;
        if (!check(hasEdge(w20, w10),
                   "no vertical wall stacking edge")) return 1;
        if (!check(hasEdge(s10, w10),
                   "no slab-to-wall support edge")) return 1;
        if (!check(hasEdge(s10, s11) && hasEdge(s11, s10),
                   "no same-story slab bond")) return 1;
        if (!check(!hasEdge(s10, s20),
                   "slabs across different stories are bonded")) return 1;

        model.damageNode(w10, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();

        if (!check(!model.nodes()[w10].alive,
                   "ground-story slot-0 wall was not destroyed")) return 1;
        if (!check(model.nodes()[w20].alive,
                   "wall above destroyed wall lost its support path")) return 1;
        if (!check(model.nodes()[s10].alive && model.nodes()[s20].alive,
                   "slabs around the destroyed wall lost their support path")) return 1;
        if (!check(model.nodes()[columnId(model, 0, 1)].alive &&
                   model.nodes()[columnId(model, 0, 6)].alive &&
                   model.nodes()[columnId(model, 1, 1)].alive,
                   "unrelated house members collapsed after one wall failure")) return 1;
    }
    {
        // Cascade delay: destroying a support schedules the overloaded
        // column but it stays alive until update() reaches its due time.
        BlastSupportModel model;
        model.setCascadeDelay(0.5f);
        model.damageColumn(3, 0, 100.0f);
        model.damageColumn(3, 1, 100.0f);
        model.damageColumn(3, 2, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const int c4 = columnId(model, 3, 3);
        if (!check(model.nodes()[c4].alive,
                   "overloaded cascade column failed without update()")) return 1;
        model.update(0.1f);
        if (!check(model.nodes()[c4].alive,
                   "overloaded cascade column failed before the delay elapsed")) return 1;
        model.update(0.6f);
        if (!check(!model.nodes()[c4].alive,
                   "overloaded cascade column did not fail after the delay elapsed")) return 1;
    }
    {
        // delay=0: tickAnalysis only schedules; update() then executes the
        // scheduled failures immediately, and the cascade propagates upward.
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        model.damageColumn(3, 0, 100.0f);
        model.damageColumn(3, 1, 100.0f);
        model.damageColumn(3, 2, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        const int c4 = columnId(model, 3, 3);
        if (!check(model.nodes()[c4].alive,
                   "delay-0 cascade node failed without update()")) return 1;
        model.update(0.0f);
        if (!check(!model.nodes()[c4].alive,
                   "delay-0 cascade node did not fail after update()")) return 1;
        if (!check(!model.nodes()[blockId(model, 3, 0)].alive,
                   "delay-0 cascade did not propagate upward after update()")) return 1;
    }
    {
        // A user-initiated damageNode destroys the target immediately even
        // with a large cascade delay, and unrelated members stay standing.
        BlastSupportModel model;
        model.setCascadeDelay(10.0f);
        model.damageNode(columnId(model, 2, 0), 100.0f);
        if (!check(!model.nodes()[columnId(model, 2, 0)].alive,
                   "direct damageNode did not destroy the member immediately")) return 1;
        if (!check(model.nodes()[blockId(model, 2, 0)].alive,
                   "undamaged neighbor was affected before the delay")) return 1;
        model.update(99999.0f);
        if (!check(model.nodes()[blockId(model, 2, 0)].alive,
                   "non-cascade neighbor collapsed after update()")) return 1;
    }
    {
        // Grid preset: 4 floors x (16 blocks + 4 columns + 8 walls) per floor
        // = 113 nodes, all alive after an untouched reset. Every floor has
        // exactly 8 wall segments (4 north + 4 west).
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        if (!check(model.activeFloors() == 4 && model.activeColumns() == 4 && model.activeBlocks() == 16 && model.activeWalls() == 8,
                   "grid preset dimensions were not applied") ||
            !check(model.nodes().size() == 1u + 4u * (16u + 4u + 8u),
                   "grid preset node count is incorrect (expected 113)"))
            return 1;
        if (!check(model.isGrid(), "grid preset was not detected as grid layout")) return 1;
        bool wallType = model.nodes()[wallId(model, 0, 0)].type == NodeType::Wall &&
                        model.nodes()[wallId(model, 3, 7)].type == NodeType::Wall;
        if (!check(wallType, "grid preset did not create Wall nodes")) return 1;
        bool wallsPerFloorOk = true;
        for (int floor = 0; floor < model.activeFloors(); ++floor)
        {
            int wallCount = 0;
            for (const blast_demo::NodeState& node : model.nodes())
                if (node.type == blast_demo::NodeType::Wall && node.floor == floor) ++wallCount;
            if (wallCount != 8) wallsPerFloorOk = false;
        }
        if (!check(wallsPerFloorOk, "grid floor does not have exactly 8 wall segments")) return 1;
        bool allAlive = true;
        for (const blast_demo::NodeState& node : model.nodes())
            if (node.type != blast_demo::NodeType::Ground && !node.alive)
                allAlive = false;
        if (!check(allAlive, "grid reset left a member damaged")) return 1;
        // Walls share the floor load equally with the columns.
        if (!check(model.nodes()[wallId(model, 2, 0)].load > 0.0f &&
                   std::fabs(model.nodes()[wallId(model, 2, 0)].load -
                             model.nodes()[columnId(model, 2, 0)].load) < 0.001f,
                   "grid wall does not share the floor load with the columns"))
            return 1;
        bool blastOk = true;
        for (const std::string& event : model.events())
            if (event.find("Blast init failed") != std::string::npos) blastOk = false;
        if (!check(blastOk, "grid preset Blast asset failed to initialize")) return 1;
    }
    {
        // Grid: destroying one wall segment redistributes the story load among
        // the seven survivors (columns + other walls) which all stay standing,
        // and the destroyed wall leaves its column neighbors untouched.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        const float before = model.nodes()[wallId(model, 2, 1)].load;
        model.damageNode(wallId(model, 2, 0), 100.0f); // destroy F3 north segment 0
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[wallId(model, 2, 0)].alive,
                   "grid north wall was not destroyed")) return 1;
        const float after = model.nodes()[wallId(model, 2, 1)].load;
        if (!check(after > before,
                   "surviving wall did not receive the destroyed wall's load"))
        {
            std::cerr << "before=" << before << " after=" << after << "\n";
            return 1;
        }
        bool survivorsShareEqually = true;
        for (int slot = 1; slot < model.activeWalls(); ++slot)
            if (std::fabs(model.nodes()[wallId(model, 2, slot)].load - after) > 0.001f)
                survivorsShareEqually = false;
        if (!check(survivorsShareEqually,
                   "surviving walls are not sharing the released load equally")) return 1;
        bool columnsAlive = model.nodes()[columnId(model, 2, 0)].alive &&
                            model.nodes()[columnId(model, 2, 1)].alive &&
                            model.nodes()[columnId(model, 2, 2)].alive &&
                            model.nodes()[columnId(model, 2, 3)].alive;
        if (!check(columnsAlive, "columns collapsed after a wall failure")) return 1;
        bool blocksAlive = true;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (!model.nodes()[blockId(model, 2, slot)].alive) blocksAlive = false;
        if (!check(blocksAlive, "grid blocks collapsed after a wall failure")) return 1;
    }
    {
        // Grid: a wall can be destroyed without taking its column neighbors
        // out. The wall on the floor below keeps its own support path.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.damageNode(wallId(model, 1, 1), 100.0f); // destroy F2 north segment 1
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[wallId(model, 1, 1)].alive,
                   "grid west wall was not destroyed")) return 1;
        if (!check(model.nodes()[columnId(model, 1, 0)].alive &&
                   model.nodes()[columnId(model, 1, 1)].alive &&
                   model.nodes()[columnId(model, 1, 2)].alive &&
                   model.nodes()[columnId(model, 1, 3)].alive,
                   "columns were damaged by the wall failure")) return 1;
        if (!check(model.nodes()[wallId(model, 2, 1)].alive,
                   "wall above the destroyed wall lost its support path")) return 1;
        if (!check(model.nodes()[blockId(model, 1, 0)].alive &&
                   model.nodes()[blockId(model, 1, 3)].alive,
                   "floor blocks collapsed after the wall failure")) return 1;
    }
    {
        // Grid: destroying two columns on a floor (with the eight walls alive)
        // still overloads the ten survivors and collapses the whole story.
        // The walls carry load, so the survivor share (including their own
        // share) must exceed capacity when only ten members remain.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.setCascadeDelay(0.0f);
        model.damageColumn(2, 0, 100.0f);
        model.damageColumn(2, 1, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            if (!check(!model.nodes()[columnId(model, 2, slot)].alive,
                       "grid story column survived after two-column loss")) return 1;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (!check(!model.nodes()[blockId(model, 2, slot)].alive,
                       "grid story block survived after two-column loss")) return 1;
        for (int slot = 0; slot < model.activeWalls(); ++slot)
            if (!check(!model.nodes()[wallId(model, 2, slot)].alive,
                       "grid story wall survived after two-column loss")) return 1;
        if (!check(model.nodes()[columnId(model, 1, 0)].alive &&
                   model.nodes()[blockId(model, 1, 0)].alive,
                   "lower floors collapsed after an upper two-column loss")) return 1;
    }
    {
        // Grid: destroying ONE column on a floor re-shares the story load
        // among the three survivors, which stay below capacity, so the whole
        // floor (blocks included) remains standing.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.damageColumn(2, 0, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        bool columnsAlive = model.nodes()[columnId(model, 2, 1)].alive &&
                            model.nodes()[columnId(model, 2, 2)].alive &&
                            model.nodes()[columnId(model, 2, 3)].alive;
        if (!check(columnsAlive, "grid floor collapsed after a single column failure")) return 1;
        bool blocksAlive = true;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (!model.nodes()[blockId(model, 2, slot)].alive) blocksAlive = false;
        if (!check(blocksAlive, "grid blocks collapsed after a single column failure")) return 1;
        // The upper same-lane column must stay supported through the lower
        // floor plate: it is not chained to the destroyed column directly.
        if (!check(model.nodes()[columnId(model, 3, 0)].alive,
                   "upper same-lane column lost support after a single lower-column failure")) return 1;
        if (!check(model.nodes()[columnId(model, 3, 1)].alive &&
                   model.nodes()[columnId(model, 3, 2)].alive &&
                   model.nodes()[columnId(model, 3, 3)].alive,
                   "other upper columns were damaged by a single lower-column failure")) return 1;
    }
    {
        // Grid: destroying ANY two columns on a floor raises the shared load
        // on the ten survivors (columns + walls), which overload and collapse;
        // the whole story and the story above it lose their Ground path while
        // the floors below stand.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.setCascadeDelay(0.0f);
        model.damageColumn(2, 0, 100.0f);
        model.damageColumn(2, 1, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            if (!check(!model.nodes()[columnId(model, 2, slot)].alive,
                       "grid story column survived after two-column loss")) return 1;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (!check(!model.nodes()[blockId(model, 2, slot)].alive,
                       "grid story block survived after two-column loss")) return 1;
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            if (!check(!model.nodes()[columnId(model, 3, slot)].alive,
                       "upper story column retained a Ground path")) return 1;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (!check(!model.nodes()[blockId(model, 3, slot)].alive,
                       "upper story block retained a Ground path")) return 1;
        if (!check(model.nodes()[columnId(model, 1, 0)].alive &&
                   model.nodes()[columnId(model, 0, 0)].alive &&
                   model.nodes()[blockId(model, 0, 0)].alive,
                   "lower floors collapsed after an upper two-column loss")) return 1;
    }
    {
        // Grid top story: destroying two top-floor columns collapses the top
        // floor only, while everything below keeps its support path.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.setCascadeDelay(0.0f);
        model.damageColumn(3, 1, 100.0f);
        model.damageColumn(3, 3, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        bool topAlive = false;
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            if (model.nodes()[columnId(model, 3, slot)].alive) topAlive = true;
        for (int slot = 0; slot < model.activeBlocks(); ++slot)
            if (model.nodes()[blockId(model, 3, slot)].alive) topAlive = true;
        if (!check(!topAlive, "grid top story survived after two-column loss")) return 1;
        if (!check(model.nodes()[columnId(model, 2, 0)].alive &&
                   model.nodes()[blockId(model, 2, 0)].alive,
                   "grid floor below the collapsed top story was damaged")) return 1;
    }
    {
        // Grid interior block isolation: B11 (slot 10, col 2, row 2) is an
        // interior block with no direct column or wall bond; it is supported
        // only through its four block-block bonds (B7, B10, B12, B15).
        // Destroying a single neighbor lets B11 detour around the dead bond
        // and survive, but destroying all four leaves it with no live support
        // edge and no Ground path, so it collapses.
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        model.setCascadeDelay(0.0f);
        const int b11 = blockId(model, 2, 10);
        const int b7 = blockId(model, 2, 6);
        const int b10 = blockId(model, 2, 9);
        const int b12 = blockId(model, 2, 11);
        const int b15 = blockId(model, 2, 14);

        model.damageNode(b7, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[b7].alive, "B7 was not destroyed")) return 1;
        if (!check(model.nodes()[b11].alive,
                   "B11 collapsed after a single neighbor was destroyed")) return 1;

        model.damageNode(b10, 100.0f);
        model.damageNode(b12, 100.0f);
        model.damageNode(b15, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[b11].alive,
                   "B11 kept a Ground path after all four neighbors were destroyed")) return 1;
        if (!check(model.nodes()[blockId(model, 2, 0)].alive &&
                   model.nodes()[columnId(model, 2, 0)].alive &&
                   model.nodes()[wallId(model, 2, 0)].alive,
                   "unrelated grid members collapsed with the interior block")) return 1;
    }
    std::cout << "PASS: chain load, released load, dirty BFS, house, grid, and cascade delay checks\n";
    return 0;
}
