# 全重构方案：抹除类型概念，统一 mesh + 通用连接

目标：算法层面不再有 Column/Slab/Wall 类型和 slot/floor 索引约定，所有构件都是"普通 mesh（带几何包围盒的节点）"，支撑/传力/承载/破碎全部由几何与拓扑派生。

---

## 一、现状诊断（重构前必须改的三层）

### 1. 类型枚举 `NodeType`（可删，改几何派生角色）
`include/NodeTypes.h:10` 定义 `Slab/Column/Wall/Ground`，驱动：
- 传力：`LoadPathSolver` 只把 Column/Wall 当 bearer；Stiffness 块/柱弯矩用不同算法
- 承载：`material.material(type)` 定容量；`gridCapacityFor` 按层累加
- 破碎：`BlastRuntime:296` 只对 Column/Wall 断开下方支撑
- 表现：`SceneLayout.h:nodeLayout()` 按类型定包围盒尺寸；渲染/命名按类型

### 2. 索引约定 `slot/floor` 三段式 id（最深的坑，必须重构）
`include/BlastSupportModel.h:62-64` 的 `blockId/columnId/wallId`：
```cpp
1 + floor*blocks + slot                 // 块
1 + floors*blocks + floor*columns + slot // 柱/墙
```
- 柱的"上下同 slot 竖链"（`columnId(f, slot)` 直连下层）靠这个公式隐含
- grid 的"四角柱、南北墙"靠 `SceneLayout.h:38-75` 的取模/象限假设
- **只要索引体系还在，"哪段是柱、哪段是墙"就写死在公式里**

### 3. 支撑边生成（可统一，已有模板）
- `rebuildHouseEdges`（BlastSupportModel.cpp:336）已示范：**纯 BoxLayout 包围盒接触 → 支撑边**，不依赖 slot 表
- 列模型 `rebuildEdges` 和 grid `rebuildGridEdges` 仍按 slot 规则硬编码

---

## 二、目标数据模型（重构后）

### 节点：纯几何 mesh
```cpp
struct NodeState
{
    int id;                 // 仅连续编号，无角色含义
    std::string name;       // 诊断用，由几何派生（如 "mesh_12"）
    BoxLayout box;          // 几何包围盒（cx,cy,cz,hx,hy,hz）← 唯一事实来源
    // 派生属性（每次求解前由几何算，不存为枚举）：
    float mass;             // 体积×密度
    float capacity;         // 由承载语义算出（见下）
    bool grounded;          // box 底触地（y≈0）
    // 运行态：
    bool alive, supported;
    float health, load, releasedLoad;
    NodeStatus status;
};
```
- **删除 `NodeType`**，保留 `NodeStatus`（状态语义仍需要）
- **删除 `slot/floor` 字段**（仅保留 `int floor` 用于分层显示/诊断，不参与算法）

### 边：纯拓扑
```cpp
struct EdgeState { int from, to; float capacity, load; bool alive; };
```
- 由"两个 mesh 的包围盒接触"派生，不区分"柱-块""块-块"种类

### 角色：运行时从几何推导，不落枚举
```cpp
enum class MemberRole { VerticalBearing, HorizontalPlate, Ground };
// 纯函数：由 box 宽高比 + 触地 + 邻居关系推导，不入 NodeState
MemberRole deriveRole(const BoxLayout& box);
```

---

## 三、分阶段实施（每阶段可验证）

### 阶段 1：引入几何派生层（不破坏现有行为）
**目标**：新增 `GeometryDerived.h/.cpp`，提供统一 API，现有模块逐步改用它，但行为不变（对比测试锁定）。

新文件：
```cpp
// GeometryDerived.h
struct MeshInfo {
    BoxLayout box;
    float mass;                 // box 体积 × 密度
    float volume;
    bool grounded;              // minY(box) <= tol
    float extentY;              // 竖向尺度（hy）
    float extentXY;             // 水平尺度（hx,hz 较大者）
};
MemberRole deriveRole(const BoxLayout&);              // 柱/板/墙由几何比例+触地判
float deriveCapacity(const BoxLayout&, const MemberRole&);  // 承载 = 上方触压mesh 体积×容重×安全系数
std::vector<std::pair<int,int>> contactEdges(const std::vector<BoxLayout>&); // 接触→边
```

改造点：
- `NodeState` 增加 `BoxLayout box` 字段（现在 box 在 `nodeLayout()` 独立算，并入节点）
- `nodeLayout()` 重命名为 `deriveMeshInfo()`，仍按旧几何生成 box（保证这阶段行为不变）

**验证**：跑全部既有测试，行为逐字节一致。

### 阶段 2：支撑边生成统一为"接触派生"
**目标**：删掉 `rebuildEdges/rebuildGridEdges/rebuildHouseEdges` 三个，统一成一个 `rebuildEdgesFromContacts()`。

```cpp
void BlastSupportModel::rebuildEdgesFromContacts()
{
    // 对每对节点 (i,j)：box 接触判定（复用 house 的 overlap/xzContact 模板）
    //   - 上下接触（i 底面 ≈ j 顶面，水平面重叠）→ 竖向支撑边（i 由 j 撑）
    //   - 同层水平接触（cy 相近，侧向重叠）→ 双向横向边（楼板连续）
    //   - 触地（minY ≤ tol）→ 到 Ground(0)
    // 边容量由"被支撑者承载"派生
}
```
- **`SupportGraphSolver` 零改动**（它只看 edges，天然通用）

**验证**：三个预设（列/grid/house）生成的边集合与旧逻辑逐条等价。

### 阶段 3：传力/承载通用化
**目标**：删掉 `LoadPathSolver` 和 `StiffnessAssembler` 里的类型分支。

- `LoadPathSolver`：bearer 概念改为"**竖向连续到 Ground 的 mesh 链**"——用 `hasGroundPath` + `deriveRole==VerticalBearing` 找承载者，不再是 `type==Column||Wall`
- 块 load 赋值：水平传力用**空间邻接聚类**，不再用 `blockTiedColumns` 分支
- `StiffnessAssembler`：
  - 柱单元：改为"**竖向接触边**"（任何上下接触的 mesh 对），不再用 `slot%blocks` 找 head/foot 块
  - 弯矩恢复：统一用**平面框架单元端力**（从位移解恢复每根接触边的 M/V），不再 1D 梁/柱二选一
  - 超载判定：容量用 `deriveCapacity`，材料用几何尺寸（去掉 `defaultMaterialFor(type)` 的分支表）

**验证**：破坏 1 根/3 根/悬挑/简支/网格全部场景，行为与旧版一致（数值可容差）。

### 阶段 4：破碎/物理通用化
**目标**：`BlastRuntime` 和 `PhysicsWorld` 不再看类型。

- `BlastRuntime`：
  - chunk/bond 由 **mesh 面片接触**生成，不再是"每构件 8 块 + 柱墙断开下方"
  - `fractureMember` 改读 `node.box` 决定碎片数量和断开哪条接触边
- `PhysicsWorld`：按 `node.box` 生成刚体形状（box shape），不按类型
- `Scene3D/OpenGLScene`：按 `node.box` 画 box，不按类型画柱/板/墙

**验证**：3D 渲染、Blast 碎片、物理坠落正常。

### 阶段 5：删除残留 + 测试重写
**目标**：删干净，全量回归。

- 删 `NodeType` 枚举、`blockId/columnId/wallId`、`slot/floor` 字段、`isHouse/isGrid` 分支
- `MaterialModel`：`defaultMaterialFor(type)` → `materialForBox(BoxLayout)`
- 重写测试：`StructuralModelTests` 等按"几何派生角色 + 通用图"断言，不再按 `node.type`
- 2D 视图：从 `node.box` 投影，不再依赖 slot 索引

---

## 四、关键设计决策

### 4.1 角色怎么派生（不落枚举）
```cpp
MemberRole deriveRole(const BoxLayout& b) {
    if (b.hy >= 2.0f * std::max(b.hx, b.hz)) return VerticalBearing;  // 细高→柱/墙
    if (b.hy <= 0.3f * std::max(b.hx, b.hz)) return HorizontalPlate;  // 扁→楼板
    return Other;
}
```
- 柱 vs 墙不再区分（都是竖向承载），因为算法只关心"竖向传力"，不需要分
- 渲染可再细分为柱/墙（表现层），但**算法层统一**

### 4.2 容量怎么派生
```cpp
float deriveCapacity(const BoxLayout& b, float supportedVolumeAbove) {
    // 承载 = 上方压覆的 mesh 体积 × 容重 × 安全系数
    // 支持"上小下大"：底层箱体大/承载力大是几何自然结果，不再靠 gridCapacityFor 累加
    return supportedVolumeAbove * kUnitWeight * kSafety;
}
```

### 4.3 id 约定删除后，竖链靠什么？
- 旧：`columnId(f, slot)` 直连 `columnId(f-1, slot)`
- 新：上下接触的 mesh 对自动形成链（A 底面贴 B 顶面 → 边 A→B）——**无需索引**

### 4.4 好处
- **任意正交建筑直接可导入**：给每个构件一个 `BoxLayout`（或从 mesh AABB 算），角色/支撑/承载/破碎全部自动派生
- 不再需要"每层块数柱数一致""四角柱""南北墙"等约束
- 新增构件类型（如斜撑、横梁）= 新增一种 box 比例，无需改代码

---

## 五、风险与对策

| 风险 | 对策 |
|---|---|
| 接触容差导致边集漂移 | 阶段 2 用"边集合逐条等价"测试锁定；容差常数收敛到 0.05-0.1 |
| 弯矩从位移恢复病态（之前踩过坑） | 阶段 3 复用已有"位移/转角钳位"，且用接触边的端力（有物理意义） |
| 5 个模块联动难并行 | 按阶段串行，每阶段可独立跑测试；阶段 1-2 不改行为 |
| Blast 碎片逻辑重写风险高 | 阶段 4 单独做，先保持"每构件 8 块"简化，接触断开逻辑后置 |

---

## 六、交付物清单

| 产物 | 说明 |
|---|---|
| `include/GeometryDerived.h` + `src/GeometryDerived.cpp` | 几何派生层（box/角色/容量/接触边） |
| `NodeState` 重构 | 删类型/索引，加 box |
| `rebuildEdgesFromContacts` | 统一边生成 |
| `LoadPathSolver`/`StiffnessAssembler` 通用化 | 删类型分支 |
| `BlastRuntime`/`PhysicsWorld`/渲染通用化 | 按 box 工作 |
| 测试重写 + 回归矩阵 | 全部场景 |
