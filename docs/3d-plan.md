# 3D 建筑破坏 Demo 规划（PhysX + Blast）

## 目标

在现有 2D 结构分析 Demo 基础上构建 3D 版本：
- 保留现有 `BlastSupportModel` / `LoadPathSolver` 结构分析逻辑。
- 引入 PhysX 5.6.1 作为刚体物理引擎。
- 未破坏且 load 未超限时，Block/Column 保持静止（PxRigidStatic）。
- load 超限时，对应构件转为动态（PxRigidDynamic），受重力自然下落并碰撞地面/相邻构件。

## 已确认决策

- 复用现有 `blast_demo` 工程改造，2D 作为配置保留。
- PhysX 5.6.1 CPU-only 已构建完成（`physx\bin\win.x86_64.vc143.mt\release\`）。
- 渲染引入 GLM 库处理相机/矩阵。

## 架构

```text
blasting_demo/
├── src/
│   ├── main.cpp                # GLFW 主循环（2D/3D 由配置切换）
│   ├── Scene3D.cpp             # 3D 渲染：透视相机、块/柱绘制、标签
│   ├── Scene3D.h
│   ├── PhysicsWorld.cpp        # PhysX 桥接层
│   ├── PhysicsWorld.h
│   ├── BuildScene.cpp          # 由 BlastSupportModel 构建 3D 构件 + PhysX 刚体
│   └── (复用) BlastSupportModel / LoadPathSolver / BlastRuntime
├── include/
│   ├── (复用) 现有头文件
│   └── glm/                    # GLM header-only
└── build.bat                   # 增加 PhysX 库和 GLM include
```

## PhysX 桥接层设计

```cpp
struct PhysicsBody {
    PxRigidActor* actor;      // static 或 dynamic
    int nodeId;               // 对应 BlastSupportModel 节点
    bool dynamic;             // 当前是否动态
};
```

- **创建**：每个 Block/Column 创建一个 Box 形状刚体。
  - load <= capacity：`PxRigidStatic`，固定在初始位置。
  - load > capacity：转 `PxRigidDynamic`。
- **切换**：静态转动态时，用原位置创建动态 actor，`setLinearVelocity(0)`，交给 PhysX 模拟。
- **每帧**：`scene->simulate()` → `fetchResults()` → 读取 actor 变换 → 渲染。
- **落地停止**：PhysX 自动处理碰撞和睡眠（到达地面后自然停止）。

## 渲染

- OpenGL 3.3 + GLFW + ImGui（沿用现有）。
- GLM 提供 `perspective()` / `lookAt()` / 相机旋转缩放平移。
- 每个构件绘制为 Box（带 load/capacity 颜色状态）。
- 标签显示构件名和 load。
- 超载/已破坏构件用不同颜色或消失。

## 构建

```bat
# 链接
PhysX_64.lib PhysXFoundation_64.lib PhysXCommon_64.lib PhysXCooking_64.lib
PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXTask_static_64.lib
# 运行时 DLL
PhysX_64.dll PhysXFoundation_64.dll PhysXCommon_64.dll PhysXCooking_64.dll
# include
physx\include
glm/
```

## 里程碑

1. **M1 基础 3D 场景**：GLM + 透视相机渲染建筑 Box，2D 分析逻辑复用。
2. **M2 PhysX 集成**：创建静态刚体，验证 load ok 时静止。
3. **M3 动态切换**：超载转动态，下落碰撞地面。
4. **M4 交互**：点击破坏、Undo、日志、UI。

## 待确认

- GLM 引入来源（工作区无 GLM，需要从 GitHub 下载或改用 PhysX 自带数学库）。
