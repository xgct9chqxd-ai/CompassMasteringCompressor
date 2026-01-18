# Phase 1.0 — Runtime Conditions Lock (Determinism)

This document is binding for Phase 1.0. It defines "runtime conditions identical" with no deferrals.

## 1) Code Identity
- Same source revision (same commit/tag) for all comparisons.
- Same build files (CMakeLists and reference_* sources) with no local edits.

## 2) Toolchain Identity
- Same compiler and version.
- Same CMake version.
- Same build type (e.g., Debug vs Release) when comparing outputs.

## 3) Floating-Point & Flags
- No experimental/unstable floating-point modes.
- No per-run or per-machine changes to floating-point related compiler flags.
- The harness/tests must not depend on undefined behavior.

## 4) Prohibited Inputs
- Randomness is prohibited (no RNG, no time-based seeds, no nondeterministic sources).
- Wall-clock time is prohibited as an input (no sleep, timers, timestamps for logic).
- Thread scheduling is prohibited as a dependency (single-threaded execution only).
- External I/O variability is prohibited (no network, no environment-variable driven logic).

## 5) Execution Boundary
- Offline executables only (reference_harness and reference_tests).
- No plugin host, no UI, no message-loop requirements.

## 5.1) Code-Style Hard Rules (Phase 1.0)

These are binding implementation constraints for all Phase 1.0 reference targets (reference_core, reference_harness, reference_tests):

- DSP path uses `double` exclusively.
- Dynamic allocation inside `processBlock` is prohibited.
- Wall-clock time is prohibited as an input (no sleeps, timers, timestamps for logic).
- Threading is prohibited (single-threaded execution only).
- Locks are prohibited (no mutexes/spinlocks/waits).
- SIMD is prohibited.

## 6) Runtime Conditions Identical (Definition Used by Tests)

For Phase 1.0, tests define "runtime conditions identical" as the following fixed, scripted boundary:

- Fixed sample rate (one explicit value per run; no mid-run changes).
- Fixed block size sequence:
  - The sequence may be constant or variable, but it must be a predefined scripted sequence.
  - No dependence on host scheduling or timing; the sequence is the schedule.
- Explicit reset events:
  - Reset points are declared and applied only at scripted boundaries (e.g., TransportStop, TransportStart).
- Deterministic initial state:
  - State is zeroed, then `prepare(...)` is called, then a single `reset(ResetReason::Prepare)` is applied before first processing.
- No dependence on host scheduling/UI timing:
  - No UI callbacks, no message loop, no sleep/timers, no thread scheduling dependencies.

## 7) Pass Condition (Phase 1.0)
- For the Phase 1.0 sanity checks, program behavior is judged by:
  - Exit code correctness (PASS = 0, FAIL != 0)
  - Deterministic text output (stable strings, no variable data)

Status: LOCKED for Phase 1.0.
