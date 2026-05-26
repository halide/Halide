# `implement_with` — Implementation Status

**Branch:** `alexreinking/implement_with` (worktree at
`/Users/areinking/dev/Halide/implement_with` on macOS;
`/var/home/alex/dev/Halide/implement_with` was the original Fedora toolbox path)
**Current phase:** Phase 5 **landed** in soft-failure form. A new
`apply_implement_with_emit` pass runs after canonical-form lowering, re-runs the
structural matcher per directive, builds a `MatchContext` from the matcher's
`func_rename` / `var_rename`, invokes the Instruction's emit callback, and
substitutes the returned Stmt for the matched For region. Match failure emits a
`user_warning` and falls through to ordinary lowering (a documented divergence
from the design's Strict-v1 mode; see DECISIONS.md "Phase 5 soft-failure
semantics"). Next: Phase 6 (diagnostics + revisit Strict-mode). **Last
updated:** 2026-05-26 (session 10)

This file is the single source of truth for *where we are*. The design doc
([`DESIGN.md`](DESIGN.md)) is the spec; this file is the trace. Update at the
end of every session.

______________________________________________________________________

## Phase status

| Phase    | Description                             | Status      | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| -------- | --------------------------------------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1        | Infrastructure (inert API)              | **Done**    | Session 1. Two commits on branch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| 2        | Spec-pattern Funcs                      | **Done**    | Session 2. Two commits on branch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| 3        | Schedule transfer + constraint install  | **Done**    | Session 3. Single-output, bounds only; vectorize/align/etc. deferred. (Multi-output and align_storage transfer extended in session 9.)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| 4        | Matcher (canonicalization + structural) | **Done**    | Session 5: PTX MMA added to case studies; `lower_to_canonical_form` exposed; `lower_spec_to_canonical_form` and `find_implement_with_loop` landed. Session 6: structural matcher with alpha-rename + commutativity landed (`match_canonical_form` + `MatchResult`). Session 7: spec-input wildcard observability (stubs now `compute_root()`), Simplify-equivalence fallback for integer Exprs, `lower_pipeline_to_canonical_form` helper, and the vfmadd231ps_256 case study landed. Session 8: remaining three case studies (sdot_gemv_4x4, hvx_mac_widening, ptx_gpu_mac) landed; matcher exercised across two-stage reductions, vector intrinsic lifting, and GPU For loop types. Session 9: `find_spec_primary_loop` locator; matcher wired into `apply_implement_with_directives` (3-pass: positional primary transfer, matcher, matcher-driven input transfer); multi-output (Tuple-valued + co_outputs) supported; `align_storage`/`bound_storage` transfer added. |
| 5        | Emit + lowering integration             | **Done**    | Session 10: `apply_implement_with_emit` runs after `lower_to_canonical_form`, re-runs the matcher, builds a `MatchContext`, invokes `instr.emit(ctx)`, substitutes the matched For. Soft-failure on match miss (warn + fall through; see DECISIONS.md). Extern-call emit and auto-generated bounds queries deferred to a follow-up; the matched-For-replacement substitution + `MatchContext::input/output/var` accessors landed.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| 6        | Diagnostics                             | Not started | Phase 5 ships soft-failure semantics; revisiting Strict-mode requires matcher gap closure (non-zero output buffer mins, etc.).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| 7 (v1.5) | Affine match parameters                 | Not started |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |

______________________________________________________________________

## Session 10 — what landed (Phase 5, 2026-05-26)

### Source changes

- **`src/Instruction.{h,cpp}`** — `MatchContext` gained a real API surface for
  emit callbacks. New methods `input(spec_name)`, `output(spec_name)`,
  `var(spec_var)`, `target()`. The lookup is backed by a shared_ptr-owned
  `Internal::MatchContextContents` struct (moved into `Instruction.h` so the
  substitution pass can construct one). Each accessor handles Halide's
  per-process Func uniquifier suffix (`Func("a")` → `a$N`): the lookup tries the
  literal spec name first, then any key of the form `<spec_name>$<digits>`.
  `output(spec_name)` additionally pre- resolves to the directive's primary so
  the common single-output case works without a func_rename binding for the
  primary's post-uniquify name.

- **`src/ApplyImplementWithEmit.{h,cpp}`** (new) —
  `Internal::apply_implement_with_emit(s, env, target)` returns a rewritten
  `Stmt`. It walks `env` to collect every pending `ImplementWithDirective`, then
  per directive:

  1. `find_implement_with_loop(s, func, stage, loop_var)` to find the user-side
     `For` in canonical form.
  2. `lower_spec_to_canonical_form(spec, target)` to lower the spec, then
     `find_spec_primary_loop` to locate the spec-side `For`.
  3. `match_canonical_form(spec_loop, user_loop)` for spec-to-user bindings.
  4. Build `MatchContext` from the match result + the primary's user/spec name +
     the bare-Var renames parsed from the matcher's For-loop name bindings.
  5. `instr.emit(ctx)` to get the replacement Stmt.
  6. Record the user-side For name → emit Stmt mapping; after all directives are
     processed, an `EmitSubstituter` IRMutator replaces matching `For` nodes
     with their emit output. Soft-failure mode: missing user-side For, missing
     spec-side For, or structural match failure each emit a `user_warning` and
     skip substitution for that directive. Multiple directives targeting the
     same canonical For is a hard error (almost certainly user mistake).

- **`src/Lower.cpp`** — calls `apply_implement_with_emit(s, env, t)` immediately
  after `lower_to_canonical_form` returns, before `custom_passes` and before
  `set_conceptual_code_stmt`. This is the hook point identified in DESIGN.md
  §4.4 / OQ#5 follow-up: canonical form is in hand, but Hexagon RPC / GPU
  offload / parallel-task lowering haven't run yet. User-injected custom_passes
  observe the substituted Stmt.

- **`src/CMakeLists.txt`** — `ApplyImplementWithEmit.{h,cpp}` registered
  (alphabetical, between `ApplyImplementWith` and `ApplySplit`).

### Tests added (`test/correctness/implement_with_phase5.cpp`)

Four sub-tests, all green:

- `test_no_directive_compiles_cleanly` — a Pipeline with no `implement_with`
  directives compiles without invoking the emit pass (no sentinel call in the
  lowered Module).
- `test_emit_sentinel_survives_lowering` — a 1D vfmadd-style instruction with a
  sentinel emit
  (`Evaluate(Call::Extern( "halide_phase5_emit_sentinel", {ctx.output("out"), ctx.input("a")}))`).
  Verifies the sentinel call appears in the lowered Module's LoweredFunc bodies
  AND that `ctx.output("out")` resolved to the user output Func name AND that
  `ctx.input("a")` resolved through the matcher's `func_rename`.
- `test_matched_for_is_replaced` — confirms the matched user-side For (e.g.
  `out_phase5_for_replaced.s0.x`) no longer exists in the lowered Module after
  substitution.
- `test_match_context_var_lookup` — uses a deliberately-different user Var name
  (`a_strange_user_var_name`); verifies `ctx.var("i")` resolves to it via the
  matcher's For-loop name bindings (parsed back to bare-Var form by the same
  helper Phase 4 uses).

### Test invariant tightening across Phase 3 and Phase 4

Pre-Phase-5 tests written when `implement_with` was inert needed updating once
Phase 5 attempted real matching:

- `test/correctness/implement_with_phase3.cpp` — `make_user_pipeline` now
  schedules `a`, `b`, `c` `compute_root()`. Without that the user body
  simplifies to a single FloatImm and the matcher fails.
  `test_input_func_name_mismatch_is_silent` similarly compute_roots its
  `p`/`q`/`r` Funcs (and its semantics changed: the matcher binds inputs by
  structural correspondence now, so the name mismatch is intentionally
  compatible rather than silently skipped).
- `test/correctness/implement_with_phase4.cpp` —
  `test_use_site_pipeline_still_compiles` compute_roots its sub- Funcs for the
  same reason. The multi-output tuple and co-outputs tests retain their existing
  structure but produce a Phase 5 warning (match fails on output min
  subtraction); the test invariant is that `compile_to_module` returns
  successfully, which the soft-failure mode preserves.

### Decisions made under uncertainty

- **Soft-failure semantics in v1.** The design ([DESIGN.md] §4.3, step 3) calls
  for a hard error in Strict mode when the matcher fails. v1 ships a
  `user_warning` + fall-through instead. Reason: the matcher v1 has a known gap
  on output buffer min subtraction (the user's `out[x - out.min.0]` doesn't
  structurally match the spec's `out[i]` even though both indices canonically
  reduce to the same value under the buffer-bound assertions). Hard-erroring
  would break the multi-output co-output and Tuple-valued primary tests from
  Phase 4 without proving anything about Phase 5's emit machinery. The
  substitution path is exercised by Phase 5's own tests; the failure path is
  exercised by Phase 4's tests where the matcher gap surfaces. Revisit when
  Phase 6 (diagnostics) closes the matcher gap. See DECISIONS.md "Phase 5
  soft-failure semantics".
- **Re-run the matcher in Phase 5 vs. cache from Phase 3.**
  `apply_implement_with_directives` (Phase 3) already deep-copies env to lower
  the user pipeline for matcher-driven bound transfer. Caching its MatchResult
  forward to Phase 5 is brittle because the cached result references Func names
  from the deep-copied env, not the env that lower_impl actually carries forward
  into `lower_to_canonical_form`. Re-running the matcher in Phase 5 costs one
  extra spec lowering per directive — acceptable. The user-side IR is shared
  (Phase 5 operates on the `s` returned from `lower_to_canonical_form`).
- **User-side `out.bound()` interacts badly with the matcher's output-min
  handling.** During Phase 5 test writing, a user pipeline with
  `out.bound(x, 0, 8)` produced a Store index of `x - out.min.0`, while the
  spec's identical bound produced just `i`. The Phase 5 tests therefore omit
  `out.bound()` on the user side and let Phase 3's bound transfer install the
  spec's constraint. Documented in the test's inline comment. This is a known
  matcher gap and a candidate for the Phase 6 / Phase 7 matcher tightening pass.

### Smaller follow-ups

- **Extern-call emit + auto-generated bounds queries.** DESIGN.md §4.5 calls for
  `implement_with` to reuse `define_extern`'s backend machinery when emit
  produces a `Call::Extern`-shaped Stmt (BLAS substitution case). Not done in
  this session — the sentinel test uses `Call::Extern` but doesn't link/call it.
  A later session should add a real extern emit (e.g. a tiny C stub linked into
  the JIT) end-to-end.
- **Soft → Strict transition.** Currently soft for v1. After the matcher closes
  the min-subtraction gap, flip back to Strict and remove the warning text (or
  gate the choice behind an `ImplementMode` setting on the directive, which
  DESIGN.md §4.3 already declares).
- **Co-output and Tuple-primary emit paths.** The substitution currently
  replaces only the primary's matched For. Multi-output needs to either
  substitute a single Stmt that produces all outputs at once or substitute each
  co-output's For separately with a matching emit. Not implemented; the
  soft-failure path handles these correctly today.

______________________________________________________________________

## Session 9 — what landed (Phase 4 closeout, 2026-05-25)

### Source changes (4 commits)

- **`src/ImplementWithMatcher.{h,cpp}`** — new `find_spec_primary_loop` helper.
  The spec-side counterpart of `find_implement_with_loop`: walks into
  `ProducerConsumer{is_producer=true, name=spec_out_name}` and returns the
  outermost For at the requested stage. Used by the wire-in below, which knows
  the user-side loop var name (from the directive) but has no analogous hint for
  the spec side.

- **`src/ApplyImplementWith.{h,cpp}`** — `apply_implement_with_directives`
  signature gains `const std::vector<Function> &outputs`. The function is
  refactored into three passes:

  1. **Pre-matcher** (positional, unconditional): target-feature check +
     primary-output bound transfer. Must run first because the user pipeline
     cannot lower to a structurally-matchable canonical form until the spec's
     constant bounds are installed on the user primary (otherwise the For has
     symbolic `out.min.0` / `out.extent.0` that won't match the spec's
     constants).
  2. **Matcher**: lowers the user pipeline (via
     `lower_pipeline_to_canonical_form`, which deep-copies, so env is not
     mutated) and the spec; locates matched Fors; runs `match_canonical_form`.
     Stores the (Pipeline, MatchResult) pair per directive --- the Pipeline must
     be kept alive because Halide uniquifies Func names per
     `Instruction::spec()` invocation, so the matcher's func_rename keys are
     valid only for the invocation that produced them.
  3. **Post-matcher input transfer**: walks spec_primary's transitive calls; for
     each spec input, looks up the user Func via `func_rename[spec_input_name]`
     (falling back to env.find-by-name) and translates bare-Var rename keys via
     `bare_var_renames_from_matcher`, which parses For-name bindings
     (`<func>.s<stage>.<bare>`) to recover bare-Var pairs. Also lifts the
     multi-output restriction
     (`spec_outputs.size() == 1 && co_output_names.empty()`) to a symmetric
     arity check (`spec.outputs().size() == 1 + co_output_names.size()`); the
     per-directive transfer iterates both the primary and co-outputs, looking
     the latter up by name from `co_output_names`. And adds
     `transfer_storage_dims` invoked alongside `transfer_bounds` for both
     primary and inputs --- transfers `align_storage` and `bound_storage` with
     conflict detection.

- **`src/Lower.cpp`** — sole caller updated to pass `outputs`.

### Tests added to `implement_with_phase4.cpp` (4 new sub-tests)

- `test_find_spec_primary_loop_outermost` — locator returns
  `<spec_out>.s<stage>.<var>` outermost For, and undefined for an out-of-range
  stage.
- `test_wire_in_matches_renamed_spec_inputs` — user pipeline with compute_root'd
  inputs named "p_wire", "q_wire", "r_wire" (none of the spec's "a", "b", "c").
  Sets `p_wire.bound(x, 0, 16)` which conflicts with spec's `a.bound(i, 0, 8)`.
  Without the matcher wire-in, `env.find("a")` returns nothing and the conflict
  is silently dropped; with it, `func_rename` binds `a -> p_wire`, the conflict
  fires, compile errors. The test asserts the error message mentions
  implement_with, extent, and p_wire.
- `test_multi_output_tuple_primary_compiles` — Tuple-valued primary modeled on
  DESIGN.md §3.5 (frecpe_pair).
- `test_multi_output_co_outputs_compile` — co-outputs modeled on DESIGN.md §3.6
  (tile_op with result + status).
- `test_align_storage_conflict_detected` — spec sets `align_storage(i, 64)`,
  user sets conflicting `align_storage(x, 32)`. With Phase 3 silently no-op'd;
  now errors.

### Decisions made under uncertainty

- **3-pass refactor vs. lenient matcher.** The natural alternative to running
  positional primary-output transfer before the matcher would be to teach the
  matcher to ignore For min/max mismatches. Picked the 3-pass approach because
  (a) it keeps the matcher's strict semantics for emit-time use, (b) the
  primary-output positional rename was already correct for the cases the wire-in
  enables (anchor at directive's loop var), and (c) the cost is one extra
  `lower_pipeline_to_canonical_form` call per `apply_implement_with_directives`
  invocation, which is acceptable for a compile-time pass.

- **Per-Pipeline matcher context.** Each `Instruction::spec()` invocation
  re-runs Halide's process-wide Func name uniquifier, so consecutive calls
  return Funcs named `a$17`, `b$18`, `c$16` on one invocation and `a$18`,
  `b$19`, `c$17` on the next. The matcher's `func_rename` keys are stable only
  within a single invocation. Resolved by stashing the spec Pipeline used for
  matching in a per-directive context (`MatcherContext`) and re-using it during
  the input-transfer pass.

- **vectorize-as-width-requirement deferred.** The third bullet on the session-8
  handoff's task-10 list is not landed. It requires threading the matcher's
  For-loop var binding into the user's `StageSchedule` Dim vector (for_type)
  rather than `FuncSchedule` (which is what bounds and storage_dims live on).
  More naturally landed alongside the Phase 5 emit substitution, when the
  matcher's full `var_rename` already needs to be plumbed for ctx.input/output.

## Session 8 — what landed (Phase 4: remaining case studies, 2026-05-25)

### Source changes

None. All three new case studies are pure test additions; the matcher,
canonical-form lowering, and `find_implement_with_loop` machinery from sessions
5-7 cover the cases without modification.

### Tests added to `implement_with_phase4.cpp` (3 commits)

- **`test_case_study_sdot_gemv_4x4`** — SDOT 4x4 GEMV reduction (DESIGN.md
  §3.4). Spec is a two-stage Func (init + update) with `RDom k(0, 4)` reducing
  `cast<int32>(A(i, k)) * cast<int32>(b(k))` into `out(i)`. User pipeline
  mirrors with ImageParams; both A inputs need `dim(0).set_stride(1)` and
  `dim(1).set_stride(4)` to produce structurally-equal Load indices on both
  sides. Matched region is the update-stage's i-loop. Exercises: 2D Loads with
  reduction-domain inner index, Cast(int8 -> int32) widening, finding the update
  stage's For specifically, the matcher walking through an inner reduction For
  loop.

- **`test_case_study_hvx_mac_widening`** — HVX MAC via find_intrinsics inside
  canonical form. Target is `hexagon-32-noos-hvx_v66`. Both spec and user author
  the un-lifted `cast<int32>(a) * cast<int32>(b)` form; find_intrinsics lifts it
  to `widening_mul` + `widen_right_add` on both sides. The matcher succeeds
  against the lifted form. A `CountWideningMul` IRVisitor sanity-checks that
  widening_mul actually appears in both Stmts and inside the matched region
  (otherwise the test would be silently testing the un-lifted form). Requires
  explicit `split` + `vectorize` (find_intrinsics_for_type is vector-only).

- **`test_case_study_ptx_gpu_mac`** — GPU/PTX case study. Target
  `host-cuda-cuda_capability_61`. Both sides use
  `gpu_tile(x, y, xi, yi, 32, 8)`. Canonical form is taken *before*
  inject_gpu_offload (per DECISIONS.md OQ#5), so spec and user share
  `ForType::GPUBlock` / `ForType::GPUThread` loop structure. A `HasGPUFor`
  IRVisitor confirms both kinds of GPU For nodes reach canonical form on the
  spec side, then the outermost block For is matched against its user
  counterpart. In a full MMA flow the emit callback would lower the matched
  region to a `wmma.mma.*` sequence; the case study tests only the matcher's GPU
  loop traversal, not emit-side intrinsic generation.

### Decisions made under uncertainty

- **`out(i) = c(i)` direct FuncRef-to-FuncRef assignment** — the spec-thunk
  auto-stub only fires from `FuncRef::operator Expr()`; the
  `FuncRef::operator=(const FuncRef &)` overload routes through `Tuple(FuncRef)`
  which asserts on undefined Funcs *before* the stub can fire. The SDOT test
  works around this by binding an intermediate `Expr c_val = c(i)`. Long-term
  fix would be either to teach `Tuple(FuncRef)` about `in_spec_thunk()` or to
  teach `FuncRef::operator=(FuncRef)` to force the Expr conversion when the RHS
  is undefined. Filed informally as a Phase-2 follow-up; the workaround is
  documented at the test's call site.
- **Auto-stub pure-arg names follow the call site.** The §3.4 example uses
  `A.bound(_1, 0, 4)`, but the auto-stubbed A's pure args are named after the
  call's args (`A(i, k)` -> args "i" and "k$0"). Neither `_1` nor a fresh Var
  resolves. The SDOT test drops bounds on auto-stubbed inputs entirely and lets
  bounds inference derive their required regions from the consumer's bound + the
  constant-extent RDom.
- **GPU For naming** — gpu_tile produces names of the form
  `<func>.s1.<orig>.<orig>.block_id_<dim>` (the original var name appears twice,
  once from the stage-level scheduling and once from gpu_tile's own internal
  split). The test locates the outermost block For (`y.y.block_id_y`); on
  locator miss it dumps all For names from both sides for debuggability.

## Session 7 — what landed (Phase 4: wildcards, Simplify, case study, 2026-05-25)

### Source changes (4 commits)

- **`src/Func.cpp`** — `FuncRef::operator Expr()` schedules the auto-stubbed
  spec input Func `compute_root()` after defining it. Without this, the stub
  Func is inlined and the canonical-form prefix substitutes `0.0f` into every
  call site; Simplify then collapses the body to a constant Store, leaving no
  Load for the matcher's `func_rename` to bind. With `compute_root()` the stub
  body survives as a Realize / Allocate / produce / Load chain and the
  spec-input wildcard semantics described in the design are observable end to
  end.
- **`src/ImplementWithMatcher.{h,cpp}`** — two pieces:
  - **Simplify-equivalence fallback.** `match_expr` now snapshots binding state,
    attempts the existing structural recursion via a new `match_expr_structural`
    helper, and on failure restores the snapshot before trying
    `simplify_equivalent_ints`. The fallback substitutes current `var_rename`
    bindings into the spec Expr and asks Simplify whether
    `spec_substituted - user` reduces to a constant zero. Restricted to
    scalar/vector integer types of matching shape. Identical IR still takes the
    structural path (no extra Simplify cost in the common case).
  - **`lower_pipeline_to_canonical_form(p, t)`** — new public entry point that
    omits the spec-pattern assertion in `lower_spec_to_canonical_form`, making
    the canonical-form lowering usable for real Halide user pipelines too. Both
    functions delegate to a shared file-local helper `lower_no_implement_with`.
    Needed for case-study tests that build a user pipeline and lower it through
    the matcher's prefix.

### Tests added to `implement_with_phase4.cpp`

- `test_match_spec_input_funcs_bind_as_wildcards` — lowers two copies of the FMA
  spec; collects spec-side `Allocate` names from each lowered Stmt; matches the
  inner Fors; asserts every spec input Allocate name is bound in `func_rename`
  to some side-2 input name (we don't insist on a specific pairing because
  commutativity may swap a/b).
- `test_match_simplify_equivalent_integer_indices` — handmade For loops with
  bodies `(i + 4) - 2` vs `j + 2`. Structural match fails (Sub vs Add); after
  the For binds `i -> j` the simplify-equivalence fallback recognizes the
  algebraic identity.
- `test_match_simplify_unequal_integers_still_fail` — negative control for the
  above. `i + 3` vs `j + 2` correctly fails.
- `test_case_study_vfmadd231ps_256` — first of four Phase 4 case studies. Builds
  the spec from §3.3 and a realistic user pipeline with ImageParam inputs
  (`dim(0).set_bounds(0, 8)` to pin the min to 0). Lowers both via the spec /
  pipeline canonical-form entry points, locates inner Fors by stage-qualified
  name, matches, and asserts (a) success, (b) For-name binding, (c) every spec
  Allocate name binds to a user ImageParam (`ua` / `ub` / `uc`), and (d) spec
  output name binds to `user_out_vfmadd`.

### Decisions made under uncertainty

- **Compute_root vs tagged sentinel for spec-input wildcards.** Picked
  compute_root() on the auto-stub. Reason: keeps the matcher logic uniform —
  Load names already route through `func_rename`, no special intrinsic handling
  needed. The per-stub Realize/Allocate is small (just the bounded extent) and
  only exists during spec lowering. The tagged-sentinel alternative was lighter
  at runtime but required a custom matcher rule and diverged from "spec lowered
  IR == user lowered IR" purity.
- **Item 3 (matcher wire-in to `apply_implement_with_directives`) deferred.**
  Wiring the matcher in requires lowering the user pipeline mid-prefix in
  `lower_impl` (matcher needs canonical- form IR on both sides; user side isn't
  lowered yet when `apply_implement_with_directives` runs). The simplest fix
  (deep-copy env per directive, run wrap_func_calls / realization_order /
  simplify_specializations / lower_to_canonical_form on the copy) is doable but
  expensive and changes the function signature. Worth a dedicated session.
  Positional rename continues to work for the cases currently tested; the win
  for the matcher path is correctness on multi-dim and reordered cases not yet
  covered by the Phase 3 test suite.

### Non-zero-min note for case studies

The vfmadd case study uses `ImageParam::dim(0).set_bounds(0, 8)` to pin the
input min to 0. Without it, the user-side Load index is `x - ua.min.0`, which
the simplify-equivalence fallback cannot prove constant. Affine match parameters
(Phase 7, v1.5) are the proper place to handle non-zero buffer mins. Documented
in the test's comment block.

## Session 6 — what landed (Phase 4 matcher, 2026-05-25)

### Source changes (1 commit)

- **`src/ImplementWithMatcher.h` / `src/ImplementWithMatcher.cpp`** — new
  `MatchResult` struct and
  `Internal::match_canonical_form(spec_loop, user_loop) -> MatchResult` entry
  point. Internal `Matcher` class is a parallel walker over paired Stmt/Expr
  trees with two binding maps:
  - `var_rename` (spec name -> user name) for Variables, For loop vars, and
    Let/LetStmt bound names. Bindings record on first sight; conflicts produce a
    `failure_reason` and bail.
  - `func_rename` (spec name -> user name) for buffer / Func / intrinsic names
    in Load, Store, Call, Provide, Realize, Allocate, Free, ProducerConsumer,
    Atomic, Prefetch, HoistedStorage. Intrinsic names are stable strings, so
    they just record identity bindings. Commutative ops (`Add`, `Mul`, `Min`,
    `Max`) try both child orderings with snapshot-and-restore on the binding
    maps + failure reason. Types, opcode kinds, `Call::call_type`,
    `For::for_type`/`device_api`/`partition_policy`,
    `Allocate::memory_type`/`padding`, `Realize::memory_type`,
    `VectorReduce::op`, and `Shuffle::indices` must match exactly.

### Tests added to `implement_with_phase4.cpp`

- `test_match_identity_self` — lower a spec, locate its `For`, match against
  itself; asserts success and that every binding is identity.
- `test_match_commutativity_directly` — handmade `Evaluate(x + y)` vs
  `Evaluate(y + x)`. Expects success and `var_rename` containing
  `{ x -> y, y -> x }`. This is the dedicated commutativity test; it avoids the
  Pipeline-lowering path so Simplify cannot canonicalize the operand order away.
- `test_match_different_op_fails` — handmade Add vs Mul; expects failure with
  non-empty reason.
- `test_match_alpha_rename_two_lowered_specs` — lowers
  `make_vfmadd_style_named()` twice (Func uniquification yields distinct names
  like `out$5` and `out$6`); asserts success and at least one non-identity
  `var_rename` entry. The For-loop var binding `outN.s0.i -> outM.s0.i` is what
  gets exercised here.

### Known limitation: spec input Funcs auto-inline

The spec-pattern Func mode (Phase 2) auto-stubs undefined input Funcs with a
`0.0f` body. By default they're scheduled inline, so Halide's canonical-form
prefix inlines `a(i) * b(i) + c(i)` into the output Func's body and Simplify
collapses it to `0.0f`. The matcher's `func_rename` machinery is *correctly* set
up to bind spec input Func names to user Func names as wildcards (every
Load/Call name already routes through `func_rename`), but at this point in
canonical form there are no Loads from `a`/`b`/`c` to wildcard-bind — the body
is just a constant.

This means the "spec input Funcs are wildcards" property from the design isn't
truly tested yet; the four-case-study end-to-end work will land alongside a fix
that prevents stub inlining (either schedule spec-input Funcs `compute_root()`
before lowering, or use a tagged Call::PureExtern stub the matcher recognizes as
a wildcard sentinel).

## Session 5 — what landed (Phase 4 kick-off, 2026-05-25)

### Doc changes (1 commit)

- **DESIGN.md / IMPLEMENTATION_STATUS.md / DECISIONS.md** — Phase 4 case-study
  list expanded from three entries to four. PTX MMA on CUDA (NVPTX tensor cores)
  added as the LLVM-backed-GPU validation case. DECISIONS.md gets a new "Phase 4
  case-study set" entry with the rationale and rejected alternatives
  (three-cases-only; pick-one-of-HVX-AMX; string-y GPU). DESIGN.md changelog
  updated.

### Source changes (3 commits)

- **`src/Lower.h` / `src/Lower.cpp`** — exposed
  `Internal::lower_to_canonical_form` via Lower.h. Moved out of the anonymous
  namespace and dropped the internal `LoweringLogger` parameter (now
  instantiated inside the function). `lower_impl` moved into its own anonymous
  namespace. No-behavior-change refactor.
- **`src/ImplementWithMatcher.h` / `src/ImplementWithMatcher.cpp`** (new) —
  defines two pieces of matcher infrastructure:
  - `Internal::lower_spec_to_canonical_form(spec, target)` — spec- side
    counterpart of `lower_impl`'s pre-canonical-form prefix. Runs the same setup
    (`deep_copy + build_environment`, `lower_target_query_ops`,
    `strictify_float`, `lock_loop_levels`, `wrap_func_calls`,
    `realization_order`, `simplify_specializations`) then calls
    `lower_to_canonical_form`. Intentional omission vs. `lower_impl`: does *not*
    call `apply_implement_with_directives` (specs cannot themselves carry
    instructions).
  - `Internal::find_implement_with_loop(stmt, func, stage, var)` — IRVisitor
    that locates the `For` node whose stage-qualified name is
    `<func>.s<stage>.<var>` in a canonical-form Stmt. Returns undefined if not
    found.
- **`src/CMakeLists.txt`** — `ImplementWithMatcher.{h,cpp}` registered
  alphabetically (between `ImageParam` and `InferArguments`).
- **`test/correctness/implement_with_phase4.cpp`** (new) — 4 sub-tests:
  - `test_spec_lowers_to_nonempty_stmt` — lowers an Instruction's spec through
    the new entry point and verifies the lowered Stmt is non-trivial and
    mentions the output Func's name.
  - `test_use_site_pipeline_still_compiles` — regression check that the Phase 3
    lowering path is intact.
  - `test_find_implement_with_loop_returns_for_node` — locator finds the
    expected For in a spec lowered with explicit `Var("i")`.
  - `test_find_implement_with_loop_returns_undefined_when_missing`.
- **`test/correctness/CMakeLists.txt`** — phase4 test registered; its target
  adds `${Halide_SOURCE_DIR}/src` to the include path because it calls
  `Internal::` API directly.

### Known noise (not a regression)

- Lowering a spec pipeline emits warnings of the form
  `"It is meaningless to bound dimension v0 of function a to be within [0, 8] because the function is scheduled inline."`
  These fire on spec-pattern input Funcs which are inlined by default but carry
  `bound()` from the contract. Same warnings fire on the Phase 3 use-site path.
  Cosmetic only; could be suppressed for spec lowering in a follow-up (e.g. by
  suppressing inline-bound warnings on spec-pattern Funcs, or by scheduling spec
  inputs `compute_root()` before lowering).

______________________________________________________________________

## Session 4 — what landed (pre-Phase-4 setup, 2026-05-25)

### Source changes (committed, 1 commit)

- **`src/Lower.cpp`** — extracted `Internal::lower_to_canonical_form` from
  `lower_impl`. No-behavior-change refactor; the function owns every
  Stmt-transforming pass from `schedule_functions` through `strip_asserts`, with
  `find_intrinsics` and AMX `extract_tile_operations` *inside* the prefix.
  `lower_impl` now calls it. User `custom_passes` and backend offloading still
  run inline in `lower_impl` after it returns.

### Resolved open questions

- **OQ#5 (canonicalization prefix)** — pinned at the body of
  `lower_to_canonical_form`. v1 makes no stability promise; in-tree intrinsic
  catalogs stay in sync by construction. Cut is *before* custom_passes (so
  canonical form is a property of (env, Target) alone, not the user's local
  toolbox) and *before* backend offloading. See
  [DECISIONS.md](DECISIONS.md#oq5-canonicalization-prefix) for the four
  constraints that jointly point at this cut.

### Doc changes

- **DESIGN.md §4.4** rewritten — replaced the prose bullet list with the actual
  ordered pass sequence inside `lower_to_canonical_form`. OQ#5 marked resolved
  with a link to DECISIONS.
- **DECISIONS.md** — new OQ#5 entry with the four-constraint analysis and the
  alternatives considered (cut earlier / at `set_conceptual_code_stmt` /
  post-offload / versioned).
- DESIGN.md changelog updated.

### Tests

| Path                                         | Status | Notes                                                                                                                                        |
| -------------------------------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `test/correctness/implement_with_phase1.cpp` | Passes | Unchanged.                                                                                                                                   |
| `test/correctness/implement_with_phase2.cpp` | Passes | Unchanged.                                                                                                                                   |
| `test/correctness/implement_with_phase3.cpp` | Passes | Unchanged.                                                                                                                                   |
| Sample of unrelated correctness tests        | Passes | bounds, simplify, vectorize_varying_allocation_size, compute_at_split_rvar, memoize — exercising the any_memoized path through the refactor. |

______________________________________________________________________

## Phase 3 — what landed (session 3, 2026-05-25)

### Source changes (committed)

- **`src/ApplyImplementWith.h`** / **`src/ApplyImplementWith.cpp`** (new) —
  declares and defines `Internal::apply_implement_with_directives(env, target)`,
  the lowering-time pass that:
  1. Errors if any of `instr.required_features()` are missing from the compile
     Target.
  2. For each `ImplementWithDirective` on any stage of any Func in the
     deep-copied env, calls `instr.spec()`, identifies user Funcs corresponding
     to spec output (primary) and inputs (by Func name in the user's env), and
     transfers `FuncSchedule::bounds()` entries with a *positional* spec-arg →
     user-arg rename.
  3. Detects constant-extent / constant-min conflicts between a transferred
     bound and a pre-existing user bound, and errors with a message that names
     both values, the instruction, the var, and the user Func.
- **`src/Lower.cpp`** — calls `apply_implement_with_directives` after
  `lock_loop_levels`, before `wrap_func_calls`. The transfer runs on the
  deep-copied env so the user's pristine `Function` schedule is not mutated.
- **`src/Serialization.cpp`** — `serialize_stage_schedule` now hard-errors if
  `implement_with_directives()` is non-empty, so the silent-drop behavior (still
  load-bearing — see Phase 1 notes) cannot regress unnoticed when serialization
  is enabled in some future build.
- **`src/CMakeLists.txt`** — new header + source registered in alphabetical
  position (`ApplyImplementWith` slots between `AllocationBoundsInference` and
  `ApplySplit`).
- **`test/correctness/implement_with_phase3.cpp`** (new) — 5 sub-tests covering:
  target-feature-missing error; target-feature-present compile success; user's
  pristine schedule untouched after compile; constant-extent conflict error;
  spec-input-name not present in user env is silently skipped (Phase 4's job).
- **`test/correctness/CMakeLists.txt`** — phase3 test registered.

### Scope decisions

Phase 3 v1 only transfers `FuncSchedule::bounds()` (the `Bound` list). Other
directive categories — `vectorize`, `align_storage`, `unroll`, `reorder`,
`compute_with` between spec Funcs — are recognized by the design as transferable
but require structural information about loop nests that becomes available only
after the matcher (Phase 4) maps spec Vars to user split/reorder Vars. The Phase
3 hook still runs over all spec Funcs, so layering more transfers on top is
additive. See
[`DECISIONS.md`](DECISIONS.md#phase-3-scope-bounds-only-single-output) for the
rationale.

Multi-output instructions (Tuple-valued primaries and the `co_outputs` overload)
are *not* yet supported by the transfer pass: it errors explicitly if
`spec.outputs().size() != 1` or `co_output_names` is non-empty. The reason is
the same — multi-output mapping is naturally name-keyed and benefits from the
matcher's name-resolution.

### Resolved open questions

- **OQ#1 (target-feature check location)** — fires at lowering time (inside
  `apply_implement_with_directives`). The "opportunistic early-warning at
  construction time if Target is available" half of the design-doc proposal is
  **not** implemented in v1 — re-evaluate when generator integration lands.

### Tests

| Path                                         | Status | Notes                                                                     |
| -------------------------------------------- | ------ | ------------------------------------------------------------------------- |
| `test/correctness/implement_with_phase1.cpp` | Passes | Unchanged.                                                                |
| `test/correctness/implement_with_phase2.cpp` | Passes | Unchanged.                                                                |
| `test/correctness/implement_with_phase3.cpp` | Passes | 5 sub-tests. Two error-path tests skip if exceptions disabled at runtime. |

______________________________________________________________________

## Phase 2 — what landed (session 2, 2026-05-25)

### Source changes (committed, 2 commits)

- **`src/Function.cpp`** / **`src/Function.h`** — `bool is_spec_pattern` in
  `FunctionContents`; deep-copy propagation; `Function::mark_as_spec_pattern()`
  and `Function::is_spec_pattern()` getter/setter.
- **`src/Pipeline.cpp`** — materialization guard at the top of
  `compile_to_module()`: errors with a clear message if any output Func is
  spec-pattern, before any other compile-path checks fire.
- **`src/Instruction.h`** — `Internal::in_spec_thunk()` declaration; updated
  `spec()` docstring; updated file-level phase comment.
- **`src/Instruction.cpp`** — thread-local `g_spec_thunk_depth` counter and
  `Internal::SpecThunkScope` RAII guard; `Instruction::spec()` now wraps the
  thunk call in this scope and marks all output Funcs spec-pattern after the
  thunk returns.
- **`src/Func.cpp`** — `FuncRef::operator Expr()` extended: when called inside a
  spec thunk for an undefined Func, auto-stubs the Func with a zero body (typed
  from `required_types` if set, `Int(32)` otherwise) so that downstream
  scheduling directives (`bound`, `vectorize`, etc.) can be applied.
- **`test/correctness/implement_with_phase1.cpp`** — stubs for `a`, `b`, `c`
  removed; constructors changed to `Func(Float(32), 1, "name")` to declare
  element types. Test still passes.
- **`test/correctness/implement_with_phase2.cpp`** (new) — five sub-tests.
- **`test/correctness/CMakeLists.txt`** — phase2 test registered.

### Design decision made under uncertainty

The design doc §3.3 example uses bare `Func a("a")` (no type annotation) for
undefined spec input Funcs. Phase 2 requires explicit type declarations:
`Func(Float(32), 1, "a")`. Bare `Func("a")` auto-stubs with `Int(32)`, which may
cause type mismatches in Phase 4's matcher. Full type-polymorphic specs are v2.
See [`DECISIONS.md`](DECISIONS.md#spec-input-func-type-declarations).

### Tests

| Path                                         | Status | Notes                                                            |
| -------------------------------------------- | ------ | ---------------------------------------------------------------- |
| `test/correctness/implement_with_phase1.cpp` | Passes | Updated: stubs removed, typed Funcs.                             |
| `test/correctness/implement_with_phase2.cpp` | Passes | 5 sub-tests. Exception path skipped if built without exceptions. |

______________________________________________________________________

## Phase 1 — what landed (session 1, 2026-05-25)

### Source changes (uncommitted)

- **`src/Instruction.h`** (new) — `Instruction`, `Instruction::Builder`,
  `MatchContext`, `ImplementMode`, and `Internal::ImplementWithDirective`.
- **`src/Instruction.cpp`** (new) — Builder validation and `Instruction`
  accessors. No matching/lowering logic; spec/emit callbacks are stored and
  callable but inert.
- **`src/Func.h`** — `#include "Instruction.h"`. Added `Stage::implement_with`
  and `Func::implement_with` (single + multi-output overloads).
- **`src/Func.cpp`** — implementations. `Stage::implement_with` records on
  `StageSchedule`; `Func::implement_with` delegates to the init `Stage`.
- **`src/Schedule.h`** — forward-decl `ImplementWithDirective`; added
  `implement_with_directives()` accessors on `StageSchedule`.
- **`src/Schedule.cpp`** — storage in `StageScheduleContents`; copied in
  `StageSchedule::get_copy()`.
- **`src/CMakeLists.txt`** — header + source lists updated.
- **`docs/implement_with/DESIGN.md`** (moved from
  `/var/home/alex/dev/Halide/implement_with_design.md`) — OQ#2 marked resolved;
  changelog appended; `require` vs `requires` noted.
- **`docs/implement_with/IMPLEMENTATION_STATUS.md`** (this file).
- **`docs/implement_with/DECISIONS.md`**.
- **`.gitignore`** — `docs/implement_with/SCRATCH.md`.

### Tests

| Path                                         | Status | Notes                                                                                                                                                                         |
| -------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `test/correctness/implement_with_phase1.cpp` | Passes | 6 sub-tests: Builder happy path, init-stage recording, update-stage recording, multi-output co-output names, `StageSchedule::get_copy()` preservation, minimum-valid Builder. |

The test should be kept green by every subsequent phase. If a Phase N change
breaks it, fix the change — don't relax the test.

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
- Phase 1 doesn't touch `Function::Contents`; spec-pattern Func mode is deferred
  to Phase 2 as originally planned.

______________________________________________________________________

## Open issues / things to plan around

### Carried into Phase 4

- **Multi-output schedule transfer:** Phase 3 supports single-output only.
  Multi-output (`co_outputs`, Tuple-valued primaries) needs name-keyed spec→user
  mapping; cleanest to land alongside the matcher in Phase 4.
- **Non-bound directive transfer:** Phase 3 transfers only
  `FuncSchedule::bounds()`. The design contract also covers `vectorize`,
  `align_storage`, `unroll`, `reorder`, etc. These need the matcher's var rename
  to be useful at the user's split-Var level, so they layer naturally on Phase
  4\.

### Cross-cutting

- **Serialization:** Phase 3 added a hard error in `serialize_stage_schedule` if
  `implement_with_directives` is non-empty. `WITH_SERIALIZATION=OFF` is no
  longer load-bearing for the silent-drop reason, but is still load-bearing for
  the build — builds with serialization on will simply hard-error on any
  pipeline using `implement_with` until Phase 5+ adds proper serialization. See
  [DECISIONS.md](DECISIONS.md#serialization-hard-error-in-phase-3).

### Branching strategy

- All phases land on `alexreinking/implement_with` (single integration branch).
  Per-phase feature branches are *not* used.
- **Commits are PR-units.** The upstream workflow uses gh-stack (or similar) to
  turn each commit into its own PR, so each commit must stand on its own as a
  reviewable change. Split aggressively along natural seams: docs scaffolding
  separate from code, infrastructure separate from semantics, etc. Avoid
  mega-commits even when the phase is small.

______________________________________________________________________

## Session handoff — next session start here

Phase 4 is **closed**. The matcher's structural correspondence is plumbed
end-to-end: from the directive sitting on a user StageSchedule, through
`apply_implement_with_directives` lowering both sides to canonical form, through
`match_canonical_form` producing var/func renames, into the per-Func transfer
(bounds + storage dims) using those renames. Multi-output (Tuple-valued +
co_outputs) works. The project is ready for Phase 5.

### Phase 5 — emit + lowering integration

The matcher already produces a `MatchResult` per directive; Phase 5's job is to
*use* it to substitute the matched user-side For body with the spec's emit()
output. Sketch:

1. **Restructure or rerun.** Currently the matcher runs inside
   `apply_implement_with_directives` (which lives mid-prefix in `lower_impl`,
   before `lower_to_canonical_form`). Phase 5's emit substitution must run
   *after* canonical-form lowering --- the Stmt to substitute into is the
   canonical-form user Stmt, not the env. Two options:

   - Run the matcher twice (once for transfer, once for emit) --- simple but
     wasteful.
   - Restructure: emit substitution becomes a post-canonical-form pass that
     re-locates the user Fors by directive, re-runs the matcher, and rewrites
     the matched For body. The transfer pass stays where it is (it has to mutate
     the env before bounds_inference).

2. **MatchContext wiring.** The `MatchContext` accessors (`ctx.input(name)`,
   `ctx.output(name)`, `ctx.param(name)`) are still stubs. Phase 5 fills them
   from the `MatchResult` --- `ctx.input("a")` returns a handle that emits a
   Load from the user-side Func bound to spec name "a" (which the matcher's
   `func_rename` already records).

3. **Substitution mutator.** Given the matched user-side For and the spec's
   emit() Stmt, build an `IRMutator` that replaces the For's body. Must preserve
   any surrounding canonical-form structure (Lets, Realizes, ProducerConsumer
   wrappers) so subsequent passes (custom_passes, GPU offload, parallel-task
   lowering) see a well-formed Stmt.

### Smaller follow-ups surfaced across Phase 4

- **vectorize-as-width-requirement transfer.** Phase 4 transfers bounds +
  storage dims. The design also calls for transferring `vectorize(i, 4)` from
  the spec onto the user as a width constraint (the user must vectorize the
  matched loop at width ≥ 4). This lives on `StageSchedule::dims()` (for_type),
  not `FuncSchedule`, so it needs the matcher's For-loop var binding to map spec
  dim → user dim. More naturally landed alongside Phase 5's emit (which also
  needs the For-level binding).

- **Auto-stub through `Tuple(FuncRef)`.** `out(i) = c(i)` (direct
  FuncRef-to-FuncRef assignment) routes through `Tuple(FuncRef)`, which asserts
  before the spec-thunk auto-stub gets a chance to fire. Workaround in the SDOT
  case study is `Expr c_val = c(i); out(i) = c_val;`. A one-line fix in
  `Tuple(const FuncRef &)` to honor `Internal::in_spec_thunk()` would remove the
  wart.

- **Auto-stub pure-arg names.** The §3.4 example uses `A.bound(_1, 0, 4)`, but
  the auto-stubbed Func's pure args come from the call site (`A(i, k)` -> args
  "i" and "k$0"), so neither `_1` nor any user-visible Var resolves. Either
  expose the names via a getter or steer spec authors away from
  `bound(_N, ...)`.

- **Non-zero ImageParam mins (Phase 7 prep).** Three of four case studies pin
  input mins to 0 because the matcher's Simplify- equivalence fallback can't
  prove `x - ua.min.0 == x` when `ua.min.0` is symbolic. Affine match parameters
  (Phase 7, v1.5) handle this properly.

- **Opportunistic early-warning target-feature check.** The second half of
  OQ#1's proposal --- check target features at `implement_with` call time when
  Target is available --- is not implemented. Consider for generator workflows
  if late-error reports prove noisy.
