#include "BlastSupportModel.h"

#include <cstdio>

using namespace blast_demo;

int main()
{
    BlastSupportModel model;
    model.setPreset(StructuralPreset::Grid4x4Floors4);
    model.setCascadeDelay(0.0f);
    model.damageColumn(3, 1, 100.0f);
    model.damageColumn(3, 2, 100.0f);
    model.damageColumn(3, 3, 100.0f);
    for (int i = 0; i < 8; ++i) model.tickAnalysis();
    model.update(99999.0f);
    std::printf("top floor F4 after 3 cols:\n");
    int dead = 0;
    for (const NodeState& n : model.nodes())
        if (n.floor == 3 && n.id != 0 && !n.alive) ++dead;
    std::printf("  floor3 dead=%d\n", dead);
    for (const NodeState& n : model.nodes())
        if (n.floor == 3 && n.id != 0 && n.alive)
            std::printf("  ALIVE %-8s\n", n.name.c_str());
    return 0;
}
