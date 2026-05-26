# `implement_with` — Decisions Log

The authoritative record of *why* the implementation looks the way it does. Each
entry captures: the decision, the alternatives considered, and the reasoning.
Future sessions should consult this before re-litigating.

Distinguish two categories:

- **Design-doc-consistent decisions** — fill in detail that
  [`DESIGN.md`](DESIGN.md) leaves underspecified. Cheap. Just record here.
- **Design-doc-revising decisions** — *also* require an update to `DESIGN.md`
  (text edit + changelog entry). Marked **[REVISES DESIGN]**.

______________________________________________________________________

## Index

- [OQ#2 — spec-Func naming](#oq2-spec-func-naming) (Phase 1, session 1)
- [`require` vs `requires`](#require-vs-requires) (Phase 1, session 1)
  **[REVISES DESIGN]**
- [Storage location for the directive](#storage-location-for-the-directive)
  (Phase 1, session 1)
- [Serialization deferral](#serialization-deferral) (Phase 1, session 1)
- [Spec input Func type declarations](#spec-input-func-type-declarations) (Phase
  2, session 2)
- [Auto-stub for undefined spec input Funcs](#auto-stub-for-undefined-spec-input-funcs)
  (Phase 2, session 2)
- [Materialization guard location](#materialization-guard-location) (Phase 2,
  session 2)
- [Schedule-transfer hook point](#schedule-transfer-hook-point) (Phase 3,
  session 3)
- [Phase 3 scope: bounds only, single-output](#phase-3-scope-bounds-only-single-output)
  (Phase 3, session 3)
- [Positional spec-Var → user-Var mapping in Phase 3](#positional-spec-var--user-var-mapping-in-phase-3)
  (Phase 3, session 3)
- [Constant-bound conflict detection](#constant-bound-conflict-detection) (Phase
  3, session 3)
- [Serialization: hard error in Phase 3](#serialization-hard-error-in-phase-3)
  (Phase 3, session 3)
- [OQ#5 — canonicalization prefix](#oq5-canonicalization-prefix) (Pre-Phase-4,
  session 4) **[REVISES DESIGN]**
- [Phase 4 case-study set](#phase-4-case-study-set) (Phase 4, session 5)
- [Phase 5 architecture: re-run the matcher post-canonical-form](#phase-5-architecture-re-run-the-matcher-post-canonical-form)
  (Phase 5, session 10)
- [Phase 5 soft-failure semantics](#phase-5-soft-failure-semantics) (Phase 5,
  session 10) **[REVISES DESIGN]**
- [MatchContext API: name strings, not Exprs](#matchcontext-api-name-strings-not-exprs)
  (Phase 5, session 10)

______________________________________________________________________

## OQ#2 — spec-Func naming

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** Spec authors must explicitly name spec-pattern Funcs via the
existing `Func(name)` constructor. The Builder's `spec()` docstring spells this
out. `MatchContext::input("a")` (Phase 3+) will key off these names.

**Alternatives considered.**

1. Macro that captures the local variable name, e.g. `HALIDE_SPEC_FUNC(a)`
   expanding to `Func a("a")`. **Rejected:** adds macro surface area Halide
   otherwise avoids, and provides no real ergonomic benefit over `Func a("a")`.
2. Positional naming via `MatchContext::input(0)`. **Rejected:** loses
   readability; the BLAS-style examples in §3.7 of DESIGN.md become essentially
   unreadable; cross-references like "the `K` dimension matches between `A` and
   `B`" lose their handle.

**Reasoning.** Matches existing Halide convention for naming Funcs. Authors who
care about names already use them; authors who don't can accept auto-generated
names from the matcher's diagnostic output. No new mechanism required.

**Implication.** The Phase 1 test enforces this by reading
`p.outputs()[0].name() == "out"`. Future phases that consume spec pipelines
(matcher, schedule transfer) can assume the names are authoritative.

______________________________________________________________________

## `require` vs `requires`

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved **\[REVISES
DESIGN\]**

**Decision.** The Builder method is named `require` (singular), not `requires`
as written throughout DESIGN.md §3.3–§3.7 and §4.1.

**Alternatives considered.**

1. Keep `requires`. **Rejected:** `requires` is a keyword in C++20 and later.
   Halide itself builds at C++17, so the *definition* would compile, but any
   **user** code at C++20+ calling `Instruction::Builder::requires(...)` would
   be a syntax error. The feature's whole point is to be a user-extensible
   library; making it only usable at C++17 would be a serious wart.
2. `with_required_features`, `target_requires`, `needs_target`, etc.
   **Rejected:** verbose and farther from the doc; `require` reads naturally and
   is closest to the original.

**Reasoning.** The cost of the rename is small (one method, one docstring line).
The cost of "this API is only usable in C++17" is ongoing and ugly.

**Doc update.** DESIGN.md's prose examples (§3.3 onward) still spell it
`requires` for now — the rename is noted in the changelog
([§11](DESIGN.md#11-changelog)) but the examples are not yet rewritten, to avoid
mid-Phase-1 churn. **Phase 2 or whichever session next touches the examples
should propagate the rename throughout.**

______________________________________________________________________

## Storage location for the directive

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is stored as a `std::vector` on
`StageScheduleContents`, with accessors exposed on `StageSchedule`.

**Alternatives considered.**

1. **Side-table in `Instruction.cpp` keyed by `Internal::Function`.** Considered
   for minimum schema churn. **Rejected:** static state that survives across
   pipelines, awkward lifetime, complicates deep_copy.
2. **`FuncScheduleContents` (per-Func, not per-Stage).** **Rejected:** §3.3 of
   DESIGN.md shows `f.update(0).implement_with(...)` — the directive is
   per-stage, so storage should be per-stage too. The Func overload of
   `implement_with` is just sugar for the init stage.

**Reasoning.** Per-stage matches the design's semantics and matches existing
per-stage directives like `prefetches`. The added field is small (one vector +
accessors); deep-copy was a one-line addition.

**Implication.** Future phases (matcher, schedule transfer) read the directive
from `definition.schedule().implement_with_directives()`. This is the canonical
location.

______________________________________________________________________

## Serialization deferral

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is **not** serialized. Phase 1
builds with `WITH_SERIALIZATION=OFF`. Builds with serialization on will silently
drop directives across serialize/deserialize.

**Alternatives considered.**

1. Add a flatbuffer schema entry and full serialize/deserialize support in Phase
   1\. **Rejected:** scope creep — flatbuffer schema changes are non-trivial and
   the directive is inert in Phase 1 anyway. Better landed when the directive
   carries semantic weight (Phase 3+).
2. Hard-error on serialization of a non-empty directive vector. **Tentatively
   accepted as a follow-up** (see below); not landed in session 1.

**Reasoning.** A new field that flatbuffers doesn't know about round-trips to
empty — bad if the user thinks it persists, fine if they don't try to serialize.
Phase 1 is inert anyway. Document the gap; don't let it pretend to work.

**Follow-up actions.**

- Before Phase 3 (where directives start influencing bounds inference), either:
  - add full serialization, or
  - file an `internal_assert` in the serializer that fires when
    `implement_with_directives` is non-empty, so it can't slip silently.

______________________________________________________________________

## Spec input Func type declarations

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** Undefined spec input Funcs must be declared with explicit element
types via the `Func(Type, dims, name)` constructor — e.g.
`Func a(Float(32), 1, "a")`. The bare `Func a("a")` form (no type) auto-stubs
with `Int(32)`, which may cause type mismatches in Phase 4's structural matcher
if the use-site type differs.

**Alternatives considered.**

1. Allow bare `Func a("a")` and infer type from context (e.g., from the output
   Func's type). **Rejected for Phase 2:** type-polymorphic specs are a v2
   feature; Phase 2 does not need to solve type inference. The design doc §3.3
   example using bare `Func a` is aspirational shorthand.
2. Require explicit stub definitions (`a(i) = 0.0f;`). **Rejected:** Phase 2's
   purpose is to eliminate this boilerplate.
3. Use `ImageParam` for spec inputs. **Rejected:** `ImageParam` does not support
   Halide scheduling directives (`bound`, `vectorize`, etc.) that form the
   instruction contract (see design doc §4.2).

**Reasoning.** The matcher (Phase 4) will need to compare spec Func types
against use-site types. Getting the type right now (via `required_types`)
produces correct spec IR; getting it wrong defers a type error until Phase 4.
The cost to spec authors is minimal: one constructor change per input Func.

**Implication.** Phase 2 and Phase 4 tests use `Func(Float(32), 1, "name")`. The
design doc's bare-`Func` examples remain aspirational and should be revisited
when v2 type-polymorphic specs are designed.

______________________________________________________________________

## Auto-stub for undefined spec input Funcs

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** When `FuncRef::operator Expr()` is called for an undefined Func
inside a spec thunk, it auto-defines the Func with a zero stub body (typed from
`required_types` if set, `Int(32)` otherwise). This allows downstream scheduling
directives (`bound`, `vectorize`, etc.) to be applied — they require the Func to
have dimension args set, which only happens via `define`.

**Alternatives considered.**

1. Register args without creating a definition (new `Function::set_args()` API).
   **Rejected:** more invasive API change; semantically no different from a stub
   body that is never executed.
2. Modify `Func::bound()` to accept undefined Funcs in spec context.
   **Rejected:** `bound` and `vectorize` both ultimately need the args list;
   bypassing those checks individually would require many touch points.
3. Thread-local flag + special Call::make path (no auto-stub). **Rejected:**
   this approach produces undefined-Func Call nodes in the IR, which breaks
   `func.outputs() == 0` invariants in several downstream consumers.

**Reasoning.** The auto-stub is the minimal change that makes the full
design-doc spec syntax work end-to-end. The stub body is irrelevant because spec
pipelines are never lowered. The `const_cast` in `FuncRef::operator Expr()` is
safe because `FuncRef` is already a handle type — const-ness on a `FuncRef` does
not imply immutability of the backing Function.

**Implication.** After auto-stubbing, spec input Funcs are defined with a stub
body. The matcher (Phase 4) identifies them as inputs by checking whether they
appear in `Pipeline::outputs()` — inputs are those referenced but not in the
outputs list, regardless of whether they have stub definitions or not.

______________________________________________________________________

## Materialization guard location

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** The spec-pattern guard is placed at the top of
`Pipeline::compile_to_module()`, which is the single choke point through which
all compile/realize paths pass (`compile_jit`, `compile_to_object`,
`compile_to_file`, `realize`, etc. all call it).

**Alternatives considered.**

1. Also guard `Pipeline::realize(vector<int32_t>)` directly. **Deferred:** the
   `realize` path's existing `has_pure_definition` checks do not fire for a
   defined output Func, so `compile_to_module` is always reached. No need to
   duplicate.
2. Guard at `Func::realize()` level. **Rejected:** this would miss other
   `Pipeline` entry points; guarding at `Pipeline::compile_to_module` is more
   complete.

**Reasoning.** Single guard, no duplication, catches all code paths.

**Implication.** The error message mentions `compile_to_module` in its context.
Future diagnostics improvements (Phase 6) may want to give more specific
pipeline-entry-point names in the error.

______________________________________________________________________

## Schedule-transfer hook point

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** Schedule transfer and target-feature check fire at *lowering time*
— specifically, inside a new pass
`Internal::apply_implement_with_directives(env, target)` invoked from
`lower_impl` after `lock_loop_levels` and before `wrap_func_calls`. The pass
mutates the deep-copied env produced earlier in `lower_impl`, never the user's
pristine `Function`.

**Alternatives considered.**

1. **Eager: at `Stage::implement_with` call time.** Mutate the user's `Function`
   schedule on the spot. **Rejected:** Target is generally not known at call
   time (generator workflows pin Target only at compile/realize time), so the
   target-feature check could not fire. Also, mutating the user's `Function` at
   directive-record time mixes declaration and side-effect in a way that makes
   it hard to recompile the same Pipeline under a different Target — a use case
   the deep-copy approach handles cleanly.
2. **Lowering time, but mutating the user's `Function` directly (not the deep
   copy).** **Rejected:** breaks the recompile-under-different-Target property,
   and would make any error in transfer leave the user's pristine state in an
   inconsistent partial-transfer state.
3. **Lowering time, in a new pass *after* bounds inference.** **Rejected:** the
   whole point of transferring bounds is that they participate in bounds
   inference. Has to be before.

**Reasoning.** Lowering time is where the Target is known and where deep-copy
semantics already exist; both are required. Resolves OQ#1 in favor of
lowering-time checking. The opportunistic call-time early-warning half of OQ#1's
proposal is not implemented in v1.

**Implication.** `Stage::implement_with` remains a pure recording call (no
validation beyond `instr.defined()`). All schedule-application semantics live in
`ApplyImplementWith.cpp` and are exercised by `compile_to_module` and downstream
entry points.

______________________________________________________________________

## Phase 3 scope: bounds only, single-output

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** Phase 3's `apply_implement_with_directives` pass transfers only
`FuncSchedule::bounds()` entries from spec Funcs to user Funcs, and supports
only single-output instructions. Multi-output (`co_outputs` or Tuple-valued
primaries) and the broader directive categories (`vectorize`, `align_storage`,
`unroll`, `reorder`, `compute_with` between spec Funcs) are deferred to Phase 4,
where they layer naturally on top of the structural matcher.

**Alternatives considered.**

1. **Transfer everything in Phase 3.** **Rejected:** most non-bound directives
   reference the spec's loop variables (e.g. `vectorize(i, 8)` becomes a Split +
   dim modification). To translate these to the user's schedule we need to know
   which user Var corresponds to the spec's `i` — which is the matcher's job.
   Doing this without the matcher means either inventing a parallel rename
   heuristic that disagrees with the matcher (bad), or scheduling the wrong
   dimension and relying on bounds-inference to catch it (also bad).
2. **Support multi-output now via name-keyed mapping.** **Rejected:** the
   name-keyed mapping needs `find_transitive_calls`-style traversal keyed by the
   directive's `co_output_names` plus the spec's output list. It works, but it's
   also the kind of name-resolution logic the matcher needs anyway, so
   colocating it there avoids duplication.

**Reasoning.** Phase 3's job is to make schedule transfer real *enough* to
validate the architectural choice (lowering-time, deep-copy env) and to install
the load-bearing target-feature check. The bounds-only, single-output slice is
sufficient to exercise both. Expanding scope at this point would push the deeper
directive translation work into a phase whose other concerns (the matcher's
IR-level reasoning) are heavier and more error-prone.

**Implication.** The `apply_implement_with_directives` pass explicitly errors
when `spec.outputs().size() != 1` or `co_output_names` is non-empty — silently
no-oping would be worse. Phase 4 must lift this restriction in the same change
that lands the matcher.

______________________________________________________________________

## Positional spec-Var → user-Var mapping in Phase 3

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved (provisional —
replaced by matcher in Phase 4)

**Decision.** In Phase 3, when transferring a `Bound` from a spec Func to its
matched user Func, the spec's Var name is renamed by argument position: spec's
`args()[0]` → user's `args()[0]`, etc. The directive's `loop_var_name` is *not*
consulted by Phase 3's renamer; positional match anchored at arg 0 is sufficient
for the bound-transfer case.

**Alternatives considered.**

1. **Anchor the rename at the directive's `loop_var_name`.** That is, find the
   user arg matching `loop_var_name`, set that as "position k", and align the
   spec's outermost Var to it. **Rejected for Phase 3:** adds complexity to
   handle deeper-than-loop-var dims, and the matcher replaces this logic anyway
   in Phase 4.
2. **Wait for the matcher and do no renaming in Phase 3.** **Rejected:** then
   transferred bounds would carry the spec's `i` literally onto the user's `out`
   (whose arg is `x`), and bounds inference would not recognize the var name.
   Even the basic target-check tests would not be runnable end-to-end without
   it.

**Reasoning.** Pragmatic: positional rename gets the bound-transfer case to a
working end-to-end demonstration. Phase 4 establishes the real spec-Var →
user-Var map from the structural match and replaces this fallback.

**Implication.** Tests in `implement_with_phase3.cpp` use 1D user Funcs and 1D
spec Funcs to keep the positional mapping unambiguous. Mixing dimensionalities
(e.g. 2D user, 1D spec scheduled on an inner split Var) currently transfers
correctly only by accident; the matcher will make it deliberate.

______________________________________________________________________

## Constant-bound conflict detection

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** When `apply_implement_with_directives` would add a transferred
`Bound{var, min, extent}` to a user Func that already has a `Bound` on the same
`var`, and both `min` (or both `extent`) are compile-time-constant integers with
different values, the pass errors immediately with a message naming both values,
the conflicting `var`, the user Func, and the instruction.

**Alternatives considered.**

1. **No detection; rely on bounds inference and runtime checks.** **Rejected:**
   the failure mode in that path is a runtime assertion failure at `realize()`
   time with a generic "extent mismatch" message — no mention of
   `implement_with`, no link to the spec that imposed the constraint.
2. **Detect everything, including symbolic conflicts.** **Rejected for v1:**
   would need a Simplify-based comparison or an SMT-style reasoner. v1.5's
   affine match parameters make symbolic conflict checking more interesting;
   defer.

**Reasoning.** The constant-vs-constant case is the most common mistake (user
sets a tile-by-N that conflicts with the instruction's fixed-width bound), and
is easy to catch with a couple of lines of `as_const_int` calls. Symbolic cases
get a less specific error from downstream bounds inference; good enough for v1.

**Implication.** Phase 3 test `test_conflicting_bound_errors` is exactly this
case (user bound 16, spec bound 8). Future v1.5 work on affine parameters should
extend `check_bound_conflict` to handle symbolic forms.

______________________________________________________________________

## Serialization: hard error in Phase 3

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved (supersedes
[`serialization-deferral`](#serialization-deferral))

**Decision.** `Serializer::serialize_stage_schedule` now `user_assert`s that
`implement_with_directives().empty()`. Pipelines using `implement_with` cannot
currently be serialized.

**Reasoning.** Phase 1 left the door open (silent drop on serialize). Phase 3
makes directives load-bearing — they influence bounds inference — so a silent
drop now produces a *different* pipeline on deserialization, not just one
missing a noop directive. Hard erroring prevents this. When Phase 5+ adds proper
serialize/deserialize support for instructions, the assert is lifted.

**Implication.** `WITH_SERIALIZATION=ON` builds work fine for pipelines that
don't use `implement_with`. Pipelines that do will hit a clear `user_assert`
mentioning `implement_with` and `WITH_SERIALIZATION=OFF`.

______________________________________________________________________

## OQ#5 — canonicalization prefix

**Phase:** Pre-Phase-4 · **Session:** 4 (2026-05-25) · **Status:** Resolved
**[REVISES DESIGN]**

**Decision.** Canonical form for the structural matcher (Phase 4+) is the Stmt
returned by
`Internal::lower_to_canonical_form(outputs, env, order, fused_groups, target, requirements, pipeline_name, trace_pipeline, log)`,
defined in `src/Lower.cpp`. This function owns every Stmt-transforming pass from
`schedule_functions` through `strip_asserts`, in the order they appeared in
`lower_impl` before extraction. The cut is *before* user-injected
`custom_passes` and *before* backend offloading (Hexagon RPC, GPU offload,
parallel-task lowering, closure generation). DESIGN.md §4.4 lists the exact
ordered pass sequence; the canonical source of truth is the function body, not
the docs.

No stability promise in v1; revisit in v1.5+ if/when out-of-tree instruction
libraries with independent release cadence become a target. Changes to
`lower_to_canonical_form` are breaking changes for in-tree instruction
declarations.

**Alternatives considered.**

1. **Cut earlier (after `partition_loops + simplify`, line ~380 in
   `lower_impl`).** **Rejected:** would put `find_intrinsics` and
   `extract_tile_operations` *outside* canonical form. HVX MAC and AMX tile
   patterns are lifted by those passes; matching pre-lifting would force spec
   authors to anticipate every lifting rewrite, which directly contradicts the
   design's intent of giving authors a high-level Halide spec syntax.
2. **Cut at `result_module.set_conceptual_code_stmt(s)` (Halide's own "this is
   canonical IR" mark, line 482).** **Rejected:** that point is *after*
   `custom_passes`. Including user-injected `IRMutator`-implemented passes in
   canonical form makes matching depend on the user's local toolbox —
   non-reproducible across pipelines that import the same instruction library.
   By cutting before custom_passes, we keep canonical form a property of (env,
   Target) alone.
3. **Cut later, after `inject_hexagon_rpc` / `inject_gpu_offload`.**
   **Rejected:** at that point the matched region is wrapped in a kernel
   parameter or extern-call shell, and matching across that boundary adds
   significant complexity. The pre-offload IR has GPU loops as `For` nodes with
   `For::GPUBlock`/`For::GPUThread` types — the matcher can recognize those
   directly.
4. **Versioned prefix (each `Instruction` carries a "matched against canonical
   form vN" tag; Halide ships compatibility implementations for prior
   prefixes).** **Rejected for v1:** heavy, premature. v1 ships in-tree
   intrinsic catalogs that update in lockstep with the prefix. Revisit when
   out-of-tree libraries become real.

**Reasoning.** Four constraints jointly point at this cut:

- *Punt stability:* we want freedom to refine the prefix as the matcher is
  built. The single-function source-of-truth model gives one place reviewers can
  detect canonical-form-changing edits; that's enough.
- *LLVM-backed GPU first, string-y backends later:* string-y backends (OpenCL,
  Metal) diverge in *codegen*, well after this cut. They inherit canonical form
  for free as long as their codegen handles the substituted intrinsic `Call`
  nodes. LLVM-backed targets (x86, ARM, CUDA via NVPTX, Hexagon) all flow
  through this exact point.
- *HVX MAC and AMX tiles:* both rely on intrinsic-lifting passes
  (`find_intrinsics`, `extract_tile_operations`) that run inside the prefix.
  Putting them inside means both spec and use-site see the lifted form.
- *Exo's "alignment is the scheduler's problem":* the user's schedule must
  produce a loop nest that structurally matches the spec's after the prefix. The
  matcher is structural-mod-alpha + Simplify-equivalence — no SMT, matching
  Halide's §6 non-goal.

**Implication.**

- Spec authors who want to match a pattern *before* FindIntrinsics lifting have
  no opt-out today. The spec should be written so its post-FindIntrinsics form
  matches the post-FindIntrinsics use-site form. Document this as a known sharp
  edge.
- `custom_passes` runs in `lower_impl` *after* the canonical-form prefix returns
  and *after* match-and-emit (Phase 5+) substitutes the matched region. User
  custom passes therefore observe the substituted Stmt, which is what they
  typically want.
- The Phase 3 schedule-transfer pass (`apply_implement_with_directives`) is
  *upstream* of canonical form — it runs in `lower_impl` before
  `lower_to_canonical_form` is invoked, so its transferred bounds participate in
  canonical-form bounds inference. This is correct and doesn't need to change.

______________________________________________________________________

## Phase 4 case-study set

**Phase:** 4 · **Session:** 5 (2026-05-25) · **Status:** Resolved

**Decision.** Four case studies validate the Phase 4 matcher before OQ#5 is
considered "settled" beyond v1's no-stability-promise scope:

1. **`vfmadd231ps_256`** (AVX2 + FMA) — single intrinsic; post- FindIntrinsics
   lifting; the §3.3 canonical example. Simplest end- to-end smoke test.
2. **SDOT 4×4 GEMV** (ARM dot-product) — multi-instruction emit (four
   broadcasting SDOTs); joint matching of related outputs; §3.4 example.
3. **HVX MAC sequences and/or AMX tile triples** — at least one, ideally both.
   Validates that `find_intrinsics` (HVX) and (gated) `extract_tile_operations`
   (AMX) running *inside* the canonical-form prefix produces match-friendly IR
   for those targets. Without this case, OQ#5's decision to keep those passes
   inside the prefix is not actually validated end-to-end.
4. **PTX MMA on CUDA** (NVPTX tensor cores) — validates LLVM-backed GPU
   matching. Canonical form is cut *before* `inject_gpu_offload`, so spec and
   use-site share `For::GPUBlock`/`For::GPUThread` loop structure; matching
   across that structure is the test that closes the "LLVM-backed backends
   first" line from [OQ#5](#oq5-canonicalization-prefix). String-y GPU backends
   (OpenCL/Metal) are out of scope for v1.

**Alternatives considered.**

1. **Three case studies (drop PTX MMA).** **Rejected:** without a GPU case the
   OQ#5 analysis is incomplete — the "we can't exclude GPU from the design"
   constraint stated during OQ#5 resolution was never actually exercised in
   code.
2. **Pick one of HVX-vs-AMX rather than allowing either.** **Deferred:** HVX
   requires a Hexagon toolchain to test JIT-able intrinsic lowering; AMX
   requires SapphireRapids-class hardware or `Target::AVX512_SapphireRapids`
   JIT. Whichever is reachable from the developer's toolchain at the time gets
   the first cut; the other can land later as a separate test.
3. **Add a string-y GPU case (OpenCL / Metal MMA).** **Rejected for v1:** OQ#5's
   stated phasing is LLVM-backed backends first, string-y codegen backends
   later. String-y backends diverge in *codegen*, well after canonical form;
   they inherit the matcher for free as long as their codegen handles the
   substituted intrinsic `Call` nodes. Revisit when v1's LLVM cases are stable.

**Reasoning.** Each case tests a distinct property of the canonical- form prefix
or the matcher mechanism. The set is the smallest one that covers single-call
vs. multi-call, post-FindIntrinsics vs. pre- intrinsic-lifting, CPU-SIMD vs.
explicit-register-file vs. GPU. Each also has a direct correspondence to the
OQ#5 constraints, so the case-study results validate the OQ#5 cut decision
concretely.

**Implication.** The Phase 4 PR series should land these tests incrementally
(vfmadd first; SDOT and PTX MMA after the matcher is stable; HVX/AMX last
because they require less common toolchains). Phase 4 is not "complete" until at
least three of the four are wired up end-to-end; the fourth (HVX or AMX,
whichever is harder to reach) can carry over into Phase 5 with the emit work,
since by then the matcher is the established part.

______________________________________________________________________

## Phase 5 architecture: re-run the matcher post-canonical-form

**Phase:** 5 · **Session:** 10 (2026-05-26) · **Status:** Resolved

**Decision.** Phase 5's emit substitution is a separate post-canonical- form
pass, `Internal::apply_implement_with_emit`. It runs in `lower_impl` immediately
after `lower_to_canonical_form` returns. For each pending directive, it
independently re-runs the structural matcher (lowers the spec via
`lower_spec_to_canonical_form`, locates the matched For on each side via
`find_implement_with_loop` / `find_spec_primary_loop`, calls
`match_canonical_form`) and uses the resulting `MatchResult` to build the
`MatchContext` passed to the Instruction's emit callback.

**Alternatives considered.**

1. **Cache the matcher's `MatchResult` from Phase 3's
   `apply_implement_with_directives` and re-use it in Phase 5.** **Rejected:**
   Phase 3's matcher operates on a deep-copied env
   (`lower_pipeline_to_canonical_form` internally deep-copies) so the user-side
   Func names in the cached `func_rename` reference an env that lower_impl drops
   on the floor. The names mostly survive into the post-canonical-form `Stmt`
   (Halide preserves Func names through the prefix), but threading a
   `MatchResult` forward through `wrap_func_calls`, `bounds_inference`,
   `storage_flattening`, and the rest of the prefix would require touching half
   a dozen API surfaces and add a load-bearing invariant that future prefix
   changes have to respect. Re-running the matcher is one extra spec lowering
   per directive — a few ms, paid at compile time — and keeps Phase 5
   self-contained.

2. **Run substitution inline in `apply_implement_with_directives`.**
   **Rejected:** that pass runs mid-prefix on env, not on a Stmt. To substitute
   Stmt regions you need the Stmt, which is what `lower_to_canonical_form`
   produces. Inlining would require producing the Stmt mid-prefix, which is
   exactly what `lower_to_canonical_form` is.

3. **Cut canonical form earlier or later so substitution can happen inline with
   matching.** **Rejected:** OQ#5 already pins the cut. Moving it for this
   reason is a much bigger change.

**Reasoning.** The cost of one extra spec lowering per directive is small. The
architectural simplicity of "Phase 3 does bound transfer, Phase 5 does emit,
both independently call the matcher" is significant for review and future
maintenance.

**Implication.** Phase 5 substitution is the only consumer of the matcher's
`MatchResult` post-Phase-3. Phase 4 case-study tests use the matcher directly.
Future passes (e.g. autoscheduler integration) can follow the same "lower →
match → consume" pattern without dragging the MatchResult around through
lowering.

______________________________________________________________________

## Phase 5 soft-failure semantics

**Phase:** 5 · **Session:** 10 (2026-05-26) · **Status:** Resolved **\[REVISES
DESIGN\]**

**Decision.** When the Phase 5 emit substitution cannot match (user- side For
not found, spec-side For not found, or structural match failure), it emits a
`user_warning` describing what failed and **falls through to ordinary lowering**
for that directive. The compile continues; the matched region computes as if no
`implement_with` were present.

This diverges from [DESIGN.md] §4.3 step 3, which calls for a hard error in
Strict mode. v1 currently has no other mode (Soft is deferred to v2), so this is
effectively unconditional soft behavior in v1.

**Alternatives considered.**

1. **Hard-error on match failure (Strict-mode-as-designed).** **Rejected for
   v1:** the matcher has known gaps that surface as non-zero buffer min
   subtraction (the user's `out[x - out.min.0]` doesn't structurally match the
   spec's `out[i]` even though both reduce to the same value under the bound
   assertions; the Simplify-equivalence fallback can't see the assertion
   context). These gaps surface in Phase 4's existing
   `test_multi_output_tuple_primary_compiles` and
   `test_multi_output_co_outputs_compile` tests, which were written to validate
   the multi-output schedule transfer plumbing without needing the matcher to
   fully succeed. Hard-erroring on match failure would break those tests without
   proving anything new about Phase 5.

2. **Strict-by-default with a `Soft` `ImplementMode` opt-out, and flip the
   failing Phase 4 tests to Soft.** **Deferred to Phase 6:** v1 has no
   per-directive mode flag exposed yet. Adding one is the right Phase 6 work
   alongside diagnostics improvements.

3. **Tighten the matcher to close the min-subtraction gap, then keep Strict.**
   **Deferred:** the right long-term answer, but landing the substitution path
   first and the matcher tightening second keeps each PR review-sized.

**Reasoning.** Phase 5's primary value is enabling end-to-end demonstrations of
the implement_with machinery. The substitution code is correct independently of
matcher coverage; the matcher's coverage is a separable axis. Shipping Phase 5
in soft mode lets the substitution path land and be tested without artificially
blocking on matcher improvements.

**Implication.**

- v1 `Func::implement_with` is a *suggestion* in practice: if the matcher
  accepts the spec/user pair, the emit fires; if not, the pipeline runs the
  original lowering. This is observable via the `user_warning` output.
- Phase 5's own tests are designed so the matcher does succeed (1D ImageParam
  inputs with pinned mins, no user-side `out.bound()` on the output Func) — so
  the emit substitution is actually exercised end-to-end by
  `implement_with_phase5.cpp`.
- Phase 6 / Phase 7 should restore Strict-mode-as-designed once the matcher's
  min-subtraction gap closes. The `user_warning` text references DECISIONS.md so
  future contributors can find this rationale.

______________________________________________________________________

## MatchContext API: name strings, not Exprs

**Phase:** 5 · **Session:** 10 (2026-05-26) · **Status:** Resolved

**Decision.** `MatchContext`'s v1 API exposes user-side names as
`const std::string &`:

- `ctx.input(spec_name)` → user-side buffer/Func name
- `ctx.output(spec_name)` → user-side output buffer/Func name
- `ctx.var(spec_var)` → user-side bare Var name
- `ctx.target()` → compile Target

Emit callbacks construct their own Halide IR (Loads, Stores, Calls, Ramps, etc.)
using these names. There is no v1 API that returns an `Expr` pre-built from
spec-pattern info (e.g. an 8-lane Load at the spec's vectorize width).

**Alternatives considered.**

1. **`ctx.input(spec_name)` returns an `Expr`** equivalent to the spec's
   reference at that name (e.g. an 8-lane vector Load). The DESIGN.md §3.3
   example uses `ctx.input("a")` as a vector-typed `Call::PureIntrinsic`
   operand, which suggests this. **Deferred:** "the right Expr" depends on the
   spec's vectorize width, the spec's scheduling directives on that Func, and
   the surrounding loop context — which all need to be threaded into
   MatchContext. v1 punts: emit authors compose Loads themselves. Once concrete
   emit patterns surface, the helper Exprs are easy to add as convenience
   methods on top of the name-string API.

2. **Pre-uniquify-name keys (require emit author to write `ctx.input("a$3")`
   matching the post-uniquify Func name).** **Rejected:** Halide uniquifies
   process-wide, so the suffix is not stable across runs. Emit authors should
   write the same string they used when declaring the spec Func.

**Reasoning.** Names are the smallest stable interface between spec authoring
and emit callback. They survive uniquification (with the `<name>$<digits>`
lookup the MatchContext implementation already handles). Higher-level accessors
can layer on later without breaking v1 emit callbacks.

**Implication.** The DESIGN.md §3.3 example doesn't compile as written today —
`ctx.input("a")` returns a string, not an Expr the caller can pass to
`Call::make`. Emit authors instead construct Loads/Ramps explicitly. DESIGN.md
examples should be updated to reflect this (or v1.5 should add the convenience
accessor and revisit the examples). For now, the test in
`test/correctness/implement_with_phase5.cpp` demonstrates the v1 emit pattern
via `Internal::StringImm::make(ctx.input("a"))` as a Call argument.
