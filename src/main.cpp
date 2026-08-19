#include "BlastSupportModel.h"
#include "GeometryDerived.h"
#include "ImGuiOpenGLBackend.h"
#include "PhysicsWorld.h"
#include "Scene3D.h"
#include "SceneTypes.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace
{
const char* const presetLabels[] = {
    "5 Floors x 4 Columns", "Shear Row (2 col x 4 plate)", "Grid 4x4 Floors 4 (+8 Walls)"};

blast_demo::StructuralPreset presetFromIndex(int index)
{
    switch (index)
    {
    case 1: return blast_demo::StructuralPreset::ShearPair;
    case 2: return blast_demo::StructuralPreset::Grid4x4Floors4;
    default: return blast_demo::StructuralPreset::Floors5Columns4;
    }
}

int presetIndex(blast_demo::StructuralPreset preset)
{
    switch (preset)
    {
    case blast_demo::StructuralPreset::ShearPair: return 1;
    case blast_demo::StructuralPreset::Grid4x4Floors4: return 2;
    default: return 0;
    }
}

void persist(const blast_demo::BlastSupportModel& model, const std::string& path)
{
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    for (const std::string& event : model.events()) file << event << '\n';
}
const blast_demo::NodeState* nodeById(const blast_demo::BlastSupportModel& model, int id)
{
    return id >= 0 && id < static_cast<int>(model.nodes().size()) ? &model.nodes()[id] : nullptr;
}
std::string executableLogPath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string result(path);
    const size_t slash = result.find_last_of("\\/");
    return result.substr(0, slash + 1) + "last_run.log";
}
}

static int runDemo(bool smoke)
{
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1200, 720, "Blast Support Graph Demo", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    blast_demo::ImGuiOpenGLBackend backend;
    ImGui::CreateContext(); ImGui::StyleColorsDark();
    glfwSetWindowSizeLimits(window, 1200, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
    ImGuiIO& io = ImGui::GetIO(); io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    if (!backend.init(window)) { ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 1; }
    blast_demo::BlastSupportModel model;
    std::string logPath = executableLogPath();
    persist(model, logPath);
    blast_demo::Scene3D scene3d;
    if (!scene3d.init())
    {
        std::fprintf(stderr, "Scene3D init failed.\n");
        backend.shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    blast_demo::PhysicsWorld physics;
    bool physicsReady = false;
    float cascadeDelay = model.cascadeDelay();
    int selected = -1; bool autoScroll = false; int filter = 0;
    bool middleDrag = false; bool leftDrag = false; bool leftMoved = false;
    double lastX = 0, lastY = 0; double leftStartX = 0, leftStartY = 0;
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double y) { ImGui::GetIO().MouseWheel = static_cast<float>(y); });

    const auto rebuildPhysicsIfReady = [&]() { if (physicsReady) physics.rebuild(model); };
    const auto enter3D = [&]() -> bool {
        if (!physicsReady)
        {
            if (!physics.init())
            {
                std::fprintf(stderr, "PhysicsWorld init failed.\n");
                return false;
            }
            physicsReady = true;
        }
        physics.rebuild(model);
        return true;
    };

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents(); backend.newFrame();
        if (!physicsReady)
        {
            if (!enter3D()) break;
        }
        model.tickAnalysis();
        model.update(static_cast<float>(glfwGetTime()));
        physics.syncFromModel(model);
        physics.simulate(1.0f / 60.0f);
        int width = 0, height = 0, fbWidth = 0, fbHeight = 0;
        glfwGetWindowSize(window, &width, &height); glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        float inspectorWidth = std::max(320.0f, std::min(480.0f, width * 0.30f));
        ImGui::SetNextWindowPos(ImVec2(std::max(1.0f, width - inspectorWidth), 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(inspectorWidth, static_cast<float>(height)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 320), ImVec2(static_cast<float>(width) * 0.95f, static_cast<float>(height)));
        const float scale = io.DisplayFramebufferScale.x;
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        inspectorWidth = ImGui::GetWindowWidth();
        const float viewportWidth = std::max(1.0f, width - inspectorWidth);
        ImGui::Text("BLAST SUPPORT ANALYSIS"); ImGui::Separator();
        if (ImGui::BeginTabBar("InspectorTabs"))
        {
            if (ImGui::BeginTabItem("Analysis"))
            {
                if (ImGui::BeginChild("analysisChild", ImVec2(0, 0), true))
                {
        if (ImGui::SliderFloat("Cascade Delay", &cascadeDelay, 0.0f, 3.0f, "%.2f s"))
            model.setCascadeDelay(cascadeDelay);
        // Structural lateral (shear) failure threshold — single source for the
        // real failure criterion (LoadPathSolver drives axial, detectLateralShear
        // drives plate shear against per-node shearCapacity).
        float plateShear = model.config().plateShearCapacity;
        if (ImGui::SliderFloat("Shear cap (load)", &plateShear, 50.0f, 5000.0f, "%.0f"))
            model.setPlateShearCapacity(plateShear);
        static const char* overhangLabels[] = {"Small", "Medium", "Large"};
        static int overhangLevel = 1;   // medium default, persists across frames
        const float f = model.config().plateOverhangFactor;
        if (f >= 4.0f) overhangLevel = 2;
        else if (f >= 2.0f) overhangLevel = 1;
        else overhangLevel = 0;
        if (ImGui::Combo("Cantilever tolerance", &overhangLevel, overhangLabels, 3))
            model.setPlateOverhang(static_cast<blast_demo::BlastSupportModel::PlateOverhang>(overhangLevel));
        static const char* redundancyLabels[] = {"Small", "Medium", "Large"};
        static int redundancyLevel = 2;   // large default (1.0)
        const float red = model.config().overloadFailureRatio;
        if (red <= 0.75f) redundancyLevel = 0;         // small (0.7)
        else if (red <= 0.9f) redundancyLevel = 1;     // medium (0.85)
        else redundancyLevel = 2;                      // large (1.0)
        if (ImGui::Combo("Column strength", &redundancyLevel, redundancyLabels, 3))
            model.setColumnStrength(static_cast<blast_demo::BlastSupportModel::ColumnStrength>(redundancyLevel));
        bool showLabels = scene3d.showLabels();
        if (ImGui::Checkbox("Show node labels", &showLabels))
            scene3d.setShowLabels(showLabels);
        bool showSupportLinks = scene3d.showSupportLinks();
        if (ImGui::Checkbox("Show support links", &showSupportLinks))
        {
            scene3d.setShowSupportLinks(showSupportLinks);
        }
        if (showSupportLinks)
        {
            float ballSize = scene3d.linkBallSize();
            if (ImGui::SliderFloat("Link ball size", &ballSize, 0.02f, 0.6f, "%.2f m"))
                scene3d.setLinkBallSize(ballSize);
        }
        int preset = presetIndex(model.preset());
        if (ImGui::Combo("Model preset", &preset, presetLabels, 3))
        {
            model.setPreset(presetFromIndex(preset));
            selected = -1;
            rebuildPhysicsIfReady();
            persist(model, logPath);
        }
        if (model.isGrid())
            ImGui::Text("Active structure: %d floors x %d blocks x %d columns x %d walls (grid 4x4)", model.activeFloors(), model.activeBlocks(), model.activeColumns(), model.activeWalls());
        else
            ImGui::Text("Active structure: %d floors x %d columns", model.activeFloors(), model.activeColumns());
        ImGui::Text("Physics bodies: %d", physics.bodyCount());
        const blast_demo::NodeState* selectedNode = nodeById(model, selected);
        if (selectedNode)
        {
            ImGui::Text("%s (%s)", selectedNode->name.c_str(), selectedNode->alive ? "live" : "removed");
            ImGui::Text("Weight %.1f | load %.1f / %.1f", selectedNode->mass, selectedNode->load, selectedNode->capacity);
            // Lateral (shear) force in load units and its share of the shear
            // capacity; !SHEAR when it exceeds it and the plate would shear-fail.
            const float slat = selectedNode->lateralShear;
            const bool sover = slat > selectedNode->shearCapacity;
            ImGui::Text("Lateral shear %.1f / %.1f%s", slat, selectedNode->shearCapacity,
                        sover ? "   !SHEAR" : "");
            if (selectedNode->alive && ImGui::Button("Destroy Selected"))
            {
                model.damageNode(selected, 100000.0f);
                persist(model, logPath);
            }
        }
        else ImGui::Text("Select a block or column in the viewport.");
        if (ImGui::Button("Reset")) { model.reset(); selected = -1; rebuildPhysicsIfReady(); persist(model, logPath); }
        ImGui::SameLine();
        if (ImGui::Button("Undo Last") && model.undoLast()) { rebuildPhysicsIfReady(); persist(model, logPath); }
        if (ImGui::Button("Step Analysis")) { model.tickAnalysis(); persist(model, logPath); }
        ImGui::Separator(); ImGui::Text("Nodes");
        ImGui::RadioButton("All", &filter, 0); ImGui::SameLine(); ImGui::RadioButton("Block", &filter, 1); ImGui::SameLine(); ImGui::RadioButton("Column", &filter, 2); ImGui::SameLine(); ImGui::RadioButton("Wall", &filter, 3);
        if (ImGui::BeginChild("nodes", ImVec2(0, 190), true))
            for (const blast_demo::NodeState& node : model.nodes())
                if (node.id != 0 && (filter == 0 || (filter == 1 && blast_demo::deriveRole(node.box) == blast_demo::MemberRole::HorizontalPlate) || ((filter == 2 || filter == 3) && blast_demo::deriveRole(node.box) == blast_demo::MemberRole::VerticalBearing)))
                {
                    // Each member shows its lateral (shear) force in load units,
                    // its shear capacity, and a !SHEAR flag when over capacity.
                    const float lat = node.lateralShear;
                    const bool over = lat > node.shearCapacity;
                    if (ImGui::Selectable(
                            (node.name + "  W " + std::to_string(static_cast<int>(node.mass)) +
                             "  V " + (lat > 0.0f ? std::to_string(static_cast<int>(lat)) : "0") +
                             "/" + std::to_string(static_cast<int>(node.shearCapacity)) +
                             (over ? "  !SHEAR" : "") + (node.alive ? "" : "  REMOVED")).c_str(),
                            selected == node.id))
                        selected = node.id;
                }
        ImGui::EndChild();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log"))
            {
                if (ImGui::BeginChild("logChild", ImVec2(0, 0), true))
                {
        if (ImGui::Button("Copy All"))
        {
            std::string all;
            for (const std::string& event : model.events()) { all += event; all += '\n'; }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& event : model.events()) ImGui::TextWrapped("%s", event.c_str());
        ImGui::PopTextWrapPos();
        if (autoScroll) ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        const bool inspectorHovered = ImGui::IsWindowHovered();
        ImGui::End();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(viewportWidth, static_cast<float>(height)), ImGuiCond_Always);
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        ImGui::InvisibleButton("scene", ImGui::GetContentRegionAvail()); const bool hovered = ImGui::IsItemHovered();
        const bool sceneInput = hovered && !inspectorHovered;
        const ImVec2 origin = ImGui::GetItemRectMin();
        blast_demo::ViewportGeometry viewport = {origin.x, origin.y, viewportWidth, static_cast<float>(height)};
        if (sceneInput && ImGui::IsMouseClicked(0))
        {
            leftDrag = true;
            leftMoved = false;
            leftStartX = lastX = io.MousePos.x;
            leftStartY = lastY = io.MousePos.y;
        }
        if (sceneInput && leftDrag && ImGui::IsMouseDown(0))
        {
            const double dx = io.MousePos.x - lastX;
            const double dy = io.MousePos.y - lastY;
            if ((io.MousePos.x - leftStartX) * (io.MousePos.x - leftStartX) +
                (io.MousePos.y - leftStartY) * (io.MousePos.y - leftStartY) > 16.0)
                leftMoved = true;
            if (leftMoved)
            {
                scene3d.orbit(static_cast<float>(dx), static_cast<float>(dy));
            }
            lastX = io.MousePos.x;
            lastY = io.MousePos.y;
        }
        if (leftDrag && ImGui::IsMouseReleased(0))
        {
            if (sceneInput && !leftMoved)
            {
                selected = scene3d.pick(model, physics, io.MousePos.x, io.MousePos.y, viewport, scale);
            }
            leftDrag = false;
        }
        if (sceneInput && ImGui::IsMouseDown(2))
        {
            if (!middleDrag) { middleDrag = true; lastX = io.MousePos.x; lastY = io.MousePos.y; }
            scene3d.pan(static_cast<float>(io.MousePos.x - lastX), static_cast<float>(lastY - io.MousePos.y));
            lastX = io.MousePos.x; lastY = io.MousePos.y;
        }
        else middleDrag = false;
        if (sceneInput && io.MouseWheel != 0)
        {
            scene3d.zoom(io.MouseWheel > 0 ? 0.9f : 1.1f);
        }
        if (!io.WantCaptureKeyboard && selected >= 0 && ImGui::IsKeyPressed(GLFW_KEY_SPACE)) { model.damageNode(selected, 35.0f); persist(model, logPath); }
        if (!io.WantCaptureKeyboard && (ImGui::IsKeyDown(GLFW_KEY_LEFT_CONTROL) || ImGui::IsKeyDown(GLFW_KEY_RIGHT_CONTROL)) &&
            ImGui::IsKeyPressed(GLFW_KEY_Z) && model.undoLast()) { rebuildPhysicsIfReady(); persist(model, logPath); }
        ImGui::End();
        ImGui::PopStyleVar(); ImGui::PopStyleColor();

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.055f, 0.075f, 0.085f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const GLsizei viewX = static_cast<GLsizei>(viewport.x * scale);
        const GLsizei viewY = static_cast<GLsizei>((height - viewport.y - viewport.height) * scale);
        const GLsizei viewW = static_cast<GLsizei>(viewport.width * scale);
        const GLsizei viewH = static_cast<GLsizei>(viewport.height * scale);
        glViewport(viewX, viewY, viewW, viewH);
        scene3d.render(model, physics, selected, viewport, scale);
        ImGui::Render();
        glViewport(0, 0, fbWidth, fbHeight);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_SCISSOR_TEST);
        backend.render(ImGui::GetDrawData()); glfwSwapBuffers(window);
        if (smoke) break;
    }
    scene3d.shutdown(); physics.shutdown(); backend.shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return 0;
}

int main(int argc, char** argv)
{
    bool smoke = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") smoke = true;
    }
    return runDemo(smoke);
}
