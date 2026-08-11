#pragma once

#include <windows.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#endif

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

namespace blast_demo
{
typedef void (APIENTRY* GLGenVertexArraysProc)(GLsizei, GLuint*);
typedef void (APIENTRY* GLBindVertexArrayProc)(GLuint);
typedef void (APIENTRY* GLDeleteVertexArraysProc)(GLsizei, const GLuint*);
typedef GLuint (APIENTRY* GLCreateShaderProc)(GLenum);
typedef void (APIENTRY* GLShaderSourceProc)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (APIENTRY* GLCompileShaderProc)(GLuint);
typedef void (APIENTRY* GLGetShaderivProc)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* GLGetShaderInfoLogProc)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY* GLCreateProgramProc)();
typedef void (APIENTRY* GLAttachShaderProc)(GLuint, GLuint);
typedef void (APIENTRY* GLLinkProgramProc)(GLuint);
typedef void (APIENTRY* GLGetProgramivProc)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* GLGetProgramInfoLogProc)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY* GLUseProgramProc)(GLuint);
typedef void (APIENTRY* GLDeleteShaderProc)(GLuint);
typedef void (APIENTRY* GLDeleteProgramProc)(GLuint);
typedef void (APIENTRY* GLGenBuffersProc)(GLsizei, GLuint*);
typedef void (APIENTRY* GLBindBufferProc)(GLenum, GLuint);
typedef void (APIENTRY* GLBufferDataProc)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* GLDeleteBuffersProc)(GLsizei, const GLuint*);
typedef GLint (APIENTRY* GLGetUniformLocationProc)(GLuint, const GLchar*);
typedef GLint (APIENTRY* GLGetAttribLocationProc)(GLuint, const GLchar*);
typedef void (APIENTRY* GLUniform1iProc)(GLint, GLint);
typedef void (APIENTRY* GLUniform4fProc)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY* GLUniformMatrix4fvProc)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY* GLActiveTextureProc)(GLenum);
typedef void (APIENTRY* GLGenTexturesProc)(GLsizei, GLuint*);
typedef void (APIENTRY* GLBindTextureProc)(GLenum, GLuint);
typedef void (APIENTRY* GLTexParameteriProc)(GLenum, GLenum, GLint);
typedef void (APIENTRY* GLTexImage2DProc)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (APIENTRY* GLPixelStoreiProc)(GLenum, GLint);
typedef void (APIENTRY* GLEnableVertexAttribArrayProc)(GLuint);
typedef void (APIENTRY* GLVertexAttribPointerProc)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRY* GLDrawElementsProc)(GLenum, GLsizei, GLenum, const void*);
typedef void (APIENTRY* GLDrawArraysProc)(GLenum, GLint, GLsizei);
typedef void (APIENTRY* GLScissorProc)(GLint, GLint, GLsizei, GLsizei);

struct GLFunctions
{
    GLGenVertexArraysProc GenVertexArrays = nullptr; GLBindVertexArrayProc BindVertexArray = nullptr;
    GLDeleteVertexArraysProc DeleteVertexArrays = nullptr; GLCreateShaderProc CreateShader = nullptr;
    GLShaderSourceProc ShaderSource = nullptr; GLCompileShaderProc CompileShader = nullptr;
    GLGetShaderivProc GetShaderiv = nullptr; GLGetShaderInfoLogProc GetShaderInfoLog = nullptr;
    GLCreateProgramProc CreateProgram = nullptr; GLAttachShaderProc AttachShader = nullptr;
    GLLinkProgramProc LinkProgram = nullptr; GLGetProgramivProc GetProgramiv = nullptr;
    GLGetProgramInfoLogProc GetProgramInfoLog = nullptr; GLUseProgramProc UseProgram = nullptr;
    GLDeleteShaderProc DeleteShader = nullptr; GLDeleteProgramProc DeleteProgram = nullptr;
    GLGenBuffersProc GenBuffers = nullptr; GLBindBufferProc BindBuffer = nullptr;
    GLBufferDataProc BufferData = nullptr; GLDeleteBuffersProc DeleteBuffers = nullptr;
    GLGetUniformLocationProc GetUniformLocation = nullptr; GLUniform1iProc Uniform1i = nullptr;
    GLGetAttribLocationProc GetAttribLocation = nullptr; GLUniform4fProc Uniform4f = nullptr;
    GLUniformMatrix4fvProc UniformMatrix4fv = nullptr; GLActiveTextureProc ActiveTexture = nullptr;
    GLGenTexturesProc GenTextures = nullptr; GLBindTextureProc BindTexture = nullptr;
    GLTexParameteriProc TexParameteri = nullptr; GLTexImage2DProc TexImage2D = nullptr;
    GLPixelStoreiProc PixelStorei = nullptr;
    GLEnableVertexAttribArrayProc EnableVertexAttribArray = nullptr;
    GLVertexAttribPointerProc VertexAttribPointer = nullptr; GLDrawElementsProc DrawElements = nullptr;
    GLDrawArraysProc DrawArrays = nullptr; GLScissorProc Scissor = nullptr;
};

extern GLFunctions gl;
bool loadGLFunctions();
}
