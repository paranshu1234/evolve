# Evolve Unreal renderer proof of concept

An optional standalone UE5 presentation client for the existing platform-independent
Evolve Project and Geometry code. The original CMake/DX12 app remains unchanged as
a fallback. This is a **synthetic schematic DNA display**, not atomic simulation or
biological-effect prediction. The composition button runs only the existing
`mock-composition-v0.1` calculation.

## Build on an existing Unreal 5.8 Windows installation

Prerequisites: a locally installed, licensed UE 5.8 and its supported MSVC/Windows
SDK toolchain. No engine source, engine assets, credentials, or third-party paid
content are in this repository. The scripts do not install or accept anything.

From the repository root in PowerShell (adjust the installed engine path):

```powershell
.\apps\unreal\EvolveUnreal\scripts\build-windows.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Package
```

This builds the editor module, generates an original small map and vertex-color
material through the installed editor's Python API, builds the game target, and
optionally cooks/packages a Development standalone app under `EvolveUnreal/Packages`.
The generated Content and all engine/build output are ignored. Assets are generated
locally rather than distributing engine-owned source or assets in the public repo.
The asset script is idempotent and preserves existing generated assets. After any
asset-script design change, regenerate in a fresh checkout to avoid stale assets.

Run the packaged `EvolveUnreal.exe` in its own window; the editor need not remain
open. For a faster iteration after the build, launch the installed editor executable
with the `.uproject` path plus `-game -windowed -ResX=1440 -ResY=900` (this is an
editor-hosted standalone window, **not evidence of a packaged build**).

## Controls

- Right mouse drag: orbit. Middle mouse drag: pan. Wheel: zoom.
- F: frame all. S: frame the selected base pair.
- Click an endpoint or use left/right arrows: select a base pair.
- A/C/G/T: edit selected scenario base. Ctrl+Z / Ctrl+Y: undo / redo.
- B: baseline comparison. H: floor grid. R: restore baseline (undoable).
- Space: current-revision **mock composition** calculation.
- Close the window to exit. Session edits are ephemeral; this POC has no save/import UI.

The overlay shows revision, selected base, sequence, undo/redo availability, vertex
count, CPU mesh rebuild time and instantaneous frame time. These are diagnostics,
not a controlled GPU benchmark. Picking uses the same approximate 24-pixel endpoint
radius as the DX12 prototype, not atomic/ray-traced selection.

## Integration boundary

`apps/simulation/src/core` remains the authoritative portable C++ Project and
Geometry implementation. `src/presentation/SceneBridge` makes a **value-owned**
snapshot with revision, strings, selection, view flags and mesh. No pointers into
mutable Project state cross the boundary. Unreal compiles those same sources through
small translation-unit wrappers; it does not fork the core implementation, depend
on CMake build artifacts, or link voice, microphone, HTTP, API or provider modules.

All commands, snapshot construction and UObject/mesh changes currently run on the
Unreal game thread. No worker thread is claimed. A future worker may consume a copied
snapshot; it must return revision-tagged results to the game thread and use the
existing `Project::accept` stale-result check. Never touch UObjects from a worker.

Core schematic units map to UE centimeters as `(Z, X, Y) * 100`; directions use the
same axis permutation without scaling. One display unit is arbitrarily 100 cm for
camera/renderer convenience. It is **not** a biological distance conversion. The
positive-determinant permutation preserves triangle winding. UE uses a locally
generated lit, two-sided vertex-color material and a directional light.

## Verification

Portable bridge checks run with the existing suite:

```sh
cmake -S apps/simulation -B apps/simulation/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/simulation/build -j2
ctest --test-dir apps/simulation/build --output-on-failure
```

Windows requires a real installed engine build, asset creation, cook/package and
runtime checks; portable tests alone do not establish these passed. Run either the
packaged executable or editor-hosted `-game` with `-EvolveSmokeTest -windowed
-ResX=1440 -ResY=900`. The smoke path exercises edit/undo/redo, stale-result rejection,
current mock result, comparison, snapshot and material availability. It writes
`Saved/EvolveSmokeTest.txt`, requests a real viewport screenshot with `HighResShot`,
and exits after at least 180 frames/eight seconds. Inspect the screenshot under the
runtime Saved/Screenshots directory for actual colored geometry and readable HUD.
A PASS text does not prove the screenshot or GPU output is correct. NullRHI must
**not** be used for visual/runtime acceptance.

Manual acceptance: packaged own-window startup without editor; visible helix and
colored complements; repeated click/keyboard selection and each base edit;
undo/redo branching; baseline restore then undo; orbit/pan/zoom/frame selection;
compare/grid toggles; resize/minimize/restore; close/relaunch; readable mock labels.
Confirm the existing DX12 CMake build and WARP smoke separately. No microphone or
live API testing is relevant to this isolated renderer module.

API references: [Epic procedural mesh API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/ProceduralMeshComponent/UProceduralMeshComponent),
[material editing API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialEditingLibrary).
