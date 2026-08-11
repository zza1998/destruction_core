#include "ImGuiOpenGLBackend.h"

#include <GLFW/glfw3.h>
#include <cstring>

namespace blast_demo
{
namespace
{
void keyCallback(GLFWwindow*, int key, int, int action, int)
{
    if (key >= 0 && key < 512)
        ImGui::GetIO().KeysDown[key] = action != GLFW_RELEASE;
}
void charCallback(GLFWwindow*, unsigned int character) { ImGui::GetIO().AddInputCharacter(character); }
GLuint shader(GLenum type, const char* source)
{
    GLuint result = gl.CreateShader(type);
    gl.ShaderSource(result, 1, &source, nullptr); gl.CompileShader(result);
    return result;
}
}

ImGuiOpenGLBackend::ImGuiOpenGLBackend()
    : m_window(nullptr), m_fontTexture(0), m_program(0), m_vao(0), m_vbo(0), m_ebo(0),
      m_textureLocation(-1), m_projectionLocation(-1) {}

bool ImGuiOpenGLBackend::init(GLFWwindow* window)
{
    m_window = window;
    if (!loadGLFunctions()) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "blast_glfw"; io.BackendRendererName = "blast_opengl3";
    glfwSetKeyCallback(window, keyCallback); glfwSetCharCallback(window, charCallback);
    const char* vertex = "#version 330 core\nlayout(location=0) in vec2 P; layout(location=1) in vec2 U; layout(location=2) in vec4 C; out vec2 uv; out vec4 col; uniform mat4 M; void main(){uv=U;col=C;gl_Position=M*vec4(P,0,1);}";
    const char* fragment = "#version 330 core\nin vec2 uv; in vec4 col; uniform sampler2D T; out vec4 O; void main(){O=col*texture(T,uv);}";
    GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
    m_program = gl.CreateProgram(); gl.AttachShader(m_program, vs); gl.AttachShader(m_program, fs); gl.LinkProgram(m_program);
    gl.DeleteShader(vs); gl.DeleteShader(fs);
    m_textureLocation = gl.GetUniformLocation(m_program, "T"); m_projectionLocation = gl.GetUniformLocation(m_program, "M");
    gl.GenVertexArrays(1, &m_vao); gl.GenBuffers(1, &m_vbo); gl.GenBuffers(1, &m_ebo);
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.GenTextures(1, &m_fontTexture); gl.BindTexture(GL_TEXTURE_2D, m_fontTexture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(m_fontTexture));
    return m_program != 0;
}

void ImGuiOpenGLBackend::newFrame()
{
    ImGuiIO& io = ImGui::GetIO(); int width = 0, height = 0; glfwGetWindowSize(m_window, &width, &height);
    int fbWidth = 0, fbHeight = 0; glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(width ? fbWidth / static_cast<float>(width) : 1.0f,
                                        height ? fbHeight / static_cast<float>(height) : 1.0f);
    double x = 0, y = 0; glfwGetCursorPos(m_window, &x, &y); io.MousePos = ImVec2(static_cast<float>(x), static_cast<float>(y));
    for (int i = 0; i < 3; ++i) io.MouseDown[i] = glfwGetMouseButton(m_window, i) == GLFW_PRESS;
    ImGui::NewFrame();
}

void ImGuiOpenGLBackend::render(ImDrawData* data)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!data || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    const float sx = 2.0f / io.DisplaySize.x, sy = 2.0f / io.DisplaySize.y;
    const float matrix[16] = {sx,0,0,0, 0,-sy,0,0, 0,0,-1,0, -1,1,0,1};
    gl.ActiveTexture(GL_TEXTURE0);
    gl.UseProgram(m_program); gl.UniformMatrix4fv(m_projectionLocation, 1, GL_FALSE, matrix); gl.Uniform1i(m_textureLocation, 0);
    gl.BindVertexArray(m_vao); gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo); gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl.EnableVertexAttribArray(0); gl.EnableVertexAttribArray(1); gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
    {
        const ImDrawList* list = data->CmdLists[listIndex];
        gl.BufferData(GL_ARRAY_BUFFER, list->VtxBuffer.Size * sizeof(ImDrawVert), list->VtxBuffer.Data, GL_STREAM_DRAW);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, list->IdxBuffer.Size * sizeof(ImDrawIdx), list->IdxBuffer.Data, GL_STREAM_DRAW);
        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback) { cmd.UserCallback(list, &cmd); continue; }
            gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(reinterpret_cast<intptr_t>(cmd.TextureId)));
            gl.Scissor(static_cast<GLint>(cmd.ClipRect.x * io.DisplayFramebufferScale.x),
                       static_cast<GLint>((io.DisplaySize.y - cmd.ClipRect.w) * io.DisplayFramebufferScale.y),
                       static_cast<GLsizei>((cmd.ClipRect.z - cmd.ClipRect.x) * io.DisplayFramebufferScale.x),
                       static_cast<GLsizei>((cmd.ClipRect.w - cmd.ClipRect.y) * io.DisplayFramebufferScale.y));
            gl.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.ElemCount), GL_UNSIGNED_SHORT,
                            reinterpret_cast<void*>(static_cast<size_t>(cmd.IdxOffset) * sizeof(ImDrawIdx)));
        }
    }
}

void ImGuiOpenGLBackend::shutdown()
{
    if (m_fontTexture) glDeleteTextures(1, &m_fontTexture);
    if (m_program) gl.DeleteProgram(m_program);
    if (m_vbo) gl.DeleteBuffers(1, &m_vbo); if (m_ebo) gl.DeleteBuffers(1, &m_ebo);
    if (m_vao) gl.DeleteVertexArrays(1, &m_vao);
}
}
