# Phase 1.0 — Golden Spec Index (Invariants → Enforcement)

Status: LOCKED for Phase 1.0.

This document is a **truthful enforcement index**.
It distinguishes between:
- **Test-Enforced Invariants** (actively exercised by fixtures), and
- **Binding-Only Invariants** (constitutionally locked but not yet test-exercised).

The index must **never claim enforcement that does not exist**.

---

## Test Inventory (Phase 1.0)

### T001 — reference_harness smoke
- Executable: `reference_harness`
- Pass condition: deterministic output containing `reference_harness OK`
- Fixture: `reference_harness/Source/main.cpp`

### T002 — reference_tests smoke
- Executable: `reference_tests`
- Pass condition: deterministic output containing `reference_tests PASS`
- Fixture: `reference_tests/Source/main.cpp`

---

## A) Test-Enforced Invariants (Phase 1.0)

These invariants are **actively enforced** by the current fixtures.

### E001 — Offline execution boundary
- Invariant: Execution occurs only in offline executables (no host, no UI, no message loop).
- Enforced by: T001, T002
- Fixtures: `reference_harness/Source/main.cpp`, `reference_tests/Source/main.cpp`

### E002 — Deterministic program outcome
- Invariant: PASS = exit code 0 with stable text output.
- Enforced by: T001, T002
- Fixtures: `reference_harness/Source/main.cpp`, `reference_tests/Source/main.cpp`

### E003 — No wall-clock or scheduling dependence
- Invariant: No sleeps, timers, timestamps, UI timing, or thread scheduling.
- Enforced by: T001, T002
- Fixtures: `reference_harness/Source/main.cpp`, `reference_tests/Source/main.cpp`

---

## B) Binding-Only Invariants (Contract-Locked, Not Yet Test-Exercised)

These invariants are **binding** for Phase 1.0 but are **not yet exercised by fixtures**.
They must not be claimed as test-enforced until explicit tests exist.

### B001 — Fixed sample rate
- Invariant: One explicit sample rate per run; no mid-run changes.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B002 — Scripted block size sequence
- Invariant: Block sizes follow a predefined scripted sequence.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B003 — Explicit reset events
- Invariant: Reset only at declared transport boundaries.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B004 — Deterministic initial state
- Invariant: Zeroed → prepare → reset(Prepare) before processing.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B005 — DSP path uses double exclusively
- Invariant: All DSP arithmetic uses `double`.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B006 — No dynamic allocation inside processBlock
- Invariant: No heap allocation during processing.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

### B007 — No threading, locks, or SIMD
- Invariant: Single-threaded, lock-free, no SIMD usage.
- Source: `reference_core/RUNTIME_CONDITIONS_LOCK.md`

---

## Enforcement Rule (Non-Negotiable)

An invariant **may not be listed as test-enforced** unless:
- A concrete test name exists, and
- A fixture demonstrably exercises the invariant.

Otherwise, it must remain **Binding-Only**.

---

End of Phase 1.0 Golden Spec Index.
