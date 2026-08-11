#include "BlastSupportModel.h"
#include "ImGuiOpenGLBackend.h"
#include "OpenGLScene.h"
#include "PhysicsWorld.h"
#include "Scene3D.h"

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
    "5 Floors x 4 Columns", "3 Floors x 4 Columns",
    "2 Floors x 4 Columns", "5 Floors x 2 Columns", "House 3 Floors",
    "Grid 4x4 Floors 4 (+8 Walls)"};
const char* const viewModeLabels[] = {"2D", "3D"};

blast_demo::StructuralPreset presetFromIndex(int index)
{
    switch (index)
    {
    case 1: return blast_demo::StructuralPreset::Floors3Columns4;
    case 2: return blast_demo::StructuralPreset::Floors2Columns4;
    case 3: return blast_demo::StructuralPreset::Floors5Columns2;
    case 4: return blast_demo::StructuralPreset::House3Floors;
    case 5: return blast_demo::StructuralPreset::Grid4x4Floors4;
    default: return blast_demo::StructuralPreset::Floors5Columns4;
    }
}

int presetIndex(blast_demo::StructuralPreset preset)
{
    switch (preset)
    {
    case blast_demo::StructuralPreset::Floors3Columns4: return 1;
    case blast_demo::StructuralPreset::Floors2Columns4: return 2;
    case blast_demo::StructuralPreset::Floors5Columns2: return 3;
    case blast_demo::StructuralPreset::House3Floors: return 4;
    case blast_demo::StructuralPreset::Grid4x4Floors4: return 5;
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

static int runDemo(bool smoke, bool start3D)
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
    blast_demo::OpenGLScene scene2d;
    blast_demo::Scene3D scene3d;
    if (!scene3d.init())
    {
        std::fprintf(stderr, "Scene3D init failed.\n");
        backend.shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    blast_demo::PhysicsWorld physics;
    bool physicsReady = false;
    float cascadeDelay = model.cascadeDelay();
    int mode = start3D ? 1 : 0;
    int selected = -1; bool autoScroll = true; int filter = 0;
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
        if (mode == 1 && !physicsReady)
        {
            if (!enter3D()) mode = 0;
        }
        model.tickAnalysis();
        model.update(static_cast<float>(glfwGetTime()));
        if (mode == 1)
        {
            physics.syncFromModel(model);
            physics.simulate(1.0f / 60.0f);
        }
        int width = 0, height = 0, fbWidth = 0, fbHeight = 0;
        glfwGetWindowSize(window, &width, &height); glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        const float inspectorWidth = std::max(360.0f, std::min(480.0f, width * 0.30f));
        const float viewportWidth = std::max(1.0f, width - inspectorWidth);
        const float scale = io.DisplayFramebufferScale.x;
        ImGui::SetNextWindowPos(ImVec2(viewportWidth, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(inspectorWidth, static_cast<float>(height)), ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("BLAST SUPPORT ANALYSIS"); ImGui::Separator();
        if (ImGui::SliderFloat("Cascade Delay", &cascadeDelay, 0.0f, 3.0f, "%.2f s"))
            model.setCascadeDelay(cascadeDelay);
        bool showLabels = scene3d.showLabels();
        if (ImGui::Checkbox("Show node labels", &showLabels))
            scene3d.setShowLabels(showLabels);
        bool showSupportLinks = scene3d.showSupportLinks();
        if (ImGui::Checkbox("Show support links", &showSupportLinks))
        {
            scene3d.setShowSupportLinks(showSupportLinks);
            scene2d.setShowSupportLinks(showSupportLinks);
        }
        int viewMode = mode;
        if (ImGui::Combo("View mode", &viewMode, viewModeLabels, 2))
        {
            if (viewMode == 1 && !enter3D()) viewMode = 0;
            mode = viewMode;
        }
        int preset = presetIndex(model.preset());
        if (ImGui::Combo("Model preset", &preset, presetLabels, 6))
        {
            model.setPreset(presetFromIndex(preset));
            selected = -1;
            rebuildPhysicsIfReady();
            persist(model, logPath);
        }
        if (model.isHouse())
            ImGui::Text("Active structure: %d floors x %d walls (house)", model.activeFloors(), model.activeColumns());
        else if (model.isGrid())
            ImGui::Text("Active structure: %d floors x %d blocks x %d columns x %d walls (grid 4x4)", model.activeFloors(), model.activeBlocks(), model.activeColumns(), model.activeWalls());
        else
            ImGui::Text("Active structure: %d floors x %d columns", model.activeFloors(), model.activeColumns());
        if (mode == 1) ImGui::Text("Physics bodies: %d", physics.bodyCount());
        const blast_demo::NodeState* selectedNode = nodeById(model, selected);
        if (selectedNode)
        {
            ImGui::Text("%s (%s)", selectedNode->name.c_str(), selectedNode->alive ? "live" : "removed");
            ImGui::Text("Weight %.1f | load %.1f / %.1f", selectedNode->mass, selectedNode->load, selectedNode->capacity);
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
                if (node.type != blast_demo::NodeType::Ground && (filter == 0 || (filter == 1 && node.type == blast_demo::NodeType::Slab) || (filter == 2 && node.type == blast_demo::NodeType::Column) || (filter == 3 && node.type == blast_demo::NodeType::Wall)))
                    if (ImGui::Selectable((node.name + "  W " + std::to_string(static_cast<int>(node.mass)) +
                        (node.alive ? "" : "  REMOVED")).c_str(), selected == node.id)) selected = node.id;
        ImGui::EndChild();
        ImGui::Text("Event log"); ImGui::SameLine();
        if (ImGui::Button("Copy All"))
        {
            std::string all;
            for (const std::string& event : model.events()) { all += event; all += '\n'; }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        if (ImGui::BeginChild("events", ImVec2(0, 0), true))
        {
            for (const std::string& event : model.events()) ImGui::TextUnformatted(event.c_str());
            if (autoScroll) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        const bool inspectorHovered = ImGui::IsWindowHovered();
        ImGui::End();

        if (mode == 1)
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        }
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
                if (mode == 1) scene3d.orbit(static_cast<float>(dx), static_cast<float>(dy));
                else scene2d.pan(static_cast<float>(dx) / viewportWidth * 2.0f,
                                 static_cast<float>(-dy) / height * 2.0f);
            }
            lastX = io.MousePos.x;
            lastY = io.MousePos.y;
        }
        if (leftDrag && ImGui::IsMouseReleased(0))
        {
            if (sceneInput && !leftMoved)
            {
                if (mode == 1) selected = scene3d.pick(model, physics, io.MousePos.x, io.MousePos.y, viewport, scale);
                else selected = scene2d.pick(model, io.MousePos.x, io.MousePos.y, viewport, scale);
            }
            leftDrag = false;
        }
        if (sceneInput && ImGui::IsMouseDown(2))
        {
            if (!middleDrag) { middleDrag = true; lastX = io.MousePos.x; lastY = io.MousePos.y; }
            if (mode == 1) scene3d.pan(static_cast<float>(io.MousePos.x - lastX), static_cast<float>(lastY - io.MousePos.y));
            else scene2d.pan(static_cast<float>(io.MousePos.x - lastX) / viewportWidth * 2.0f,
                             static_cast<float>(lastY - io.MousePos.y) / height * 2.0f);
            lastX = io.MousePos.x; lastY = io.MousePos.y;
        }
        else middleDrag = false;
        if (sceneInput && io.MouseWheel != 0)
        {
            if (mode == 1) scene3d.zoom(io.MouseWheel > 0 ? 0.9f : 1.1f);
            else scene2d.zoomAt(io.MousePos.x, io.MousePos.y, viewport, scale, io.MouseWheel > 0 ? 1.1f : 0.9f);
        }
        if (!io.WantCaptureKeyboard && selected >= 0 && ImGui::IsKeyPressed(GLFW_KEY_SPACE)) { model.damageNode(selected, 35.0f); persist(model, logPath); }
        if (!io.WantCaptureKeyboard && (ImGui::IsKeyDown(GLFW_KEY_LEFT_CONTROL) || ImGui::IsKeyDown(GLFW_KEY_RIGHT_CONTROL)) &&
            ImGui::IsKeyPressed(GLFW_KEY_Z) && model.undoLast()) { rebuildPhysicsIfReady(); persist(model, logPath); }
        if (mode == 0) scene2d.render(model, selected, viewport, scale);
        ImGui::End();
        if (mode == 1) { ImGui::PopStyleVar(); ImGui::PopStyleColor(); }

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.055f, 0.075f, 0.085f, 1.0f);
        if (mode == 1)
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const GLsizei viewX = static_cast<GLsizei>(viewport.x * scale);
            const GLsizei viewY = static_cast<GLsizei>((height - viewport.y - viewport.height) * scale);
            const GLsizei viewW = static_cast<GLsizei>(viewport.width * scale);
            const GLsizei viewH = static_cast<GLsizei>(viewport.height * scale);
            glViewport(viewX, viewY, viewW, viewH);
            scene3d.render(model, physics, selected, viewport, scale);
        }
        else
        {
            glClear(GL_COLOR_BUFFER_BIT);
        }
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
    bool threeD = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") smoke = true;
        else if (arg == "--3d") threeD = true;
    }
    return runDemo(smoke, threeD);
}
