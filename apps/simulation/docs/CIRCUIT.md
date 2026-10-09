# Experimental circuit inspector

This milestone adds one bounded biological model to the native Evolve project:
**BioModels BIOMD0000000012, Elowitz2000 repressilator**, a synthetic *E. coli*
three-gene negative-feedback oscillator. It is a separate educational circuit.
It does not infer kinetics from the DNA editor, simulate HBB, predict a whole
cell, or connect sequence substitutions to phenotype.

## Run it

Build Evolve using the existing Windows instructions. Click **Circuit lab** to
open the bundled reference trajectory, initially paused at simulation minute 0.
The Python scientific dependencies are **not required for replay**.

- **Play / Pause:** replay the stored trajectory by elapsed real time.
- **Reset:** return to minute 0, paused, retaining the selected replay speed.
- **Timeline:** seek within the available simulated interval.
- **Speed:** simulation minutes per real second, explicitly separate from solver settings.
- **Open trace… / Ctrl+O:** load another compatible local CSV. Invalid imports retain the previous trace.
- **Esc / Close:** close the inspector without editing the DNA project.
- Minimizing pauses replay. End-of-trace pauses without wrapping.

It can also start independently of the DX12 viewport:

```powershell
.\build\Release\Evolve.exe --circuit-trace .\biology\examples\repressilator.csv
```

The cards and plots consistently identify LacI, TetR and cI. Protein and mRNA
panels have separate abundance scales, numerical axes, matching color/line-style
legends, and a current-time cursor. The three T-bar links explicitly show the
repression direction. Abundances are continuous deterministic model amounts,
not literal rendered molecule counts. The scene includes the declared model
hash, solver, model identifier, and scientific limitations.

## Compute a new trajectory

The version- and hash-pinned Python sidecar uses libRoadRunner's actual CVODE
integration of the unchanged SBML, including its assignment rules. See
[the biology README](../biology/README.md) for virtual-environment installation,
commands, solver settings, provenance, and numerical tests. No network access is
needed to solve after dependencies are installed. No biological solving occurs
on the native UI thread, and importing/replaying does not execute Python.

```sh
python biology/repressilator.py --duration-minutes 600 --sample-minutes 1 --output biology/output/run.csv
```

Run that command with the sidecar's configured virtual-environment interpreter.
The output pairs a strict CSV with richer JSON provenance and the CSV digest.
They are individually atomically replaced, not a two-file transaction.

## Architecture and trust boundaries

1. `biology/repressilator.py` verifies the bundled XML hash and supported runtime,
   validates bounded inputs, runs CVODE, and writes a finite nonnegative trace.
2. `core/CircuitTrace` validates a bounded streaming CSV and exposes immutable
   samples. Mandatory units/model identity prevent accidental model mixing.
   A metadata hash is a producer declaration; the native reader does **not**
   authenticate numerical values or independently verify the JSON sidecar.
3. `PlaybackController` advances a simulation-minute cursor from elapsed seconds.
   Rendering frequency does not set integration steps. Between stored samples,
   linear interpolation is presentation only and is not a fresh solver result.
4. `core/CircuitScene` creates the shared vector display list, including plots,
   labels, circuit links, and the cursor. Dense plot data is extrema-preserving
   decimated for display; underlying trajectory samples remain unchanged.
5. `desktop/CircuitWindow` is an additive Win32/GDI inspector within the native
   Evolve executable. The existing DX12 DNA renderer is unchanged.
6. `evolve_circuit_preview` exports that exact display list as SVG on Linux or
   Windows. This is an inspectable rendering artifact, not a replacement web app.

Example portable preview:

```sh
./build/evolve_circuit_preview biology/examples/repressilator.csv build/circuit.svg 225
```

## Scientific scope

- Six species, twelve reactions; transcription, translation and degradation in
  a three-repressor feedback circuit.
- Minutes and item amounts in one 1 fL compartment, following the SBML and notes.
  Parameter unit annotations are incomplete. No claim of complete dimensional
  validation is made.
- Initial TetR mRNA (`Y`) is 20; the other five amounts start at zero.
- Parameters come from the curated model, not newly invented HBB kinetics.
- Deterministic ODE behavior omits the stochastic noise discussed in the paper,
  spatial structure, resource competition, sequence-specific regulation and
  growth/division. These tests validate software/numerical behavior, not a new
  biological experiment or a fit to patient data.
- Published reference: [Elowitz and Leibler, Nature (2000)](https://doi.org/10.1038/35002125).
  Encoded model: [official BioModels source](https://github.com/biomodels/BIOMD0000000012),
  CC0-1.0. Exact commit and hashes are recorded with the bundled model.

## Integration and validation boundary

This additive branch started from remote main `603305a0236b4f4d0ad06d4ed2ff965df38dd824`.
Later renderer, cell, visual, IBM and HBB work held elsewhere was not present in
that source base. Integrate the new modules onto the appropriate newer branch
when available; do not replace it with this older main checkout.

Portable tests and shared SVG inspection can run on Linux. Cross-compiling the
Windows application checks syntax and linkage, **not** Win32 behavior or DX12
rendering. Before merging, run the existing Windows WARP smoke test and these
manual inspector checks on Windows:

1. Open twice: reuse one inspector; no duplicate timer/window.
2. Play, pause, change speed, seek while paused/playing, reset, reach the end.
3. Minimize/restore: replay remains paused with readable controls.
4. Cancel Open, import malformed/oversized CSV: current data is preserved.
5. Open a valid trace at a Unicode path. Close/reopen repeatedly.
6. Tab/Shift+Tab and keyboard trackbar controls; Ctrl+O and Esc.
7. Resize and use system DPI 100%, 150%, 200%; inspect text/axes/controls.
8. DNA edits, undo, and renderer remain independent of circuit navigation.
