"""Numerical and contract tests against the real, pinned libRoadRunner solver."""
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import repressilator as circuit


class ModelTests(unittest.TestCase):
    def test_pinned_bytes_and_license_provenance(self):
        manifest = json.loads((circuit.BASE_DIR / "models/provenance.json").read_text())
        self.assertEqual(manifest["sha256"], circuit.MODEL_SHA256)
        self.assertEqual(hashlib.sha256(circuit.MODEL_PATH.read_bytes()).hexdigest(), circuit.MODEL_SHA256)
        license_path = circuit.BASE_DIR / "models" / manifest["license_file"]
        self.assertEqual(hashlib.sha256(license_path.read_bytes()).hexdigest(), manifest["license_sha256"])
        self.assertEqual(manifest["source_commit"], "7de3848305224ace0523c1328ac3423d2c007f68")
        self.assertFalse(manifest["modified"])

    def test_inventory_initial_amounts_and_units(self):
        info = circuit.inspect_model()
        self.assertEqual(len(info["species"]), 6)
        self.assertEqual(info["reaction_count"], 12)
        self.assertEqual(len(info["assignment_rule_variables"]), 9)
        self.assertEqual({s["id"]: s["initial_amount"] for s in info["species"]}, dict(zip(circuit.SPECIES, circuit.INITIAL_AMOUNTS)))
        self.assertEqual(info["compartment"]["volume_litre"], 1e-15)
        self.assertEqual(info["time_unit"], "minute")
        self.assertEqual(info["quantity_unit"], "item_per_cell")

    def test_unpinned_dependency_is_rejected(self):
        with mock.patch.object(circuit.importlib.metadata, "version", return_value="0.0.0"):
            with self.assertRaisesRegex(circuit.SimulationError, "Expected libroadrunner==2.10.0"):
                circuit.make_solver(circuit.Settings())

    def test_changed_sbml_is_rejected_before_loading(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "altered.xml"
            path.write_bytes(circuit.MODEL_PATH.read_bytes() + b"\n")
            with self.assertRaisesRegex(circuit.SimulationError, "SHA-256 mismatch"):
                circuit.inspect_model(path)

    def test_real_solver_assignment_rules(self):
        solver, info = circuit.make_solver(circuit.Settings())
        self.assertEqual(solver.integrator.getName(), "cvode")
        self.assertEqual(info["validation"]["structural_diagnostics"], "")
        self.assertEqual(info["validation"]["unit_warning_count"], 21)
        self.assertEqual(info["validation"]["unit_warning_codes"], ["99505"])
        expected = {
            "t_ave": 2 / math.log(2), "kd_mRNA": math.log(2) / 2,
            "kd_prot": math.log(2) / 10, "k_tl": 20 * math.log(2) / 2,
            "a_tr": (0.5 - 0.0005) * 60, "a0_tr": 0.0005 * 60,
            "beta": 0.2, "alpha": 29.97 * 20 * 10 / (math.log(2) * 40),
            "alpha0": 0.03 * 20 * 10 / (math.log(2) * 40),
        }
        for key, value in expected.items():
            with self.subTest(parameter=key):
                self.assertAlmostEqual(float(solver[key]), value, places=12)
        # The XML placeholder value 216.404 must not replace its assignment rule.
        self.assertNotAlmostEqual(float(solver["alpha"]), 216.404, places=3)
        initial_rates = dict(zip(solver.model.getFloatingSpeciesIds(), solver.getRatesOfChange()))
        for species, rate in {"X": 30, "Y": 30 - 20 * math.log(2) / 2, "Z": 30,
                              "PX": 0, "PY": 20 * 20 * math.log(2) / 2, "PZ": 0}.items():
            self.assertAlmostEqual(float(initial_rates[species]), rate, places=11)


class NumericalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.settings = circuit.Settings()
        cls.rows, cls.provenance = circuit.simulate(cls.settings)

    def test_default_trajectory_is_finite_nonnegative_and_sampled(self):
        self.assertEqual(len(self.rows), 601)
        self.assertEqual(self.rows[0], (0, 0, 20, 0, 0, 0, 0))
        self.assertEqual([row[0] for row in self.rows], list(range(601)))
        self.assertTrue(all(math.isfinite(v) and v >= 0 for row in self.rows for v in row))
        self.assertGreater(max(row[4] for row in self.rows), 1000)
        self.assertEqual(self.provenance["dependencies"], {"libroadrunner": "2.10.0", "numpy": "2.2.6"})

    def test_fresh_run_is_deterministic(self):
        rerun, _ = circuit.simulate(self.settings)
        self.assertEqual(self.rows, rerun)

    def test_reset_restores_initial_state_parameters_and_trajectory(self):
        solver, _ = circuit.make_solver(self.settings)
        times = list(range(101))
        original = solver.simulate(times=times).tolist()
        solver["Y"] = 91
        solver["ps_a"] = 0
        solver["ps_0"] = 0
        solver.resetToOrigin()
        self.assertEqual(float(solver["time"]), 0)
        self.assertEqual(tuple(float(solver[s]) for s in circuit.SPECIES), circuit.INITIAL_AMOUNTS)
        self.assertEqual(float(solver["ps_a"]), 0.5)
        self.assertEqual(float(solver["ps_0"]), 0.0005)
        self.assertEqual(original, solver.simulate(times=times).tolist())

    @staticmethod
    def normalized_error(rows, reference):
        return max(abs(a - b) / (1 + abs(b)) for row, ref in zip(rows, reference) for a, b in zip(row[1:], ref[1:]))

    def test_tighter_tolerances_converge(self):
        tight, _ = circuit.simulate(circuit.Settings(relative_tolerance=1e-10, absolute_tolerance=1e-12))
        reference, _ = circuit.simulate(circuit.Settings(relative_tolerance=1e-11, absolute_tolerance=1e-13))
        default_error = self.normalized_error(self.rows, reference)
        tight_error = self.normalized_error(tight, reference)
        self.assertLess(default_error, 1e-4)
        self.assertLess(tight_error, 1e-6)
        self.assertLess(tight_error, default_error / 5)

    def test_chunked_integration_matches_whole_run(self):
        solver, _ = circuit.make_solver(self.settings)
        chunked = []
        for start in range(0, 600, 100):
            result = solver.simulate(times=list(range(start, start + 101))).tolist()
            chunked.extend(result if start == 0 else result[1:])
        circuit.validate_rows(chunked, self.settings.times())
        self.assertEqual(len(chunked), len(self.rows))
        self.assertLess(self.normalized_error(chunked, self.rows), 1e-4)

    def test_analytic_decay_with_both_transcription_parameters_zero(self):
        solver, _ = circuit.make_solver(circuit.Settings(relative_tolerance=1e-11, absolute_tolerance=1e-13))
        # Change the two SBML independent parameters, not derived a_tr/alpha.
        solver["ps_a"] = 0
        solver["ps_0"] = 0
        self.assertEqual(float(solver["a_tr"]), 0)
        self.assertEqual(float(solver["a0_tr"]), 0)
        rows = solver.simulate(times=list(range(41)))
        kd_mrna, kd_protein = math.log(2) / 2, math.log(2) / 10
        for row in rows:
            time, x, y, z, px, py, pz = [float(v) for v in row]
            expected_y = 20 * math.exp(-kd_mrna * time)
            expected_py = 20 * kd_mrna * 20 / (kd_mrna - kd_protein) * (math.exp(-kd_protein * time) - math.exp(-kd_mrna * time))
            self.assertAlmostEqual(y, expected_y, delta=5e-8)
            self.assertAlmostEqual(py, expected_py, delta=5e-7)
            self.assertEqual((x, z, px, pz), (0, 0, 0, 0))


class InputAndOutputTests(unittest.TestCase):
    def test_reject_invalid_settings(self):
        invalid = [
            {"duration_minutes": 0}, {"duration_minutes": -1}, {"duration_minutes": float("nan")},
            {"duration_minutes": float("inf")}, {"duration_minutes": 10081},
            {"sample_minutes": 0}, {"sample_minutes": 0.001}, {"sample_minutes": 601},
            {"sample_minutes": True}, {"duration_minutes": 10080, "sample_minutes": 0.01},
            {"relative_tolerance": 0}, {"relative_tolerance": 1e-13}, {"relative_tolerance": float("nan")},
            {"absolute_tolerance": -1}, {"absolute_tolerance": 1e-15}, {"absolute_tolerance": 0.1},
        ]
        for values in invalid:
            with self.subTest(values=values), self.assertRaises(circuit.SimulationError):
                circuit.Settings(**values)

    def test_time_grid_has_exact_end_and_supports_partial_interval(self):
        self.assertEqual(circuit.Settings(duration_minutes=2.5).times(), [0, 1, 2, 2.5])
        self.assertEqual(len(circuit.Settings(duration_minutes=0.3, sample_minutes=0.1).times()), 4)
        times = circuit.Settings(duration_minutes=10000, sample_minutes=0.1).times()
        self.assertEqual(len(times), circuit.MAX_ROWS)
        self.assertEqual(times[-1], 10000)

    def test_nonfinite_negative_and_malformed_output_is_rejected(self):
        valid = [[0, *circuit.INITIAL_AMOUNTS], [1, 1, 1, 1, 1, 1, 1]]
        for value in (float("nan"), float("inf"), -1e-30):
            rows = [row[:] for row in valid]
            rows[1][2] = value
            with self.subTest(value=value), self.assertRaises(circuit.SimulationError):
                circuit.validate_rows(rows)
        for rows in (valid[:1], [valid[0], valid[0]], [valid[0], valid[1][:-1]], [[0, 0, 19, 0, 0, 0, 0], valid[1]]):
            with self.assertRaises(circuit.SimulationError):
                circuit.validate_rows(rows)
        with self.assertRaises(circuit.SimulationError):
            circuit.validate_rows(valid, [0, 2])

    def test_trace_contract_provenance_and_safe_overwrite(self):
        settings = circuit.Settings(duration_minutes=2.5)
        rows, provenance = circuit.simulate(settings)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "trace.csv"
            output, sidecar = circuit.write_outputs(target, rows, provenance, settings)
            raw = output.read_bytes()
            lines = raw.decode().splitlines()
            self.assertEqual(lines[0], "# evolve-circuit-trace-v1")
            self.assertIn("# solver=libRoadRunner 2.10.0 CVODE", lines)
            self.assertIn("# model_sha256=" + circuit.MODEL_SHA256, lines)
            self.assertIn("# model_id=BIOMD0000000012", lines)
            self.assertIn("# time_unit=minute", lines)
            self.assertIn("# quantity_unit=item_per_cell", lines)
            parsed = list(csv.reader(line for line in lines if not line.startswith("#")))
            self.assertEqual(parsed[0], list(circuit.COLUMNS))
            self.assertEqual([tuple(map(float, row)) for row in parsed[1:]], rows)
            document = json.loads(sidecar.read_text())
            self.assertEqual(document["trace"]["csv_sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(document["trace"]["csv_filename"], "trace.csv")
            with self.assertRaises(circuit.SimulationError):
                circuit.write_outputs(target, rows, provenance, settings)
            circuit.write_outputs(target, rows, provenance, settings, overwrite=True)
            self.assertEqual(raw, target.read_bytes())
            self.assertEqual(sorted(p.name for p in Path(folder).iterdir()), ["trace.csv", "trace.json"])
            with self.assertRaises(circuit.SimulationError):
                circuit.write_outputs(Path(folder) / "trace.xml", rows, provenance, settings)

    def test_failed_staging_preserves_existing_files_and_cleans_temporary(self):
        settings = circuit.Settings(duration_minutes=1)
        rows, provenance = circuit.simulate(settings)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "trace.csv"
            sidecar = target.with_suffix(".json")
            target.write_text("old CSV")
            sidecar.write_text("old JSON")
            original_stage = circuit._stage_file
            calls = 0
            def fail_second(path, payload):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("simulated disk failure")
                return original_stage(path, payload)
            with mock.patch.object(circuit, "_stage_file", side_effect=fail_second), self.assertRaises(OSError):
                circuit.write_outputs(target, rows, provenance, settings, overwrite=True)
            self.assertEqual(target.read_text(), "old CSV")
            self.assertEqual(sidecar.read_text(), "old JSON")
            self.assertEqual(len(list(Path(folder).iterdir())), 2)

    def test_concurrent_producer_is_never_overwritten(self):
        settings = circuit.Settings(duration_minutes=1)
        rows, provenance = circuit.simulate(settings)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "trace.csv"
            original_stage = circuit._stage_file
            calls = 0
            def concurrent_writer(path, payload):
                nonlocal calls
                staged = original_stage(path, payload)
                calls += 1
                if calls == 2:
                    # Another producer wins after our checks and staging.
                    target.write_text("concurrent producer CSV")
                return staged
            with mock.patch.object(circuit, "_stage_file", side_effect=concurrent_writer):
                with self.assertRaisesRegex(circuit.SimulationError, "appeared during publication"):
                    circuit.write_outputs(target, rows, provenance, settings)
            self.assertEqual(target.read_text(), "concurrent producer CSV")
            # Two-file publication is not a transaction: the sidecar may have
            # been committed first, and its hash identifies the mismatched pair.
            sidecar = json.loads(target.with_suffix(".json").read_text())
            self.assertNotEqual(sidecar["trace"]["csv_sha256"], hashlib.sha256(target.read_bytes()).hexdigest())
            self.assertEqual(sorted(p.name for p in Path(folder).iterdir()), ["trace.csv", "trace.json"])

    def test_no_replace_publication_fails_safely_without_hardlink_support(self):
        settings = circuit.Settings(duration_minutes=1)
        rows, provenance = circuit.simulate(settings)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "trace.csv"
            with mock.patch.object(circuit.os, "link", side_effect=OSError("hard links unavailable")):
                with self.assertRaisesRegex(OSError, "hard links unavailable"):
                    circuit.write_outputs(target, rows, provenance, settings)
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_checked_in_example_matches_its_hash_and_numerical_baseline(self):
        example = circuit.BASE_DIR / "examples/repressilator.csv"
        provenance = json.loads(example.with_suffix(".json").read_text())
        self.assertEqual(hashlib.sha256(example.read_bytes()).hexdigest(), provenance["trace"]["csv_sha256"])
        settings = circuit.Settings(**provenance["settings"])
        parsed = list(csv.reader(line for line in example.read_text().splitlines() if not line.startswith("#")))
        rows = [tuple(map(float, row)) for row in parsed[1:]]
        circuit.validate_rows(rows, settings.times())
        rerun, _ = circuit.simulate(settings)
        self.assertLess(NumericalTests.normalized_error(rerun, rows), 1e-4)

    def test_symlink_outputs_are_rejected(self):
        settings = circuit.Settings(duration_minutes=1)
        rows, provenance = circuit.simulate(settings)
        with tempfile.TemporaryDirectory() as folder:
            original = Path(folder) / "original.csv"
            original.write_text("untouched")
            target = Path(folder) / "linked.csv"
            try:
                target.symlink_to(original)
            except (OSError, NotImplementedError):
                self.skipTest("Symlink creation is unavailable for this account")
            with self.assertRaisesRegex(circuit.SimulationError, "symlink"):
                circuit.write_outputs(target, rows, provenance, settings, overwrite=True)
            self.assertEqual(original.read_text(), "untouched")

    def test_cli_offline_smoke_and_invalid_input(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "cli.csv"
            command = [sys.executable, str(circuit.BASE_DIR / "repressilator.py"), "--duration-minutes", "2", "--output", str(target)]
            run = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("Wrote 3 samples", run.stdout)
            self.assertTrue(target.with_suffix(".json").exists())
            bad = subprocess.run(command + ["--sample-minutes", "nan"], capture_output=True, text=True, check=False)
            self.assertEqual(bad.returncode, 2)
            self.assertIn("must be finite", bad.stderr)
            self.assertNotIn("Traceback", bad.stderr)


if __name__ == "__main__":
    unittest.main()
