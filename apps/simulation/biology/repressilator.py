#!/usr/bin/env python3
"""Offline, pinned-SBML repressilator simulation with libRoadRunner's CVODE.

The reaction laws and assignment rules live exclusively in the vendored SBML.
This module configures the solver, validates results, and writes trace artifacts.
"""
from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import hashlib
import importlib.metadata
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import sys
import tempfile
from typing import Any, Sequence
import xml.etree.ElementTree as ET

BASE_DIR = Path(__file__).resolve().parent
MODEL_PATH = BASE_DIR / "models" / "BIOMD0000000012.xml"
MODEL_ID = "BIOMD0000000012"
MODEL_SHA256 = "0fb6eb542c51de086feb614550994536faf07fb07b134641887e8c377b128987"
SOLVER_VERSION = "2.10.0"
NUMPY_VERSION = "2.2.6"
SPECIES = ("X", "Y", "Z", "PX", "PY", "PZ")
COLUMNS = ("time_minutes", *SPECIES)
INITIAL_AMOUNTS = (0.0, 20.0, 0.0, 0.0, 0.0, 0.0)
FORMAT_VERSION = "evolve-circuit-trace-v1"
MAX_DURATION_MINUTES = 10080.0  # Seven simulated days, not wall-clock days.
MAX_ROWS = 100001
NS = {"s": "http://www.sbml.org/sbml/level2/version3"}


class SimulationError(ValueError):
    """Invalid input, an unsupported dependency, or an invalid model/result."""


@dataclass(frozen=True)
class Settings:
    duration_minutes: float = 600.0
    sample_minutes: float = 1.0
    relative_tolerance: float = 1e-8
    absolute_tolerance: float = 1e-10

    def __post_init__(self) -> None:
        ranges = {
            "duration_minutes": (0.01, MAX_DURATION_MINUTES),
            "sample_minutes": (0.01, MAX_DURATION_MINUTES),
            "relative_tolerance": (1e-12, 1e-3),
            "absolute_tolerance": (1e-14, 1e-3),
        }
        for name, (low, high) in ranges.items():
            value = getattr(self, name)
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise SimulationError(f"{name} must be a finite number")
            if not math.isfinite(value) or not low <= value <= high:
                raise SimulationError(f"{name} must be finite and within [{low:g}, {high:g}]")
        if self.sample_minutes > self.duration_minutes:
            raise SimulationError("sample_minutes must not exceed duration_minutes")
        if self._intervals() + 1 > MAX_ROWS:
            raise SimulationError(f"Requested trace exceeds {MAX_ROWS} rows; increase sample_minutes")

    def _intervals(self) -> int:
        ratio = self.duration_minutes / self.sample_minutes
        nearest = round(ratio)
        if math.isclose(ratio, nearest, rel_tol=1e-12, abs_tol=1e-12):
            return int(nearest)
        return math.ceil(ratio)

    def times(self) -> list[float]:
        # Explicit solver output times; a final partial interval is allowed.
        count = self._intervals()
        return [i * self.sample_minutes for i in range(count)] + [self.duration_minutes]


def inspect_model(path: Path = MODEL_PATH) -> dict[str, Any]:
    """Check exact trusted bytes and the biological/unit inventory before JIT load."""
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise SimulationError(f"Cannot read bundled SBML: {exc}") from exc
    digest = hashlib.sha256(raw).hexdigest()
    if digest != MODEL_SHA256:
        raise SimulationError(f"Bundled SBML SHA-256 mismatch: expected {MODEL_SHA256}, got {digest}")
    root = ET.fromstring(raw)
    model = root.find("s:model", NS)
    if model is None or model.get("id") != MODEL_ID:
        raise SimulationError("Unexpected SBML model identity")
    species = model.findall("s:listOfSpecies/s:species", NS)
    reactions = model.findall("s:listOfReactions/s:reaction", NS)
    rules = model.findall("s:listOfRules/s:assignmentRule", NS)
    compartments = model.findall("s:listOfCompartments/s:compartment", NS)
    if len(species) != 6 or {s.get("id") for s in species} != set(SPECIES):
        raise SimulationError("Expected the six repressilator species")
    if len(reactions) != 12 or len(rules) != 9:
        raise SimulationError("Expected twelve reactions and nine assignment rules")
    if len(compartments) != 1 or compartments[0].get("id") != "cell" or float(compartments[0].get("size", "nan")) != 1.0:
        raise SimulationError("Expected one fixed cell compartment of volume 1 fL")
    initial = {s.get("id"): float(s.get("initialAmount", "nan")) for s in species}
    if tuple(initial[s] for s in SPECIES) != INITIAL_AMOUNTS:
        raise SimulationError("Unexpected initial species amounts")
    if any(s.get("hasOnlySubstanceUnits") != "true" or s.get("compartment") != "cell" for s in species):
        raise SimulationError("Species must be item amounts in the cell compartment")
    units = {u.get("id"): u.find("s:listOfUnits/s:unit", NS) for u in model.findall("s:listOfUnitDefinitions/s:unitDefinition", NS)}
    if (units["time"].get("kind") != "second" or units["time"].get("multiplier") != "60"
            or units["volume"].get("kind") != "litre" or units["volume"].get("scale") != "-15"
            or units["substance"].get("kind") != "item"):
        raise SimulationError("Unexpected SBML unit definitions")
    return {
        "model_id": MODEL_ID,
        "model_sha256": digest,
        "sbml_level": int(root.get("level", "0")),
        "sbml_version": int(root.get("version", "0")),
        "species": [{"id": s.get("id"), "name": s.get("name"), "initial_amount": initial[s.get("id")]} for s in species],
        "reaction_count": len(reactions),
        "assignment_rule_variables": [r.get("variable") for r in rules],
        "compartment": {"id": "cell", "volume_litre": 1e-15, "count": 1},
        "time_unit": "minute",
        "quantity_unit": "item_per_cell",
    }


def _solver_module() -> Any:
    try:
        for package, expected in (("libroadrunner", SOLVER_VERSION), ("numpy", NUMPY_VERSION)):
            actual = importlib.metadata.version(package)
            if actual != expected:
                raise SimulationError(f"Expected {package}=={expected}, found {actual}; install requirements.lock")
        import roadrunner
        return roadrunner
    except (ImportError, importlib.metadata.PackageNotFoundError) as exc:
        raise SimulationError(
            "Cannot load the pinned solver. Install requirements.lock in the biology venv. "
            "On nonstandard Linux Python builds, ensure the Python shared library is on the loader path. "
            f"Original error: {exc}"
        ) from exc


def make_solver(settings: Settings) -> tuple[Any, dict[str, Any]]:
    """Return a real RoadRunner object and model/solver validation information.

    This API is used by verification tests. The CLI exposes no SBML or kinetic
    parameter override and never opens an untrusted model or network URL.
    """
    inventory = inspect_model()
    roadrunner = _solver_module()
    # Verify the same bytes that will be passed to the compiler, even if the
    # model file changed after inventory inspection. Never load by URL/path.
    raw_bytes = MODEL_PATH.read_bytes()
    if hashlib.sha256(raw_bytes).hexdigest() != MODEL_SHA256:
        raise SimulationError("Bundled SBML changed during validation")
    raw = raw_bytes.decode("utf-8")
    checks = (roadrunner.VALIDATE_GENERAL | roadrunner.VALIDATE_IDENTIFIER
              | roadrunner.VALIDATE_MATHML | roadrunner.VALIDATE_OVERDETERMINED)
    structural_diagnostics = roadrunner.validateSBML(raw, checks)
    if structural_diagnostics:
        raise SimulationError(f"SBML structural validation failed: {structural_diagnostics}")
    # Unit checking is deliberately separate: this curated model has incomplete
    # parameter unit annotations. Preserve and disclose warnings, not fix the XML.
    unit_diagnostics = roadrunner.validateSBML(raw, roadrunner.VALIDATE_UNITS)
    if "[Error]" in unit_diagnostics or "[Fatal]" in unit_diagnostics:
        raise SimulationError(f"SBML unit validation reports an error: {unit_diagnostics}")
    runner = roadrunner.RoadRunner(raw)
    runner.setIntegrator("cvode")
    runner.integrator.relative_tolerance = settings.relative_tolerance
    runner.integrator.absolute_tolerance = settings.absolute_tolerance
    runner.integrator.stiff = True
    runner.integrator.variable_step_size = False
    runner.integrator.maximum_num_steps = 100000
    runner.integrator.max_output_rows = MAX_ROWS
    # Bare species identifiers select amounts, not concentration selections [X].
    runner.timeCourseSelections = ["time", *SPECIES]
    if set(runner.model.getFloatingSpeciesIds()) != set(SPECIES) or len(runner.model.getReactionIds()) != 12:
        raise SimulationError("Loaded solver inventory differs from the pinned model")
    if tuple(float(runner[s]) for s in SPECIES) != INITIAL_AMOUNTS:
        raise SimulationError("Loaded solver initial state differs from SBML")
    inventory["validation"] = {
        "structural_checks": ["general", "identifiers", "mathml", "overdetermined"],
        "structural_diagnostics": structural_diagnostics,
        "unit_warning_count": unit_diagnostics.count("[Warning]"),
        "unit_warning_codes": sorted(set(re.findall(r"\((\d+) \[Warning\]\)", unit_diagnostics))),
        "unit_consistency": "Incomplete parameter annotations; full dimensional consistency is not established",
    }
    inventory["solver_build"] = roadrunner.getVersionStr()
    inventory["parameters_at_start"] = {p: float(runner[p]) for p in runner.model.getGlobalParameterIds()}
    return runner, inventory


def validate_rows(rows: Sequence[Sequence[float]], expected_times: Sequence[float] | None = None) -> None:
    """Reject malformed, negative, or nonfinite traces. No numerical clamping."""
    if not 2 <= len(rows) <= MAX_ROWS:
        raise SimulationError(f"Trace must contain between 2 and {MAX_ROWS} rows")
    if expected_times is not None and len(rows) != len(expected_times):
        raise SimulationError("Solver returned an unexpected number of samples")
    previous = -math.inf
    for i, row in enumerate(rows):
        if len(row) != len(COLUMNS):
            raise SimulationError(f"Trace row {i} has the wrong number of columns")
        if any(not math.isfinite(float(v)) for v in row):
            raise SimulationError(f"Trace row {i} contains a nonfinite value")
        time = float(row[0])
        if time < 0 or time <= previous:
            raise SimulationError(f"Trace time must be nonnegative and strictly increasing at row {i}")
        if expected_times is not None and not math.isclose(time, expected_times[i], rel_tol=1e-12, abs_tol=1e-12):
            raise SimulationError(f"Solver sample time differs from requested time at row {i}")
        if any(float(v) < 0 for v in row[1:]):
            raise SimulationError(f"Negative species amount at row {i}; no clamping is performed. Try tighter tolerances")
        previous = time
    if float(rows[0][0]) != 0 or tuple(float(v) for v in rows[0][1:]) != INITIAL_AMOUNTS:
        raise SimulationError("Trace must begin at time zero with the unchanged SBML initial amounts")


def simulate(settings: Settings) -> tuple[list[tuple[float, ...]], dict[str, Any]]:
    runner, model = make_solver(settings)
    times = settings.times()
    try:
        runner.resetToOrigin()
        result = runner.simulate(times=times)
    except RuntimeError as exc:
        raise SimulationError(f"CVODE failed: {exc}") from exc
    if list(result.colnames) != ["time", *SPECIES]:
        raise SimulationError("Solver returned unexpected column selections")
    rows = [tuple(float(v) for v in row) for row in result]
    validate_rows(rows, times)
    provenance = {
        "format": FORMAT_VERSION,
        "model": model,
        "model_source": json.loads((BASE_DIR / "models" / "provenance.json").read_text(encoding="utf-8")),
        "solver": {"name": "libRoadRunner", "version": SOLVER_VERSION, "integrator": "CVODE",
                   "stiff": True, "maximum_num_steps": 100000, "variable_step_size": False},
        "dependencies": {"libroadrunner": SOLVER_VERSION, "numpy": NUMPY_VERSION},
        "runtime": {"python": platform.python_version(), "platform": platform.platform(), "machine": platform.machine()},
        "settings": asdict(settings),
        "trace": {"columns": list(COLUMNS), "rows": len(rows), "start_minutes": rows[0][0],
                  "end_minutes": rows[-1][0], "negative_value_policy": "reject any negative amount; no clamping"},
        "scope": "Deterministic educational synthetic E. coli repressilator; not a whole-cell, HBB, medical, or sequence-effect prediction",
    }
    return rows, provenance


def render_csv(rows: Sequence[Sequence[float]], settings: Settings) -> bytes:
    validate_rows(rows, settings.times())
    stream = io.StringIO(newline="")
    stream.write(f"# {FORMAT_VERSION}\n")
    metadata = {
        "model_id": MODEL_ID, "model_sha256": MODEL_SHA256,
        "solver": f"libRoadRunner {SOLVER_VERSION} CVODE", "time_unit": "minute",
        "quantity_unit": "item_per_cell", "relative_tolerance": format(settings.relative_tolerance, ".17g"),
        "absolute_tolerance": format(settings.absolute_tolerance, ".17g"),
        "duration_minutes": format(settings.duration_minutes, ".17g"),
        "sample_minutes": format(settings.sample_minutes, ".17g"),
    }
    for key, value in metadata.items():
        stream.write(f"# {key}={value}\n")
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(COLUMNS)
    writer.writerows([format(float(v), ".17g") for v in row] for row in rows)
    return stream.getvalue().encode("utf-8")


def _stage_file(path: Path, payload: bytes) -> Path:
    """Stage beside the target so replacement stays on the same filesystem."""
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    staged = Path(name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
    except BaseException:
        staged.unlink(missing_ok=True)
        raise
    return staged


def _publish_file(staged: Path, target: Path, overwrite: bool) -> None:
    if overwrite:
        os.replace(staged, target)
    else:
        # A preflight existence check alone is racy. A same-directory hard link
        # atomically creates the destination only if it is still absent. Never
        # fall back to replacement on filesystems without hard-link support.
        try:
            os.link(staged, target)
        except FileExistsError as exc:
            raise SimulationError(f"Output appeared during publication; no existing file was replaced: {target}") from exc


def write_outputs(output: Path, rows: Sequence[Sequence[float]], provenance: dict[str, Any],
                  settings: Settings, overwrite: bool = False) -> tuple[Path, Path]:
    """Write individually atomic CSV/JSON; CSV is the last committed artifact.

    Two filenames cannot be replaced as a single filesystem transaction. The
    sidecar records the exact CSV hash so a consumer can detect a mismatched pair.
    A failed second publication may leave only the new JSON or a mismatched pair.
    Without overwrite, hard-link publication never replaces a concurrent file.
    """
    output = Path(output).absolute()
    if output.suffix.lower() != ".csv":
        raise SimulationError("Output path must end in .csv")
    sidecar = output.with_suffix(".json")
    for path in (output, sidecar):
        if path.is_symlink():
            raise SimulationError(f"Refusing a symlink output: {path}")
        if path.exists() and (not overwrite or not path.is_file()):
            raise SimulationError(f"Output already exists (use --overwrite for files): {path}")
    payload = render_csv(rows, settings)
    document = dict(provenance)
    document["trace"] = dict(provenance["trace"], csv_filename=output.name, csv_sha256=hashlib.sha256(payload).hexdigest())
    json_payload = (json.dumps(document, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    staged: list[Path] = []
    try:
        staged_csv = _stage_file(output, payload)
        staged.append(staged_csv)
        staged_json = _stage_file(sidecar, json_payload)
        staged.append(staged_json)
        _publish_file(staged_json, sidecar, overwrite)
        _publish_file(staged_csv, output, overwrite)
    finally:
        for path in staged:
            path.unlink(missing_ok=True)
    return output, sidecar


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration-minutes", type=float, default=600.0, help="Simulated duration, 0.01–10080 (default: 600)")
    parser.add_argument("--sample-minutes", type=float, default=1.0, help="Output spacing, minimum 0.01 (default: 1)")
    parser.add_argument("--relative-tolerance", type=float, default=1e-8)
    parser.add_argument("--absolute-tolerance", type=float, default=1e-10)
    parser.add_argument("--output", type=Path, default=BASE_DIR / "output" / "repressilator.csv")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing CSV/JSON files atomically per file")
    args = parser.parse_args(argv)
    try:
        settings = Settings(args.duration_minutes, args.sample_minutes, args.relative_tolerance, args.absolute_tolerance)
        rows, provenance = simulate(settings)
        output, sidecar = write_outputs(args.output, rows, provenance, settings, args.overwrite)
    except (SimulationError, OSError, RuntimeError) as exc:
        parser.exit(2, f"error: {exc}\n")
    print(f"Wrote {len(rows)} samples, 0–{settings.duration_minutes:g} minutes, with libRoadRunner {SOLVER_VERSION} CVODE")
    print(f"Trace: {output}\nProvenance: {sidecar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
