# Static Gravity Solver Integration Implementation Plan

> **For agentic workers:** REQUIRED: Use booming-code:subagent-driven-development (if subagents available) or booming-code:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the preset-specific whole-storey mass sharing path with a deterministic, geometry-derived static gravity solver that re-routes load after direct member failure, releases unsupported components, and fails overloaded or eccentric vertical bearings.

**Architecture:** Keep `NodeState` as the current renderable/fracturable structural piece and `EdgeState` as a directed possible load-transfer connection. Add a pure `StaticGravitySolver` that derives an acyclic routing rank from live directed edges, carries mass and weighted horizontal center of mass down those edges, and reports unsupported nodes plus capacity failures without mutating the model. `BlastSupportModel` remains the state owner: it applies direct break, schedules or executes returned failures, sends Blast fracture requests, and lets `PhysicsWorld` convert confirmed released nodes to dynamic bodies.

**Technical Stack:** C++14, existing `NodeState`/`EdgeState`, `BoxLayout` geometry helpers, `BlastSupportModel`, Blast Toolkit, PhysX, standalone C++ test executable built by `build_tests.bat`.

**Reference specification:** `docs/static-gravity-structural-destruction-design.md`

---

## Current-Code Assessment

The current `LoadPathSolver::route` (`src/LoadPathSolver.cpp`) derives floor levels from vertical contacts, then assigns one whole-storey total to every surviving bearer in proportion to footprint area. It cannot retain a load's center of mass, distinguish a configured share from geometry area, or evaluate an overhang. Its fallback support logic and `BlastSupportModel::detectLateralShear` (`src/BlastSupportModel.cpp`) are specifically for plate shear relays; those are out of scope for the static-gravity design and must not drive first-version failure.

The present data model is usable without a disruptive asset migration:

- A `NodeState` continues to be an authored chunk that has mass, geometry, a Blast representation, and current capacity.
- A directed `EdgeState` continues to mean "load can move from `from` to its support `to`." Contact creation already supplies vertical down edges and bidirectional horizontal contact edges.
- A vertical bearing's capacity remains attached to that node for this migration. It is evaluated from the aggregate load entering and passing through the bearing node, not from the edge.
- `NodeStatus::Unsupported` remains the released-to-physics state; a new `NodeStatus::Overhanging` is not required because an overhang is an overload reason, not a separate state.

The first integration deliberately does not add a stair preset or a `Stair` node type. The requirement "stairs do not bear load" is satisfied by authoring stairs outside `m_nodes`/`m_edges` as visual/physics attachments. A later stair system must not add stair contact edges to the static solver until its separate `StructuralBrace` feature is designed.

## Target Algorithm

For each analysis wave, operate only on `alive` nodes and live edges whose endpoints are alive.

1. Compute `distanceToGround` for every node by reverse BFS from Ground (`0`) across directed live edges with a strictly positive `shareWeight`. Ground has distance `0`; a node with no distance is unsupported. Sort IDs whenever enqueuing/traversing so results are deterministic. An edge with zero or negative sharing weight is not a support edge for any purpose, which prevents a node that cannot transfer mass from being treated as a valid lower route.
2. Do not use raw geometry height as the routing key. A horizontal relay must be allowed when it shortens the directed path to Ground. Instead, use `distanceToGround`: an outgoing edge is a valid downward route only when `distance[to] < distance[from]`. This removes cycles and allows a plate over a broken column to route through a same-storey neighbor that has a shorter live route to Ground.
3. Initialize each supported node's accumulator with its own mass and horizontal center `(box.cx, box.cz)`. Process nodes in descending distance, then descending ID only as a deterministic tie-breaker. For each node, split its accumulated mass among valid outgoing edges by `EdgeState::shareWeight`; send both mass and mass-weighted X/Z moment to the support node.
4. After receiving all higher-node transfers, a node's `carriedMass` is its load. A vertical bearing gets compression utilization `carriedMass / capacity` and bending utilization `horizontalDistance(carriedCOM, box center) / maxOverhang`; a plate/other node is not made to fail by axial or bending capacity in this version.
5. The combined utilization is `compression + bending`. All overloads from one solve are returned as a batch. The controller applies that batch, rebuilds analysis state, and repeats. This removes visit-order-dependent cascades.

For compatibility with the demo's existing mass-unit capacities, `gravity` is not multiplied into the comparison: `capacity` remains "supported mass." The public diagnostics can report `N = carriedMass * 9.81f` and `M = N * eccentricity`; the pass/fail test stays in mass and meters. This makes the migration preserve existing tuning while meeting the design document's force interpretation.

### Locked Integration Decisions

- `capacity` remains an authored mass capacity in this migration. `StructuralConfig::lowerColumnCapacity`, `upperColumnCapacity`, and `gridCapacityFor()` keep producing this same unit. `overloadFailureRatio` is currently unused by the active solver and remains unused; do not silently introduce it into the new criterion. `plateShearCapacity` and `ShearPair` remain public compatibility data/preset, but no longer produce structural failures.
- `shareWeight` is initialized to `1.0f` for every contact edge. It replaces the active area-weighted sharing rule; a later authoring pass may override it per member without changing the solver.
- `maxOverhang` is `0` for all migrated existing nodes, meaning bending is disabled for legacy presets until a designer authors a value. This avoids changing their collapse tuning accidentally. The inverted-L scenario is validated in the pure solver, not forced into an unrelated existing preset.
- Direct breaks occur immediately. Nodes reported unsupported are immediately failed as one deterministic batch in `tickAnalysis`, then the graph is solved again. Overloaded nodes are queued as one deterministic failure wave at `m_currentTime + m_cascadeDelay`; all members in that wave execute together when `update()` reaches its due time, then a new solve starts. There is no top-down filter or intra-wave stagger in this mode.
- The maximum cascade count is `64`. If the limit is reached, every still-live node in the most recently unresolved supported component is failed as `Unsupported`, and the model emits one `Cascade guard reached; released unresolved components.` event. This is an explicit safety fallback, not a successful analysis outcome.
- `PhysicsWorld::syncFromModel()` converts a body only when its node is confirmed `alive == false`; it remains idempotent because it skips bodies already marked dynamic. `Unsupported` is always confirmed with `alive == false` before physics synchronization. A member that is Blast-fractured has its original body replaced with fragments; an unsupported intact component receives a dynamic whole-node body and no fracture spawn.

## File Structure

| File | Responsibility |
|---|---|
| `include/NodeTypes.h` | Add explicit per-node static-gravity authoring and diagnostic fields, and edge share weight. |
| `include/StaticGravitySolver.h` | Define the pure solver result and solver API. No Blast, PhysX, UI, or preset dependency. |
| `src/StaticGravitySolver.cpp` | Implement reachability, deterministic route selection, mass/COM accumulation, and utilization checks. |
| `src/BlastSupportModel.cpp` | Initialize defaults from existing geometry/capacity, replace old analysis driver, apply solver results and cascades, and retire shear-driven failure from this mode. |
| `include/BlastSupportModel.h` | Own the new solver and expose only minimal static-gravity configuration needed by the model. |
| `tests/StaticGravitySolverTests.cpp` | Unit-test arbitrary small graphs without Blast or PhysX. |
| `tests/StructuralModelTests.cpp` | Replace obsolete storey/shear assumptions with end-to-end model tests that validate integration, scheduling, and release. |
| `include/ContactEdges.h` | Explicitly initialize every generated contact edge's `shareWeight` to `1.0f`. |
| `tests/PhysicsSmokeTests.cpp` | Validate confirmed release, fragmentation, and idempotent PhysX synchronization. |
| `build_tests.bat` | Compile and execute pure solver and PhysX smoke tests with their required source/link inputs. |

## Chunk 1: Pure Static Solver and Data Contract

### Task 1: Add only the data required by static routing

**Files:**
- Modify: `include/NodeTypes.h:11-59`
- Create: `include/StaticGravitySolver.h`

- [ ] **Step 1: Write failing pure-solver tests before changing production data**

Create `tests/StaticGravitySolverTests.cpp` with a minimal graph builder that assigns node IDs matching vector indices. Define these initial tests against the planned API:

```cpp
StaticGravitySolver solver;
StaticGravityResult result = solver.solve(nodes, edges);

if (!check(result.nodes[1].supported, "grounded support is not supported")) return 1;
if (!check(nearlyEqual(result.nodes[1].carriedMass, 15.0f),
           "column did not receive its own and platform mass")) return 1;
if (!check(result.overloadedNodes.empty(), "under-capacity column overloaded")) return 1;
```

Use an explicit graph builder in the test file rather than aggregate initializers. It must create Ground (`id=0`, alive), a `VerticalBearing` root (`hx=hz=0.5f`, `hy=2.0f`), and a `HorizontalPlate` platform (`hx=1.0f`, `hy=0.1f`, `hz=1.0f`); it must set every endpoint's `alive`, `mass`, `capacity`, `BoxLayout`, and `maxOverhang` by named assignment. Add a two-column centered platform case that asserts each column carries half the platform mass plus its own mass, a disconnected component case that asserts every disconnected node is returned in `unsupportedNodes`, and an invalid graph case that ignores broken edges and non-positive share weights.

- [ ] **Step 2: Compile the test to establish the expected failure**

Temporarily add this command to `build_tests.bat` after the existing pure tests:

```bat
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /Fe:"%OUT%\StaticGravitySolverTests.exe" "%DEMO_DIR%tests\StaticGravitySolverTests.cpp" "%DEMO_DIR%src\StaticGravitySolver.cpp"
if errorlevel 1 exit /b 1
"%OUT%\StaticGravitySolverTests.exe"
if errorlevel 1 exit /b 1
```

Run: `build_tests.bat`

Expected: compilation fails because `StaticGravitySolver.h` and `StaticGravitySolver.cpp` do not exist.

- [ ] **Step 3: Extend the shared state without changing existing behavior**

In `include/NodeTypes.h`, add fields with safe defaults:

```cpp
struct NodeState {
    // Existing fields remain unchanged.
    float maxOverhang = 0.0f;       // 0 means ignore bending in v1.
    float carriedMass = 0.0f;       // Solver diagnostic, includes own mass.
    float carriedComX = 0.0f;       // Solver diagnostic.
    float carriedComZ = 0.0f;
    float compressionUtilization = 0.0f;
    float bendingUtilization = 0.0f;
    float utilization = 0.0f;
};

struct EdgeState {
    // Existing fields remain unchanged.
    float shareWeight = 1.0f;
};
```

Do not rely on aggregate initialization compatibility for this migration. Convert the existing positional `NodeState` and `EdgeState` initializers in `tests/StructuralModelTests.cpp`, `src/BlastSupportModel.cpp`, and contact-edge helpers to named assignment helpers before adding fields. This avoids compiler-specific C++14 behavior around default member initializers and ensures `shareWeight` is always explicitly initialized to `1.0f` by production edge construction.

Create `include/StaticGravitySolver.h`:

```cpp
#pragma once

#include "NodeTypes.h"
#include <vector>

namespace blast_demo {
struct StaticGravityNodeResult {
    bool supported = false;
    float carriedMass = 0.0f;
    float carriedComX = 0.0f;
    float carriedComZ = 0.0f;
    float compressionUtilization = 0.0f;
    float bendingUtilization = 0.0f;
    float utilization = 0.0f;
};
struct StaticGravityResult {
    std::vector<StaticGravityNodeResult> nodes;
    std::vector<int> distanceToGround;
    std::vector<float> edgeTransferredMass;
    std::vector<int> unsupportedNodes;
    std::vector<int> overloadedNodes;
};
class StaticGravitySolver {
public:
    StaticGravityResult solve(const std::vector<NodeState>& nodes,
                              const std::vector<EdgeState>& edges) const;
};
}
```

- [ ] **Step 4: Implement deterministic support and gravity routing**

Create `src/StaticGravitySolver.cpp`. The implementation must:

- Validate `edge.from` and `edge.to` indices before using them.
- Build reverse adjacency from live edges only when both endpoints are alive.
- BFS from node `0`, visiting predecessor IDs in ascending order, to calculate `distanceToGround`.
- Add every alive non-ground node whose distance is `-1` to `unsupportedNodes` in ascending ID order.
- Initialize each supported non-ground node with `mass`, `box.cx`, and `box.cz`; Ground receives no own mass.
- Process supported nodes in decreasing distance then decreasing ID. Select outgoing live edges only when their target distance is smaller. Ignore non-positive `shareWeight`; if no valid weighted edge remains, report the node unsupported instead of dividing by zero.
- Transfer `mass * (weight / weightSum)` and the corresponding X/Z weighted moments to each target.
- Finalize carried COM as moment divided by carried mass, falling back to the node's box center for zero mass.
- Evaluate only nodes whose `deriveRole(node.box) == MemberRole::VerticalBearing`. Clamp a non-positive capacity to a small positive authored minimum (`0.001f` mass) before dividing, so invalid zero capacity cannot create an indestructible bearing. Use `node.maxOverhang > 0` to enable bending. Append failures once, in ascending ID order.
- Never mutate input nodes or edges.

The solver must not contain `floor`, `slot`, preset names, Blast, PhysX, `LoadPathSolver`, or lateral-shear logic.

- [ ] **Step 5: Run pure solver tests and verify they pass**

Run: `build_tests.bat`

Expected: `StaticGravitySolverTests.exe` prints `PASS` after the existing test executables pass.

- [ ] **Step 6: Commit the isolated solver**

```bash
git add include/NodeTypes.h include/StaticGravitySolver.h src/StaticGravitySolver.cpp tests/StaticGravitySolverTests.cpp build_tests.bat
git commit -m "feat: add static gravity load solver"
```

## Chunk 2: Capacity Behavior and Solver-Level Regression Coverage

### Task 2: Lock down overhang, rerouting, and deterministic cascades at solver level

**Files:**
- Modify: `tests/StaticGravitySolverTests.cpp`
- Modify: `src/StaticGravitySolver.cpp` only if a new test finds an algorithm defect

- [ ] **Step 1: Add failing tests for the required structural scenarios**

Add the following independent graph tests. Use the named graph builder from Task 1 and set `capacity` in mass units. The inverted-L fixture has Ground `0`, root `1` with `box=(0, 2, 0, 0.5, 2, 0.5)`, a plate `2` with `box=(3, 4.1, 0, 1, 0.1, 1)`, and exactly `2 -> 1`, `1 -> 0` live edges with `shareWeight=1`. This makes the plate-to-root load COM exactly 3 meters off the root center.

```cpp
// Inverted L: 5 mass on a platform at x=3 is carried by a vertical root at x=0.
// Root capacity is 100, maxOverhang is 2; compression passes, bending fails.
if (!check(contains(result.overloadedNodes, root),
           "overhang did not overload inverted-L root")) return 1;
if (!check(result.nodes[root].compressionUtilization < 1.0f &&
           result.nodes[root].bendingUtilization > 1.0f,
           "inverted-L utilization components are wrong")) return 1;
```

Add a second version with `maxOverhang = 4` that survives. Add a reroute fixture with live edges `topPlate -> leftPlate`, `topPlate -> rightPlate`, `leftPlate -> leftColumn`, `rightPlate -> rightColumn`, `leftColumn -> Ground`, and `rightColumn -> Ground`, then mark `leftColumn -> Ground` dead. Assert only the route through `rightPlate/rightColumn` transfers top-plate mass. Add a horizontal bidirectional-edge case where `A <-> B` exists, `B -> column -> Ground` is live, and A's direct groundward branch is broken; assert `distance[A] == distance[B] + 1`, `A -> B` carries exactly A's accumulated mass, `B -> A` carries zero, and no node receives the mass twice. Add repeated solve calls and assert all result vectors and per-node values are identical.

- [ ] **Step 2: Run the focused executable and confirm failure**

Run: `build\StaticGravitySolverTests.exe`

Expected: FAIL until the implementation correctly computes horizontal COM, only routes to strictly smaller ground distance, and sorts outputs deterministically.

- [ ] **Step 3: Make the smallest solver correction**

Correct only `src/StaticGravitySolver.cpp` as required. In particular:

- Use the accumulated COM, not the bearing's own center, for eccentricity.
- Use horizontal X/Z distance (`sqrt(dx*dx + dz*dz)`), never Y distance.
- Do not use equal-distance edges, even when they are horizontal.
- Do not mark an already unreachable node twice if an edge has no positive share weight.

- [ ] **Step 4: Run all current test executables**

Run: `build_tests.bat`

Expected: all executables, including `StaticGravitySolverTests.exe`, print PASS. Existing structural tests may still pass because production integration has not yet changed.

- [ ] **Step 5: Commit the behavior coverage**

```bash
git add tests/StaticGravitySolverTests.cpp src/StaticGravitySolver.cpp
git commit -m "test: cover static gravity rerouting and overhang"
```

## Chunk 3: Integrate Static Gravity Analysis Into the Building Model

### Task 3: Replace whole-storey failure decisions with static solver results

**Files:**
- Modify: `include/BlastSupportModel.h:14-160`
- Modify: `src/BlastSupportModel.cpp:1-799`
- Modify: `include/ContactEdges.h:23-66`
- Modify: `build.bat` and `build_tests.bat`

- [ ] **Step 1: Add end-to-end tests that intentionally fail under the old model**

In `tests/StructuralModelTests.cpp`, add model-level checks that do not assume area-weighted whole-storey behavior:

```cpp
// After directly removing one support, the model's node diagnostics must show
// a finite carriedMass and utilization on every surviving vertical bearing.
model.damageColumn(3, 0, 100.0f);
for (int i = 0; i < 8; ++i) model.tickAnalysis();
if (!check(model.nodes()[columnId(model, 3, 1)].carriedMass > 0.0f,
           "static solver did not write carried mass to surviving bearing")) return 1;

// An unsupported top component must become Unsupported and be released after
// a zero-delay analysis/update cycle.
```

Do not add a model-level inverted-L preset or test injection API. `BlastSupportModel` only owns production presets; the pure solver fixture in Task 2 is the coverage for arbitrary geometry and bending. Model-level tests cover the existing production graph's integration, scheduling, confirmed release, Blast fracture, and physics hand-off.

- [ ] **Step 2: Run structural tests to confirm the missing integration**

Run: `build_tests.bat`

Expected: the new checks fail because `BlastSupportModel` still invokes `LoadPathSolver::route` and does not populate static-gravity diagnostics.

- [ ] **Step 3: Wire the new solver into model state**

In `include/BlastSupportModel.h`, add only forward declarations:

```cpp
class StaticGravitySolver;
struct StaticGravityResult;
```

The header declares `void applyStaticGravityResult(const StaticGravityResult& result);` but stores only `std::unique_ptr<StaticGravitySolver>`. It does not include `StaticGravitySolver.h`; `src/BlastSupportModel.cpp` includes the complete header before defining the constructor/destructor and helper. This keeps incomplete types legal under C++14.

Then:

- Forward-declare both `StaticGravitySolver` and `StaticGravityResult`, then declare `applyStaticGravityResult(const StaticGravityResult&)`; include `StaticGravitySolver.h` in `src/BlastSupportModel.cpp`.
- Replace `m_loadPathSolver` with `std::unique_ptr<StaticGravitySolver> m_staticGravitySolver` for the default mode. Do not retain both active failure drivers.
- Add a private `applyStaticGravityResult(const StaticGravityResult&)` helper declaration; include the result header in the `.cpp`, not the public header, if possible.

In the constructor and reset path, construct the static solver and call it once after `rebuildEdges()` to populate initial diagnostics. Leave `maxOverhang = 0.0f` for every migrated production node so legacy presets preserve their axial-only tuning. Only explicit future authoring may set a positive overhang; do not derive it from geometry, floor, slot, or preset. Keep existing `capacity` initialization initially so all legacy tuning remains in mass units.

In `tickAnalysis()`:

- Keep dirty marking and pending-failure timing infrastructure.
- Remove calls to `LoadPathSolver::route` and `detectLateralShear` as failure drivers. Reset `lateralShear` to zero for all nodes; its old inspector value is no longer meaningful in this mode.
- Solve the complete live graph rather than only an affected subgraph; the existing structures are small and whole-graph solving avoids partial-load errors. Keep dirty BFS only for deciding whether a solve is necessary.
- Copy each `StaticGravityNodeResult` to the corresponding live `NodeState`: `supported`, `load` (alias of `carriedMass` for existing UI), carried-mass/COM fields, and three utilization fields.
- For every returned unsupported node, immediately call `executePendingFail(id, NodeStatus::Unsupported)` in ascending ID order; do not create a pending entry. Re-run the solver after that batch.
- For every overloaded node, call `scheduleFail(id, NodeStatus::Overloaded, reason)` in ascending ID order, selecting `"bending"` when bending is the dominant term and `"axial"` otherwise. All overloads reported by one solver result receive identical due times; remove the existing top-down filter and stagger behavior. Avoid duplicate scheduling through the existing pending lookup.

Update `scheduleFail`/`executePendingFail` diagnostic handling so `"bending"` logs the actual moment and moment limit. Compute snapshots from model state:

```cpp
const float normalForce = node.carriedMass * 9.81f;
const float eccentricity = std::sqrt(
    (node.carriedComX - node.box.cx) * (node.carriedComX - node.box.cx) +
    (node.carriedComZ - node.box.cz) * (node.carriedComZ - node.box.cz));
const float moment = normalForce * eccentricity;
const float momentLimit = node.capacity * 9.81f * node.maxOverhang;
```

Keep direct `damageNode` behavior immediate. A directly broken node must cut every incident structural edge; remove the special rule that retains horizontal edges from a dead node. A dead node is not a valid relay in the static model. Existing live neighbors retain their own horizontal edges and may reroute through them.

In `include/ContactEdges.h`, replace every positional `EdgeState` aggregate construction with a small local `makeEdge(from, to, alive)` helper that named-assigns `capacity=0`, `load=0`, `alive`, and `shareWeight=1.0f`.

Remove the `LoadPathSolver` include, member construction, reset call, tick call, and source file from both `build.bat` and `build_tests.bat`. Do not delete `LoadPathSolver` yet; leave it unbuilt but present until the new path has passed regression validation.

- [ ] **Step 4: Update obsolete model expectations**

Revise or remove tests that assert any behavior deliberately replaced by the specification:

- Tests that require whole-floor area-based sharing rather than path-based `shareWeight` sharing.
- Tests that require a broken plate to retain structural horizontal bonds and keep carrying load.
- The `lateralShear` failure assertions only. Retain API/preset construction coverage for `plateShearCapacity` and `ShearPair` unless a separate compatibility-removal change is approved.

Add a `ShearPair` compatibility test that destroys an end column, runs analysis, and asserts no live node transitions to `Overloaded` solely from `lateralShear`; also assert all live nodes have `lateralShear == 0.0f` after static solver analysis. This proves the legacy preset remains constructible while shear is no longer an active failure driver.

Keep and adapt tests for direct break immediacy, support removal, component release, deterministic cascade delay, lower floors surviving upper failures, Blast initialization, undo, and PhysX conversion.

- [ ] **Step 5: Run the full regression suite**

Run: `build_tests.bat`

Expected: `StructuralModelTests.exe`, `StaticGravitySolverTests.exe`, `ReducedStaticsTests.exe`, `SparseCholeskyTests.exe`, and `choltest2.exe` all print PASS.

- [ ] **Step 6: Commit the integrated solver**

```bash
git add include/BlastSupportModel.h src/BlastSupportModel.cpp build.bat build_tests.bat tests/StructuralModelTests.cpp
git commit -m "feat: route building gravity through support graph"
```

## Chunk 4: Release Semantics, Physics Bridge, and Documentation

### Task 4: Make unsupported components detach predictably and document non-bearing stairs

**Files:**
- Modify: `src/BlastSupportModel.cpp:206-247, 601-688`
- Modify: `src/PhysicsWorld.cpp:34-42, 160-204`
- Create: `tests/PhysicsSmokeTests.cpp`
- Modify: `build_tests.bat`
- Modify: `README.md:3-50`
- Modify: `docs/static-gravity-structural-destruction-design.md` only if implementation reveals a necessary clarified rule
- Test: `tests/StructuralModelTests.cpp`

- [ ] **Step 1: Add failing release/physics bridge tests**

Add a test that destroys the only support below a multi-node upper component, runs `tickAnalysis()` (no `update()` needed for unsupported release), and checks every disconnected structural node has:

```cpp
!node.alive && !node.supported && node.status == blast_demo::NodeStatus::Unsupported
```

Add a separate test that verifies a surviving but overloaded node remains static until its scheduled failure is executed, preserving the existing cascade-delay presentation behavior. This test documents the fixed distinction: an unsupported component is failed in `tickAnalysis`; an overload is a delayed failure effect.

Create `tests/PhysicsSmokeTests.cpp` instead of adding PhysX dependencies to `StructuralModelTests.cpp`. It initializes `PhysicsWorld`, builds a default `BlastSupportModel`, and tests `isDynamic()` after two `syncFromModel()` calls on a confirmed unsupported node. It separately verifies that a direct break creates fragments and removes the original member body, while an unsupported intact component creates no fragments and converts its original body to dynamic. Add a dedicated build target in `build_tests.bat` using the same PhysX include paths, library paths, source files, DLL copies, and link libraries as `build.bat`; execute `PhysicsSmokeTests.exe` after `StructuralModelTests.exe`.

Make the cascade guard independently testable: add `unsigned int maxCascadeWaves = 64` to `StructuralConfig`, clamp it to at least `1`, and have `tickAnalysis()` use that exact field. In `StructuralModelTests.cpp`, instantiate `StructuralConfig config; config.maxCascadeWaves = 1;` with an ordinary two-wave fixture: first direct break makes a live bearing overload, and its scheduled overload then disconnects the upper component. Execute the first wave, then call analysis again; assert the guard event text is emitted and every remaining node in the affected component is confirmed `Unsupported`/dead. This tests the fallback without fabricating 65 graph layers.

- [ ] **Step 2: Run tests to confirm release timing is explicit**

Run: `build_tests.bat`

Expected: FAIL if the model leaves nodes alive/supported after solver reports no ground route, if `PhysicsWorld` converts a merely scheduled overload before `executePendingFail`, or if repeated synchronization duplicates/recreates a dynamic body.

- [ ] **Step 3: Implement the boundary behavior**

Keep `PhysicsWorld::shouldFall` as the sole physics decision point. It returns true only for `alive == false`; it must not independently infer contact support or consume impulses. `syncFromModel()` skips already dynamic bodies, so a confirmed release remains idempotent.

In `include/BlastSupportModel.h`, change the declaration to `executePendingFail(int nodeId, NodeStatus status, const std::string& reason, float snapN, float snapV, float snapM, bool spawnFragments);`. Update every call site explicitly: `damageNode()` retains its current direct fracture call; `update()` invokes `executePendingFail(..., true)` for scheduled overloads; immediate unsupported release in `tickAnalysis()` invokes `executePendingFail(..., false)`. In `executePendingFail`, cut every incident edge for every failure reason. Do not preserve horizontal edges from dead nodes. This exactly matches the static solver's "only live nodes and live edges route load" contract. Use `spawnFragments` to keep an unsupported intact component whole for `convertToDynamic()`, while a broken/overloaded member is visually replaced by Blast fragments.

There are no current decorative-child or stair runtime objects in the demo. This version therefore has no attachment-release interface and no related test. When such objects are introduced, their owner component's confirmed release event must detach them without adding support edges.

- [ ] **Step 4: Document the active algorithm and stairs constraint**

Update `README.md` to state:

- Static gravity is the only structural load.
- The solver routes mass along live directed support edges and checks vertical-bearing compression plus COM overhang.
- It is not a finite-element or impulse solver.
- The current demo has no structural stair edges; future decorative stairs must remain outside the support graph, and a later load-bearing stair requires a separate brace feature.
- `LoadPathSolver` is legacy/retired from the default analysis path, if it remains in the tree.

- [ ] **Step 5: Run build, test, and smoke validation**

Run sequentially:

```bat
build_tests.bat
build.bat
build\BlastSupportGraphDemo.exe --smoke-test
```

Expected: every test executable prints PASS; the demo builds; smoke test exits `0` without a Blast initialization error.

- [ ] **Step 6: Commit the completed integration**

```bash
git add src/BlastSupportModel.cpp src/PhysicsWorld.cpp README.md docs/static-gravity-structural-destruction-design.md tests/StructuralModelTests.cpp
git commit -m "docs: describe static gravity destruction model"
```

## Completion Checklist

- [ ] No active structural failure decision is based on `LoadPathSolver`, area-weighted whole-storey totals, or `detectLateralShear`.
- [ ] Live edges only transfer load to strictly shorter routes to Ground; cycles cannot duplicate mass.
- [ ] Every surviving node exposes carried mass and carried COM diagnostics.
- [ ] Vertical bearings fail from combined compression and overhang utilization.
- [ ] Broken or unsupported nodes cannot serve as a horizontal relay.
- [ ] Unsupported components become dynamic only after the structural model confirms release.
- [ ] No stair enters `NodeState`/`EdgeState` as a load-bearing connection in this version.
- [ ] Full build, focused tests, and smoke test have current passing output.
