# `implement_with` — Implementation Status

**Branch:** `alexreinking/implement_with` (worktree at
`/var/home/alex/dev/Halide/implement_with`)
**Current phase:** Phase 2 complete; Phase 3 not started.
**Last updated:** 2026-05-25 (session 2)

This file is the single source of truth for *where we are*. The design doc
([`DESIGN.md`](DESIGN.md)) is the spec; this file is the trace. Update at
the end of every session.

---

## Phase status

| Phase | Description                              | Status      | Notes |
|-------|------------------------------------------|-------------|-------|
| 1     | Infrastructure (inert API)               | **Done**    | Session 1. Two commits on branch. |
| 2     | Spec-pattern Funcs                       | **Done**    | Session 2. Two commits on branch. |
| 3     | Schedule transfer + constraint install   | Not started | Next phase. |
| 4     | Matcher (canonicalization + structural)  | Not started | Most complex phase. Resolve OQ#5 first. |
| 5     | Emit + lowering integration              | Not started | |
| 6     | Diagnostics                              | Not started | |
| 7 (v1.5) | Affine match parameters               | Not started | |

---

## Phase 2 — what landed (session 2, 2026-05-25)

### Source changes (committed, 2 commits)

- **`src/Function.cpp`** / **`src/Function.h`** — `bool is_spec_pattern` in
  `FunctionContents`; deep-copy propagation; `Function::mark_as_spec_pattern()`
  and `Function::is_spec_pattern()` getter/setter.
- **`src/Pipeline.cpp`** — materialization guard at the top of
  `compile_to_module()`: errors with a clear message if any output Func is
  spec-pattern, before any other compile-path checks fire.
- **`src/Instruction.h`** — `Internal::in_spec_thunk()` declaration;
  updated `spec()` docstring; updated file-level phase comment.
- **`src/Instruction.cpp`** — thread-local `g_spec_thunk_depth` counter and
  `Internal::SpecThunkScope` RAII guard; `Instruction::spec()` now wraps the
  thunk call in this scope and marks all output Funcs spec-pattern after the
  thunk returns.
- **`src/Func.cpp`** — `FuncRef::operator Expr()` extended: when called inside
  a spec thunk for an undefined Func, auto-stubs the Func with a zero body
  (typed from `required_types` if set, `Int(32)` otherwise) so that downstream
  scheduling directives (`bound`, `vectorize`, etc.) can be applied.
- **`test/correctness/implement_with_phase1.cpp`** — stubs for `a`, `b`, `c`
  removed; constructors changed to `Func(Float(32), 1, "name")` to declare
  element types. Test still passes.
- **`test/correctness/implement_with_phase2.cpp`** (new) — five sub-tests.
- **`test/correctness/CMakeLists.txt`** — phase2 test registered.

### Design decision made under uncertainty

The design doc §3.3 example uses bare `Func a("a")` (no type annotation) for
undefined spec input Funcs.  Phase 2 requires explicit type declarations:
`Func(Float(32), 1, "a")`.  Bare `Func("a")` auto-stubs with `Int(32)`, which
may cause type mismatches in Phase 4's matcher.  Full type-polymorphic specs
are v2.  See [`DECISIONS.md`](DECISIONS.md#spec-input-func-type-declarations).

### Tests

| Path                                                  | Status | Notes |
|-------------------------------------------------------|--------|-------|
| `test/correctness/implement_with_phase1.cpp`          | Passes | Updated: stubs removed, typed Funcs. |
| `test/correctness/implement_with_phase2.cpp`          | Passes | 5 sub-tests. Exception path skipped if built without exceptions. |

---

## Phase 1 — what landed (session 1, 2026-05-25)

### Source changes (uncommitted)

- **`src/Instruction.h`** (new) — `Instruction`, `Instruction::Builder`,
  `MatchContext`, `ImplementMode`, and `Internal::ImplementWithDirective`.
- **`src/Instruction.cpp`** (new) — Builder validation and `Instruction`
  accessors. No matching/lowering logic; spec/emit callbacks are stored
  and callable but inert.
- **`src/Func.h`** — `#include "Instruction.h"`. Added
  `Stage::implement_with` and `Func::implement_with` (single + multi-output
  overloads).
- **`src/Func.cpp`** — implementations. `Stage::implement_with` records on
  `StageSchedule`; `Func::implement_with` delegates to the init `Stage`.
- **`src/Schedule.h`** — forward-decl `ImplementWithDirective`; added
  `implement_with_directives()` accessors on `StageSchedule`.
- **`src/Schedule.cpp`** — storage in `StageScheduleContents`; copied in
  `StageSchedule::get_copy()`.
- **`src/CMakeLists.txt`** — header + source lists updated.
- **`docs/implement_with/DESIGN.md`** (moved from
  `/var/home/alex/dev/Halide/implement_with_design.md`) — OQ#2 marked
  resolved; changelog appended; `require` vs `requires` noted.
- **`docs/implement_with/IMPLEMENTATION_STATUS.md`** (this file).
- **`docs/implement_with/DECISIONS.md`**.
- **`.gitignore`** — `docs/implement_with/SCRATCH.md`.

### Tests

| Path                                                  | Status | Notes |
|-------------------------------------------------------|--------|-------|
| `test/correctness/implement_with_phase1.cpp`          | Passes | 6 sub-tests: Builder happy path, init-stage recording, update-stage recording, multi-output co-output names, `StageSchedule::get_copy()` preservation, minimum-valid Builder. |

The test should be kept green by every subsequent phase. If a Phase N
change breaks it, fix the change — don't relax the test.

### Build

Verified with:

```
toolbox enter fedora-toolbox-43
# deps: cmake ninja-build gcc-c++ zlib-devel (one-time)
uv pip install --python .venv --index halide=https://pypi.halide-lang.org/simple \
    "halide-llvm~=23.0.0.dev0"
export Halide_LLVM_ROOT="$(./.venv/bin/halide-llvm --prefix)"
cmake -G Ninja -S . -B build/phase1 -DCMAKE_BUILD_TYPE=Release \
    -DHalide_LLVM_ROOT="$Halide_LLVM_ROOT" \
    -DWITH_SERIALIZATION=OFF -DWITH_PYTHON_BINDINGS=OFF \
    -DWITH_TUTORIALS=OFF -DWITH_DOCS=OFF -DWITH_UTILS=OFF \
    -DWITH_AUTOSCHEDULERS=OFF -DHalide_WASM_BACKEND=OFF -DBUILD_SHARED_LIBS=ON
ninja -C build/phase1 -j $(nproc) correctness_implement_with_phase1
./build/phase1/test/correctness/correctness_implement_with_phase1
# → Success!
```

`WITH_SERIALIZATION=OFF` is load-bearing — see "Open issues" below.

### Resolved open questions

- **OQ#2 (spec-Func naming)** — explicit `Func("name")`. See
  [`DECISIONS.md`](DECISIONS.md#oq2-spec-func-naming).

### Decisions made under uncertainty

See [`DECISIONS.md`](DECISIONS.md). Highlights from session 1:
- Builder method named `require` (singular) instead of `requires`.
- `StageSchedule` got new storage rather than a side-table.
- Phase 1 doesn't touch `Function::Contents`; spec-pattern Func mode is
  deferred to Phase 2 as originally planned.

---

## Open issues / things to plan around

### Carried into Phase 3

- **Schedule transfer (§4.2–§4.3, Phase 3):** Spec Funcs carry
  scheduling directives as contractual constraints. Phase 3 transfers
  these to the corresponding matched user Funcs at `implement_with`
  call time so they participate in bounds inference. This is the
  next phase's primary task.
- **Canonicalization prefix (OQ#5):** must be pinned down before Phase
  4 starts. Not yet decided.

### Cross-cutting

- **Serialization:** `WITH_SERIALIZATION=ON` (the default) will silently
  drop `implement_with` directives across serialize/deserialize today.
  Fix when Phase 2 or 3 adds anything load-bearing to the directive.
  Until then, builds with serialization enabled may quietly lose
  directives — file an assert that fires on first serialization of a
  schedule with non-empty `implement_with_directives` so it can't slip.

### Branching strategy

- All phases land on `alexreinking/implement_with` (single integration
  branch). Per-phase feature branches are *not* used.
- **Commits are PR-units.** The upstream workflow uses gh-stack (or
  similar) to turn each commit into its own PR, so each commit must
  stand on its own as a reviewable change. Split aggressively along
  natural seams: docs scaffolding separate from code, infrastructure
  separate from semantics, etc. Avoid mega-commits even when the
  phase is small.

---

## Session handoff — next session start here

1. **Begin Phase 3 (schedule transfer + constraint installation).**
   Read §4.2–§4.3 of DESIGN.md carefully. Per §8.2:
   - At `implement_with` schedule application time, identify the
     corresponding user Func for each spec pipeline Func (spec output →
     user primary/co-output by name; spec inputs → matched by name
     from the user's pipeline body, or by position).
   - Transfer every scheduling directive on each spec Func to the
     corresponding user Func. These directives then participate in
     normal bounds inference and layout determination.
   - Target-feature checking: error if `instr.required_features()` are
     not all enabled in the compile Target (at schedule-application
     time or lowering time per OQ#1 — see DESIGN.md §7.1).
   - Tests: schedules that violate transferred constraints error cleanly
     with helpful diagnostics.
2. **Resolve OQ#4** in the design doc — Phase 2 is landed; record
   the auto-stub decision in DESIGN.md's open-questions section.
3. **Serialization guard (cross-cutting):** Before Phase 3 makes
   directives load-bearing (they will influence bounds inference),
   add an `internal_assert` in the serializer that fires when
   `implement_with_directives` is non-empty, so that the silent-drop
   behavior can't silently corrupt a pipeline.
