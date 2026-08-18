# Blast Support Graph Demo

This demo visualizes a simplified building support graph and drives structural
collapse from static gravity load routing, with PhysX providing the falling/debris
presentation.

## Structural model

The active analysis path is a static gravity solver (`StaticGravitySolver`):

- Gravity is the only load. Every member's own mass plus the mass carried down to
  it is routed along live, directed support edges toward Ground (`id 0`).
- A node is supported only when it has a strictly closer live route to Ground.
  Load flows from the farthest node toward Ground; each node splits its carried
  mass among its closer outlets by `EdgeState::shareWeight` (all `1.0f` by
  default). The load's horizontal center of mass travels with it.
- A vertical bearing (column or wall, derived from geometry) fails when its
  combined utilization reaches `1.0`:
  `compressionUtilization (carriedMass / capacity) + bendingUtilization
  (carried-COM offset / maxOverhang)`. `maxOverhang` is `0` by default, so legacy
  presets use axial capacity only; an authored positive overhang adds the
  inverted-L / eccentric-load check.
- Directly broken members and overloaded members are failed and cut all their
  incident edges; an unsupported component (no live path to Ground) is released
  as a dynamic body instead of being re-routed into artificial load paths.
- `tickAnalysis()` re-solves the whole live graph each pass until no node is
  unsupported, then schedules overloads as one delayed wave. `update()` executes
  overdue overload failures and re-analyzes.

This is a static screening model, not a finite-element or impulse solver: contact
forces, explosions, shear/diaphragm failure, and load-bearing stairs are not part
of this version. Lateral shear (`plateShearCapacity`, `ShearPair`) remains as
public API/preset data but no longer drives structural failure.

## Modules

- `StaticGravitySolver` owns ground-distance BFS, deterministic load routing, and
  compression/bending utilization. It has no Blast, PhysX, UI, or preset coupling.
- `BlastRuntime` owns the Blast framework asset/actor/group lifecycle and
  column-bond fracture.
- `SupportGraphSolver` owns directed Ground reachability and dirty-node bookkeeping
  used to trigger re-analysis.
- `BlastSupportModel` owns state, snapshots, events, presets, and orchestrates the
  static solve + failure scheduling.
- `PhysicsWorld` consumes confirmed releases to convert intact components to
  dynamic bodies and spawn debris fragments for broken/overloaded members.

## Interactions

- Click a block or column to select it; press `Space` to damage the selected node.
- Press `S` to run an explicit analysis step, `R` to reset, `Esc` to exit.
- The right panel shows member weight, carried mass, utilization, status, and events.

## Build

Run `build.bat` from a Visual Studio Developer Command Prompt. Set
`BLAST_ROOT` if the SDK is not located one directory above this demo.

```bat
build.bat
```

Run `build_tests.bat` to build and execute the full test suite (structural model,
static solver, reduced statics, sparse Cholesky, and PhysX smoke tests).

The scripts link against the Release libraries under
`..\_build\windows-x86_64\release\blast-sdk` by default.
