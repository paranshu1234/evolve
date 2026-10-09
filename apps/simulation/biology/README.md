# Repressilator solver workbench

This is a real, deliberately small biological simulation: the deterministic
**Elowitz–Leibler synthetic E. coli repressilator**, BioModels
[BIOMD0000000012](https://www.ebi.ac.uk/biomodels/BIOMD0000000012).
It integrates the vendored SBML with **libRoadRunner 2.10.0 / CVODE**. The Python
code does not reimplement the reaction laws or replace them with an animation.
The native Evolve viewer can replay its exported trace without a Python runtime.

This circuit is a separate educational example. It does not simulate HBB,
whole-cell physiology, edited DNA consequences, human disease, or treatment. A
numerically reproducible trajectory is not experimental or clinical validation.

## Install

Use **64-bit CPython 3.11, 3.12, or 3.13**, with a matching official libRoadRunner
wheel. Official 2.10.0 wheels cover Windows x64, Linux x64 (glibc 2.28+), macOS
x64 (15+) and macOS ARM64 (14+). Python 3.12 / Linux x64 is the platform tested
for this workbench; other supported wheel platforms still need local validation.
The version-and-hash lock intentionally excludes Python 3.14 and source builds.
Only libRoadRunner and its required NumPy dependency are installed; tests use
Python's standard-library `unittest`.

From `apps/simulation` on Windows PowerShell:

```powershell
py -3.12 -m venv biology/.venv
biology/.venv/Scripts/python.exe -m pip install --require-hashes --only-binary=:all: -r biology/requirements.lock
biology/.venv/Scripts/python.exe -m pip check
```

On Linux/macOS:

```sh
python3 -m venv biology/.venv
biology/.venv/bin/python -m pip install --require-hashes --only-binary=:all: -r biology/requirements.lock
biology/.venv/bin/python -m pip check
```

The lock pins `libroadrunner==2.10.0` and `numpy==2.2.6` and includes official
PyPI wheel hashes. Setup downloads these packages from PyPI. **Simulation itself
is offline:** it accepts neither arbitrary SBML inputs nor URLs, downloads
nothing, launches no external commands, and does not read pickle files. The
venv, bytecode and generated `output/` directory are ignored by Git.

Some nonstandard Linux Python installations do not expose `libpython` to the
dynamic loader. If importing RoadRunner reports `libpython3.x.so` missing, use
a standard shared-library Python installation or add that installation's
existing `lib` directory to the command's `LD_LIBRARY_PATH`. No additional
solver installation should be necessary. Linux also needs the native runtime
libraries required by the official wheel (including ncurses).

## Run

From `apps/simulation`, using the venv Python from above:

```sh
biology/.venv/bin/python biology/repressilator.py --output biology/output/repressilator.csv
```

On Windows, replace `biology/.venv/bin/python` with
`biology/.venv/Scripts/python.exe` in the run and test commands.

Defaults simulate **600 minutes**, sampled every **1 minute**, producing 601
rows including time zero. This takes well under a second in the tested cloud
runtime, but timing depends on the machine. Both the CSV and adjacent JSON
provenance file are written. Existing files are refused unless `--overwrite`
is explicitly supplied.

```sh
biology/.venv/bin/python biology/repressilator.py --duration-minutes 600 --sample-minutes 1 --relative-tolerance 1e-10 --absolute-tolerance 1e-12 --output biology/output/tighter.csv
```

Numerical bounds:

- Duration: 0.01–10,080 simulated minutes (seven days)
- Sampling interval: at least 0.01 minutes and no greater than duration
- At most 100,001 samples, including time zero
- Relative tolerance: 1e-12–1e-3; default 1e-8
- Absolute tolerance: 1e-14–1e-3; default 1e-10 item
- A duration not divisible by the interval ends with one shorter interval

Sampling interval controls recorded output times, not CVODE's adaptive internal
integration steps. There are no model-parameter or sequence-edit switches.
The test-only analytic-decay experiment changes the solver's two independent
transcription parameters in memory; it never edits the vendored model.

## Model and units

The network is LacI repressing TetR, TetR repressing lambda cI, and lambda cI
repressing LacI. Its six variables are:

| CSV ID | Biological quantity | Initial amount |
| --- | --- | ---: |
| X | LacI mRNA | 0 |
| Y | TetR mRNA | 20 |
| Z | lambda cI mRNA | 0 |
| PX | LacI protein | 0 |
| PY | TetR protein | 0 |
| PZ | lambda cI protein | 0 |

The SBML contains **12 reactions**, **9 assignment rules**, and one compartment
of size **1 femtolitre** (1 cubic micrometre). Its time unit is **minute**;
substance is **item**. We select bare species IDs from RoadRunner, which returns
amounts, rather than bracketed concentration selections. Amounts are labeled
`item_per_cell` because this model represents one fixed cell. Do not interpret
the values as molarity. Deterministic continuous amounts can be fractional;
they are not stochastic integer molecule counts.

The XML declares the time, substance and volume units, but lacks complete
parameter unit annotations. libSBML, bundled with RoadRunner, reports 21 unit
warnings (code 99505) for this exact file. General, identifier, MathML and
overdetermination checks pass. The sidecar records these facts. Full automatic
dimensional consistency has **not** been established, and the XML is not silently
rewritten to suppress warnings.

Important evaluated assignments include `kd_mRNA = ln(2)/2`,
`kd_prot = ln(2)/10`, `a_tr = (0.5 - 0.0005)*60 = 29.97`, and `a0_tr = 0.03`.
`alpha` evaluates to approximately **216.18785187721116**, not its unevaluated
XML placeholder value of 216.404. These values are read from RoadRunner after
loading; the SBML assignment rules remain authoritative.

## Trace contract and provenance

The checked-in `examples/repressilator.csv` and `.json` are generated by the
actual solver with default settings. They are a small baseline for native-viewer
integration and regression checks, not an independent experimental reference.
To regenerate them intentionally:

```sh
biology/.venv/bin/python biology/repressilator.py --output biology/examples/repressilator.csv --overwrite
```

CSV format:

```text
# evolve-circuit-trace-v1
# model_id=BIOMD0000000012
# model_sha256=0fb6eb542c51de086feb614550994536faf07fb07b134641887e8c377b128987
# solver=libRoadRunner 2.10.0 CVODE
# time_unit=minute
# quantity_unit=item_per_cell
...additional # key=value metadata...
time_minutes,X,Y,Z,PX,PY,PZ
0,0,20,0,0,0,0
```

Each CSV row has seven finite numeric fields. Times begin at zero and increase
strictly. Every species amount must be nonnegative. **Any negative value,
including a tiny numerical negative, causes rejection; there is no clamping.**
The complete result is validated before any output is committed. Floats use
17 significant digits for round-trip precision.

The JSON contains the source commit, SBML checksum, dependency versions, solver
build, numerical settings, unit caveat, species and reaction inventory,
evaluated parameters, runtime, and SHA-256 of the actual CSV bytes. CSV and JSON
are staged in the destination directory and flushed. Without `--overwrite`,
same-directory hard links publish each file atomically only if its destination
is still absent, so a concurrent producer is never overwritten. Filesystems
without hard-link support fail safely rather than falling back to replacement.
With `--overwrite`, each destination is atomically replaced. CSV is published
last. A pair of filenames cannot be committed as one transaction: interruption
or a competing writer between publications may leave just the new JSON or a
mismatched pair. The CSV hash in JSON detects that mismatch; resolve the conflict
and regenerate both before using provenance.

Same-environment reruns and solver resets are deterministic in tests. Different
platforms or dependency builds may change low floating-point bits. Compare
numerically using documented tolerances rather than expecting cross-platform
byte identity. The JSON intentionally records the actual runtime.

## Verification

```sh
biology/.venv/bin/python -m unittest discover -s biology/tests -v
```

The tests run the actual pinned solver; they do not mock its numerical engine:

- Pinned model/license bytes, six species, twelve reactions, nine rules and units
- Initial amounts, initial derivatives, and all nine derived assignments
- 600-minute finite, nonnegative default trajectory with exact requested times
- Identical fresh reruns; reset restores species, parameters and trajectory
- Default versus tighter and reference tolerances, with convergence
- Six consecutive 100-minute chunks versus one 600-minute run
- Analytic exponential mRNA decay and protein response with both `ps_a` and
  `ps_0` set to zero; untouched species remain zero
- NaN/infinity/negative values, row limits, malformed traces and bad arguments
- Exact CSV contract, sidecar hash, symlink/overwrite protection, concurrent
  producer protection, unsupported hard links and staged-write failure
- Checked-in example reproducibility and a subprocess CLI smoke test

These are verification checks for this ODE implementation. They do not establish
predictive accuracy for arbitrary biology. The decay case is an analytic check;
the convergence and chunking comparisons use the same solver, not an independent
solver cross-validation. The finite nonnegativity check examines recorded output
samples, not every internal integration step.

## Source and attribution

- [BioModels model record](https://www.ebi.ac.uk/biomodels/BIOMD0000000012)
- [Pinned official model source](https://github.com/biomodels/BIOMD0000000012/blob/7de3848305224ace0523c1328ac3423d2c007f68/BIOMD0000000012/BIOMD0000000012.xml)
- Elowitz MB, Leibler S. *A synthetic oscillatory network of transcriptional
  regulators.* Nature 403, 335–338 (2000). [doi:10.1038/35002125](https://doi.org/10.1038/35002125)
- [libRoadRunner 2.10.0 official PyPI release](https://pypi.org/project/libroadrunner/2.10.0/)
- [libRoadRunner official source and documentation](https://github.com/sys-bio/roadrunner)
- Welsh et al. *libRoadRunner 2.0: a high performance SBML simulation and analysis
  library.* [doi:10.1093/bioinformatics/btac770](https://doi.org/10.1093/bioinformatics/btac770)

The XML and `models/LICENSE-CC0.txt` are unchanged copies from official BioModels
repository commit `7de3848305224ace0523c1328ac3423d2c007f68`. The encoded model is
[CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/). Exact source and
license checksums are in `models/provenance.json`. libRoadRunner is separately
Apache-2.0 licensed; NumPy is separately BSD-3-Clause licensed. Dependency
binaries are installed from PyPI, not vendored into this repository.
