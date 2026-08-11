#include "BlastSupportModel.h"
#include "PhysicsWorld.h"

#include <iostream>

using namespace blast_demo;

int main()
{
    BlastSupportModel model;
    PhysicsWorld physics;
    if (!physics.init()) { std::cerr << "physics init failed\n"; return 1; }
    physics.rebuild(model);

    // Destroy a middle-floor column (F3-C1), not bottom.
    model.damageColumn(2, 0, 100.0f);
    for (int i = 0; i < 8; ++i) model.tickAnalysis();
    physics.syncFromModel(model);

    std::cerr << "structure after destroying F3-C1:\n";
    for (const auto& node : model.nodes())
        if (node.floor >= 1)
            std::cerr << "  " << node.name << " alive=" << node.alive
                      << " dyn=" << physics.isDynamic(node.id)
                      << " load=" << node.load << "\n";

    std::cerr << "\nphysics dynamic bodies:\n";
    for (size_t i = 0; i < model.nodes().size(); ++i)
        if (physics.isDynamic(static_cast<int>(i)))
            std::cerr << "  " << model.nodes()[i].name << "\n";

    physics.shutdown();
    return 0;
}
