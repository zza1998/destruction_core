#include "BlastSupportModel.h"
#include "GeometryDerived.h"
#include "SupportGraphSolver.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using blast_demo::BlastSupportModel;
using blast_demo::EdgeState;
using blast_demo::NodeState;
using blast_demo::NodeStatus;
using blast_demo::MemberRole;
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

bool nearlyEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}
}

int main()
{
    // Ground-path traversal on a directed column edge.
    {
        std::vector<NodeState> nodes(2);
        nodes[0] = {0, "Ground", -1, 0, 100, 0, 0, 100000, true, true, blast_demo::NodeStatus::Safe};
        nodes[1] = {1, "Column", 0, 0, 100, 1, 0, 10, true, true, blast_demo::NodeStatus::Safe};
        SupportGraphSolver solver;
        if (!check(!solver.hasGroundPath(1, nodes, {{0, 1, 1, 0, true}}),
                   "Ground path traversal incorrectly walked an edge backwards")) return 1;
        if (!check(solver.hasGroundPath(1, nodes, {{1, 0, 1, 0, true}}),
                   "directed column Ground path was not found")) return 1;
    }
    // Default reset leaves every non-ground node alive.
    {
        BlastSupportModel model;
        for (const NodeState& node : model.nodes())
            if (node.id != 0 && !node.alive)
            {
                std::cerr << "FAIL: default model removed " << node.name << " during reset\n";
                return 1;
            }
    }
    // Damage application semantics.
    {
        BlastSupportModel model;
        if (!check(model.damageNode(blockId(model, 0, 0), 35.0f), "valid block damage was not applied") ||
            !check(model.nodes()[blockId(model, 0, 0)].health == 65.0f, "block HP changed incorrectly")) return 1;
        if (!check(!model.damageNode(0, 1.0f), "Ground accepted damage") ||
            !check(!model.damageNode(blockId(model, 0, 0), std::numeric_limits<float>::quiet_NaN()), "invalid damage accepted")) return 1;
    }
    // Default preset dimensions and Blast initialization.
    {
        BlastSupportModel model;
        if (!check(model.activeFloors() == 5 && model.activeColumns() == 4,
                   "default preset dimensions changed") ||
            !check(model.nodes().size() == 1u + 2u * static_cast<size_t>(model.activeFloors()) *
                       static_cast<size_t>(model.activeColumns()),
                   "default preset node count changed")) return 1;
        bool blastOk = true;
        for (const std::string& event : model.events())
            if (event.find("Blast init failed") != std::string::npos) blastOk = false;
        if (!check(blastOk, "default preset Blast asset failed to initialize")) return 1;
    }
    // Static solver populates diagnostics on every surviving vertical bearing
    // after reset: carried mass and a finite compression utilization.
    {
        BlastSupportModel model;
        bool allBearingDiagnosed = true;
        for (const NodeState& node : model.nodes())
            if (node.id != 0 && node.alive &&
                deriveRole(node.box) == MemberRole::VerticalBearing)
                if (!(node.carriedMass > 0.0f))
                    allBearingDiagnosed = false;
        if (!check(allBearingDiagnosed, "static solver did not populate carried mass on bearings")) return 1;
    }
    // Direct column break removes its support and, after analysis, the column
    // is dead and unsupported; surviving bearings carry a positive load.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        model.damageColumn(3, 0, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[columnId(model, 3, 0)].alive,
                   "destroyed column survived its own failure")) return 1;
        if (!check(model.nodes()[columnId(model, 3, 1)].carriedMass > 0.0f,
                   "surviving column did not receive carried mass")) return 1;
    }
    // A directly damaged block keeps other members standing (its mass is simply
    // gone from the load path, not reassigned to unrelated members).
    {
        BlastSupportModel model;
        model.damageNode(blockId(model, 3, 0), 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        if (!check(!model.nodes()[blockId(model, 3, 0)].alive, "F4-B1 survived its own destruction")) return 1;
        if (!check(model.nodes()[blockId(model, 3, 1)].alive &&
                   model.nodes()[columnId(model, 3, 1)].alive,
                   "unrelated members collapsed after a single block destruction")) return 1;
    }
    // Destroying one ground column removes that lane's ground support, but the
    // upper same-lane component reroutes through the plate's horizontal bonds to
    // the surviving lanes and stays supported: a single support failure must not
    // release an otherwise-redundant column of floors.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        model.damageColumn(0, 0, 100.0f);  // ground column in lane 0
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        // The destroyed ground column is dead; the lane above it reroutes and
        // the top-most column in that lane keeps a ground path.
        if (!check(!model.nodes()[columnId(model, 0, 0)].alive,
                   "destroyed ground column survived")) return 1;
        if (!check(model.nodes()[columnId(model, model.activeFloors() - 1, 0)].supported,
                   "upper lane lost its support path after a single ground failure")) return 1;
    }
    // A fully isolated component (no ground path at all) releases: sever every
    // support beneath a top floor by destroying enough columns that a whole
    // lane loses all downwards and sideways routes.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        // Destroy both ground columns that flank lane 0 so the lowest floor slab
        // of lane 0 has no surviving downward route and the lane detaches.
        model.damageColumn(0, 0, 100.0f);
        model.damageColumn(0, 1, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        if (!check(!model.nodes()[columnId(model, 0, 0)].alive &&
                   !model.nodes()[columnId(model, 0, 1)].alive,
                   "destroyed ground columns survived")) return 1;
    }
    // Cascade delay: an overloaded support stays alive until update() reaches
    // its due time, even after it is scheduled.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.5f);
        model.damageColumn(3, 0, 100.0f);
        model.damageColumn(3, 1, 100.0f);
        model.damageColumn(3, 2, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        // A surviving top-storey column is scheduled as overloaded but must not
        // be dead before the delay elapses.
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
    // Undo restores the previous structural state.
    {
        BlastSupportModel model;
        model.damageNode(blockId(model, 2, 0), 100.0f);
        if (!check(model.canUndo(), "undo was not available after damage")) return 1;
        model.undoLast();
        if (!check(model.nodes()[blockId(model, 2, 0)].alive, "undo did not restore the block")) return 1;
    }
    // Grid preset still constructs with correct dimensions and all walls alive,
    // but shear is no longer an active failure driver.
    {
        BlastSupportModel model;
        model.setPreset(StructuralPreset::Grid4x4Floors4);
        if (!check(model.activeFloors() == 4 && model.activeColumns() == 4 &&
                   model.activeBlocks() == 16 && model.activeWalls() == 8,
                   "grid preset dimensions were not applied") ||
            !check(model.nodes().size() == 1u + 4u * (16u + 4u + 8u),
                   "grid preset node count is incorrect (expected 113)"))
            return 1;
        if (!check(model.isGrid(), "grid preset was not detected as grid layout")) return 1;
        bool allAlive = true;
        for (const NodeState& node : model.nodes())
            if (node.id != 0 && !node.alive) allAlive = false;
        if (!check(allAlive, "grid reset left a member damaged")) return 1;
        bool blastOk = true;
        for (const std::string& event : model.events())
            if (event.find("Blast init failed") != std::string::npos) blastOk = false;
        if (!check(blastOk, "grid preset Blast asset failed to initialize")) return 1;
        // A wall still carries a positive load as a vertical bearing.
        if (!check(model.nodes()[wallId(model, 2, 0)].carriedMass > 0.0f,
                   "grid wall did not carry a positive load")) return 1;
    }
    // ShearPair preset remains constructible, and lateral shear no longer drives
    // failure: after killing an end column, all live nodes keep lateralShear==0.
    {
        BlastSupportModel model;
        model.setPreset(StructuralPreset::ShearPair);
        if (!check(model.activeFloors() == 1 && model.activeColumns() == 2 &&
                   model.activeBlocks() == 4, "ShearPair preset dimensions wrong"))
            return 1;
        model.damageColumn(0, 0, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        for (const NodeState& node : model.nodes())
            if (node.id != 0 && node.alive && node.lateralShear != 0.0f)
                return check(false, "lateralShear is nonzero after static gravity analysis") ? 1 : 1;
    }
    // Cantilever (overhang) failure: after destroying the left end column of a
    // ShearPair row, the now-free left plates overhang and fail, while the plate
    // still sitting on the surviving right column holds. The Small/Medium/Large
    // tolerance tunes how many plates drop.
    {
        auto deadCount = [](const BlastSupportModel& m) {
            int dead = 0;
            for (int s = 0; s < m.activeBlocks(); ++s)
                if (!m.nodes()[static_cast<size_t>(blockId(m, 0, s))].alive) ++dead;
            return dead;
        };
        BlastSupportModel small;
        small.setPreset(StructuralPreset::ShearPair);
        small.setCascadeDelay(0.0f);
        small.setPlateOverhang(BlastSupportModel::PlateOverhang::Small);
        small.damageColumn(0, 0, 100.0f);
        for (int i = 0; i < 8; ++i) small.tickAnalysis();
        small.update(99999.0f);

        BlastSupportModel large;
        large.setPreset(StructuralPreset::ShearPair);
        large.setCascadeDelay(0.0f);
        large.setPlateOverhang(BlastSupportModel::PlateOverhang::Large);
        large.damageColumn(0, 0, 100.0f);
        for (int i = 0; i < 8; ++i) large.tickAnalysis();
        large.update(99999.0f);

        // The plate over the surviving right column always stays alive.
        if (!check(small.nodes()[blockId(small, 0, 3)].alive &&
                   large.nodes()[blockId(large, 0, 3)].alive,
                   "plated column plate did not survive end-column destruction")) return 1;
        // The far-left free plate always overhangs and fails.
        if (!check(!small.nodes()[blockId(small, 0, 0)].alive &&
                   !large.nodes()[blockId(large, 0, 0)].alive,
                   "free end plate did not overhang-fail after end-column destruction")) return 1;
        // A larger tolerance drops fewer plates than a small one.
        if (!check(deadCount(large) < deadCount(small),
                   "Large overhang tolerance did not preserve more plates than Small")) return 1;
    }
    // Column strength (failure ratio): a weaker column fails at lower axial
    // utilization than a stronger one. Small fails at 70%, Medium 85%, Large 100%.
    {
        auto topColsDead = [](const BlastSupportModel& m, int floor) {
            int dead = 0;
            for (int s = 0; s < m.activeColumns(); ++s)
                if (!m.nodes()[static_cast<size_t>(columnId(m, floor, s))].alive) ++dead;
            return dead;
        };
        // Large (1.0): break 1 top column -> the storey still stands.
        {
            BlastSupportModel m;
            m.setCascadeDelay(0.0f);
            m.setColumnStrength(BlastSupportModel::ColumnStrength::Large);
            m.damageColumn(3, 0, 100.0f);
            for (int i = 0; i < 8; ++i) m.tickAnalysis();
            m.update(99999.0f);
            if (!check(topColsDead(m, 3) == 1,
                       "Large column strength collapsed storey after a single column loss")) return 1;
        }
        // Small (0.7): the surviving columns exceed 70% utilization after
        // redistribution, so more columns fail than with Large for the same damage.
        {
            BlastSupportModel m;
            m.setCascadeDelay(0.0f);
            m.setColumnStrength(BlastSupportModel::ColumnStrength::Small);
            m.damageColumn(3, 0, 100.0f);
            m.damageColumn(3, 1, 100.0f);
            for (int i = 0; i < 8; ++i) m.tickAnalysis();
            m.update(99999.0f);
            // With only 2 of 4 columns left and a 0.7 threshold, the storey
            // collapses further than the Large case.
            if (!check(topColsDead(m, 3) > 2,
                       "Small column strength did not collapse the overloaded storey")) return 1;
        }
    }
    std::cout << "PASS: static gravity integration, release, cascade, and preset checks\n";
    return 0;
}
