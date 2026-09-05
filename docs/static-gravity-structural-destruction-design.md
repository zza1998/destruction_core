# Static Gravity Structural Destruction Design

## 1. Goal

Create a gameplay-oriented structural destruction model for buildings and similar assemblies. The first version answers one question after a player or script directly breaks a member:

> Can the remaining structure support itself under gravity?

The model should produce intuitive outcomes such as a platform tilting after a column is removed, an inverted-L structure failing at its root due to an overhang, overloaded columns breaking, and a cascade after a critical support is lost.

This is a static screening model, not finite element analysis and not a replacement for rigid-body simulation. It intentionally excludes impact forces, explosions, collision impulses, vibration, material fracture propagation, and dynamic load combinations.

## 2. Scope

### Included

- Gravity as the only load: `weight = mass * gravity`.
- Directly breaking selected structural members.
- Recomputing load paths after every break.
- Compression and simplified bending checks for vertical supports.
- Support loss, overload failure, and deterministic cascades.
- Structural and decorative classification.
- Stairs that do not carry building load.
- Releasing unsupported pieces to the rigid-body physics system.

### Excluded From Version One

- Contact impulses, explosions, projectile damage, and collision damage.
- Elastic deformation, spring behavior, and force integration over time.
- Full beam/frame stiffness analysis.
- Shear, torsion, local buckling, and detailed material-specific failure modes.
- A stair acting as a brace or a load-bearing path between floors.
- Automatic mesh fracture details; this model only decides which structural connection fails.

## 3. Design Principles

- **Intuitive before physically exact.** A player should understand why a part falls or survives.
- **Static and deterministic.** Identical input and break order produce identical results.
- **Data-driven.** Designers describe capacity and structural roles with understandable values.
- **Separate analysis from presentation.** The solver decides support and failure; animation, debris, and PhysX present the result.
- **Incremental complexity.** The model has clear extension points for later impulse damage or stiffness-based analysis.

## 4. Structural Representation

The structure is an undirected support graph with directed load routing chosen during analysis.

### 4.1 Nodes

A node represents a rigid structural chunk: a floor panel, beam segment, roof section, platform, wall block, or other assembly that moves as one body until detached.

```cpp
struct StructuralNode {
    NodeId id;
    float mass;
    Vector3 centerOfMass;
    bool isAnchor;          // Foundation/world-fixed node.
    bool isStructural;      // False for decorative-only objects.
    bool isSupported;
    bool isReleased;
};
```

An anchor has no weight contribution for this system and is the final destination of all valid load paths.

### 4.2 Members

A member represents a connection that can transmit gravity load: a column, wall section, beam support, bracket, or a parent attachment. Members are the normal failure unit.

```cpp
enum class MemberKind {
    Column,
    Wall,
    Beam,
    Bracket,
    Attachment
};

struct StructuralMember {
    MemberId id;
    NodeId a;
    NodeId b;
    MemberKind kind;

    Vector3 axis;               // Normalized local primary axis.
    Vector3 failurePoint;       // Point used for eccentricity checks.
    float maxSupportedMass;     // Designer-facing vertical load capacity.
    float maxOverhang;          // Designer-facing allowed COM offset.
    float shareWeight;          // Relative load share when parallel paths exist.

    bool isBroken;
    bool isLoadBearing;
};
```

`maxSupportedMass` and `maxOverhang` are the preferred first-version authoring values. Internally they become force and moment capacities:

```text
compressionCapacity = maxSupportedMass * gravity
bendingCapacity     = compressionCapacity * maxOverhang
```

`shareWeight` defaults to `1`. It lets designers make one support intentionally stronger without exposing stiffness or engineering units.

### 4.3 Structural Roles

- **Anchor:** fixed foundation, terrain, or an indestructible world attachment.
- **Load-bearing node/member:** participates in gravity transfer.
- **Decorative attachment:** follows a parent while attached but never supports other structural nodes.
- **Released node:** no longer has a valid route to an anchor and is controlled by rigid-body physics.

## 5. Authoring Model

Designers should choose templates and a small set of gameplay values instead of stiffness, modulus, or sectional properties.

### 5.1 Member Templates

| Template | Intended use | Default behavior |
|---|---|---|
| Light column | Furniture, props | Low capacity and short allowed overhang |
| Heavy column | Building supports | High capacity and medium allowed overhang |
| Bearing wall | Wall-supported structure | High capacity and high parallel share |
| Cantilever bracket | Balconies, signs | Moderate capacity and short overhang limit |
| Beam support | Local floor/roof support | Shares vertical load between endpoints |
| Decorative attachment | Railings, trim | Does not enter support analysis |

### 5.2 Designer Inputs

- Node mass.
- Member template.
- `maxSupportedMass`.
- `maxOverhang`.
- `shareWeight`.
- Whether the member is initially broken or directly breakable.
- Whether a node is an anchor, structural, or decorative.

Useful authoring language is "supports 10 tons" and "supports a center of mass up to 1.5 m off-center," not "compression capacity in newtons" or "section moment of inertia."

## 6. Analysis Pipeline

Run the analysis after a direct break and repeat it until no new failures occur.

```text
Directly break selected member
        |
        v
Find nodes with a valid path to any anchor
        |
        v
Release unreachable structural components
        |
        v
Route gravity load through remaining valid paths
        |
        v
Evaluate member utilization
        |
        v
Break failed members
        |
        +---- repeat until stable
```

The solver must use a stable sort by ID for nodes and members so a cascade is repeatable.

### 6.1 Step A: Apply Direct Breaks

Mark directly targeted members as `isBroken = true`. Their visual joint, Blast bond, or equivalent connection is disabled by the presentation layer.

### 6.2 Step B: Determine Supported Components

First traverse unbroken load-bearing members outward from every anchor to identify candidate components and assign each node a support depth. An anchor has depth `0`; a node's depth is its shortest eligible route to an anchor. For ties, use lower world height first and then stable node ID.

A candidate node is supported only if it has at least one usable outgoing member to a node of lower support depth, or it is an anchor. This rule prevents a horizontal link or a loop from being mistaken for a gravity support path.

Nodes without such a downward anchor path are not load-bearing for the current iteration. Mark their connected component as released and hand it to the rigid-body physics system. Decorative children of released nodes are released with their parent.

Reachability is only a necessary condition. Capacity checks in later steps decide whether a valid path is strong enough.

### 6.3 Step C: Select Downward Load Paths

For each supported node, choose only neighbors of lower support depth. This prevents cyclic double counting. If no eligible neighbor exists, the node is treated as unsupported and released before load accumulation.

If multiple downward members are available, split the node's carried weight by `shareWeight`:

```text
memberLoad_i = totalCarriedLoad * shareWeight_i / sum(shareWeight)
```

`totalCarriedLoad` includes the node's own weight and all load transferred from nodes above it.

The initial depth can be based on vertical position, then resolved by graph distance to an anchor. For unusual layouts, an authored support priority can break ties.

### 6.4 Step D: Accumulate Loads

Process supported nodes from highest support depth to lowest. For each outgoing downward member, accumulate:

- transferred vertical force;
- supported mass;
- weighted center of mass of the transferred load.

The weighted center of mass is required for simple overhang behavior.

```text
combinedMass = sum(childMass)
combinedCOM  = sum(childMass * childCOM) / combinedMass
verticalForce = combinedMass * gravity
```

### 6.5 Step E: Evaluate Capacity

For a vertical column or wall, calculate two utilization terms.

```text
compressionUtilization = carriedMass / maxSupportedMass

eccentricity = horizontalDistance(carriedCOM, member.failurePoint)
bendingUtilization = eccentricity / maxOverhang
```

If `maxOverhang` is zero, the bending utilization is treated as zero for members that intentionally ignore overhang, or as failure for members configured as strictly centered-only.

Use one simple combined criterion:

```text
utilization = compressionUtilization + bendingUtilization
failure when utilization >= 1.0
```

This deliberately gives designers a predictable tradeoff: a support at half of its weight capacity can tolerate approximately half of its configured overhang. Later versions may replace this with separate thresholds or a material-specific interaction curve without changing authored values.

For a beam or bracket in version one, use the same calculation at its designated support point. Its template determines whether it accepts load from one endpoint, both endpoints, or only an authored parent node.

### 6.6 Step F: Cascade

All members that fail in the same iteration are collected first, then broken together. Re-run Steps B through F until no new member fails.

Batching prevents results from changing solely because members happened to be visited in a different order.

## 7. Key Scenarios

### 7.1 Single Column

A platform transfers its own mass and every supported child mass to its one column. The column fails if the total mass exceeds `maxSupportedMass`, then the platform component is released.

### 7.2 Multiple Parallel Columns

Each live column receives a share of the carried load based on `shareWeight`. Removing one column redistributes its portion across the survivors. If a survivor exceeds its utilization threshold, it breaks during the next cascade iteration.

### 7.3 Inverted-L Structure

The horizontal arm's mass moves the supported component center of mass away from the vertical root member.

```text
bendingUtilization = horizontal COM offset / root.maxOverhang
```

The root can therefore fail from an overhang even when the total supported mass remains below its compression limit.

### 7.4 Unsupported Fragment

After a member is directly broken, an upper component may no longer reach any anchor. It is immediately released rather than being assigned artificial load paths. PhysX then controls falling, collisions, and final debris behavior.

### 7.5 Decorative Object

A railing, lamp, or visual stair part does not add a support path. It may add its mass to its structural parent if desired, but it never receives transferred load from other structural nodes.

## 8. Stairs

Version one treats a stair as a non-load-bearing attachment.

```cpp
struct StairAttachment {
    NodeId stairNode;
    NodeId lowerLanding;
    NodeId upperLanding;
    bool isAttached;
};
```

Rules:

1. A stair is not a `StructuralMember` and is excluded from gravity routing.
2. It does not support floors, platforms, walls, or other stairs.
3. It remains attached only while both landing nodes exist, are supported, and retain their relevant attachment points.
4. If either landing is released, destroyed, or loses the attachment, release the stair to rigid-body physics.
5. If a stair is split by authored destruction, each resulting stair piece independently applies the same endpoint rule.

This prevents ordinary stairs from unexpectedly holding up a building after its primary supports fail. A future `StructuralBrace` stair mode can be introduced as a separate feature rather than weakening this rule.

## 9. Failure and Presentation Contract

The structural solver emits decisions; it does not own rendering or physics bodies.

```cpp
enum class StructuralEventType {
    MemberDirectlyBroken,
    MemberOverloaded,
    ComponentReleased,
    StairReleased,
    AnalysisStable
};

struct StructuralEvent {
    StructuralEventType type;
    MemberId memberId;
    NodeId nodeId;
    float utilization;
};
```

Consumers react as follows:

- Break the matching joint, constraint, or fracture bond for a broken member.
- Enable dynamic rigid bodies for released components.
- Detach decorative children and stairs from released parents.
- Play material-specific visual and audio effects using the event type and utilization.

The analysis result must be applied before the next physics frame, so a released component begins falling without one-frame ghost support.

## 10. Stability and Guardrails

- Set a maximum cascade iteration count. On reaching it, release any unresolved component and report a diagnostic rather than looping forever.
- Ignore zero-mass nodes in center-of-mass division; use the node position as fallback.
- Clamp all capacities to positive authored minima.
- Do not route load through broken, decorative, or released members.
- Do not use a member that connects equal or increasing support depth as a downward route unless it has explicit authored direction.
- Cache supported components and invalidate only around a changed member when optimization becomes necessary; correctness comes first.
- Log every load contribution in debug builds so designers can inspect why a support failed.

## 11. Validation Scenarios

Automated tests should use fixed graph layouts and assert final member/node states.

| Scenario | Expected result |
|---|---|
| One platform, one column below capacity | Column survives; platform supported |
| One platform, one column above capacity | Column fails; platform releases |
| Two identical columns, centered platform | Each receives half the load |
| Two columns, remove one | Remaining column receives full load |
| Two columns, remaining capacity insufficient | Remaining column fails in next iteration; platform releases |
| Inverted-L below compression but beyond overhang | Root fails due to bending utilization |
| Inverted-L within both limits | Root survives |
| Disconnected top floor | Entire disconnected component releases |
| Stair with both supported landings | Stair stays attached and carries no structural load |
| Stair with one landing released | Stair releases; building load result is unchanged by the stair |
| Same graph and same direct break repeated | Same cascade events and final state |

## 12. Future Extensions

The following are intentionally deferred but compatible with this design:

- Add impact and explosion damage as an extra utilization or damage term.
- Add damage accumulation and a yield state before final failure.
- Use material templates to vary brittle versus ductile presentation.
- Replace weighted sharing with stiffness-based sharing for higher-fidelity structures.
- Add tension, shear, torsion, buckling, and directional member behavior.
- Add structural-brace stairs that explicitly contribute a limited diagonal load path.
- Move from a static solver to a reduced stiffness or finite-element solver for selected high-value structures.

## 13. Acceptance Criteria

- Breaking a load-bearing member triggers only gravity-based analysis.
- The solver recomputes load distribution and cascades until stable.
- A component without an anchor path is released to physics.
- Parallel supports share gravity load predictably by configured share weights.
- An inverted-L root can fail due to configured overhang even below its weight limit.
- A stair never prevents a building component from releasing in version one.
- Designers can author ordinary behavior without entering engineering constants.
- Repeated runs with identical setup produce identical structural outcomes.
