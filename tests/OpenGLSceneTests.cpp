#include "OpenGLScene.h"
#include "BlastSupportModel.h"

#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    using namespace blast_demo;
    const ViewportGeometry viewport = {100.0f, 50.0f, 800.0f, 600.0f};
    const WorldPoint center = screenToWorld(500.0f, 350.0f, viewport, 1.0f, 0.0f, 0.0f, 1.0f);
    if (std::fabs(center.x) > 0.001f || std::fabs(center.y) > 0.001f)
    {
        std::cerr << "FAIL: 1x screen-to-world center\n";
        return 1;
    }
    const WorldPoint highDpi = screenToWorld(500.0f, 350.0f, viewport, 2.0f, 0.0f, 0.0f, 1.0f);
    if (std::fabs(highDpi.x - center.x) > 0.001f || std::fabs(highDpi.y - center.y) > 0.001f)
    {
        std::cerr << "FAIL: high-DPI screen-to-world conversion\n";
        return 1;
    }

    std::vector<SceneNode> nodes = {
        {7, NodeType::Slab, {-2.0f, -1.0f, 2.0f, 2.0f}, true},
        {8, NodeType::Column, {1.0f, -1.0f, 2.0f, 2.0f}, true},
        {9, NodeType::Column, {4.0f, -1.0f, 2.0f, 2.0f}, false}};
    if (hitTest(nodes, WorldPoint{-1.0f, 0.0f}) != 7 ||
        hitTest(nodes, WorldPoint{2.0f, 0.0f}) != 8 ||
        hitTest(nodes, WorldPoint{5.0f, 0.0f}) != -1)
    {
        std::cerr << "FAIL: block/column/removed hit testing\n";
        return 1;
    }

    BlastSupportModel model;
    const float before = model.nodes()[1].health;
    const int selected = hitTest(nodes, WorldPoint{-1.0f, 0.0f});
    if (selected != 7 || model.nodes()[1].health != before)
    {
        std::cerr << "FAIL: selection changed node HP\n";
        return 1;
    }
    std::cout << "PASS: OpenGL scene helpers\n";
    return 0;
}
