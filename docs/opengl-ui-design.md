# OpenGL Structural Demo UI Design

## Goal

Replace the fixed-coordinate GDI interface with an OpenGL + GLFW + Dear ImGui
application that can select and damage both block and column nodes without UI
overlap.

## Dependencies

- GLFW from `../../flow/external/glfw`, resolved relative to `blast_demo/build.bat`
- Dear ImGui core from `../../flow/external/imgui`, resolved relative to the build script
- A minimal GLFW/OpenGL ImGui backend implemented inside this demo because the
  vendored ImGui 1.72b has no backend sources and is not a docking build
- OpenGL functions required by the renderer are loaded explicitly from
  `glfwGetProcAddress`; no additional loader package is required
- OpenGL 3.3 core on Windows
- Existing Blast SDK and `BlastSupportModel`

## Layout

- Main viewport: orthographic structural scene rendered with OpenGL and assigned
  all logical window width left after the Inspector.
- Inspector: fixed ImGui panel with logical width
  `clamp(windowLogicalWidth * 0.30, 360, 480)` containing selection details, damage
  controls, node filters/table, simulation controls, and full event history.
- ImGui computes panel sizing from the framebuffer dimensions; the Inspector is
  internally scrollable and the GLFW logical window has a minimum size of
  1200x720. Framebuffer dimensions are used only for OpenGL viewport/scissor.

## Scene Interaction

- Blocks and columns each have a world-space rectangle and stable node ID.
- Left click performs rectangle hit testing after screen-to-world conversion.
- GLFW logical cursor coordinates are converted using the window-to-framebuffer
  scale before viewport and world conversion, including high-DPI displays.
- Selected nodes receive a visible outline.
- Mouse wheel zooms around the cursor; middle drag pans the scene.
- Removed nodes are not rendered or hit-testable.
- Scene input is accepted only while the scene viewport is hovered and ImGui is
  not capturing mouse/keyboard input. Minimized/zero-size framebuffers skip rendering.

## Damage

- Add `bool damageNode(nodeId, amount)` to the model. `true` means the request
  was valid and damage was applied; it does not mean the node was destroyed.
  Destruction is read from the resulting node state. Node IDs equal stable
  `nodes()` indices in this demo.
- Reject invalid IDs, Ground, removed nodes, non-finite amounts, and amounts
  less than or equal to zero without mutating state.
- Column damage preserves the existing direct-damage and Blast bond fracture
  path. Existing `damageColumn` delegates to `damageNode` after resolving its ID.
- Block damage reduces block HP; at zero, all incoming and outgoing application edges become
  inactive and the support/load solver reruns.
- Inspector buttons apply 10, 35, or 100 damage to the selected node.
- Direct scene interaction selects only; damage is explicit to avoid accidental
  edits while panning or inspecting.

## Visual Encoding

- Safe: green.
- Warning: amber.
- Overloaded: orange.
- Unsupported/broken: red before removal; removed nodes are hidden.
- Edges use load/capacity utilization color where available.
- Labels are drawn through ImGui's foreground draw list and display node name
  and HP only in the viewport; detailed load/capacity
  information remains in the inspector.

## Logging

- ImGui child window displays the complete current-run log with vertical scroll.
- Auto-scroll is optional and enabled by default.
- `build/last_run.log` remains the persisted copy of the latest run only.

## Verification

- Existing structural model tests must continue to pass.
- Add tests that direct damage targets a block or column by node ID and that a
  destroyed block disables its incident application edges.
- Add pure-function tests for node hit testing and screen-to-world conversion,
  including framebuffer scale and explicit selection-without-damage behavior.
- Build the OpenGL executable and run a startup smoke test.
- Add `--smoke-test`: create an OpenGL 3.3 context, initialize the minimal ImGui
  backend, render one frame, and exit with status 0.
- Link the existing x64 import library at
  `../../flow/external/glfw/win64/glfw3dll.lib`, copy `glfw3.dll` next to the
  executable, and link `opengl32.lib`. This avoids CRT mismatches from the
  prebuilt static library. The executable must not compile or link
  `GdiRenderer.cpp`; GLFW may still use GDI internally.
