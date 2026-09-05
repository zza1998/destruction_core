# 刚度层设计文档（Stiffness Layer Design）

## 目标

在现有 Demo 基础上新增"刚度求解层"：用材料参数组装刚度矩阵，经 Cholesky 求节点位移，再恢复轴力/剪力/弯矩，与现有"整层均分"模式**并存可切换**。

## 已确认决策

1. 每个结构节点 3 DOF：`dx`、`dy`、`rotation`（平面内，rotation 为绕 Z 轴转角）。
2. 求解模式并存：Inspector 提供开关在 `LoadPathSolver(均分)` 与 `StiffnessSolver(刚度)` 之间切换。
3. 内力计算：轴力 + 剪力 + 弯矩都算。

## 架构

```text
MaterialModel        材料参数 + 刚度计算
StiffnessAssembler   装配全局 K、f；从位移恢复内力
SparseCholeskySolver 解 K·u = f（已存在）
StiffnessSolver      门面：协调材料/组装/求解/内力，输出超容量节点
```

```text
BlastSupportModel
  ├── LoadPathSolver      （现有，均分模式）
  └── StiffnessSolver      （新增，刚度模式）
        ├── MaterialModel
        ├── StiffnessAssembler
        └── SparseCholeskySolver
```

## 1. MaterialModel

```cpp
struct MaterialParams
{
    float youngModulus;      // E
    float density;           // 密度 → 自重
    float sectionWidth;      // 截面宽（柱/墙）
    float sectionDepth;      // 截面深
    float compressiveCapacity;
    float tensileCapacity;
    float shearCapacity;
    float momentCapacity;
};

class MaterialModel
{
public:
    // 每类构件的材料（柱/墙/楼板）
    void setMaterial(NodeType type, const MaterialParams& p);
    const MaterialParams& material(NodeType type) const;

    // 单元几何属性
    float area(NodeType type, float length) const;      // A = width*depth
    float momentOfInertia(NodeType type) const;         // I = b*h^3/12
    float axialStiffness(NodeType type, float length) const;  // EA/L
    float bendingStiffness(NodeType type, float length) const; // EI/L^3
    float mass(NodeType type, float length) const;      // density*A*L
};
```

## 2. StiffnessAssembler（3 DOF 平面杆系）

### 自由度映射

每个结构节点分配 3 个自由度：

```text
dofBase(nodeId) = nodeId * 3
  +0 : dx
  +1 : dy
  +2 : rotation
```

地面节点（Ground）固定：所有 3 DOF 施加齐次约束（对应行/列置为边界条件）。

### 单元类型

**柱/墙（竖向杆件）**：连接上层节点 i 与下层节点 j（或 Ground）。

局部 6x6 杆单元刚度（轴向 + 弯曲）：

```text
                    u_i   v_i  θ_i   u_j   v_j  θ_j
u_i   [  EA/L   0     0    -EA/L  0     0    ]
v_i   [   0   12EI/L³ 6EI/L²  0  -12EI/L³ 6EI/L²]
θ_i   [   0   6EI/L²  4EI/L   0  -6EI/L²  2EI/L ]
u_j   [ -EA/L   0     0    EA/L   0     0    ]
v_j   [   0  -12EI/L³ -6EI/L² 0  12EI/L³ -6EI/L²]
θ_j   [   0   6EI/L²  2EI/L   0  -6EI/L²  4EI/L ]
```

**楼板（水平传力）**：连接柱顶节点间的水平单元，提供面内剪切/拉压刚度（模拟楼板 diaphragm），或简化为柱间轴向杆件。

### 组装

```text
K[globalDOF x globalDOF] 稀疏
对每个单元：局部刚度 K_e → 旋转/平移到全局 → 叠加到 K[baseA..baseA+2][baseB..baseB+2]

f[globalDOF]
  每节点自重：f[dy] -= mass * g
  边界：Ground 自由度被约束
```

### 求解

```text
SparseCholeskySolver solver;
solver.addCoefficient(dof, dof, K[...]);   // 装配 K（下三角）
solver.factorize();
solver.solve(f, u);                          // u = 位移
```

（边界约束：Ground 的行/列处理——在组装时对 Ground 自由度只放对角大值，或求解后清零。）

## 3. 内力恢复

由位移差算每单元内力：

```text
单元 i→j（柱/墙，竖向）：
  轴向力   N = (EA/L) * (v_i - v_j)              // 竖向位移差
  剪力     V = (12EI/L³)(v_i - v_j) + (6EI/L²)(θ_i + θ_j)
  弯矩     M = (6EI/L²)(v_i - v_j) + (4EI/L)(θ_i) + (2EI/L)(θ_j)
```

超容量判定：

```text
|N| > compressiveCapacity  → 轴压失效
|N| > tensileCapacity      → 轴拉失效
|V| > shearCapacity        → 剪切失效
|M| > momentCapacity       → 弯矩失效
```

任一超限 → 节点标记失效（配合级联延迟）。

## 4. StiffnessSolver（门面）

```cpp
struct StiffnessResult
{
    std::vector<double> displacement;      // u
    std::vector<float> axialForce;         // 每节点单元轴力
    std::vector<float> shearForce;
    std::vector<float> moment;
    std::vector<int> overloadedNodes;      // 超容量节点
};

class StiffnessSolver
{
public:
    bool solve(const std::vector<NodeState>& nodes,
               const std::vector<EdgeState>& edges,
               int activeFloors, int activeColumns, int activeBlocks, int activeWalls,
               const MaterialModel& material,
               StiffnessResult& out) const;
};
```

- 只对存活且有 Ground 路径的构件建单元。
- 失效构件从 K 中移除（对应自由度置为弱约束）。
- 返回超容量节点，交给 BlastSupportModel 排入级联延迟。

## 5. 与现有机制配合

```text
每帧 tickAnalysis：
  if (使用均分模式)
      LoadPathSolver::route(...)          // 现有
  else
      StiffnessSolver::solve(...)         // 新增
      → overloadedNodes 排入 scheduleFail

级联延迟、hasGroundPath、Blast 碎片、物理：不变
```

## 6. Inspector 参数

```text
求解模式   Combo: 均分 / 刚度
E          滑块
截面宽     滑块
截面深     滑块
密度       滑块
抗压/拉/剪/弯矩容量   滑块（或按构件类型）
```

## 7. 文件

```text
新增：
  include/MaterialModel.h
  src/MaterialModel.cpp
  include/StiffnessAssembler.h
  src/StiffnessAssembler.cpp
  include/StiffnessSolver.h
  src/StiffnessSolver.cpp
  tests/StiffnessSolverTests.cpp
复用：
  include/SparseCholeskySolver.h
修改：
  include/BlastSupportModel.h/.cpp  （加求解模式、StiffnessSolver 调用）
  src/main.cpp                       （Inspector 参数、模式开关）
  build.bat / build_tests.bat
```

## 8. 测试

- 单柱自重：轴力 = 重力，位移 u = 自重/EA·L。
- 两柱 + 一板对称：两柱轴力相等 = 总重/2。
- 破坏一柱后重组装：另一柱轴力增大。
- 3 DOF 位移量纲/符号检查（压力为负、拉力为正）。
- 弯曲：悬臂柱端弯矩 = 力×臂长。
- 模式切换：均分与刚度结果在对称简单工况应接近。

## 9. 风险与简化

- 平面 3 DOF 假设：仅平面内，后续可扩 6 DOF（3D）。
- 楼板简化为水平传力杆：先近似。
- 弯曲自由度需要局部-全局坐标变换，实现要仔细。
- 失效构件自由度处理：弱约束而非硬删，避免矩阵奇异。
