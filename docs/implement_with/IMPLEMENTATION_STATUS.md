# `implement_with` — Implementation Status

**Branch:** `alexreinking/implement_with` (worktree at
`/var/home/alex/dev/Halide/implement_with`)
**Current phase:** Phase 3 complete; OQ#5 (canonicalization prefix)
resolved; Phase 4 ready to start.
**Last updated:** 2026-05-25 (session 4)

This file is the single source of truth for *where we are*. The design doc
([`DESIGN.md`](DESIGN.md)) is the spec; this file is the trace. Update at
the end of every session.

---

## Phase status

| Phase | Description                              | Status      | Notes |
|-------|------------------------------------------|-------------|-------|
| 1     | Infrastructure (inert API)               | **Done**    | Session 1. Two commits on branch. |
| 2     | Spec-pattern Funcs                       | **Done**    | Session 2. Two commits on branch. |
| 3     | Schedule transfer + constraint install   | **Done**    | Session 3. Single-output, bounds only; vectorize/align/etc. deferred. |
| 4     | Matcher (canonicalization + structural)  | Not started | OQ#5 resolved session 4; `lower_to_canonical_form` extracted. Ready to start. |
| 5     | Emit + lowering integration              | Not started | |
| 6     | Diagnostics                              | Not started | |
| 7 (v1.5) | Affine match parameters               | Not started | |

---

## Session 4 — what landed (pre-Phase-4 setup, 2026-05-25)

### Source changes (committed, 1 commit)

- **`src/Lower.cpp`** — extracted `Internal::lower_to_canonical_form`
  from `lower_impl`. No-behavior-change refactor; the function owns
  every Stmt-transforming pass from `schedule_functions` through
  `strip_asserts`, with `find_intrinsics` and AMX
  `extract_tile_operations` *inside* the prefix. `lower_impl` now
  calls it. User `custom_passes` and backend offloading still run
  inline in `lower_impl` after it returns.

### Resolved open questions

- **OQ#5 (canonicalization prefix)** — pinned at the body of
  `lower_to_canonical_form`. v1 makes no stability promise; in-tree
  intrinsic catalogs stay in sync by construction. Cut is *before*
  custom_passes (so canonical form is a property of (env, Target)
  alone, not the user's local toolbox) and *before* backend
  offloading. See
  [DECISIONS.md](DECISIONS.md#oq5-canonicalization-prefix) for the
  four constraints that jointly point at this cut.

### Doc changes

- **DESIGN.md §4.4** rewritten — replaced the prose bullet list with
  the actual ordered pass sequence inside `lower_to_canonical_form`.
  OQ#5 marked resolved with a link to DECISIONS.
- **DECISIONS.md** — new OQ#5 entry with the four-constraint analysis
  and the alternatives considered (cut earlier / at
  `set_conceptual_code_stmt` / post-offload / versioned).
- DESIGN.md changelog updated.

### Tests

| Path                                                  | Status | Notes |
|-------------------------------------------------------|--------|-------|
| `test/correctness/implement_with_phase1.cpp`          | Passes | Unchanged. |
| `test/correctness/implement_with_phase2.cpp`          | Passes | Unchanged. |
| `test/correctness/implement_with_phase3.cpp`          | Passes | Unchanged. |
| Sample of unrelated correctness tests                 | Passes | bounds, simplify, vectorize_varying_allocation_size, compute_at_split_rvar, memoize — exercising the any_memoized path through the refactor. |

---

## Phase 3 — what landed (session 3, 2026-05-25)

### Source changes (committed)

- **`src/ApplyImplementWith.h`** / **`src/ApplyImplementWith.cpp`** (new) —
  declares and defines `Internal::apply_implement_with_directives(env,
  target)`, the lowering-time pass that:
    1. Errors if any of `instr.required_features()` are missing from the
       compile Target.
    2. For each `ImplementWithDirective` on any stage of any Func in the
       deep-copied env, calls `instr.spec()`, identifies user Funcs
       corresponding to spec output (primary) and inputs (by Func name in
       the user's env), and transfers `FuncSchedule::bounds()` entries
       with a *positional* spec-arg → user-arg rename.
    3. Detects constant-extent / constant-min conflicts between a
       transferred bound and a pre-existing user bound, and errors with
       a message that names both values, the instruction, the var, and
       the user Func.
- **`src/Lower.cpp`** — calls `apply_implement_with_directives` after
  `lock_loop_levels`, before `wrap_func_calls`. The transfer runs on the
  deep-copied env so the user's pristine `Function` schedule is not
  mutated.
- **`src/Serialization.cpp`** — `serialize_stage_schedule` now hard-errors
  if `implement_with_directives()` is non-empty, so the silent-drop
  behavior (still load-bearing — see Phase 1 notes) cannot regress
  unnoticed when serialization is enabled in some future build.
- **`src/CMakeLists.txt`** — new header + source registered in
  alphabetical position (`ApplyImplementWith` slots between
  `AllocationBoundsInference` and `ApplySplit`).
- **`test/correctness/implement_with_phase3.cpp`** (new) — 5 sub-tests
  covering: target-feature-missing error; target-feature-present
  compile success; user's pristine schedule untouched after compile;
  constant-extent conflict error; spec-input-name not present in user
  env is silently skipped (Phase 4's job).
- **`test/correctness/CMakeLists.txt`** — phase3 test registered.

### Scope decisions

Phase 3 v1 only transfers `FuncSchedule::bounds()` (the `Bound` list).
Other directive categories — `vectorize`, `align_storage`, `unroll`,
`reorder`, `compute_with` between spec Funcs — are recognized by the
design as transferable but require structural information about loop
nests that becomes available only after the matcher (Phase 4) maps spec
Vars to user split/reorder Vars. The Phase 3 hook still runs over all
spec Funcs, so layering more transfers on top is additive. See
[`DECISIONS.md`](DECISIONS.md#phase-3-scope-bounds-only-single-output)
for the rationale.

Multi-output instructions (Tuple-valued primaries and the `co_outputs`
overload) are *not* yet supported by the transfer pass: it errors
explicitly if `spec.outputs().size() != 1` or `co_output_names` is
non-empty. The reason is the same — multi-output mapping is naturally
name-keyed and benefits from the matcher's name-resolution.

### Resolved open questions

- **OQ#1 (target-feature check location)** — fires at lowering time
  (inside `apply_implement_with_directives`). The "opportunistic
  early-warning at construction time if Target is available" half of the
  design-doc proposal is **not** implemented in v1 — re-evaluate when
  generator integration lands.

### Tests

| Path                                                  | Status | Notes |
|-------------------------------------------------------|--------|-------|
| `test/correctness/implement_with_phase1.cpp`          | Passes | Unchanged. |
| `test/correctness/implement_with_phase2.cpp`          | Passes | Unchanged. |
| `test/correctness/implement_with_phase3.cpp`          | Passes | 5 sub-tests. Two error-path tests skip if exceptions disabled at runtime. |

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

### Carried into Phase 4

- **Multi-output schedule transfer:** Phase 3 supports single-output
  only. Multi-output (`co_outputs`, Tuple-valued primaries) needs
  name-keyed spec→user mapping; cleanest to land alongside the matcher
  in Phase 4.
- **Non-bound directive transfer:** Phase 3 transfers only
  `FuncSchedule::bounds()`. The design contract also covers
  `vectorize`, `align_storage`, `unroll`, `reorder`, etc. These need
  the matcher's var rename to be useful at the user's split-Var level,
  so they layer naturally on Phase 4.

### Cross-cutting

- **Serialization:** Phase 3 added a hard error in
  `serialize_stage_schedule` if `implement_with_directives` is
  non-empty. `WITH_SERIALIZATION=OFF` is no longer load-bearing for
  the silent-drop reason, but is still load-bearing for the build —
  builds with serialization on will simply hard-error on any pipeline
  using `implement_with` until Phase 5+ adds proper serialization. See
  [DECISIONS.md](DECISIONS.md#serialization-hard-error-in-phase-3).

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

1. **Begin Phase 4 (matcher).** OQ#5 is resolved and
   `lower_to_canonical_form` is in place — both spec and use-site can
   now be lowered to a single canonical form. Per DESIGN.md §4.4 and
   §8.2 Phase 4:
   - Add a spec-pipeline lowering entry point that builds an env from
     a Pipeline and calls `lower_to_canonical_form`. The spec doesn't
     have args/requirements/etc.; supply empty defaults.
   - Locate the matched region in the user's canonical Stmt by walking
     for a `For` node whose qualified name corresponds to the
     directive's `loop_var_name` (stage-qualified per Halide's
     internal naming convention).
   - Structural matcher with alpha-equivalence + commutativity, reusing
     `IRMatcher` from `src/IRMatch.h`. FindIntrinsics has already run
     inside the prefix, so HVX MAC / AMX tile patterns will be matched
     in their lifted form.
   - Establish the spec-Var → user-Var rename map from the match.
     (Phase 3 currently uses a positional fallback anchored at arg 0;
     replace that with the matched rename map.)
   - Joint multi-output matching (and extend Phase 3 schedule transfer
     accordingly).
2. **Extend schedule transfer once the matcher provides a rename map.**
   Currently `ApplyImplementWith.cpp` transfers only
   `FuncSchedule::bounds()` and only single-output. Once the matcher
   gives us a proper var rename, layer in `align_storage`, `vectorize`
   (as a width requirement), and the multi-output path. Update Phase 3
   tests rather than the test file's filename — they should keep
   passing as Phase 4 lands.
3. **Tests for Phase 4:** the design's "get to end-to-end early"
   guidance (§8.1) now has three concrete case studies attached from
   the OQ#5 conversation:
   - `vfmadd231ps_256` (single FMA on AVX2/FMA — the canonical
     §3.3 example).
   - SDOT 4×4 GEMV on ARM (§3.4 — multi-instruction emit, tests joint
     matching of the four broadcasting SDOTs).
   - At least one of HVX MAC sequences or AMX tile triples (validates
     that the matcher works with intrinsic-lifted IR).
   These three should be wired up before declaring OQ#5 "settled"
   beyond v1's no-stability-promise scope.

### What is NOT yet done that's worth noting

- The opportunistic *early-warning* target-feature check at
  `implement_with` call time (the second half of OQ#1's proposal) is
  not implemented. Consider it if generator workflows surface noisy
  late-error reports.
- The user's `MatchContext::input/output/param` accessors are still
  stubs — wired up in Phase 5 alongside emit substitution.

### What is NOT yet done that's worth noting

- The opportunistic *early-warning* target-feature check at
  `implement_with` call time (the second half of OQ#1's proposal) is
  not implemented. Consider it if generator workflows surface noisy
  late-error reports.
- The user's `MatchContext::input/output/param` accessors are still
  stubs — wired up in Phase 5 alongside emit substitution.
