#include "BlastSupportModel.h"
#include "PhysicsWorld.h"

#include <iostream>

using blast_demo::BlastSupportModel;
using blast_demo::PhysicsWorld;

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << "\n";
    return condition;
}

int columnId(const BlastSupportModel& model, int floor, int slot)
{
    return 1 + model.activeFloors() * model.activeBlocks() +
        floor * model.activeColumns() + slot;
}

int blockId(const BlastSupportModel& model, int floor, int slot)
{
    return 1 + floor * model.activeBlocks() + slot;
}
}

int main()
{
    {
        BlastSupportModel model;
        PhysicsWorld world;
        if (!check(world.init(), "PhysX failed to initialize")) return 1;
        world.rebuild(model);
        if (!check(world.hasBody(columnId(model, 0, 0)), "ground column has no body")) return 1;
        if (!check(!world.isDynamic(columnId(model, 0, 0)), "intact column should be static")) return 1;

        world.shutdown();
    }
    // Confirmed unsupported release becomes dynamic exactly once; a second sync
    // is idempotent.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        PhysicsWorld world;
        world.init();
        world.rebuild(model);
        // Destroy every top-storey column so the F5 floor loses every downwards
        // route and is released as one intact component.
        const int top = model.activeFloors() - 1;
        for (int slot = 0; slot < model.activeColumns(); ++slot)
            model.damageColumn(top, slot, 100.0f);
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        world.syncFromModel(model);
        const int topBlock = blockId(model, top, 0);
        if (!check(!model.nodes()[topBlock].alive, "upper component was not released")) return 1;
        if (!check(world.isDynamic(topBlock), "released component did not become dynamic")) return 1;
        world.syncFromModel(model);
        if (!check(world.isDynamic(topBlock), "second sync lost dynamic state")) return 1;
        world.shutdown();
    }
    // A directly broken member produces fragments and removes its original body;
    // an intact unsupported release converts the whole body to dynamic instead.
    {
        BlastSupportModel model;
        model.setCascadeDelay(0.0f);
        PhysicsWorld world;
        world.init();
        world.rebuild(model);
        model.damageNode(blockId(model, 3, 0), 100.0f);   // direct break -> fragments
        for (int i = 0; i < 8; ++i) model.tickAnalysis();
        model.update(99999.0f);
        world.syncFromModel(model);
        if (!check(world.fragmentCount() > 0, "direct break produced no fragments")) return 1;
        if (!check(!world.hasBody(blockId(model, 3, 0)), "broken member body was not removed")) return 1;
        world.shutdown();
    }
    std::cout << "PASS: physics bridge release, fragment, and idempotency checks\n";
    return 0;
}