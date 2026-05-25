# `implement_with` — Implementation Status

**Branch:** `alexreinking/implement_with` (worktree at
`/var/home/alex/dev/Halide/implement_with`)
**Current phase:** Phase 1 complete; Phase 2 not started.
**Last updated:** 2026-05-25 (session 1)

This file is the single source of truth for *where we are*. The design doc
([`DESIGN.md`](DESIGN.md)) is the spec; this file is the trace. Update at
the end of every session.

---

## Phase status

| Phase | Description                              | Status      | Notes |
|-------|------------------------------------------|-------------|-------|
| 1     | Infrastructure (inert API)               | **Done**    | Session 1. Test green. Not yet committed. |
| 2     | Spec-pattern Funcs                       | Not started | Touches `Function::Contents`. |
| 3     | Schedule transfer + constraint install   | Not started | |
| 4     | Matcher (canonicalization + structural)  | Not started | Most complex phase. Resolve OQ#5 first. |
| 5     | Emit + lowering integration              | Not started | |
| 6     | Diagnostics                              | Not started | |
| 7 (v1.5) | Affine match parameters               | Not started | |

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

### Carried into Phase 2

- **Spec-pattern Func mode (§4.7, OQ#4):** Phase 1 sidesteps this by
  having the test stub spec inputs (`a(i) = i;` etc.). The design doc's
  syntax (`out(i) = a(i) * b(i) + c(i)` with `a/b/c` undefined) is not
  yet legal — Phase 2 is required to make it so. **This is the next
  phase's first task.**
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

1. **Begin Phase 2 (spec-pattern Func mode).** Read §4.7 of DESIGN.md
   carefully. First sub-task per §8.2:
   - Add spec-pattern mode flag to `Function::Contents`.
   - Implement errors at materialization sites (`realize`,
     `compile_jit`, `compile_to_static_library`, etc.).
   - Mark Funcs in a spec pipeline as spec-pattern when the Builder's
     spec thunk returns.
   - Tests should demonstrate that the design doc's intended spec
     syntax (e.g., `out(i) = a(i) * b(i) + c(i)` with undefined `a/b/c`)
     becomes legal inside a spec thunk, and that any attempt to
     `realize` a spec pipeline errors cleanly.
2. **Update Phase 1 test:** once spec-pattern Func mode exists, restore
   the spec body in `implement_with_phase1.cpp` to the design-doc
   syntax (drop the stub definitions of `a/b/c`). The Phase 1 test will
   then double as a Phase 2 regression check.
3. **Resolve OQ#4** in the design doc when Phase 2 lands.
