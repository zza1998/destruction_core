# Blast Support Graph Demo

This demo visualizes a five-floor simplified building support graph without PhysX.

`ReducedStaticsSolver` is an independent 2D reduced statics module. It solves
support reactions from force and moment equilibrium (`sum Fx`, `sum Fy`,
`sum M`) and reports reaction utilization, instability, and capacity failure.
It is intentionally separate from the Blast support graph so its equations can
be inspected and later integrated into the building model.

## Modules and fixes

- `BlastRuntime` owns Blast framework, asset, actor, group, and column-bond
  fracture lifecycle. It does not decide structural support or load results.
- `SupportGraphSolver` owns directed Ground reachability, dirty BFS, and local
  affected-node collection. `BlastSupportModel` retains public API, state,
  snapshots, events, and analysis orchestration.
- Support paths remain `Column -> lower Block -> neighboring Block -> lower
  Column/Ground`; columns never connect directly across floors.
- Loads use a one-way chain on each floor, from outside `slot0` toward inside
  `slot3`. A block receives its own mass, the same-slot upper column load, and
  the carried load from the previous slot. The resulting value is written to
  both the block and its column. A live block and column must both be within
  capacity; if either is broken or overloaded, its released load continues to
  the next live slot. Column self-weight and column load continue through the
  same slot on the floor below.
- Analysis uses dirty BFS and processes only affected floor components, from
  high floors down to low floors, until the dirty queue is stable. Horizontal
  and vertical `EdgeState.load` values show the actual chain transfer.
- `ReducedStaticsSolver` remains an independent module and is not used by the
  Blast support load analysis.

- Click a block or column to select it; press `Space` to damage the selected node.
- The model propagates floor mass through the directional chain and marks
  overloaded or unsupported regions.
- `StructuralConfig` controls column capacity and the overload failure ratio.
  There is no hard-coded minimum-column collapse rule.
  A one-layer grounded column remains independent after its upper load is gone;
  with live upper load, its actual redistributed load can still break it.
- The right panel shows block/column weight, load/capacity utilization, status, and events.
- Press `S` to run an explicit analysis step.
- Press `R` to reset and `Esc` to exit.
- Every block and column has its own normalized mass, shown as `W` in the
  viewport and `Weight` in the Inspector. All masses are below 100.

Blast Toolkit owns the sample asset/actor/group lifecycle and bond fracture
operation. The application layer adds floor semantics, mass, capacity, and
load propagation. This is a real-time structural screening model, not a finite
element solver and not a rigid-body simulation; PhysX can be added later as a
separate bridge after the support analysis is validated.

## Build

Run `build.bat` from a Visual Studio Developer Command Prompt. Set
`BLAST_ROOT` if the SDK is not located one directory above this demo.

```bat
build.bat
```

The script links against the Release libraries under
`..\_build\windows-x86_64\release\blast-sdk` by default.
