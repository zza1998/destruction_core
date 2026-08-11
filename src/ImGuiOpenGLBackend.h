#pragma once

#include "GLFunctions.h"
#include <imgui.h>

struct GLFWwindow;

namespace blast_demo
{
class ImGuiOpenGLBackend
{
public:
    ImGuiOpenGLBackend();
    bool init(GLFWwindow* window);
    void newFrame();
    void render(ImDrawData* drawData);
    void shutdown();

private:
    GLFWwindow* m_window;
    GLuint m_fontTexture;
    GLuint m_program;
    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_ebo;
    GLint m_textureLocation;
    GLint m_projectionLocation;
};
}
