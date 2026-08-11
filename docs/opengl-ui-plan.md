# OpenGL Structural Demo UI Implementation Plan

**目标：** 用 OpenGL + GLFW + Dear ImGui 替换重叠的 GDI UI，并支持 Block/Column 双类型选择与伤害。

**架构：** 保留已验证的 BlastSupportModel 求解器；新增 `damageNode` 统一模型入口。OpenGL 负责结构场景和拾取，ImGui 负责响应式 Inspector、节点状态和完整日志。GLFW 使用仓库预编译 DLL。

**技术栈：** C++14、Win32、OpenGL 3.3、GLFW 预编译 x64、Dear ImGui 1.72b 核心、Blast 5.x。

---

## Chunk 1: Model API and tests

### Task 1: Add unified node damage API

**Files:**
- Modify: `include/BlastSupportModel.h`
- Modify: `src/BlastSupportModel.cpp`
- Modify: `tests/StructuralModelTests.cpp`

- [ ] Add failing tests for valid block damage, valid column damage, invalid/Ground IDs, non-positive damage, `NaN`, positive/negative infinity, and removed-node damage; every rejected request must leave state unchanged.
- [ ] Run `build_tests.bat`; expect failure because `damageNode` is absent.
- [ ] Implement `bool damageNode(int nodeId, float amount)` with finite-positive validation, stable node-ID bounds, no mutation for Ground/removed nodes, Block HP reduction, incident-edge invalidation at zero HP, immediate support/load solver rerun, and existing column Blast fracture delegation. Its return value means only that a valid request was applied, not that the node was destroyed.
- [ ] Assert that destroying a Block disables every incident incoming and outgoing application edge.
- [ ] Make `damageColumn` resolve its ID and delegate to `damageNode`.
- [ ] Run `build_tests.bat`; expect all structural and API tests to pass.

## Chunk 2: Build wiring and OpenGL foundations

### Task 2: Wire vendored dependencies and shared GL loading

**Files:**
- Modify: `build.bat`
- Create: `src/GLFunctions.h`, `src/GLFunctions.cpp`
- Create: `src/OpenGLSmokeMain.cpp` as a temporary context-only build target

- [ ] Add GLFW and ImGui include paths using `../../flow/external/...` relative to `build.bat`; compile `imgui.cpp`, `imgui_draw.cpp`, and `imgui_widgets.cpp` directly.
- [ ] Link `../../flow/external/glfw/win64/glfw3dll.lib`, `opengl32.lib`, and required Windows libraries; copy `glfw3.dll` beside the executable.
- [ ] Add a temporary context-only target; keep the normal GDI target unchanged until Task 3 replaces `main.cpp`.
- [ ] Implement shared OpenGL 3.3 function loading through `glfwGetProcAddress`, including shader, VAO, VBO, EBO, uniform, texture, blend, and scissor entry points used by both renderers.
- [ ] Build a minimal context executable before proceeding.

## Chunk 3: OpenGL/ImGui window and backend

### Task 3: Replace GDI entry point

**Files:**
- Replace: `src/main.cpp`
- Delete from build: `src/GdiRenderer.cpp`, `include/GdiRenderer.h`
- Create: `src/ImGuiOpenGLBackend.h`, `src/ImGuiOpenGLBackend.cpp`
- Create: `src/OpenGLScene.h`, `src/OpenGLScene.cpp`
- Use: `src/GLFunctions.h`, `src/GLFunctions.cpp`

- [ ] Add a smoke command expectation: `BlastSupportGraphDemo.exe --smoke-test` initializes GLFW, creates an OpenGL 3.3 context, initializes the local ImGui backend, renders one frame, and exits 0.
- [ ] Create an OpenGL 3.3 Core Profile context and implement GLFW callbacks, DPI framebuffer scaling, ImGui frame lifecycle, and OpenGL clear/present.
- [ ] Implement minimal ImGui backend: clipboard optional, keyboard/mouse state, font texture, shader, VAO/VBO/EBO, scissor rectangles, and indexed draw commands.
- [ ] Implement an OpenGLScene shader/VAO/VBO pipeline for rectangles, lines,
  selected outlines, and color encoding using the shared GL function layer and an orthographic projection derived from pan, zoom, and viewport bounds.
- [ ] Encode safe/warning/overloaded/unsupported node colors and edge load/capacity utilization colors.
- [ ] Draw node name and HP labels with ImGui's foreground draw list inside a scene-viewport clip rectangle so labels cannot overlap the Inspector.
- [ ] Implement cursor-centered mouse-wheel zoom and middle-button scene panning.
- [ ] Convert left-click logical cursor coordinates through framebuffer scaling and screen-to-world mapping before selecting a Block or Column.
- [ ] Exclude removed nodes from both rendering and hit testing.
- [ ] Gate scene input on viewport hover and `WantCaptureMouse/Keyboard`.
- [ ] Skip scene and ImGui rendering when the framebuffer is minimized or has zero width/height.
- [ ] Build a smoke binary; expect successful one-frame exit.
- [ ] Switch the normal target to the new `main.cpp` and remove both `GdiRenderer.cpp` and temporary `OpenGLSmokeMain.cpp` from normal builds.

## Chunk 4: Inspector and interaction

### Task 4: Add responsive Inspector UI

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/OpenGLScene.cpp`
- Modify: `README.md`

- [ ] Add fixed Inspector width `clamp(logicalWidth * .30, 360, 480)` and minimum logical window size `1200x720`; assign all remaining logical width to the main viewport and make the complete Inspector internally scrollable.
- [ ] Add selected node details with HP and load/capacity, `Damage 10/35/100`, Destroy, Reset, Random Damage, Step Analysis, a filterable Block/Column node table, and full scrollable event log.
- [ ] Enable event-log auto-scroll by default and provide a checkbox to disable it while inspecting older entries.
- [ ] Add Block/Column hit-testing and explicit damage only through inspector/button or `Space`; selection alone never damages.
- [ ] Preserve `build/last_run.log` as a truncated latest-run file and refresh it after each event.
- [ ] Update README with build, runtime controls, DLL deployment, and node semantics.

### Task 5: Add coordinate and interaction tests

**Files:**
- Create: `tests/OpenGLSceneTests.cpp`
- Modify: `build_tests.bat`
- Test: `src/OpenGLScene.cpp`

- [ ] Test screen-to-world with 1x and high-DPI framebuffer scales.
- [ ] Test block and column hit detection, removed-node exclusion, and empty-space misses.
- [ ] Test selection mutates only selected ID and never changes node HP.
- [ ] Keep coordinate/hit helpers free of a live GL context so tests run headlessly.

## Chunk 5: Final verification

### Task 6: Verify Windows application

**Files:**
- Verify: `build.bat`
- Verify: `build_tests.bat`

- [ ] Confirm no GDI renderer files are compiled.
- [ ] Run structural tests and OpenGL smoke test.
- [ ] Start the normal executable for two seconds, verify it remains alive, then terminate that exact process and verify no demo process remains.
- [ ] Confirm `last_run.log` is created and only contains the current run.
