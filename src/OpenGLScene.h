#pragma once

#include "BlastSupportModel.h"

#include <vector>

namespace blast_demo
{
struct WorldPoint { float x; float y; };
struct ViewportGeometry { float x; float y; float width; float height; };
struct SceneRect { float x; float y; float width; float height; };
struct SceneNode
{
    int id;
    NodeType type;
    SceneRect rect;
    bool alive;
};

WorldPoint screenToWorld(float screenX, float screenY, const ViewportGeometry& viewport,
                         float framebufferScale, float panX, float panY, float zoom);
int hitTest(const std::vector<SceneNode>& nodes, WorldPoint point);

class OpenGLScene
{
public:
    OpenGLScene();
    void render(const BlastSupportModel& model, int selectedId, const ViewportGeometry& viewport,
                float framebufferScale);
    void pan(float dx, float dy);
    void zoomAt(float screenX, float screenY, const ViewportGeometry& viewport,
                float framebufferScale, float factor);
    int pick(const BlastSupportModel& model, float screenX, float screenY,
             const ViewportGeometry& viewport, float framebufferScale) const;
    float zoom() const { return m_zoom; }

    void setShowSupportLinks(bool show) { m_showSupportLinks = show; }
    bool showSupportLinks() const { return m_showSupportLinks; }

private:
    float m_panX;
    float m_panY;
    float m_zoom;
    bool m_showSupportLinks = false;
};
}
