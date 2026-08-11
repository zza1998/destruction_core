#include "OpenGLScene.h"
#include "GLFunctions.h"
#include "SceneLayout.h"
#ifndef BLAST_SCENE_HEADLESS
#include <imgui.h>
#endif

#include <algorithm>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace blast_demo
{
WorldPoint screenToWorld(float screenX, float screenY, const ViewportGeometry& viewport,
                         float framebufferScale, float panX, float panY, float zoom)
{
    // Mouse and viewport coordinates are logical GLFW/ImGui coordinates.
    // Framebuffer scale belongs to GL viewport/scissor only; applying it to
    // both numerator and denominator would be redundant and obscures the
    // actual inverse of worldToScreen.
    (void)framebufferScale;
    const float safeZoom = zoom > 0.0f ? zoom : 1.0f;
    const float localX = (screenX - viewport.x) / viewport.width;
    const float localY = (screenY - viewport.y) / viewport.height;
    return {panX + (localX - 0.5f) / (0.12f * safeZoom),
            panY + (0.5f - localY) / (0.12f * safeZoom)};
}

int hitTest(const std::vector<SceneNode>& nodes, WorldPoint point)
{
    for (const SceneNode& node : nodes)
        if (node.alive && point.x >= node.rect.x && point.x <= node.rect.x + node.rect.width &&
            point.y >= node.rect.y && point.y <= node.rect.y + node.rect.height)
            return node.id;
    return -1;
}

SceneRect modelNodeRect(const NodeState& node, bool gridMode)
{
    if (gridMode)
    {
        // 2D top-down of the 4x4 block grid: x follows the grid column, y
        // follows the grid row (each floor is a band), and the columns sit at
        // the four corners of the band.
        const float col = static_cast<float>(gridCol(node.slot));
        const float row = static_cast<float>(gridRow(node.slot));
        const float x = (col - 1.5f) * 2.6f;
        const float y = static_cast<float>(node.floor) * 2.2f - 4.5f + (row - 1.5f) * 0.55f;
        if (node.type == NodeType::Slab)
            return {x - 1.1f, y - 0.25f, 2.2f, 0.5f};
        if (node.type == NodeType::Wall)
        {
            // 2D top-down of the grid walls: the 4 north segments run along
            // the top edge of the band (one per grid column, each as wide as a
            // block) and the 4 west segments run along its left edge (one per
            // grid row, each as tall as a block row).
            const float floorY = static_cast<float>(node.floor) * 2.2f;
            if (node.slot < 4)
            {
                const float cx = (static_cast<float>(node.slot) - 1.5f) * 2.6f;
                return {cx - 1.1f, floorY - 5.7f, 2.2f, 0.2f};
            }
            const float cy = floorY - 4.5f +
                (static_cast<float>(node.slot - 4) - 1.5f) * 0.55f;
            return {-5.1f, cy - 0.275f, 0.2f, 0.55f};
        }
        const float cx = gridColumnCol(node.slot) == 0 ? -0.95f : 0.95f;
        const float cy = gridColumnRow(node.slot) == 0 ? -0.22f : 0.22f;
        return {x + cx - 0.3f, y + cy - 0.3f, 0.6f, 0.6f};
    }
    const float blockX = static_cast<float>(node.slot) * 2.6f - 4.0f;
    const float blockY = static_cast<float>(node.floor) * 2.2f - 4.5f;
    if (node.type == NodeType::Slab)
        return {blockX, blockY, 2.2f, 0.45f};
    return {blockX + 0.7f, blockY - 1.8f, 0.8f, 1.8f};
}

 #ifndef BLAST_SCENE_HEADLESS
namespace
{
SceneRect nodeRect(const NodeState& node, bool gridMode)
{
    return modelNodeRect(node, gridMode);
}
ImVec2 worldToScreen(WorldPoint point, const ViewportGeometry& viewport, float panX, float panY, float zoom)
{
    return ImVec2(viewport.x + viewport.width * (0.5f + (point.x - panX) * zoom * 0.12f),
                  viewport.y + viewport.height * (0.5f - (point.y - panY) * zoom * 0.12f));
}
ImU32 nodeColor(const NodeState& node)
{
    if (!node.alive || !node.supported) return IM_COL32(190, 70, 70, 255);
    if (node.status == NodeStatus::Falling) return IM_COL32(125, 95, 220, 255); // purple
    if (node.status == NodeStatus::Overloaded) return IM_COL32(225, 110, 45, 255);
    if (node.status == NodeStatus::Warning || node.health < 50.0f) return IM_COL32(220, 165, 55, 255);
    return IM_COL32(70, 145, 105, 255);
}
}
 #endif

OpenGLScene::OpenGLScene() : m_panX(0.0f), m_panY(0.0f), m_zoom(1.0f) {}

void OpenGLScene::pan(float dx, float dy) { m_panX += dx / m_zoom; m_panY += dy / m_zoom; }

void OpenGLScene::zoomAt(float screenX, float screenY, const ViewportGeometry& viewport,
                         float framebufferScale, float factor)
{
    const WorldPoint before = screenToWorld(screenX, screenY, viewport, framebufferScale, m_panX, m_panY, m_zoom);
    m_zoom = std::max(0.1f, std::min(8.0f, m_zoom * factor));
    const WorldPoint after = screenToWorld(screenX, screenY, viewport, framebufferScale, m_panX, m_panY, m_zoom);
    m_panX += before.x - after.x;
    m_panY += before.y - after.y;
}

int OpenGLScene::pick(const BlastSupportModel& model, float screenX, float screenY,
                      const ViewportGeometry& viewport, float framebufferScale) const
{
    std::vector<SceneNode> nodes;
    for (const NodeState& node : model.nodes())
        if (node.type != NodeType::Ground)
        {
            nodes.push_back({node.id, node.type, modelNodeRect(node, model.isGrid()), node.alive});
        }
    const WorldPoint world = screenToWorld(screenX, screenY, viewport, framebufferScale, m_panX, m_panY, m_zoom);
    // Test smaller geometry first when a block and column share a boundary.
    std::sort(nodes.begin(), nodes.end(), [](const SceneNode& a, const SceneNode& b) {
        return a.rect.width * a.rect.height < b.rect.width * b.rect.height;
    });
    return hitTest(nodes, world);
}

void OpenGLScene::render(const BlastSupportModel& model, int selectedId, const ViewportGeometry& viewport,
                         float framebufferScale)
{
#ifdef BLAST_SCENE_HEADLESS
    (void)model; (void)selectedId; (void)viewport; (void)framebufferScale;
    return;
#else
    (void)framebufferScale;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewport.x, viewport.y), ImVec2(viewport.x + viewport.width, viewport.y + viewport.height), true);
    const std::string structureLabel = std::to_string(model.activeFloors()) + " floors x " +
        std::to_string(model.activeColumns()) + " columns";
    draw->AddText(ImVec2(viewport.x + 12.0f, viewport.y + 12.0f),
                  IM_COL32(235, 240, 240, 255), structureLabel.c_str());
    // "Show support links": draw one line per live directed support edge
    // (edge.from depends on edge.to) between the two node rectangles. Color
    // marks the supporter: white = Ground, green = column/wall, yellow =
    // block. Hidden by default so the default view stays clean.
    if (m_showSupportLinks)
    {
        for (const EdgeState& edge : model.edges())
        {
            const NodeState& from = model.nodes()[edge.from];
            const NodeState& to = model.nodes()[edge.to];
            if (edge.from <= 0 || !from.alive || !to.alive) continue;
            const SceneRect a = nodeRect(from, model.isGrid()), b = nodeRect(to, model.isGrid());
            const ImVec2 p0 = worldToScreen({a.x + a.width * 0.5f, a.y + a.height * 0.5f}, viewport, m_panX, m_panY, m_zoom);
            const ImVec2 p1 = worldToScreen({b.x + b.width * 0.5f, b.y + b.height * 0.5f}, viewport, m_panX, m_panY, m_zoom);
            const ImU32 linkColor = to.type == NodeType::Ground ? IM_COL32(255, 255, 255, 255)
                : (to.type == NodeType::Column || to.type == NodeType::Wall) ? IM_COL32(90, 220, 90, 230)
                                                                             : IM_COL32(240, 215, 60, 230);
            draw->AddLine(p0, p1, linkColor, 2.0f);
        }
    }
    for (const NodeState& node : model.nodes())
    {
        if (node.type == NodeType::Ground || !node.alive) continue;
        const SceneRect rect = nodeRect(node, model.isGrid());
        const ImVec2 min = worldToScreen({rect.x, rect.y + rect.height}, viewport, m_panX, m_panY, m_zoom);
        const ImVec2 max = worldToScreen({rect.x + rect.width, rect.y}, viewport, m_panX, m_panY, m_zoom);
        draw->AddRectFilled(min, max, nodeColor(node), 3.0f);
        if (node.id == selectedId) draw->AddRect(min, max, IM_COL32(255, 255, 255, 255), 3.0f, 0, 3.0f);
        draw->AddText(ImVec2(min.x, max.y + 2.0f), IM_COL32(235, 240, 240, 255),
                      (node.name + " W " + std::to_string(static_cast<int>(node.mass))).c_str());
    }
    draw->PopClipRect();
#endif
}
}
