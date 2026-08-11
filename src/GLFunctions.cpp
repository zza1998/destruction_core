#include "GLFunctions.h"

namespace blast_demo
{
GLFunctions gl;
template <typename T> bool load(T& target, const char* name)
{
    target = reinterpret_cast<T>(glfwGetProcAddress(name));
    return target != nullptr;
}
bool loadGLFunctions()
{
    bool ok = true;
#define LOAD(name) ok = load(gl.name, "gl" #name) && ok
    LOAD(GenVertexArrays); LOAD(BindVertexArray); LOAD(DeleteVertexArrays);
    LOAD(CreateShader); LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog); LOAD(CreateProgram); LOAD(AttachShader); LOAD(LinkProgram);
    LOAD(GetProgramiv); LOAD(GetProgramInfoLog); LOAD(UseProgram); LOAD(DeleteShader);
    LOAD(DeleteProgram); LOAD(GenBuffers); LOAD(BindBuffer); LOAD(BufferData);
    LOAD(DeleteBuffers); LOAD(GetUniformLocation); LOAD(Uniform1i); LOAD(UniformMatrix4fv);
    LOAD(GetAttribLocation); LOAD(Uniform4f);
    LOAD(ActiveTexture);
    LOAD(GenTextures); LOAD(BindTexture); LOAD(TexParameteri); LOAD(TexImage2D); LOAD(PixelStorei);
    LOAD(EnableVertexAttribArray); LOAD(VertexAttribPointer); LOAD(DrawElements); LOAD(DrawArrays); LOAD(Scissor);
#undef LOAD
    return ok;
}
}
