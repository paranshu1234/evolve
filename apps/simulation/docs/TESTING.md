# Verification

## Experimental voice verification

Local checks on 2026-10-04: clean Linux CMake Release configure/build and all three CTest executables passed (34 existing core checks, 327 voice checks, 125 provider checks). GCC strict-warning builds passed. ASan/UBSan fixtures passed with leak detection disabled because LeakSanitizer is unsupported under this environment's ptrace. WindowsVoice, VoicePanel, Main and ConversationalProvider translation units cross-compiled for Windows x64 using Zig 0.16/MinGW and Microsoft's DirectXMath headers. Cross-compilation does not prove MSVC, renderer runtime, microphone, TTS or live API behavior.

The voice branch adds separate `evolve_voice_tests` and `evolve_provider_tests` executables. These exercise local parsing, strict action validation, request/revision/epoch guards, one-use confirmations, cancellation, replay rejection, JSON/provider fixtures, bounded conversational context and delayed transport completions without live audio or paid API calls.

The WARP smoke path also opens the native Voice Agent panel, sends a typed selection through its Send control, rejects a pending restore, checks data preservation, and closes the panel. It asserts that microphone and cloud remain disabled. This is not live speech or cloud evidence.

Before merging, run the existing Windows build/CTest/WARP workflow for the exact final commit and perform the [manual voice checks](VOICE.md). With explicit provider/data/billing approval and a user-provisioned key, separately verify live conversation, a safe edit plus undo, cancellation, follow-up reference, missing credentials, and provider failure. Do not replace these gates with fixture results.

## Monorepo verification performed on 2026-09-26

- Clean configure and Release build passed with Visual Studio 2026 / MSVC 19.51.
- CTest passed all 34 core/geometry checks from `apps/simulation`.
- CMake installation assembled the executable, README, changelog, examples, and component documentation.
- Windows WARP smoke test passed: DX12 initialization, geometry readback (32,698 visible pixels), orbit/zoom, resize, compare, edit, mock analysis, and save/reopen.
- The C++ implementation files are unchanged by the directory migration. MSVC exception unwinding is now explicitly enabled with `/EHsc` in CMake.
- Hardware-driver, accessibility, and full manual desktop validation remain separate checks.

## Historical verification performed on 2026-09-18

- Linux CMake Release configure/build and CTest: passed; all 34 project/geometry checks passed.
- Existing console skeleton: compiled and ran successfully.
- Desktop and renderer: cross-compiled for Windows x64 with LLVM/MinGW (Zig 0.16), using Microsoft's DirectXMath headers, and linked into a Windows executable. This checks native code and Windows API linkage; it is not an MSVC build or runtime test.
- GitHub Actions run 35302221039 stopped with failed jobs before any job steps were recorded; build logs were unavailable. The cause was not established from the available metadata.
- Windows runtime smoke test, GPU screenshot, and manual UI validation remain pending. No rendered screenshot is claimed.

## Automated

`evolve_tests` exercises 34 conditions, including FASTA normalization, format/size errors, immutable baseline, undo/redo branching, stale-result rejection, serialization, non-destructive failed loads/imports, bounded meshes, and finite geometry.

The Windows workflow compiles all desktop code and runs `Evolve.exe --warp --smoke-test`. This launches the real application, makes an edit, toggles baseline comparison, orbits/zooms, resizes, renders and reads back a BMP, asserts visible geometry, completes the mock analysis and reopens a saved project. It exits nonzero and writes `smoke-test-error.log` on failure. The workflow enforces an external 60-second timeout.

Artifacts from a successful Windows run: the portable app package; a separate evidence package with the viewport BMP, smoke log and round-tripped project. Inspect the actual workflow result; the presence of this workflow file alone does not establish a passing Windows build.

## Manual desktop checks

- Launch on hardware and with `--warp`; confirm lit helix and readable UI.
- Orbit, pan and zoom, then frame-all. Resize and minimize/restore. Move the lighting slider.
- Click bases; verify list selection and inspector agree. Edit each letter; verify complementary colors and gold changed-base markers.
- Compare; confirm dim baseline left and scenario right. Toggle grid and rotation.
- Undo, redo, branch a new edit after undo, restore baseline, then undo the restoration.
- Import `examples/demo.fasta`. Reject invalid characters and multiple records without losing the current project.
- Save/open using a Unicode path. Cancel a save dialog and verify unsaved state remains. Verify the close prompt.
- Run/cancel analysis, edit during a run, and export only a completed current result.
- Traverse controls with Tab; check Ctrl+S, Ctrl+Z and Ctrl+Y. Optional sound should play only when enabled.

The smoke test is not a substitute for manual mouse/keyboard, accessibility, multi-monitor and hardware-driver testing.
