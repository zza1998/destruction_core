#pragma once

namespace blast_demo
{
// Viewport rectangle in screen pixel coordinates (origin top-left). Shared by
// Scene3D and main.cpp for hit-testing/render sizing; the old 2D scene
// (OpenGLScene) no longer exists.
struct ViewportGeometry
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
}
