#pragma once

#include "BlastSupportModel.h"
#include "GLFunctions.h"
#include "OpenGLScene.h"

#include <glm/glm.hpp>

#include <unordered_map>

namespace blast_demo
{
class PhysicsWorld;

class Scene3D
{
public:
    Scene3D();
    ~Scene3D();

    bool init();
    void shutdown();

    void render(const BlastSupportModel& model, PhysicsWorld& physics, int selectedId,
                const ViewportGeometry& viewport, float framebufferScale);

    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float factor);

    void setShowLabels(bool show) { m_showLabels = show; }
    bool showLabels() const { return m_showLabels; }

    void setShowSupportLinks(bool show) { m_showSupportLinks = show; }
    bool showSupportLinks() const { return m_showSupportLinks; }

    int pick(const BlastSupportModel& model, const PhysicsWorld& physics, float screenX, float screenY,
             const ViewportGeometry& viewport, float framebufferScale) const;

private:
    glm::vec3 eye() const;
    glm::mat4 computeVP(const ViewportGeometry& viewport) const;
    bool buildBuffers();
    void drawGrid() const;

    GLuint m_program = 0;
    GLuint m_edgeProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_edgeVao = 0;
    GLuint m_edgeVbo = 0;
    GLuint m_edgeEbo = 0;
    GLuint m_gridVao = 0;
    GLuint m_gridVbo = 0;
    GLint m_vpLocation = -1;
    GLint m_modelLocation = -1;
    GLint m_colorLocation = -1;
    GLint m_lightLocation = -1;
    GLint m_edgeVpLocation = -1;
    GLint m_edgeModelLocation = -1;
    GLint m_edgeColorLocation = -1;
    size_t m_edgeIndexCount = 0;
    size_t m_gridVertexCount = 0;
    // Dynamic line buffer for the "Show support links" debug overlay.
    GLuint m_linkVao = 0;
    GLuint m_linkVbo = 0;

    glm::vec3 m_target{0.0f, 4.0f, 0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.45f;
    float m_distance = 18.0f;
    mutable glm::mat4 m_vpCache{1.0f};
    // Tracks when a node first became dynamic debris so it can be hidden a
    // fixed time after falling, instead of lingering forever.
    std::unordered_map<int, float> m_debrisBirthTime;
    static constexpr float kDebrisLifetime = 5.0f;
    bool m_showLabels = true;
    bool m_showSupportLinks = false;
};
}
