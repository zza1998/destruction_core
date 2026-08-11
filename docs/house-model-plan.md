# 房子模型（墙承重）实现计划

**目标：** 在现有柱/楼板模型之外，新增独立「房子」预设：每层四面墙围合 + 楼板，层间靠墙承重，无柱。

**架构：** 复用现有 `BlastSupportModel` / `LoadPathSolver` / `PhysicsWorld` / `Scene3D`。新增 `NodeType::Wall` 和房子预设。墙在荷载求解中作为「承重件」（等同 column 角色，整层均分），渲染为薄墙板（3D）。

**技术栈：** C++14、现有 BlastSupportModel、PhysX、Scene3D、GLM。

## Chunk 1: 模型层

### Task 1: 新增墙类型与房子预设

- [ ] `NodeType` 增加 `Wall`。
- [ ] `StructuralPreset` 增加 `House3Floors`（每层 4 面墙 + 楼板）。
- [ ] `BlastSupportModel::reset()` 根据预设创建节点：
  - 柱模式：保持现有 `block + column`。
  - 房子模式：每层 4 个 `Wall`（slot 0..3 对应 北/南/东/西）+ 4 个 `Slab`（楼板）或 1 个楼板表示。
- [ ] ID 布局复用现有 `blockId/columnId`（墙 = column 角色），确保 `LoadPathSolver` 无需改动即可均分承重。
- [ ] `rebuildEdges()`：房子模式每层墙垂直相连，楼板连到墙。
- [ ] 新增回归测试：房子预设下节点数量、默认 reset 全部存活、破坏一面墙后其余墙均分增加、破坏全部墙后楼板失去支撑。

## Chunk 2: 渲染层

### Task 2: 3D 渲染墙

- [ ] `SceneLayout.h`：`Wall` 渲染为薄墙板（按 slot 确定朝向：北/南为长薄板，东/西为深薄板），`Slab` 为水平楼板。
- [ ] `Scene3D`：按 `NodeType::Wall` 与 `Slab` 区分 box 尺寸/朝向；墙间、墙-楼板拼接成房子外观。
- [ ] `PhysicsWorld`：墙创建对应 box 刚体（static/dynamic）。
- [ ] 相机初始视角能看到房子全貌。

## Chunk 3: 集成与验证

### Task 3: 预设下拉与验证

- [ ] `main.cpp` 预设下拉增加 `House 3 Floors`。
- [ ] 2D 模式：房子也按简化 2D 表示（每层 4 墙+楼板 符号）。
- [ ] `--smoke-test` 房子模式跑通。
- [ ] 构建 + 全部测试通过。
