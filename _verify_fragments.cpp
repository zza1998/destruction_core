#include "BlastSupportModel.h"
#include "PhysicsWorld.h"

#include <cstdio>
#include <vector>

using namespace blast_demo;

static int failures = 0;

static void check(bool cond, const char* msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
    else
    {
        std::printf("ok: %s\n", msg);
    }
}

int main()
{
    // Each structural member is pre-fractured into 8 fragments (2x2x2), so a
    // destroyed member always splits into exactly 8 visible debris bodies.
    const int kExpectedFragments = 8;

    // Use a single model (one per process - the Blast framework is a process
    // wide singleton), exercising the grid preset where the new load-bearing
    // walls live.
    BlastSupportModel model;
    model.setPreset(StructuralPreset::Grid4x4Floors4);
    PhysicsWorld physics;
    if (!physics.init())
    {
        std::fprintf(stderr, "FAIL: physics init\n");
        return 1;
    }
    physics.rebuild(model);

    // Destroy a load-bearing wall (F2 north wall). The full pipeline:
    // damageNode -> BlastRuntime::fractureMember splits the member's 8
    // pre-fractured fragments -> visible chunks are collected -> PhysicsWorld
    // spawns anonymous debris bodies.
    const int destroyedWall = model.wallId(1, 0);
    model.damageNode(destroyedWall, 100.0f);
    for (int i = 0; i < 8; ++i) model.tickAnalysis();
    physics.syncFromModel(model);

    check(physics.fragmentCount() == kExpectedFragments,
          "physics spawned exactly 8 fragment bodies after wall destruction");

    bool wallFragments = physics.fragmentCount() > 0;
    for (int i = 0; i < physics.fragmentCount(); ++i)
        if (physics.fragmentNodeIdFor(i) != destroyedWall) wallFragments = false;
    check(wallFragments, "all wall fragments belong to the destroyed wall");

    float hx = 0, hy = 0, hz = 0;
    physics.fragmentExtentsFor(0, hx, hy, hz);
    check(hx > 0.05f && hz > 0.05f && hy > 0.05f, "fragment bodies have finite extents");

    check(!physics.hasBody(destroyedWall),
          "destroyed wall body replaced by its fragments");

    bool columnsStand = model.nodes()[model.columnId(1, 0)].alive &&
                        model.nodes()[model.columnId(1, 1)].alive &&
                        model.nodes()[model.columnId(1, 2)].alive &&
                        model.nodes()[model.columnId(1, 3)].alive;
    check(columnsStand, "column neighbors stand after the wall shattered");

    // Destroy a grid column next: it shatters into the same 8 fragments.
    const int destroyedColumn = model.columnId(2, 0);
    model.damageNode(destroyedColumn, 100.0f);
    for (int i = 0; i < 8; ++i) model.tickAnalysis();
    physics.syncFromModel(model);
    check(physics.fragmentCount() == 2 * kExpectedFragments,
          "destroying a column added exactly 8 more fragment bodies");
    check(!physics.hasBody(destroyedColumn),
          "destroyed column body replaced by its fragments");

    // Fragments must keep simulating for a while (falling), then be removed
    // after the 5 s lifetime.
    bool stayedLiveForAWhile = true;
    for (int frame = 0; frame < 120; ++frame)
    {
        model.update(0.016f);
        physics.syncFromModel(model);
        physics.simulate(1.0f / 60.0f);
    }
    if (physics.fragmentCount() == 0)
        stayedLiveForAWhile = false;
    check(stayedLiveForAWhile, "fragments still present before the lifetime ends");

    for (int frame = 0; frame < 300; ++frame)
    {
        model.update(0.016f);
        physics.syncFromModel(model);
        physics.simulate(1.0f / 60.0f);
    }
    check(physics.fragmentCount() == 0, "fragment bodies removed after lifetime");

    physics.shutdown();
    if (failures == 0) std::printf("PASS: pre-fracture fragment pipeline\n");
    return failures == 0 ? 0 : 1;
}
