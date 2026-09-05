#include "Scene3D.h"
#include "PhysicsWorld.h"
#include "SceneLayout.h"
#include "GeometryDerived.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace blast_demo
{
namespace
{
const char* const kVertexShader =
    "#version 330 core\n"
    "layout(location=0) in vec3 P;\n"
    "layout(location=1) in vec3 N;\n"
    "uniform mat4 VP;\n"
    "uniform mat4 M;\n"
    "out vec3 worldN;\n"
    "void main(){\n"
    "    worldN = mat3(M) * N;\n"
    "    gl_Position = VP * M * vec4(P, 1.0);\n"
    "}\n";

const char* const kFragmentShader =
    "#version 330 core\n"
    "in vec3 worldN;\n"
    "uniform vec4 color;\n"
    "uniform vec4 lightDir;      // lightDir.xyz = normalized direction TO the light\n"
    "out vec4 O;\n"
    "void main(){\n"
    "    vec3 n = normalize(worldN);\n"
    "    vec3 L = normalize(lightDir.xyz);\n"
    "    float diff = max(dot(n, L), 0.0);\n"
    "    float light = 0.18 + 0.82 * diff;\n"
    "    O = vec4(color.rgb * light, color.a);\n"
    "}\n";

const char* const kEdgeVertexShader =
    "#version 330 core\n"
    "layout(location=0) in vec3 P;\n"
    "uniform mat4 VP;\n"
    "uniform mat4 M;\n"
    "void main(){ gl_Position = VP * M * vec4(P, 1.0); }\n";

const char* const kEdgeFragmentShader =
    "#version 330 core\n"
    "uniform vec4 color;\n"
    "out vec4 O;\n"
    "void main(){ O = color; }\n";

// 24 vertices: position(3) + normal(3), one copy per face so each face has a
// constant normal (flat shading keeps the box edges visible).
const float kCubeVertices[] = {
    // -X face
    -1, -1, -1,  -1, 0, 0,   -1,  1, -1,  -1, 0, 0,  -1,  1,  1,  -1, 0, 0,  -1, -1,  1,  -1, 0, 0,
    // +X face
     1, -1, -1,   1, 0, 0,    1,  1, -1,   1, 0, 0,   1,  1,  1,   1, 0, 0,   1, -1,  1,   1, 0, 0,
    // -Y face
    -1, -1, -1,  0, -1, 0,    1, -1, -1,  0, -1, 0,   1, -1,  1,  0, -1, 0,  -1, -1,  1,  0, -1, 0,
    // +Y face
    -1,  1, -1,  0,  1, 0,    1,  1, -1,  0,  1, 0,   1,  1,  1,  0,  1, 0,  -1,  1,  1,  0,  1, 0,
    // -Z face
    -1, -1, -1,  0, 0, -1,    1, -1, -1,  0, 0, -1,   1,  1, -1,  0, 0, -1,  -1,  1, -1,  0, 0, -1,
    // +Z face
    -1, -1,  1,  0, 0,  1,    1, -1,  1,  0, 0,  1,   1,  1,  1,  0, 0,  1,  -1,  1,  1,  0, 0,  1,
};

const unsigned kCubeIndices[] = {
     0, 1, 2,  0, 2, 3,
     4, 6, 5,  4, 7, 6,
     8, 9,10,  8,10,11,
    12,14,13, 12,15,14,
    16,17,18, 16,18,19,
    20,22,21, 20,23,22,
};

// 8 corners used only for the line-only edge shader (position only).
const float kEdgeVertices[] = {
    -1, -1, -1,  1, -1, -1,  1, 1, -1,  -1, 1, -1,
    -1, -1,  1,  1, -1,  1,  1, 1,  1,  -1, 1,  1,
};

const unsigned kEdgeIndices[] = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7,
};

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Scene3D] shader compile error:\n%s\n", log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vs, GLuint fs)
{
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Scene3D] program link error:\n%s\n", log);
        gl.DeleteProgram(program);
        return 0;
    }
    return program;
}

float rayBox(const glm::vec3& origin, const glm::vec3& dir,
             const glm::vec3& mn, const glm::vec3& mx)
{
    float tmin = -1e30f;
    float tmax = 1e30f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float o = origin[axis];
        const float d = dir[axis];
        if (std::fabs(d) < 1e-6f)
        {
            if (o < mn[axis] || o > mx[axis]) return -1.0f;
        }
        else
        {
            float t1 = (mn[axis] - o) / d;
            float t2 = (mx[axis] - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
        }
    }
    return tmin <= tmax ? tmin : -1.0f;
}

glm::vec4 statusColor(const NodeState& node, float utilization)
{
    if (!node.alive) return glm::vec4(0.32f, 0.30f, 0.28f, 1.0f);
    if (node.status == NodeStatus::Falling)
        return glm::vec4(0.45f, 0.35f, 0.85f, 1.0f); // purple: physically collapsing
    if (node.status == NodeStatus::Overloaded) return glm::vec4(0.90f, 0.42f, 0.15f, 1.0f);
    // Stiffness-mode utilization highlight takes priority: >80% turns amber.
    if (utilization > 1.0f) return glm::vec4(0.90f, 0.30f, 0.15f, 1.0f);
    if (utilization > 0.8f) return glm::vec4(0.85f, 0.64f, 0.18f, 1.0f);
    if (node.status == NodeStatus::Warning || node.health < 50.0f)
        return glm::vec4(0.85f, 0.64f, 0.18f, 1.0f);
    return glm::vec4(0.24f, 0.58f, 0.38f, 1.0f);
}
}

Scene3D::Scene3D() = default;

Scene3D::~Scene3D()
{
    shutdown();
}

bool Scene3D::init()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs)
    {
        if (vs) gl.DeleteShader(vs);
        if (fs) gl.DeleteShader(fs);
        return false;
    }
    m_program = linkProgram(vs, fs);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    if (!m_program) return false;

    GLuint evs = compileShader(GL_VERTEX_SHADER, kEdgeVertexShader);
    GLuint efs = compileShader(GL_FRAGMENT_SHADER, kEdgeFragmentShader);
    if (!evs || !efs)
    {
        if (evs) gl.DeleteShader(evs);
        if (efs) gl.DeleteShader(efs);
        return false;
    }
    m_edgeProgram = linkProgram(evs, efs);
    gl.DeleteShader(evs);
    gl.DeleteShader(efs);
    if (!m_edgeProgram) return false;

    m_vpLocation = gl.GetUniformLocation(m_program, "VP");
    m_modelLocation = gl.GetUniformLocation(m_program, "M");
    m_colorLocation = gl.GetUniformLocation(m_program, "color");
    m_lightLocation = gl.GetUniformLocation(m_program, "lightDir");
    m_edgeVpLocation = gl.GetUniformLocation(m_edgeProgram, "VP");
    m_edgeModelLocation = gl.GetUniformLocation(m_edgeProgram, "M");
    m_edgeColorLocation = gl.GetUniformLocation(m_edgeProgram, "color");
    return buildBuffers();
}

bool Scene3D::buildBuffers()
{
    gl.GenVertexArrays(1, &m_vao);
    gl.GenBuffers(1, &m_vbo);
    gl.GenBuffers(1, &m_ebo);
    gl.BindVertexArray(m_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STREAM_DRAW);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                           reinterpret_cast<const void*>(3 * sizeof(float)));
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STREAM_DRAW);

    gl.GenVertexArrays(1, &m_edgeVao);
    gl.GenBuffers(1, &m_edgeVbo);
    gl.GenBuffers(1, &m_edgeEbo);
    gl.BindVertexArray(m_edgeVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_edgeVbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(kEdgeVertices), kEdgeVertices, GL_STREAM_DRAW);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_edgeEbo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kEdgeIndices), kEdgeIndices, GL_STREAM_DRAW);
    m_edgeIndexCount = sizeof(kEdgeIndices) / sizeof(kEdgeIndices[0]);

    std::vector<float> grid;
    const float half = 15.0f;
    for (float z = -half; z <= half; z += 1.0f)
    {
        grid.push_back(-half); grid.push_back(0.0f); grid.push_back(z);
        grid.push_back(half); grid.push_back(0.0f); grid.push_back(z);
    }
    for (float x = -half; x <= half; x += 1.0f)
    {
        grid.push_back(x); grid.push_back(0.0f); grid.push_back(-half);
        grid.push_back(x); grid.push_back(0.0f); grid.push_back(half);
    }
    m_gridVertexCount = grid.size() / 3;
    gl.GenVertexArrays(1, &m_gridVao);
    gl.GenBuffers(1, &m_gridVbo);
    gl.BindVertexArray(m_gridVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(grid.size() * sizeof(float)), grid.data(), GL_STREAM_DRAW);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    // Dynamic line buffer used by the "Show support links" overlay. It is
    // re-filled every frame with one 3-float endpoint pair per link, so it
    // only needs a position attribute (the edge shader supplies the color).
    gl.GenVertexArrays(1, &m_linkVao);
    gl.GenBuffers(1, &m_linkVbo);
    gl.BindVertexArray(m_linkVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_linkVbo);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    gl.BindVertexArray(0);
    return true;
}

void Scene3D::shutdown()
{
    if (m_program) gl.DeleteProgram(m_program);
    if (m_edgeProgram) gl.DeleteProgram(m_edgeProgram);
    if (m_vao) gl.DeleteVertexArrays(1, &m_vao);
    if (m_vbo) gl.DeleteBuffers(1, &m_vbo);
    if (m_ebo) gl.DeleteBuffers(1, &m_ebo);
    if (m_edgeVao) gl.DeleteVertexArrays(1, &m_edgeVao);
    if (m_edgeVbo) gl.DeleteBuffers(1, &m_edgeVbo);
    if (m_edgeEbo) gl.DeleteBuffers(1, &m_edgeEbo);
    if (m_gridVao) gl.DeleteVertexArrays(1, &m_gridVao);
    if (m_gridVbo) gl.DeleteBuffers(1, &m_gridVbo);
    if (m_linkVao) gl.DeleteVertexArrays(1, &m_linkVao);
    if (m_linkVbo) gl.DeleteBuffers(1, &m_linkVbo);
    m_program = m_edgeProgram = 0;
    m_vao = m_vbo = m_ebo = 0;
    m_edgeVao = m_edgeVbo = m_edgeEbo = m_gridVao = m_gridVbo = m_linkVao = m_linkVbo = 0;
}

glm::vec3 Scene3D::eye() const
{
    const float cp = std::cos(m_pitch);
    const float sp = std::sin(m_pitch);
    const float cy = std::cos(m_yaw);
    const float sy = std::sin(m_yaw);
    const glm::vec3 dir(sy * cp, sp, cy * cp);
    return m_target + m_distance * dir;
}

glm::mat4 Scene3D::computeVP(const ViewportGeometry& viewport) const
{
    const float aspect = viewport.width / std::max(1.0f, viewport.height);
    const glm::mat4 view = glm::lookAt(eye(), m_target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 200.0f);
    return proj * view;
}

void Scene3D::orbit(float dx, float dy)
{
    m_yaw -= dx * 0.01f;
    m_pitch = std::max(-1.3f, std::min(1.3f, m_pitch + dy * 0.01f));
}

void Scene3D::pan(float dx, float dy)
{
    const glm::vec3 fwd = glm::normalize(m_target - eye());
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, fwd);
    const float scale = m_distance * 0.0025f;
    m_target += right * (-dx * scale) + up * (dy * scale);
}

void Scene3D::zoom(float factor)
{
    m_distance = std::max(3.0f, std::min(60.0f, m_distance * factor));
}

void Scene3D::drawGrid() const
{
    gl.UseProgram(m_edgeProgram);
    gl.Uniform4f(m_edgeColorLocation, 0.16f, 0.18f, 0.20f, 1.0f);
    gl.UniformMatrix4fv(m_edgeVpLocation, 1, GL_FALSE, glm::value_ptr(m_vpCache));
    const glm::mat4 identity(1.0f);
    gl.UniformMatrix4fv(m_edgeModelLocation, 1, GL_FALSE, glm::value_ptr(identity));
    gl.BindVertexArray(m_gridVao);
    gl.DrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_gridVertexCount));
    gl.BindVertexArray(0);
}

void Scene3D::render(const BlastSupportModel& model, PhysicsWorld& physics, int selectedId,
                     const ViewportGeometry& viewport, float framebufferScale)
{
    (void)framebufferScale;
    m_vpCache = computeVP(viewport);
    const glm::mat4& vp = m_vpCache;
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    drawGrid();

    gl.UseProgram(m_program);
    gl.UniformMatrix4fv(m_vpLocation, 1, GL_FALSE, glm::value_ptr(vp));
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.35f, 0.85f, 0.45f));
    gl.Uniform4f(m_lightLocation, lightDir.x, lightDir.y, lightDir.z, 1.0f);
    gl.BindVertexArray(m_vao);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    for (const NodeState& node : model.nodes())
    {
        if (node.id == 0) continue;
        if (!physics.hasBody(node.id)) continue;
        const bool standing = node.alive;
        const bool debris = !node.alive && physics.isDynamic(node.id);
        if (!standing && !debris) continue;

        const float now = static_cast<float>(ImGui::GetTime());
        if (debris)
        {
            const auto found = m_debrisBirthTime.find(node.id);
            if (found == m_debrisBirthTime.end())
                m_debrisBirthTime[node.id] = now;
            else if (now - found->second >= kDebrisLifetime)
            {
                // Hide the debris visually and remove its PhysX body so it is
                // no longer simulated or clickable.
                m_debrisBirthTime.erase(node.id);
                physics.removeBody(node.id);
                continue;
            }
        }
        else
        {
            m_debrisBirthTime.erase(node.id);
        }

        float pose[16] = {};
        if (!physics.transformFor(node.id, pose)) continue;
        const BoxLayout layout = node.box;
        const glm::mat4 poseMat = glm::make_mat4(pose);
        const glm::mat4 modelMatrix = poseMat *
            glm::scale(glm::mat4(1.0f), glm::vec3(layout.hx, layout.hy, layout.hz));

        // Color purely by structural state (alive/fallen/status); the stiffness
        // utilization pass was removed with the diagnostic stiffness layer.
        const glm::vec4 color = statusColor(node, 0.0f);
        gl.Uniform4f(m_colorLocation, color.r, color.g, color.b, color.a);
        gl.UniformMatrix4fv(m_modelLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
        gl.DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

        if (node.id == selectedId)
        {
            gl.UseProgram(m_edgeProgram);
            gl.Uniform4f(m_edgeColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
            gl.UniformMatrix4fv(m_edgeVpLocation, 1, GL_FALSE, glm::value_ptr(vp));
            gl.UniformMatrix4fv(m_edgeModelLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
            gl.BindVertexArray(m_edgeVao);
            gl.DrawElements(GL_LINES, static_cast<GLsizei>(m_edgeIndexCount), GL_UNSIGNED_INT, nullptr);
            gl.BindVertexArray(m_vao);
            gl.UseProgram(m_program);
        }

        const glm::vec4 worldTop = poseMat * glm::vec4(0.0f, layout.hy, 0.0f, 1.0f);
        const glm::vec4 clip = vp * worldTop;
        if (m_showLabels && clip.w > 0.01f)
        {
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float screenX = viewport.x + (ndc.x * 0.5f + 0.5f) * viewport.width;
            const float screenY = viewport.y + (0.5f - ndc.y * 0.5f) * viewport.height;
            const std::string label = node.name + "  L " + std::to_string(static_cast<int>(node.load));
            const ImU32 textColor = node.id == selectedId ? IM_COL32(255, 255, 255, 255)
                                                          : IM_COL32(215, 228, 228, 255);
            draw->AddText(ImVec2(screenX - 16.0f, screenY + 2.0f), textColor, label.c_str());
        }
    }

    // "Show support links": draw one line per live directed support edge
    // (edge.from depends on edge.to). Color marks the supporter: white = Ground,
    // green = column/wall, yellow = block. The line endpoints are pushed outward
    // from each member's centre past its surface, so a link floats in the gap
    // between members instead of being buried inside a solid box.
    if (m_showSupportLinks)
    {
        std::vector<float> links;
        links.reserve(model.edges().size() * 6u);
        std::vector<glm::vec3> endpoints;
        std::vector<glm::vec4> linkColors;
        std::vector<glm::vec4> endpointColors;
        for (const EdgeState& edge : model.edges())
        {
            if (edge.from <= 0 || edge.from >= static_cast<int>(model.nodes().size()) ||
                edge.to < 0 || edge.to >= static_cast<int>(model.nodes().size()))
                continue;
            const NodeState& from = model.nodes()[static_cast<size_t>(edge.from)];
            const NodeState& to = model.nodes()[static_cast<size_t>(edge.to)];
            if (!from.alive || !to.alive) continue;
            const BoxLayout a = from.box;
            const BoxLayout b = to.box;
            glm::vec3 pa(a.cx, a.cy, a.cz);
            glm::vec3 pb(b.cx, b.cy, b.cz);
            glm::vec3 dir = pb - pa;
            const float len = glm::length(dir);
            if (len < 1e-4f) continue;
            dir /= len;
            // Push each endpoint out along the direction past the member's
            // surface plus a small gap so the line is clearly visible.
            const float gap = 0.12f;
            const float rA = std::max(a.hx, std::max(a.hy, a.hz));
            const float rB = std::max(b.hx, std::max(b.hy, b.hz));
            pa += dir * (rA + gap);
            pb -= dir * (rB + gap);
            links.push_back(pa.x); links.push_back(pa.y); links.push_back(pa.z);
            links.push_back(pb.x); links.push_back(pb.y); links.push_back(pb.z);
            endpoints.push_back(pa);
            endpoints.push_back(pb);
            glm::vec4 color;
            if (to.id == 0)
                color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            else if (deriveRole(to.box) == MemberRole::VerticalBearing)
                color = glm::vec4(0.9f, 0.2f, 0.2f, 1.0f);
            else
                color = glm::vec4(0.95f, 0.85f, 0.25f, 1.0f);
            linkColors.push_back(color);
            endpointColors.push_back(color);
            endpointColors.push_back(color);
        }
        if (!links.empty())
        {
            const glm::mat4 identity(1.0f);
            gl.UseProgram(m_edgeProgram);
            gl.UniformMatrix4fv(m_edgeVpLocation, 1, GL_FALSE, glm::value_ptr(vp));
            gl.UniformMatrix4fv(m_edgeModelLocation, 1, GL_FALSE, glm::value_ptr(identity));
            gl.BindVertexArray(m_linkVao);
            gl.BindBuffer(GL_ARRAY_BUFFER, m_linkVbo);
            gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(links.size() * sizeof(float)),
                          links.data(), GL_STREAM_DRAW);
            // Draw the links on top of the boxes so they are never hidden
            // inside a solid member: disable depth testing while drawing the
            // lines, then restore it for the boxes below.
            glDisable(GL_DEPTH_TEST);
            size_t vertex = 0;
            for (size_t i = 0; i < linkColors.size(); ++i)
            {
                const glm::vec4& c = linkColors[i];
                gl.Uniform4f(m_edgeColorLocation, c.r, c.g, c.b, c.a);
                gl.DrawArrays(GL_LINES, static_cast<GLsizei>(vertex), 2);
                vertex += 2;
            }
            glEnable(GL_DEPTH_TEST);
            // Draw a small lit cube at each link endpoint so the connection
            // points stand out. Reuse the box program/VAO (unit cube scaled to
            // a small ball size) with the endpoint's colour.
            gl.UseProgram(m_program);
            gl.UniformMatrix4fv(m_vpLocation, 1, GL_FALSE, glm::value_ptr(vp));
            gl.BindVertexArray(m_vao);
            const float ballHalf = std::max(0.02f, m_linkBallSize);
            for (size_t i = 0; i < endpoints.size(); ++i)
            {
                const glm::vec3& p = endpoints[i];
                const glm::vec4& c = endpointColors[i];
                const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), p) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(ballHalf));
                gl.Uniform4f(m_colorLocation, c.r, c.g, c.b, c.a);
                gl.UniformMatrix4fv(m_modelLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
                gl.DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            }
            gl.BindVertexArray(m_vao);
            gl.UseProgram(m_program);
        }
    }

    // Visual debris: anonymous physics bodies spawned when a member broke.
    // They are not NodeState bodies, so they are drawn from PhysicsWorld's
    // fragment list instead of the structure model.
    const int fragmentCount = physics.fragmentCount();
    for (int i = 0; i < fragmentCount; ++i)
    {
        float pose[16] = {};
        if (!physics.fragmentTransformFor(i, pose)) continue;
        float hx = 0.0f, hy = 0.0f, hz = 0.0f;
        physics.fragmentExtentsFor(i, hx, hy, hz);
        const int sourceNodeId = physics.fragmentNodeIdFor(i);
        glm::vec4 color(0.32f, 0.30f, 0.28f, 1.0f);
        if (sourceNodeId >= 0 && sourceNodeId < static_cast<int>(model.nodes().size()))
            color = statusColor(model.nodes()[static_cast<size_t>(sourceNodeId)], 0.0f);
        const glm::mat4 poseMat = glm::make_mat4(pose);
        const glm::mat4 modelMatrix = poseMat *
            glm::scale(glm::mat4(1.0f), glm::vec3(hx, hy, hz));
        gl.Uniform4f(m_colorLocation, color.r, color.g, color.b, color.a);
        gl.UniformMatrix4fv(m_modelLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
        gl.DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

int Scene3D::pick(const BlastSupportModel& model, const PhysicsWorld& physics,
                  float screenX, float screenY, const ViewportGeometry& viewport,
                  float framebufferScale) const
{
    (void)framebufferScale;
    const glm::mat4 vp = computeVP(viewport);
    const glm::mat4 inverseVP = glm::inverse(vp);
    const float ndcX = (2.0f * (screenX - viewport.x)) / std::max(1.0f, viewport.width) - 1.0f;
    const float ndcY = 1.0f - (2.0f * (screenY - viewport.y)) / std::max(1.0f, viewport.height);
    const glm::vec4 nearPoint = inverseVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farPoint = inverseVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
    const glm::vec3 dir = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);

    int best = -1;
    float bestT = 1e30f;
    for (const NodeState& node : model.nodes())
    {
        if (node.id == 0) continue;
        if (!node.alive && !physics.isDynamic(node.id)) continue;
        float pose[16] = {};
        if (!physics.transformFor(node.id, pose)) continue;
        const BoxLayout layout = node.box;
        const glm::vec3 center(pose[12], pose[13], pose[14]);
        const glm::vec3 mn = center - glm::vec3(layout.hx, layout.hy, layout.hz);
        const glm::vec3 mx = center + glm::vec3(layout.hx, layout.hy, layout.hz);
        const float t = rayBox(origin, dir, mn, mx);
        if (t > 0.0f && t < bestT)
        {
            bestT = t;
            best = node.id;
        }
    }
    return best;
}
}
