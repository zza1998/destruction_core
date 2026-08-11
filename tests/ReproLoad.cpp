#include "BlastSupportModel.h"
#include <iostream>

using namespace blast_demo;

int main()
{
    BlastSupportModel model;
    model.setPreset(StructuralPreset::Grid4x4Floors4);
    model.setCascadeDelay(0.0f);

    const int damaged[] = {14, 6, 9, 11, 13, 5, 7, 15}; // B15,B7,B10,B12,B14,B6,B8,B16 => slots
    for (int slot : damaged)
    {
        model.damageNode(model.blockId(2, slot), 100.0f);
        model.tickAnalysis();
        model.update(1.0f);
    }
    for (int i = 0; i < 4; ++i) { model.tickAnalysis(); model.update(1.0f); }

    std::cerr << "F3 (floor 2) blocks state:\n";
    for (const auto& node : model.nodes())
        if (node.type == NodeType::Slab && node.floor == 2)
            std::cerr << "  " << node.name << " slot=" << node.slot
                      << " col=" << (node.slot % 4) << " row=" << (node.slot / 4)
                      << " alive=" << node.alive << "\n";
    return 0;
}
