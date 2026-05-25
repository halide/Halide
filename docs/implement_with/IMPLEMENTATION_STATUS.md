# `implement_with` — Implementation Status

**Branch:** `alexreinking/implement_with` (worktree at
`/var/home/alex/dev/Halide/implement_with`)
**Current phase:** Phase 4 in progress. Structural matcher with
alpha-rename + commutativity landed. Spec-input-Func wildcard
semantics (currently degenerate because stub Funcs inline to 0.0f),
Simplify-equivalence on integer expressions, and the wire-in to
`apply_implement_with_directives` are the next pieces.
**Last updated:** 2026-05-25 (session 6)

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
| 4     | Matcher (canonicalization + structural)  | In progress | Session 5: PTX MMA added to case studies; `lower_to_canonical_form` exposed; `lower_spec_to_canonical_form` and `find_implement_with_loop` landed. Session 6: structural matcher with alpha-rename + commutativity landed (`match_canonical_form` + `MatchResult`). Spec-input wildcard semantics (blocked on stub-inline prevention), Simplify-equivalence, and apply_implement_with_directives wire-in still pending. |
| 5     | Emit + lowering integration              | Not started | |
| 6     | Diagnostics                              | Not started | |
| 7 (v1.5) | Affine match parameters               | Not started | |

---

## Session 6 — what landed (Phase 4 matcher, 2026-05-25)

### Source changes (1 commit)

- **`src/ImplementWithMatcher.h` / `src/ImplementWithMatcher.cpp`** —
  new `MatchResult` struct and `Internal::match_canonical_form(spec_loop,
  user_loop) -> MatchResult` entry point. Internal `Matcher` class is a
  parallel walker over paired Stmt/Expr trees with two binding maps:
    - `var_rename` (spec name -> user name) for Variables, For loop
      vars, and Let/LetStmt bound names. Bindings record on first
      sight; conflicts produce a `failure_reason` and bail.
    - `func_rename` (spec name -> user name) for buffer / Func /
      intrinsic names in Load, Store, Call, Provide, Realize,
      Allocate, Free, ProducerConsumer, Atomic, Prefetch,
      HoistedStorage. Intrinsic names are stable strings, so they
      just record identity bindings.
  Commutative ops (`Add`, `Mul`, `Min`, `Max`) try both child orderings
  with snapshot-and-restore on the binding maps + failure reason.
  Types, opcode kinds, `Call::call_type`,
  `For::for_type`/`device_api`/`partition_policy`,
  `Allocate::memory_type`/`padding`, `Realize::memory_type`,
  `VectorReduce::op`, and `Shuffle::indices` must match exactly.

### Tests added to `implement_with_phase4.cpp`

- `test_match_identity_self` — lower a spec, locate its `For`, match
  against itself; asserts success and that every binding is identity.
- `test_match_commutativity_directly` — handmade `Evaluate(x + y)` vs
  `Evaluate(y + x)`. Expects success and `var_rename` containing
  `{ x -> y, y -> x }`. This is the dedicated commutativity test;
  it avoids the Pipeline-lowering path so Simplify cannot
  canonicalize the operand order away.
- `test_match_different_op_fails` — handmade Add vs Mul; expects
  failure with non-empty reason.
- `test_match_alpha_rename_two_lowered_specs` — lowers
  `make_vfmadd_style_named()` twice (Func uniquification yields
  distinct names like `out$5` and `out$6`); asserts success and at
  least one non-identity `var_rename` entry. The For-loop var
  binding `outN.s0.i -> outM.s0.i` is what gets exercised here.

### Known limitation: spec input Funcs auto-inline

The spec-pattern Func mode (Phase 2) auto-stubs undefined input Funcs
with a `0.0f` body. By default they're scheduled inline, so Halide's
canonical-form prefix inlines `a(i) * b(i) + c(i)` into the output
Func's body and Simplify collapses it to `0.0f`. The matcher's
`func_rename` machinery is *correctly* set up to bind spec input
Func names to user Func names as wildcards (every Load/Call name
already routes through `func_rename`), but at this point in
canonical form there are no Loads from `a`/`b`/`c` to wildcard-bind
— the body is just a constant.

This means the "spec input Funcs are wildcards" property from the
design isn't truly tested yet; the four-case-study end-to-end work
will land alongside a fix that prevents stub inlining (either
schedule spec-input Funcs `compute_root()` before lowering, or use
a tagged Call::PureExtern stub the matcher recognizes as a
wildcard sentinel).

## Session 5 — what landed (Phase 4 kick-off, 2026-05-25)

### Doc changes (1 commit)

- **DESIGN.md / IMPLEMENTATION_STATUS.md / DECISIONS.md** — Phase 4
  case-study list expanded from three entries to four. PTX MMA on
  CUDA (NVPTX tensor cores) added as the LLVM-backed-GPU validation
  case. DECISIONS.md gets a new "Phase 4 case-study set" entry with
  the rationale and rejected alternatives (three-cases-only;
  pick-one-of-HVX-AMX; string-y GPU). DESIGN.md changelog updated.

### Source changes (3 commits)

- **`src/Lower.h` / `src/Lower.cpp`** — exposed
  `Internal::lower_to_canonical_form` via Lower.h. Moved out of the
  anonymous namespace and dropped the internal `LoweringLogger`
  parameter (now instantiated inside the function). `lower_impl`
  moved into its own anonymous namespace. No-behavior-change
  refactor.
- **`src/ImplementWithMatcher.h` / `src/ImplementWithMatcher.cpp`**
  (new) — defines two pieces of matcher infrastructure:
    - `Internal::lower_spec_to_canonical_form(spec, target)` — spec-
      side counterpart of `lower_impl`'s pre-canonical-form prefix.
      Runs the same setup (`deep_copy + build_environment`,
      `lower_target_query_ops`, `strictify_float`,
      `lock_loop_levels`, `wrap_func_calls`, `realization_order`,
      `simplify_specializations`) then calls
      `lower_to_canonical_form`. Intentional omission vs.
      `lower_impl`: does *not* call
      `apply_implement_with_directives` (specs cannot themselves
      carry instructions).
    - `Internal::find_implement_with_loop(stmt, func, stage, var)`
      — IRVisitor that locates the `For` node whose stage-qualified
      name is `<func>.s<stage>.<var>` in a canonical-form Stmt.
      Returns undefined if not found.
- **`src/CMakeLists.txt`** — `ImplementWithMatcher.{h,cpp}` registered
  alphabetically (between `ImageParam` and `InferArguments`).
- **`test/correctness/implement_with_phase4.cpp`** (new) — 4 sub-tests:
    - `test_spec_lowers_to_nonempty_stmt` — lowers an Instruction's
      spec through the new entry point and verifies the lowered
      Stmt is non-trivial and mentions the output Func's name.
    - `test_use_site_pipeline_still_compiles` — regression check
      that the Phase 3 lowering path is intact.
    - `test_find_implement_with_loop_returns_for_node` — locator
      finds the expected For in a spec lowered with explicit
      `Var("i")`.
    - `test_find_implement_with_loop_returns_undefined_when_missing`.
- **`test/correctness/CMakeLists.txt`** — phase4 test registered;
  its target adds `${Halide_SOURCE_DIR}/src` to the include path
  because it calls `Internal::` API directly.

### Known noise (not a regression)

- Lowering a spec pipeline emits warnings of the form
  `"It is meaningless to bound dimension v0 of function a to be
  within [0, 8] because the function is scheduled inline."` These
  fire on spec-pattern input Funcs which are inlined by default but
  carry `bound()` from the contract. Same warnings fire on the
  Phase 3 use-site path. Cosmetic only; could be suppressed for
  spec lowering in a follow-up (e.g. by suppressing inline-bound
  warnings on spec-pattern Funcs, or by scheduling spec inputs
  `compute_root()` before lowering).

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

1. **Make spec-input-Func wildcards observable.** The matcher's
   `func_rename` map already binds Load/Call names as wildcards, but
   the canonical-form lowering inlines spec input Funcs (their `0.0f`
   stub bodies) so there are no Loads from `a`/`b`/`c` left to bind.
   Two options:
   - **`compute_root()` spec inputs before lowering.** Modify
     `lower_spec_to_canonical_form` (or Phase 2's spec-pattern Func
     setup) to schedule auto-stubbed input Funcs `compute_root()` so
     they survive as Realize/ProducerConsumer/Load chains in
     canonical IR. The matcher then walks those Loads, binding the
     spec input names into `func_rename`. Risk: the spec lowered IR
     now contains real Allocate/Free for the stub bodies, which the
     user IR will not have at the matched For region. The matcher
     would need to either match outside the For (and have the
     locator return a wider region) or special-case Realize/Allocate
     skipping for known spec-input Func names.
   - **Tagged stub sentinel.** Replace the `0.0f` stub with a
     dedicated `Call::PureIntrinsic` named (say) `implement_with_input`
     carrying the spec-input Func name as a string arg. The matcher
     recognizes this sentinel and treats matching it as a wildcard
     bind. Cleaner separation; needs a small Phase 2 edit.
2. **Simplify-equivalence for integer expressions.** Two algebraically
   equal indices that aren't lexically equal (e.g. `(i + 4) - 2` vs
   `i + 2`) currently fail to match outside commutative-op contexts.
   Wrap integer comparisons with `simplify(spec_idx - user_idx) == 0`
   or similar. Care needed around free Variables — the simplifier
   doesn't know about alpha-renaming, so call simplify *after* the
   rename has been resolved, or substitute bindings before
   simplifying.
3. **Wire the matcher into `apply_implement_with_directives`.**
   Replace Phase 3's positional spec-Var -> user-Var rename with
   the matcher's `var_rename`. The Phase 3 tests should still pass
   (the 1D positional case is a degenerate matcher case); the win
   is correctness for multi-dim and split-renamed cases.
4. **Joint multi-output matching.** Extend the matcher to match
   against multiple co-computed outputs at the same loop level
   (Tuple primaries; `compute_with`-fused co-outputs). Lift the
   `co_output_names.empty()` and `outputs().size() == 1`
   restrictions in `apply_implement_with_directives`.
5. **Extend schedule transfer once the matcher provides a rename map.**
   Currently `ApplyImplementWith.cpp` transfers only
   `FuncSchedule::bounds()` and only single-output. Once the matcher
   gives us a proper var rename, layer in `align_storage`, `vectorize`
   (as a width requirement), and the multi-output path. Update Phase 3
   tests rather than the test file's filename — they should keep
   passing as Phase 4 lands.
6. **Tests for Phase 4:** the design's "get to end-to-end early"
   guidance (§8.1) has four case studies attached, each chosen to
   exercise a distinct property of the canonical-form prefix and the
   matcher:
   - `vfmadd231ps_256` (single FMA on AVX2/FMA — the canonical §3.3
     example) — simplest case; validates the post-FindIntrinsics
     match on a single intrinsic call.
   - SDOT 4×4 GEMV on ARM (§3.4 — multi-instruction emit) — tests
     joint matching of the four broadcasting SDOTs and multi-output
     scheduling.
   - HVX MAC sequences *or* AMX tile triples — validates that
     `find_intrinsics` (HVX) and (gated) `extract_tile_operations`
     (AMX) inside the prefix produce match-friendly canonical IR. At
     least one of these should be wired up; both is better.
   - PTX MMA on CUDA (NVPTX tensor cores) — validates LLVM-backed GPU
     matching. Canonical form is taken *before* `inject_gpu_offload`,
     so spec and use-site share `For::GPUBlock`/`For::GPUThread` loop
     structure; this is the test that closes the "LLVM-backed
     backends first" line from the OQ#5 analysis. Warp-scoped
     fragment IR also exercises shared-state subtleties that the CPU
     case studies cannot.
   The four together should be wired up before declaring OQ#5 "settled"
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
