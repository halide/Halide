# `implement_with` — Decisions Log

The authoritative record of *why* the implementation looks the way it
does. Each entry captures: the decision, the alternatives considered, and
the reasoning. Future sessions should consult this before re-litigating.

Distinguish two categories:

- **Design-doc-consistent decisions** — fill in detail that
  [`DESIGN.md`](DESIGN.md) leaves underspecified. Cheap. Just record here.
- **Design-doc-revising decisions** — *also* require an update to
  `DESIGN.md` (text edit + changelog entry). Marked **[REVISES DESIGN]**.

---

## Index

- [OQ#2 — spec-Func naming](#oq2-spec-func-naming) (Phase 1, session 1)
- [`require` vs `requires`](#require-vs-requires) (Phase 1, session 1) **[REVISES DESIGN]**
- [Storage location for the directive](#storage-location-for-the-directive) (Phase 1, session 1)
- [Serialization deferral](#serialization-deferral) (Phase 1, session 1)
- [Spec input Func type declarations](#spec-input-func-type-declarations) (Phase 2, session 2)
- [Auto-stub for undefined spec input Funcs](#auto-stub-for-undefined-spec-input-funcs) (Phase 2, session 2)
- [Materialization guard location](#materialization-guard-location) (Phase 2, session 2)
- [Schedule-transfer hook point](#schedule-transfer-hook-point) (Phase 3, session 3)
- [Phase 3 scope: bounds only, single-output](#phase-3-scope-bounds-only-single-output) (Phase 3, session 3)
- [Positional spec-Var → user-Var mapping in Phase 3](#positional-spec-var--user-var-mapping-in-phase-3) (Phase 3, session 3)
- [Constant-bound conflict detection](#constant-bound-conflict-detection) (Phase 3, session 3)
- [Serialization: hard error in Phase 3](#serialization-hard-error-in-phase-3) (Phase 3, session 3)
- [OQ#5 — canonicalization prefix](#oq5-canonicalization-prefix) (Pre-Phase-4, session 4) **[REVISES DESIGN]**

---

## OQ#2 — spec-Func naming

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** Spec authors must explicitly name spec-pattern Funcs via the
existing `Func(name)` constructor. The Builder's `spec()` docstring spells
this out. `MatchContext::input("a")` (Phase 3+) will key off these names.

**Alternatives considered.**

1. Macro that captures the local variable name, e.g.
   `HALIDE_SPEC_FUNC(a)` expanding to `Func a("a")`. **Rejected:** adds
   macro surface area Halide otherwise avoids, and provides no real
   ergonomic benefit over `Func a("a")`.
2. Positional naming via `MatchContext::input(0)`. **Rejected:** loses
   readability; the BLAS-style examples in §3.7 of DESIGN.md become
   essentially unreadable; cross-references like "the `K` dimension
   matches between `A` and `B`" lose their handle.

**Reasoning.** Matches existing Halide convention for naming Funcs.
Authors who care about names already use them; authors who don't can
accept auto-generated names from the matcher's diagnostic output. No new
mechanism required.

**Implication.** The Phase 1 test enforces this by reading
`p.outputs()[0].name() == "out"`. Future phases that consume spec
pipelines (matcher, schedule transfer) can assume the names are
authoritative.

---

## `require` vs `requires`

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved
**[REVISES DESIGN]**

**Decision.** The Builder method is named `require` (singular), not
`requires` as written throughout DESIGN.md §3.3–§3.7 and §4.1.

**Alternatives considered.**

1. Keep `requires`. **Rejected:** `requires` is a keyword in C++20 and
   later. Halide itself builds at C++17, so the *definition* would
   compile, but any **user** code at C++20+ calling
   `Instruction::Builder::requires(...)` would be a syntax error. The
   feature's whole point is to be a user-extensible library; making it
   only usable at C++17 would be a serious wart.
2. `with_required_features`, `target_requires`, `needs_target`, etc.
   **Rejected:** verbose and farther from the doc; `require` reads
   naturally and is closest to the original.

**Reasoning.** The cost of the rename is small (one method, one
docstring line). The cost of "this API is only usable in C++17" is
ongoing and ugly.

**Doc update.** DESIGN.md's prose examples (§3.3 onward) still spell it
`requires` for now — the rename is noted in the changelog
([§11](DESIGN.md#11-changelog)) but the examples are not yet rewritten,
to avoid mid-Phase-1 churn. **Phase 2 or whichever session next touches
the examples should propagate the rename throughout.**

---

## Storage location for the directive

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is stored as a
`std::vector` on `StageScheduleContents`, with accessors exposed on
`StageSchedule`.

**Alternatives considered.**

1. **Side-table in `Instruction.cpp` keyed by `Internal::Function`.**
   Considered for minimum schema churn. **Rejected:** static state that
   survives across pipelines, awkward lifetime, complicates deep_copy.
2. **`FuncScheduleContents` (per-Func, not per-Stage).** **Rejected:**
   §3.3 of DESIGN.md shows `f.update(0).implement_with(...)` — the
   directive is per-stage, so storage should be per-stage too. The Func
   overload of `implement_with` is just sugar for the init stage.

**Reasoning.** Per-stage matches the design's semantics and matches
existing per-stage directives like `prefetches`. The added field is
small (one vector + accessors); deep-copy was a one-line addition.

**Implication.** Future phases (matcher, schedule transfer) read the
directive from `definition.schedule().implement_with_directives()`. This
is the canonical location.

---

## Serialization deferral

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is **not** serialized.
Phase 1 builds with `WITH_SERIALIZATION=OFF`. Builds with serialization
on will silently drop directives across serialize/deserialize.

**Alternatives considered.**

1. Add a flatbuffer schema entry and full serialize/deserialize support
   in Phase 1. **Rejected:** scope creep — flatbuffer schema changes are
   non-trivial and the directive is inert in Phase 1 anyway. Better
   landed when the directive carries semantic weight (Phase 3+).
2. Hard-error on serialization of a non-empty directive vector.
   **Tentatively accepted as a follow-up** (see below); not landed in
   session 1.

**Reasoning.** A new field that flatbuffers doesn't know about
round-trips to empty — bad if the user thinks it persists, fine if they
don't try to serialize. Phase 1 is inert anyway. Document the gap; don't
let it pretend to work.

**Follow-up actions.**

- Before Phase 3 (where directives start influencing bounds inference),
  either:
  - add full serialization, or
  - file an `internal_assert` in the serializer that fires when
    `implement_with_directives` is non-empty, so it can't slip silently.

---

## Spec input Func type declarations

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** Undefined spec input Funcs must be declared with explicit
element types via the `Func(Type, dims, name)` constructor — e.g.
`Func a(Float(32), 1, "a")`. The bare `Func a("a")` form (no type) auto-stubs
with `Int(32)`, which may cause type mismatches in Phase 4's structural
matcher if the use-site type differs.

**Alternatives considered.**

1. Allow bare `Func a("a")` and infer type from context (e.g., from the
   output Func's type). **Rejected for Phase 2:** type-polymorphic specs are
   a v2 feature; Phase 2 does not need to solve type inference. The
   design doc §3.3 example using bare `Func a` is aspirational shorthand.
2. Require explicit stub definitions (`a(i) = 0.0f;`). **Rejected:** Phase 2's
   purpose is to eliminate this boilerplate.
3. Use `ImageParam` for spec inputs. **Rejected:** `ImageParam` does not support
   Halide scheduling directives (`bound`, `vectorize`, etc.) that form the
   instruction contract (see design doc §4.2).

**Reasoning.** The matcher (Phase 4) will need to compare spec Func types
against use-site types. Getting the type right now (via `required_types`)
produces correct spec IR; getting it wrong defers a type error until Phase 4.
The cost to spec authors is minimal: one constructor change per input Func.

**Implication.** Phase 2 and Phase 4 tests use `Func(Float(32), 1, "name")`.
The design doc's bare-`Func` examples remain aspirational and should be
revisited when v2 type-polymorphic specs are designed.

---

## Auto-stub for undefined spec input Funcs

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** When `FuncRef::operator Expr()` is called for an undefined Func
inside a spec thunk, it auto-defines the Func with a zero stub body (typed
from `required_types` if set, `Int(32)` otherwise). This allows downstream
scheduling directives (`bound`, `vectorize`, etc.) to be applied — they
require the Func to have dimension args set, which only happens via `define`.

**Alternatives considered.**

1. Register args without creating a definition (new `Function::set_args()` API).
   **Rejected:** more invasive API change; semantically no different from a
   stub body that is never executed.
2. Modify `Func::bound()` to accept undefined Funcs in spec context.
   **Rejected:** `bound` and `vectorize` both ultimately need the args list;
   bypassing those checks individually would require many touch points.
3. Thread-local flag + special Call::make path (no auto-stub). **Rejected:**
   this approach produces undefined-Func Call nodes in the IR, which breaks
   `func.outputs() == 0` invariants in several downstream consumers.

**Reasoning.** The auto-stub is the minimal change that makes the full
design-doc spec syntax work end-to-end. The stub body is irrelevant because
spec pipelines are never lowered. The `const_cast` in `FuncRef::operator Expr()`
is safe because `FuncRef` is already a handle type — const-ness on a `FuncRef`
does not imply immutability of the backing Function.

**Implication.** After auto-stubbing, spec input Funcs are defined with a stub
body. The matcher (Phase 4) identifies them as inputs by checking whether they
appear in `Pipeline::outputs()` — inputs are those referenced but not in the
outputs list, regardless of whether they have stub definitions or not.

---

## Materialization guard location

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** The spec-pattern guard is placed at the top of
`Pipeline::compile_to_module()`, which is the single choke point through
which all compile/realize paths pass (`compile_jit`, `compile_to_object`,
`compile_to_file`, `realize`, etc. all call it).

**Alternatives considered.**

1. Also guard `Pipeline::realize(vector<int32_t>)` directly.
   **Deferred:** the `realize` path's existing `has_pure_definition` checks
   do not fire for a defined output Func, so `compile_to_module` is always
   reached. No need to duplicate.
2. Guard at `Func::realize()` level. **Rejected:** this would miss other
   `Pipeline` entry points; guarding at `Pipeline::compile_to_module` is
   more complete.

**Reasoning.** Single guard, no duplication, catches all code paths.

**Implication.** The error message mentions `compile_to_module` in its
context. Future diagnostics improvements (Phase 6) may want to give more
specific pipeline-entry-point names in the error.

---

## Schedule-transfer hook point

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** Schedule transfer and target-feature check fire at *lowering
time* — specifically, inside a new pass
`Internal::apply_implement_with_directives(env, target)` invoked from
`lower_impl` after `lock_loop_levels` and before `wrap_func_calls`. The
pass mutates the deep-copied env produced earlier in `lower_impl`, never
the user's pristine `Function`.

**Alternatives considered.**

1. **Eager: at `Stage::implement_with` call time.** Mutate the user's
   `Function` schedule on the spot. **Rejected:** Target is generally
   not known at call time (generator workflows pin Target only at
   compile/realize time), so the target-feature check could not fire.
   Also, mutating the user's `Function` at directive-record time mixes
   declaration and side-effect in a way that makes it hard to recompile
   the same Pipeline under a different Target — a use case the deep-copy
   approach handles cleanly.
2. **Lowering time, but mutating the user's `Function` directly (not the
   deep copy).** **Rejected:** breaks the recompile-under-different-Target
   property, and would make any error in transfer leave the user's
   pristine state in an inconsistent partial-transfer state.
3. **Lowering time, in a new pass *after* bounds inference.**
   **Rejected:** the whole point of transferring bounds is that they
   participate in bounds inference. Has to be before.

**Reasoning.** Lowering time is where the Target is known and where
deep-copy semantics already exist; both are required. Resolves OQ#1 in
favor of lowering-time checking. The opportunistic call-time early-warning
half of OQ#1's proposal is not implemented in v1.

**Implication.** `Stage::implement_with` remains a pure recording call
(no validation beyond `instr.defined()`). All schedule-application
semantics live in `ApplyImplementWith.cpp` and are exercised by
`compile_to_module` and downstream entry points.

---

## Phase 3 scope: bounds only, single-output

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** Phase 3's `apply_implement_with_directives` pass transfers
only `FuncSchedule::bounds()` entries from spec Funcs to user Funcs, and
supports only single-output instructions. Multi-output (`co_outputs` or
Tuple-valued primaries) and the broader directive categories
(`vectorize`, `align_storage`, `unroll`, `reorder`, `compute_with`
between spec Funcs) are deferred to Phase 4, where they layer naturally
on top of the structural matcher.

**Alternatives considered.**

1. **Transfer everything in Phase 3.** **Rejected:** most non-bound
   directives reference the spec's loop variables (e.g. `vectorize(i, 8)`
   becomes a Split + dim modification). To translate these to the user's
   schedule we need to know which user Var corresponds to the spec's
   `i` — which is the matcher's job. Doing this without the matcher
   means either inventing a parallel rename heuristic that disagrees
   with the matcher (bad), or scheduling the wrong dimension and
   relying on bounds-inference to catch it (also bad).
2. **Support multi-output now via name-keyed mapping.** **Rejected:**
   the name-keyed mapping needs `find_transitive_calls`-style traversal
   keyed by the directive's `co_output_names` plus the spec's output
   list. It works, but it's also the kind of name-resolution logic the
   matcher needs anyway, so colocating it there avoids duplication.

**Reasoning.** Phase 3's job is to make schedule transfer real *enough*
to validate the architectural choice (lowering-time, deep-copy env) and
to install the load-bearing target-feature check. The bounds-only,
single-output slice is sufficient to exercise both. Expanding scope at
this point would push the deeper directive translation work into a
phase whose other concerns (the matcher's IR-level reasoning) are
heavier and more error-prone.

**Implication.** The `apply_implement_with_directives` pass explicitly
errors when `spec.outputs().size() != 1` or `co_output_names` is
non-empty — silently no-oping would be worse. Phase 4 must lift this
restriction in the same change that lands the matcher.

---

## Positional spec-Var → user-Var mapping in Phase 3

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved
(provisional — replaced by matcher in Phase 4)

**Decision.** In Phase 3, when transferring a `Bound` from a spec Func
to its matched user Func, the spec's Var name is renamed by argument
position: spec's `args()[0]` → user's `args()[0]`, etc. The directive's
`loop_var_name` is *not* consulted by Phase 3's renamer; positional
match anchored at arg 0 is sufficient for the bound-transfer case.

**Alternatives considered.**

1. **Anchor the rename at the directive's `loop_var_name`.** That is,
   find the user arg matching `loop_var_name`, set that as "position k",
   and align the spec's outermost Var to it. **Rejected for Phase 3:**
   adds complexity to handle deeper-than-loop-var dims, and the matcher
   replaces this logic anyway in Phase 4.
2. **Wait for the matcher and do no renaming in Phase 3.**
   **Rejected:** then transferred bounds would carry the spec's
   `i` literally onto the user's `out` (whose arg is `x`), and bounds
   inference would not recognize the var name. Even the basic
   target-check tests would not be runnable end-to-end without it.

**Reasoning.** Pragmatic: positional rename gets the bound-transfer
case to a working end-to-end demonstration. Phase 4 establishes the
real spec-Var → user-Var map from the structural match and replaces
this fallback.

**Implication.** Tests in `implement_with_phase3.cpp` use 1D user Funcs
and 1D spec Funcs to keep the positional mapping unambiguous. Mixing
dimensionalities (e.g. 2D user, 1D spec scheduled on an inner split
Var) currently transfers correctly only by accident; the matcher will
make it deliberate.

---

## Constant-bound conflict detection

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved

**Decision.** When `apply_implement_with_directives` would add a
transferred `Bound{var, min, extent}` to a user Func that already has
a `Bound` on the same `var`, and both `min` (or both `extent`) are
compile-time-constant integers with different values, the pass errors
immediately with a message naming both values, the conflicting `var`,
the user Func, and the instruction.

**Alternatives considered.**

1. **No detection; rely on bounds inference and runtime checks.**
   **Rejected:** the failure mode in that path is a runtime assertion
   failure at `realize()` time with a generic "extent mismatch" message
   — no mention of `implement_with`, no link to the spec that imposed
   the constraint.
2. **Detect everything, including symbolic conflicts.** **Rejected for
   v1:** would need a Simplify-based comparison or an SMT-style
   reasoner. v1.5's affine match parameters make symbolic conflict
   checking more interesting; defer.

**Reasoning.** The constant-vs-constant case is the most common
mistake (user sets a tile-by-N that conflicts with the instruction's
fixed-width bound), and is easy to catch with a couple of lines of
`as_const_int` calls. Symbolic cases get a less specific error from
downstream bounds inference; good enough for v1.

**Implication.** Phase 3 test
`test_conflicting_bound_errors` is exactly this case (user bound 16,
spec bound 8). Future v1.5 work on affine parameters should extend
`check_bound_conflict` to handle symbolic forms.

---

## Serialization: hard error in Phase 3

**Phase:** 3 · **Session:** 3 (2026-05-25) · **Status:** Resolved
(supersedes [`serialization-deferral`](#serialization-deferral))

**Decision.** `Serializer::serialize_stage_schedule` now `user_assert`s
that `implement_with_directives().empty()`. Pipelines using
`implement_with` cannot currently be serialized.

**Reasoning.** Phase 1 left the door open (silent drop on serialize).
Phase 3 makes directives load-bearing — they influence bounds
inference — so a silent drop now produces a *different* pipeline on
deserialization, not just one missing a noop directive. Hard erroring
prevents this. When Phase 5+ adds proper serialize/deserialize support
for instructions, the assert is lifted.

**Implication.** `WITH_SERIALIZATION=ON` builds work fine for pipelines
that don't use `implement_with`. Pipelines that do will hit a clear
`user_assert` mentioning `implement_with` and `WITH_SERIALIZATION=OFF`.

---

## OQ#5 — canonicalization prefix

**Phase:** Pre-Phase-4 · **Session:** 4 (2026-05-25) · **Status:** Resolved
**[REVISES DESIGN]**

**Decision.** Canonical form for the structural matcher (Phase 4+) is the
Stmt returned by `Internal::lower_to_canonical_form(outputs, env, order,
fused_groups, target, requirements, pipeline_name, trace_pipeline, log)`,
defined in `src/Lower.cpp`. This function owns every Stmt-transforming
pass from `schedule_functions` through `strip_asserts`, in the order they
appeared in `lower_impl` before extraction. The cut is *before*
user-injected `custom_passes` and *before* backend offloading (Hexagon
RPC, GPU offload, parallel-task lowering, closure generation). DESIGN.md
§4.4 lists the exact ordered pass sequence; the canonical source of
truth is the function body, not the docs.

No stability promise in v1; revisit in v1.5+ if/when out-of-tree
instruction libraries with independent release cadence become a target.
Changes to `lower_to_canonical_form` are breaking changes for in-tree
instruction declarations.

**Alternatives considered.**

1. **Cut earlier (after `partition_loops + simplify`, line ~380 in
   `lower_impl`).** **Rejected:** would put `find_intrinsics` and
   `extract_tile_operations` *outside* canonical form. HVX MAC and AMX
   tile patterns are lifted by those passes; matching pre-lifting would
   force spec authors to anticipate every lifting rewrite, which
   directly contradicts the design's intent of giving authors a
   high-level Halide spec syntax.
2. **Cut at `result_module.set_conceptual_code_stmt(s)` (Halide's own
   "this is canonical IR" mark, line 482).** **Rejected:** that point
   is *after* `custom_passes`. Including user-injected
   `IRMutator`-implemented passes in canonical form makes matching
   depend on the user's local toolbox — non-reproducible across
   pipelines that import the same instruction library. By cutting
   before custom_passes, we keep canonical form a property of (env,
   Target) alone.
3. **Cut later, after `inject_hexagon_rpc` / `inject_gpu_offload`.**
   **Rejected:** at that point the matched region is wrapped in a kernel
   parameter or extern-call shell, and matching across that boundary
   adds significant complexity. The pre-offload IR has GPU loops as
   `For` nodes with `For::GPUBlock`/`For::GPUThread` types — the
   matcher can recognize those directly.
4. **Versioned prefix (each `Instruction` carries a "matched against
   canonical form vN" tag; Halide ships compatibility implementations
   for prior prefixes).** **Rejected for v1:** heavy, premature. v1
   ships in-tree intrinsic catalogs that update in lockstep with the
   prefix. Revisit when out-of-tree libraries become real.

**Reasoning.** Four constraints jointly point at this cut:

- *Punt stability:* we want freedom to refine the prefix as the matcher
  is built. The single-function source-of-truth model gives one place
  reviewers can detect canonical-form-changing edits; that's enough.
- *LLVM-backed GPU first, string-y backends later:* string-y backends
  (OpenCL, Metal) diverge in *codegen*, well after this cut. They
  inherit canonical form for free as long as their codegen handles the
  substituted intrinsic `Call` nodes. LLVM-backed targets (x86, ARM,
  CUDA via NVPTX, Hexagon) all flow through this exact point.
- *HVX MAC and AMX tiles:* both rely on intrinsic-lifting passes
  (`find_intrinsics`, `extract_tile_operations`) that run inside the
  prefix. Putting them inside means both spec and use-site see the
  lifted form.
- *Exo's "alignment is the scheduler's problem":* the user's schedule
  must produce a loop nest that structurally matches the spec's after
  the prefix. The matcher is structural-mod-alpha + Simplify-equivalence
  — no SMT, matching Halide's §6 non-goal.

**Implication.**

- Spec authors who want to match a pattern *before* FindIntrinsics
  lifting have no opt-out today. The spec should be written so its
  post-FindIntrinsics form matches the post-FindIntrinsics use-site
  form. Document this as a known sharp edge.
- `custom_passes` runs in `lower_impl` *after* the canonical-form
  prefix returns and *after* match-and-emit (Phase 5+) substitutes the
  matched region. User custom passes therefore observe the substituted
  Stmt, which is what they typically want.
- The Phase 3 schedule-transfer pass (`apply_implement_with_directives`)
  is *upstream* of canonical form — it runs in `lower_impl` before
  `lower_to_canonical_form` is invoked, so its transferred bounds
  participate in canonical-form bounds inference. This is correct and
  doesn't need to change.
