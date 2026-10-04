# Evolve.ai v0.1

A native Windows / DirectX 12 prototype for exploring a schematic DNA helix and trying reversible virtual sequence edits. This first version prioritizes the desktop foundation and interaction loop. It uses synthetic example data and **does not predict biological effects**.

The original project vision is preserved in the repository's public concept documentation. See the root README for project-wide links.

## What works

- Procedural 3D double helix with complementary A/T and G/C pairs, colored bases, backbone connectors, depth testing, studio lighting and a ground grid.
- Orbit, pan, zoom, frame-all, auto-rotation and base selection in the viewport or sequence list.
- Virtual A/C/G/T substitutions, changed-base markers, undo/redo and restore-baseline.
- Side-by-side baseline/scenario comparison.
- Import a single FASTA record or plain A/C/G/T sequence, from 4 to 256 bases.
- Save/open `.evolve` projects with immutable baseline and edited sequence; unsaved-change prompts and atomic file replacement.
- Cancellable mock analysis showing sequence GC content and edit counts, with explicit mock provenance. The short job delay is illustrative; there is no AI model behind it.
- CSV result export, lighting control, optional completion sound and keyboard navigation.
- Hardware DX12 with software WARP fallback; an automated renderer smoke mode.

## Experimental Voice Agent (feature branch)

Open **Voice Agent** from the workspace toolbar. Typed local commands work without a microphone or network. Optional local Windows speech provides push-to-talk and spoken replies; an optional OpenAI Responses adapter adds conversational interpretation of text and bounded workspace tools. Both microphone and cloud use start disabled. Cloud API usage is billed separately and requires explicit in-app consent and a user-provisioned key.

This is experimental integration code, not evidence of live voice verification. See [voice controls and manual verification](docs/VOICE.md) and [conversational provider, privacy, and setup](docs/CONVERSATIONAL_PROVIDER.md). The scientific analysis remains the same mock composition calculation; the conversation model does not turn it into a biological predictor.

## Install, build and run on Windows

### Prerequisites

Use Windows 10 or 11 x64. A DirectX 12 graphics driver is recommended, but the application can use Windows' software WARP renderer when hardware acceleration is unavailable.

Install **Visual Studio 2022 or newer** and select these components in Visual Studio Installer:

- **Desktop development with C++** workload
- **C++ CMake tools for Windows**
- A current **Windows 10 or Windows 11 SDK**

Git is needed to clone the repository. Visual Studio supplies the MSVC compiler, Windows SDK and CMake integration. After installing or modifying Visual Studio, open a new **Developer PowerShell for Visual Studio** so its compiler tools are available.

Verify CMake before building:

```powershell
cmake --version
```

If that command is not recognized, reopen Developer PowerShell or add the CMake component through Visual Studio Installer.

### Build from source

From the repository root, enter the simulation directory:

```powershell
cd .\apps\simulation
```

Run the build helper:

```powershell
.\scripts\build-windows.ps1 -Configuration Release
```

The helper configures a 64-bit CMake build, compiles the application and test executable, and runs the portable core tests. A successful build ends by printing the executable path:

```text
apps\simulation\build\Release\Evolve.exe
```

If PowerShell blocks the script, permit local scripts only for the current terminal session and rerun it:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\build-windows.ps1 -Configuration Release
```

To perform the same steps manually:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

For a debuggable build, replace `Release` with `Debug`. Its executable will be under `build\Debug`.

### Run the desktop application

After a successful Release build, launch:

```powershell
.\build\Release\Evolve.exe
```

If graphics initialization fails, update the graphics driver or force the Windows software renderer:

```powershell
.\build\Release\Evolve.exe --warp
```

The first screen contains a synthetic 24-base example, so no input file is required to begin. Imported sequences stay local, and the application makes no network requests.

### Verify the renderer

The optional smoke test launches the real DirectX 12 path with WARP, performs an edit and mock analysis, captures a rendered frame, and verifies project save/reopen:

```powershell
.\build\Release\Evolve.exe --warp --smoke-test
$LASTEXITCODE
Get-Content .\smoke-test.log
```

An exit code of `0` indicates success. The generated `evolve-v0.1-viewport.bmp`, `smoke-test.evolve` and log files are test evidence and are ignored by Git.

### Use a prebuilt package

The Windows CI workflow builds a portable **Evolve-v0.1-Windows-x64** artifact after all tests pass. Open a successful Simulation workflow run in GitHub Actions, download the artifact, extract it, and run `Evolve.exe`. The package uses the static MSVC runtime and does not need installation, but it is not currently code-signed, so Windows may show an unrecognized-publisher warning.

The original console skeleton is maintained in `legacy/simulation.cpp` and builds as `evolve_cli`; it is separate from the desktop application. Generated binaries are not tracked in Git.

## First walkthrough

1. Start Evolve. A synthetic 24-base-pair example is already visible.
2. Drag to orbit; use the mouse wheel to zoom. Right-drag or middle-drag pans.
3. Click a colored endpoint or select a row in the sequence list.
4. Choose A, C, G or T in **Virtual edit**. The opposite base updates to its complement. A gold center marker identifies an edit; pale endpoints identify the selected pair.
5. Enable **Compare**. The dim baseline is on the left, the current scenario on the right.
6. Click **Run mock analysis** to compare GC percentage and edited-base count. Edits cancel pending analysis and invalidate its old result.
7. Save a project, reopen it, or export the composition results as CSV.

| Action | Control |
|---|---|
| Orbit | Left-drag inside viewport |
| Pan | Right-drag or middle-drag |
| Zoom | Mouse wheel over viewport |
| Select base | Click an endpoint or a sequence row |
| Frame all | F while viewport is focused, or Frame all button |
| Save | Ctrl+S |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Move between controls | Tab / Shift+Tab |

## Structure

- `src/core/`: project state, import/serialization, mock analysis, procedural mesh.
- `src/renderer/`: DX12 device, shaders, GPU resources, orbit camera, picking, readback.
- `src/desktop/`: Windows application, native controls, dialogs, mock job lifecycle.
- `tests/`: portable project and geometry checks.
- `examples/`: synthetic FASTA and saved scenario.
- `scripts/`: Windows build helper.
- `docs/`: implementation notes, roadmap and original vision.

See [architecture](docs/ARCHITECTURE.md), [manual checks](docs/TESTING.md), and [v0.1 release notes](CHANGELOG.md).

## Test without Windows

The project core and geometry builder are portable. The desktop renderer requires Windows.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Direct compiler alternative:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc src/core/Project.cpp src/core/Geometry.cpp tests/CoreTests.cpp -o evolve_tests
./evolve_tests
```

## Current limits

This is a C++/DX12 foundation, not an Unreal project. The repository started with a C++ console skeleton; v0.1 introduces a small renderer behind a separate project model. It does not extract or redistribute Unreal code. Unreal remains a possible future presentation implementation.

Geometry uses arbitrary display units and is illustrative. There is no atomistic structure, physical dynamics, gene annotation, medical interpretation, DNA-to-phenotype model, AI assistant, voice recognition, cloud upload or scientific backend service. The desktop makes no network requests.

v0.1 supports substitutions only, one short sequence, synchronous geometry upload, and deliberately serialized GPU frames. It is not intended for full genomes. The UI uses system-DPI-aware native controls; moving between monitors with different scaling may require restarting for crisp text. `.evolve` saves baseline and scenario sequences; camera settings, transient run results and undo history are not persisted. CSV separately exports completed results.

Future work: a supported model/solver adapter, asynchronous scientific host, richer project provenance, scalable geometry, and a larger UI. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
