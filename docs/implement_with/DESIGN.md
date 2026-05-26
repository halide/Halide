# `implement_with`: User-Defined Instruction Selection in Halide

**Status:** Draft design — v1 spec for implementation handoff
**Audience:** Halide contributors, instruction-library authors, kernel writers

## 1. Summary

This document proposes `implement_with`, a Halide scheduling directive that lets
users commit a region of a pipeline to be implemented by a user-defined
*instruction* — a hardware intrinsic, a fixed sequence of intrinsics, or an
external library call — declared in an out-of-tree library.

An instruction's contract is itself a small Halide `Pipeline`: a pure-Halide
specification of what it computes, scheduled with directives that express its
bounds, layout, and loop-structure requirements. Instruction selection is the
substitution of this small pipeline (plus an attached hardware emission) for
a matching region of the user's larger pipeline.

The design is inspired by Exo's `replace` mechanism but reuses Halide's existing
language for both algorithms and schedules: there is no separate constraint
vocabulary, no separate spec language. The same `Pipeline`, `Func`, `Var`,
`RDom`, `bound`, `align_storage`, etc. that users write to specify pipelines
are the vocabulary in which instruction contracts are written.

The goal is to make hardware idioms expressible as user-extensible libraries
rather than as built-in compiler passes, while preserving Halide's trust
properties for scheduling directives: errors at scheduling time, not silently
degraded code.

The feature is primarily a manual-scheduling power tool. Autoscheduler-
readiness is a secondary property of the design and not on the critical path.

## 2. Motivation

Halide today recognizes hardware intrinsics through a built-in pass
(`FindIntrinsics.cpp`) that runs implicitly and matches a fixed catalog of
patterns. Adding a new intrinsic requires modifying Halide. This has three
consequences:

1. **Per-architecture intrinsic recognition is a growing maintenance burden.**
   Every new ISA extension (AVX-512 variants, AMX, ARM SME, HVX revisions)
   requires compiler changes, and the pattern-matching code is brittle to
   schedule changes.

2. **Users cannot trust that an intrinsic will be emitted.** Today, a schedule
   that "should" produce a vfmadd may or may not, depending on lowering,
   simplifier behavior, and LLVM. There is no way to *assert* that a specific
   instruction is used.

3. **Multi-instruction idioms are second-class.** Hardware-idiomatic sequences
   (four broadcasting SDOTs for a 4×4 GEMV, AMX tile load/compute/store
   triples, HVX multiply-accumulate patterns) are either hand-emitted in
   backend code or absent. There is no user-facing way to declare them.

`implement_with` addresses all three by making instructions user-defined
library objects with explicit contracts, and making instruction selection an
explicit, checkable scheduling directive.

## 3. Design Overview

### 3.1 The two new concepts

**`Instruction`**: A user-defined object declaring a substitutable
implementation. It carries:

- a **spec**: a thunk that returns a `Pipeline` describing what the instruction
  computes. The pipeline's `Func`s — both its inputs and its outputs — are
  *spec-pattern Funcs* (see §4.7): synthetic Funcs that exist only as match
  patterns, never lowered or codegen'd. Their *bodies* define the match
  pattern; their *schedules* define the contract on the matched region
  (bounds, layout, loop structure).
- **match parameters** (optional, v1.5): named integer parameters that the
  spec is parameterized over and that the matcher binds from the use site.
  All bounds expressions involving these parameters must be affine with
  integer coefficients.
- a **target requirement**: a set of `Target::Feature` flags that must be
  enabled.
- an **emit callback**: a function from `MatchContext` to lowered Halide IR.

**`Func::implement_with`**: A scheduling directive that commits a Func, at a
named loop level, to be implemented by a specific `Instruction`. At schedule
application time it checks the target requirement and installs the spec
pipeline's schedule constraints on the matched Funcs. At lowering time it
confirms a structural match between the spec pipeline and the lowered IR at
the named loop level, and replaces that region with the emit callback's
output. Match failures are hard errors.

### 3.2 No separate constraint vocabulary

All constraints — bounds, strides, alignment, vector width, storage layout,
co-computation requirements between outputs — are expressed by scheduling the
spec pipeline using Halide's existing scheduling directives. There is no
parallel "constraints" API. If Halide can express something via a scheduling
directive on a Func or Pipeline, an instruction declaration can use it as a
constraint.

This has a consequence worth being explicit about: the expressiveness of
instruction contracts scales with Halide's scheduling surface area. Future
scheduling directives become future constraint vocabulary automatically.

### 3.3 Example: single-intrinsic instruction

```cpp
// In halide-instructions-x86 (external library)
Instruction vfmadd231ps_256 = Instruction::declare("vfmadd231ps_256")
    .spec([]() -> Pipeline {
        Var i;
        Func a, b, c, out;

        // Body: what the instruction computes
        out(i) = a(i) * b(i) + c(i);

        // Contract: how the surrounding schedule must shape the region
        for (Func& f : {a, b, c, out}) {
            f.bound(i, 0, 8)
             .align_storage(i, 32)
             .vectorize(i, 8);
        }

        return Pipeline({out});
    })
    .requires({Target::FMA, Target::AVX2})
    .emit([](const MatchContext& ctx) -> Stmt {
        return Evaluate::make(
            Call::make(Float(32, 8),
                       "llvm.x86.fma.vfmadd231.ps.256",
                       {ctx.input("a"), ctx.input("b"), ctx.input("c")},
                       Call::PureIntrinsic));
    });
```

User-side schedule:

```cpp
output.update(0)
      .tile(x, y, xo, yo, xi, yi, 8, 4)
      .vectorize(xi)
      .implement_with(xi, vfmadd231ps_256);
```

### 3.4 Example: multi-instruction sequence

```cpp
// 4x4 GEMV implemented as four broadcasting SDOTs on ARM
Instruction sdot_gemv_4x4 = Instruction::declare("sdot_gemv_4x4")
    .spec([]() -> Pipeline {
        Var i; RDom k(0, 4);
        Func A, b, c, out;

        out(i) = c(i) + sum(A(i, k) * b(k));

        // Contract
        A.bound(i, 0, 4).bound(_1, 0, 4)            // 4x4
         .align_storage(i, 16);
        b.bound(i, 0, 4).align_storage(i, 4);
        c.bound(i, 0, 4).align_storage(i, 16);
        out.bound(i, 0, 4).align_storage(i, 16);

        return Pipeline({out});
    })
    .requires({Target::ARMDotProd})
    .emit([](const MatchContext& ctx) -> Stmt {
        return generate_sdot_sequence(ctx);  // four SDOTs with lane broadcasts
    });
```

Same `implement_with` surface; emit produces a sequence rather than a single
call.

### 3.5 Example: multi-output instruction (Tuple-valued)

```cpp
// ARMv8 frecpe + frecps: two related outputs per lane in one idiom
Instruction frecpe_pair = Instruction::declare("frecpe_pair")
    .spec([]() -> Pipeline {
        Var i;
        Func x, out;

        // Tuple-valued output: estimate, and one Newton step refinement
        Expr est = fast_inverse(x(i));
        out(i) = Tuple(est, (2.0f - x(i) * est));

        for (Func& f : {x, out}) {
            f.bound(i, 0, 4).align_storage(i, 16).vectorize(i, 4);
        }

        return Pipeline({out});
    })
    .requires({Target::ARMv8})
    .emit([](const MatchContext& ctx) -> Stmt {
        // Emit frecpe + frecps as a paired sequence operating on ctx.input("x")
        // producing both components of ctx.output("out")
        return generate_frecpe_pair(ctx);
    });
```

### 3.6 Example: multi-output instruction (multiple Funcs)

```cpp
// Hypothetical accelerator instruction producing a result tile and a status word
Instruction tile_op = Instruction::declare("tile_op")
    .spec([]() -> Pipeline {
        Var i, j;
        Func A, B, result, status;

        result(i, j) = /* computation over A, B */;
        status() = /* scalar status derived from A, B */;

        // The two outputs are co-computed at the tile granularity
        result.compute_with(status, /* at level */ i);

        A.bound(i, 0, 16).bound(j, 0, 16).align_storage(i, 64);
        B.bound(i, 0, 16).bound(j, 0, 16).align_storage(i, 64);
        result.bound(i, 0, 16).bound(j, 0, 16).align_storage(i, 64);
        // status is a scalar Func, no bound needed

        return Pipeline({result, status});
    })
    .requires({/* target features */})
    .emit([](const MatchContext& ctx) -> Stmt { /* ... */ });
```

User-side schedule:

```cpp
my_result.compute_with(my_status, i);
my_result.implement_with(i, tile_op, /*co_outputs=*/{my_status});
```

### 3.7 Example: BLAS substitution (v1.5, affine parameters)

```cpp
Instruction sgemm = Instruction::declare("sgemm")
    .params({"M", "N", "K"})
    .spec([](Expr M, Expr N, Expr K) -> Pipeline {
        Var i, j; RDom k(0, K);
        Func A, B, C, out;

        out(i, j) = C(i, j) + sum(A(i, k) * B(k, j));

        // Affine bounds in match parameters. Shared parameters express
        // cross-buffer relational constraints implicitly: A and B both
        // mention K, so the matcher unifies their shared dimension.
        A.bound(i, 0, M).bound(j, 0, K).align_storage(i, 64);
        B.bound(i, 0, K).bound(j, 0, N).align_storage(i, 64);
        C.bound(i, 0, M).bound(j, 0, N).align_storage(i, 64);
        out.bound(i, 0, M).bound(j, 0, N).align_storage(i, 64);

        return Pipeline({out});
    })
    .requires({/* build-time flag for BLAS linkage */})
    .emit([](const MatchContext& ctx) -> Stmt {
        int M = ctx.param("M"), N = ctx.param("N"), K = ctx.param("K");
        return Call::make(Int(32), "cblas_sgemm",
                          {/* marshaled args with concrete M, N, K */},
                          Call::Extern);
    });
```

## 4. Detailed Design

### 4.1 Instruction declaration API

```cpp
class Instruction {
public:
    static Builder declare(const std::string& name);
    // ... accessor methods ...
};

class Instruction::Builder {
public:
    // v1: zero-arg spec thunk returning a Pipeline
    Builder& spec(std::function<Pipeline()> spec_fn);
    // v1.5: spec parameterized over match parameters (passed as Exprs)
    Builder& spec(std::function<Pipeline(const std::vector<Expr>&)> spec_fn);
    Builder& params(std::vector<std::string> param_names);          // v1.5

    Builder& requires(std::set<Target::Feature> features);
    Builder& emit(std::function<Stmt(const MatchContext&)> emit_fn);
    Instruction build();
};
```

**Convenience overload:** A spec thunk that returns a `Func` rather than a
`Pipeline` is implicitly wrapped via `Pipeline({f})`. Single-output
instructions stay terse.

`MatchContext` exposes:
- `input(name)` → matched buffer/Expr for each spec pipeline input Func
- `output(name)` → matched buffer/Expr for each spec pipeline output Func
- `param(name)` → resolved integer value for each match parameter (v1.5)
- `type(name)` → resolved type of each input/output (for type-polymorphic
  specs, v2)

### 4.2 Spec pipelines: how constraints are read

The spec pipeline is *not* a normal Halide pipeline. Its Funcs are spec-pattern
Funcs (§4.7), which means they are inspected for two purposes:

1. **Their definitions** form the match pattern. The matcher compares them
   structurally against the lowered IR at the user's named loop level.

2. **Their schedules** form the contract. When `implement_with` commits a
   user's Func to be implemented by this instruction, every scheduling
   directive applied to a spec pipeline Func is *transferred* to the
   corresponding matched Func in the user's pipeline, where it participates
   in normal bounds inference, layout determination, and consistency checking.

This means there is no separate `BufferConstraints` vocabulary, no separate
constraint-installation mechanism. The spec author writes:

```cpp
a.bound(i, 0, 8).align_storage(i, 32);
```

and `implement_with` ensures that the user's matched `a`-Func has this
`bound` and `align_storage` applied at schedule application time. Halide's
existing machinery does the rest.

**Cross-buffer relational constraints are implicit in shared expressions.**
If two spec Funcs both have `bound(i, 0, M)` for the same `Expr M`, the
matcher unifies them: the matched Funcs must have consistent extents in
that dimension. For constant `M` this is trivially checked; for `M` a match
parameter (v1.5), this is part of the affine solve.

**Loop structure is part of the contract.** The spec pipeline's loop nest
structure — the `Var`s used in the body, any `reorder` or `compute_with`
applied to the spec — implicitly requires that the user's schedule produce a
loop nest of the same shape at the matched region. The spec's `i` is matched
against whichever user `Var` is at the named loop level; deeper loops are
matched against the user's loops below that level.

### 4.3 Scheduling directive

```cpp
Func& Func::implement_with(VarOrRVar loop_level,
                            const Instruction& instr,
                            ImplementMode mode = ImplementMode::Strict);

// Multi-output variant
Func& Func::implement_with(VarOrRVar loop_level,
                            const Instruction& instr,
                            std::vector<Func> co_outputs,
                            ImplementMode mode = ImplementMode::Strict);
```

For multi-output instructions, the directive is applied to one Func and
names its co-outputs. The co-outputs must already be scheduled to share a
loop nest with the primary (via `compute_with`) or be members of a single
Tuple-valued Func. The matcher uses this information to know which user
Funcs to match against which spec outputs.

Semantics at schedule application time:

1. Check that `instr.target_features()` are all enabled in the compile
   Target. If not, error.
2. For each spec pipeline Func, identify the corresponding user Func (input
   Funcs by name in the body; output Funcs by name and via the
   `co_outputs` list).
3. Transfer every scheduling directive on each spec Func to its
   corresponding user Func. These directives participate in normal bounds
   inference and layout determination from this point on.
4. Mark the primary Func (and the named loop level) for matching during
   lowering.

Semantics at lowering time:

1. After standard scheduling-driven lowering reaches the named loop level,
   structurally match the body against the spec pipeline (§4.4).
2. If matching succeeds, bind match parameters and invoke the emit callback.
   Replace the matched region with the returned Stmt.
3. If matching fails in `Strict` mode, error with diagnostic output
   comparing canonical spec form to canonical use-site form. In `Soft` mode
   (deferred to v2), fall back to ordinary lowering.

### 4.4 Matcher design

The matcher operates on lowered Halide IR at a single, well-defined point
in `lower_impl`: the Stmt returned by
`Internal::lower_to_canonical_form` (defined in `src/Lower.cpp`). This is
the deepest point in lowering at which the Stmt is still "the program
semantics," before user-injected `custom_passes` run and before backend
offloading (Hexagon RPC, GPU offload, parallel-task lowering, closure
generation).

The exact prefix is the sequence of passes inside that function, in order
(some are gated on `Target` features and run only when those features are
on; both spec-lowering and use-site-lowering see the same gating because
they share a Target):

1. **Loop-nest construction:** `schedule_functions`, then optionally
   `inject_memoization`, then `inject_tracing` and `add_parameter_checks`.
2. **Bounds inference and infrastructure:** `compute_function_value_bounds`,
   `clamp_unsafe_accesses`, `bounds_inference`, `add_split_factor_checks`,
   `remove_extern_loops`, `sliding_window`, `uniquify_variable_names`,
   first `simplify`, `simplify_correlated_differences`,
   `allocation_bounds_inference`, `add_image_checks`, `remove_undef`,
   `storage_folding`, `debug_to_file`, `inject_prefetch`,
   `lower_safe_promises`, `skip_stages`, `fork_async_producers`,
   `split_tuples`.
3. **GPU var canonicalization** (if `Target::has_gpu_feature()`):
   `canonicalize_gpu_vars`, followed by a `simplify_correlated_differences`
   + `bound_small_allocations` pair.
4. **Storage flattening and host/device staging:** `storage_flattening`,
   `add_atomic_mutex`, `unpack_buffers`, optionally
   `rewrite_memoized_allocations`, and (if host↔device copies are
   needed) `select_gpu_api` + `inject_host_dev_buffer_copies` + a
   second `select_gpu_api`.
5. **Second simplification block:** `simplify` + `unify_duplicate_lets`,
   `reduce_prefetch_dimension`, `simplify_correlated_differences`,
   `bound_constant_extent_loops`.
6. **Loop transformations:** `unroll_loops`, `vectorize_loops` +
   `simplify`, optionally `fuse_gpu_thread_loops`, `rewrite_interleavings`
   + `simplify`, `partition_loops` + `simplify`, `stage_strided_loads`,
   `trim_no_ops`, `rebase_loops_to_zero`, `hoist_loop_invariant_if_statements`,
   `inject_early_frees`, optionally `fuzz_float_stores`.
7. **Final normalization:** `simplify_correlated_differences`,
   `bound_small_allocations`, optionally `inject_profiling` and
   `lower_warp_shuffles`, `common_subexpression_elimination`,
   `lower_unsafe_promises`.
8. **Intrinsic lifting:** optionally `extract_tile_operations` (AMX, gated on
   `Target::AVX512_SapphireRapids`), `flatten_nested_ramps`,
   `remove_dead_allocations` + `simplify` + `hoist_loop_invariant_values` +
   `hoist_loop_invariant_if_statements`, **`find_intrinsics`**,
   `hoist_prefetches`, optionally `strip_asserts` (gated on `Target::NoAsserts`).

The spec pipeline is lowered through the *same* `lower_to_canonical_form`
function, so the spec and use-site IRs are produced by identical pass
sequences modulo Target-conditional gates. (Recall that the spec is a real
Pipeline; it can be lowered just like any other Pipeline. The only
difference is that the lowering output is used as a match pattern rather
than codegen'd.) FindIntrinsics runs *inside* the prefix, so the matcher
sees patterns already lifted to Halide intrinsic `Call` nodes. AMX tile
extraction also runs inside, so AMX-style instruction declarations match
post-lifting. HVX MAC patterns recognized by FindIntrinsics likewise match
in their lifted form.

**Stability.** v1 makes no stability promise about the prefix. The single
source of truth is `lower_to_canonical_form` in `src/Lower.cpp` — changes
to that function change canonical form and constitute breaking changes
for in-tree instruction declarations. Out-of-tree instruction libraries
are not yet a target use case; revisit prefix stability (e.g. via per-
instruction prefix versioning) in v1.5+. OQ#5 captures the rationale.

Matching is then structural with the following allowances:

- alpha-equivalence on loop and `Let`-bound variables,
- commutativity of `+`, `*`, `min`, `max`,
- `Simplify`-equivalence of integer expressions (reusing `IRMatcher`
  infrastructure from `src/IRMatch.h`),
- canonical handling of `ramp`/`broadcast` vs. `Load`-with-affine-index at
  vector widths declared in the spec.

**Multi-output matching.** When the spec has multiple outputs, the matcher
performs joint matching at the named loop level: it identifies all the user
Funcs being computed at that loop level (those co-computed via
`compute_with`, or all components of a Tuple-valued Func) and matches them
collectively against the spec's output Funcs. Loop structure must match
jointly; each output's body must match the corresponding spec output's body.

**Match parameter binding (v1.5).** Match parameters are bound by solving
the affine equation system that results from equating spec-side bounds
expressions against use-site bounds expressions. The system is linear with
integer coefficients and is solvable in closed form. Failure to solve
(over- or under-determined system, non-integer solutions) is a match failure.

### 4.5 Integration with `define_extern`

When the emit callback produces a `Call::Extern`-shaped Stmt (the BLAS case,
or any instruction backed by an external library), lowering reuses the
existing `define_extern` backend machinery:

- the call is emitted as an extern call with the standard ABI,
- bounds query support is **auto-generated** from the spec pipeline's
  schedule (the same scheduling directives that act as the contract also
  describe what input region the call needs given a requested output
  region — this is exactly what `define_extern`'s bounds query expresses,
  just derived rather than hand-written),
- scheduling restrictions inherent to opaque stages are inherited
  automatically.

`define_extern` remains as a separate, algorithm-level feature for stages
that are *fundamentally* opaque. `implement_with` is the schedule-level
feature for stages with a pure Halide definition that admits substitution.

### 4.6 Error model

All errors fire as early as possible:

- **Target feature missing** → schedule application time, references the
  directive and the missing feature.
- **Constraint conflict** (e.g., schedule tiles by 8 but spec requires
  `bound(i, 0, 16)`) → schedule application time or bounds inference time,
  references both the directive and the conflicting user schedule call.
- **Bounds infeasible across pipeline** (e.g., a producer's stride is
  incompatible with the spec's required input layout) → bounds inference
  time, with the standard infeasibility diagnostic enhanced to mention the
  directive.
- **Loop structure mismatch** (user's loop nest at the matched region does
  not align with the spec's loop nest) → lowering time, with diagnostic
  output.
- **Structural body mismatch** → lowering time, with a printout of the
  canonical spec form and the canonical use-site form, and (ideally) a diff.
- **Match parameter solve failure** (v1.5) → lowering time, identifying
  which parameter could not be uniquely bound.

### 4.7 Spec-pattern Funcs and Pipelines

Spec-pattern Funcs are a new internal mode for Halide `Func`. They have the
following properties:

- They are never lowered to executable code; they exist solely to define
  match patterns and carry contract-bearing schedules.
- It is an error to `realize`, `compile_jit`, `compile_to_static_library`,
  or otherwise materialize a spec-pattern Func or a `Pipeline` containing
  them.
- Most scheduling directives apply normally and are interpreted as
  contractual constraints: `bound`, `align_storage`, `align_bounds`,
  `vectorize` (as a width requirement, not a lowering action), `unroll`
  (as an unroll-factor requirement on the matched loop), `reorder`,
  `reorder_storage`, stride directives.
- `compute_with` between two spec Funcs is a *requirement* that the
  matched user Funcs are also co-computed at the matched loop level. It is
  not literally applied to the spec (which is never lowered).
- `compute_at`, `store_at`, `compute_root`, and other directives that
  position a Func within a larger pipeline are not meaningful in spec
  context and are errors.
- Spec pipelines are produced by `Instruction::Builder::spec`. Their Funcs
  are implicitly marked as spec-pattern when the spec thunk returns.

This is a small new mode; most of `Func`'s API is shared. The mode is
checked at the operations that would materialize the Func, not threaded
through every directive.

## 5. Scope: What's In v1, What's Deferred

### v1 (initial implementation)
- Instruction declaration API with `Pipeline`-returning specs
- Constant bounds only (no match parameters)
- Single-output and multi-output instructions (Tuple-valued and multi-Func)
- Strict mode only
- Structural matcher with alpha-equivalence, commutativity,
  Simplify-equivalence, joint multi-output matching
- Spec-pattern Func / Pipeline mode
- Schedule-transfer from spec Funcs to matched user Funcs
- Target feature checking
- Auto-generated bounds queries when emit produces extern calls
- Diagnostic printouts on match failure

### v1.5 (follow-on)
- Affine match parameters
- BLAS-style substitutions with parameterized shape
- Parameterized emit (emit branches on bound parameter values)

### v2 and beyond
- Soft mode (fallback on match failure)
- Type-polymorphic specs (one declaration covers multiple vector widths /
  element types)
- Verification: JIT both spec and emit at concrete shape, check equivalence
  on sample inputs
- Autoscheduler integration: surface instructions as queryable scheduling
  primitives
- Ambient-mode requirements (ARM SME streaming mode and similar)
- Bilinear / non-affine bounds (probably never — escape hatches via derived
  parameters)

## 6. Non-Goals

- Replacing `FindIntrinsics.cpp`. The existing implicit recognition stays;
  the new feature is opt-in and explicit. (A long-term project to
  reimplement `FindIntrinsics` as a library of `implement_with`
  declarations is plausible but out of scope.)
- Autoscheduler-first design. The feature must be fully usable without an
  autoscheduler; autoscheduler-readiness is a property of the data model,
  not a driving requirement.
- Replacing `define_extern`. The two coexist; `implement_with` reuses
  `define_extern`'s backend where useful.
- A separate constraint vocabulary. All constraints are expressed via
  Halide scheduling directives on the spec pipeline.
- A dependency on external SMT solvers (Z3 or otherwise). The matcher
  relies on Halide's existing `Simplify`, `IRMatcher`, and `Solve` modules,
  plus structural equivalence on canonical-form IR. Z3 would be far too
  heavyweight a dependency for Halide, and an SMT-backed matcher trades
  diagnostic quality for completeness in a way that is not worth it for
  this feature. Equivalences the matcher *should* handle but cannot are
  treated as bugs or feature requests in `Simplify`/`Solve` — fixes there
  benefit the rest of Halide. The affine match parameter case in v1.5
  reduces to closed-form linear algebra and does not require SMT.

## 7. Open Questions

1. ~~**Where exactly does the target-feature check fire?**~~ **Resolved
   (session 3, Phase 3):** lowering time, inside
   `Internal::apply_implement_with_directives` (called from `lower_impl`
   after `lock_loop_levels`, before `wrap_func_calls`). The
   opportunistic early-warning call-time check from the original
   proposal is **not** implemented in v1; revisit if generator workflows
   surface noisy late-error reports. See
   [DECISIONS.md](DECISIONS.md#schedule-transfer-hook-point).

2. ~~**How are spec inputs/outputs named?**~~ **Resolved (session 1, Phase 1):**
   spec authors must explicitly name spec-pattern Funcs via the
   `Func("name")` constructor — e.g. `Func a("a"), out("out");`. The
   Builder API documents this; the matcher (Phase 4) and `MatchContext`
   (Phase 3+) will key off these names. No macro magic; matches Halide
   convention. See [DECISIONS.md](DECISIONS.md#oq2-spec-func-naming).

3. **Multi-output directive placement.** Proposal in §4.3 is to apply
   `implement_with` to one Func with co-outputs listed. Alternatives: apply
   it once per output (with consistency checking), or lift it to a
   Pipeline-level scheduling directive (which would require introducing
   Pipeline-level scheduling, which Halide does not currently have).
   Proposal: primary-Func-with-co-outputs is least invasive; revisit if a
   Pipeline-level scheduling surface emerges for other reasons.

4. ~~**Spec-pattern Func mode plumbing.**~~ **Resolved (session 2, Phase 2):**
   `bool is_spec_pattern` in `FunctionContents`; `Pipeline::compile_to_module`
   guards against materializing spec-pattern pipelines; `Instruction::spec()`
   marks output Funcs after the thunk returns; `FuncRef::operator Expr()` auto-
   stubs undefined input Funcs inside a spec thunk. See
   [DECISIONS.md](DECISIONS.md#auto-stub-for-undefined-spec-input-funcs).

5. ~~**What is the precise canonicalization prefix?**~~ **Resolved
   (session 4, pre-Phase-4):** the prefix is the body of
   `Internal::lower_to_canonical_form` in `src/Lower.cpp` — every
   Stmt-transforming pass from `schedule_functions` through
   `strip_asserts`, with `find_intrinsics` and (gated)
   `extract_tile_operations` *inside* the prefix so that HVX MAC and AMX
   tile patterns match in their lifted form. The cut is *before* user-
   injected `custom_passes` and *before* backend offloading
   (Hexagon RPC, GPU offload, parallel-task lowering). v1 makes no
   stability promise — prefix changes are breaking changes; revisit in
   v1.5+ when out-of-tree instruction libraries become a target use
   case. See §4.4 above and
   [DECISIONS.md](DECISIONS.md#oq5-canonicalization-prefix).

6. **Diagnostics on match failure.** The "diff between canonical spec and
   canonical use-site form" idea is appealing but the implementation is
   non-trivial. v1 may need to settle for a side-by-side printout. Worth
   scoping carefully — this is the primary debugging tool for users of the
   feature and its quality determines whether the feature is usable in
   practice.

7. **How should instruction libraries be discovered and linked?** External
   libraries declare `Instruction` objects in C++. The natural pattern is
   for such libraries to be header-only or linked statically. Should there
   be a registry / discovery mechanism, or is "the user includes the right
   header" sufficient? Proposal: header-inclusion is sufficient for v1.

8. **Spec-pipeline scheduling directives: which are legal, which are
   errors, which are reinterpreted?** §4.7 lists the obvious cases but a
   complete enumeration is needed before implementation. Some directives
   (`prefetch`, `hexagon`, `gpu_*`) have non-obvious semantics in spec
   context and need explicit decisions.

## 8. Implementation Plan

### 8.1 Development process notes

**Sequential dependencies.** The phases below are sequential dependencies, not
parallelizable workstreams. Phase 2 (spec-pattern Funcs) and Phase 3 (schedule
transfer) together form the load-bearing piece of the design — once those work,
the rest is more conventional engineering.

**Get to end-to-end early.** Before starting Phase 4's matcher work in earnest,
push for a working end-to-end on a trivial single-intrinsic example (e.g.,
`vfmadd231ps_256` from §3.3), even if the matcher is just a stub that always
succeeds. This gives a testable harness to iterate against and surfaces
integration issues before they compound. The matcher is the most complex piece
of the implementation; do not let it be the first piece that has to integrate
with everything else.

**Open questions to resolve before each phase.**
- Before Phase 1 completes: #2 (spec-Func naming) and #4 (spec-pattern Func
  mode plumbing). Both affect the API shape.
- Before Phase 4 starts: #5 (canonicalization prefix). The matcher needs a
  stable definition of canonical form.
- Deferrable: #8 (spec-context directive legality). Pick a minimal legal set
  for v1 and expand as test cases reveal needs.

**PR structure.** This is an upstream Halide feature, not a fork. Each phase
should be a separate PR for reviewability. Within a phase, prefer multiple
small PRs over one large PR. Phase 2 and Phase 4 are the phases most likely
to need splitting (spec-pattern Func mode touches `Function::Contents`; the
matcher is substantial enough on its own).

**Watch for structural impact on existing Halide internals.** Flag any place
where implementing this requires non-trivial structural changes to:
- `Function::Contents` or `Schedule` (Phase 2 will touch these),
- the lowering pipeline (Phase 4 will need a stable hook point after the
  canonicalization prefix),
- `IRMatch.h` (Phase 4 will likely need extensions for the joint multi-output
  case and for alpha-equivalence over loop variables),
- bounds-inference (Phase 3 will need to ensure transferred constraints
  participate cleanly).

Structural changes in these areas are the most likely source of unanticipated
design issues. Surface them in PR descriptions rather than papering over them;
the design may need revision in light of what the existing code can
accommodate.

**Tests.** Each phase should land with tests, not deferred to a later phase.
The end-to-end test from "get to end-to-end early" should be maintained as a
regression test throughout — if any subsequent phase breaks it, that phase has
a problem.

### 8.2 Phases

**Phase 1 — Infrastructure**
1. `Instruction` and `Instruction::Builder` classes, no semantics yet.
2. `MatchContext` class, no binding logic yet.
3. `Func::implement_with` directive (single-output and multi-output
   overloads), recorded in the schedule but inert.

**Phase 2 — Spec-pattern Funcs**
4. Add spec-pattern mode flag to `Func`/`Function`.
5. Implement errors at materialization sites (`realize`, `compile_*`).
6. Implement Builder logic to mark Funcs in a spec pipeline as spec-pattern
   when the spec thunk returns.
7. Tests: spec pipelines cannot be realized; spec-pattern Funcs accept
   scheduling directives without error.

**Phase 3 — Schedule transfer and constraint installation**
8. At `implement_with` schedule application time: identify corresponding
   user Funcs for each spec Func (by name, with co-outputs explicitly
   listed for multi-output).
9. Transfer scheduling directives from spec Funcs to user Funcs.
10. Target-feature checking.
11. Tests: schedules that violate transferred constraints error cleanly
    with helpful diagnostics.

**Phase 4 — Matcher**
12. Canonicalization pass: lower both spec pipeline and use-site region to
    canonical form via the §4.4 prefix.
13. Structural matcher with alpha-equivalence + commutativity, reusing
    `IRMatcher`.
14. Joint multi-output matching.
15. Tests cover four case studies, each exercising a distinct property of
    the canonical-form prefix:
    - `vfmadd231ps_256` (§3.3) — single intrinsic, post-FindIntrinsics
      lifting.
    - SDOT 4×4 GEMV on ARM (§3.4) — multi-instruction emit; joint
      matching of four broadcasting SDOTs.
    - HVX MAC sequences and/or AMX tile triples — validates that
      `find_intrinsics` and (gated) `extract_tile_operations` inside the
      prefix produce match-friendly canonical IR.
    - PTX MMA on CUDA (NVPTX tensor cores) — validates LLVM-backed GPU
      matching against pre-offload `For::GPUBlock`/`For::GPUThread`
      structure (canonical form is cut *before* `inject_gpu_offload`).

**Phase 5 — Emit and lowering integration**
16. Emit callback invocation and Stmt substitution.
17. Integration with `define_extern` backend for extern-call emits.
18. Auto-generated bounds queries derived from spec schedule.
19. Tests: multi-instruction emits (SDOT 4×4), extern-call emit
    (fixed-shape BLAS without parameters).

**Phase 6 — Diagnostics**
20. Match-failure diagnostic with canonical forms printed.
21. Loop-structure-mismatch diagnostic.
22. Tests covering each error class in §4.6.

**Phase 7 (v1.5) — Affine parameters**
23. `Instruction::params` API and `MatchContext::param`.
24. Affine constraint solver for parameter binding.
25. Schedule transfer extended to affine-parameterized bounds.
26. Tests: parameterized BLAS, parameterized reductions.

## 9. References

- Exo's `replace` directive: see Ikarashi et al., "Exocompilation for
  Productive Programming of Hardware Accelerators," PLDI 2022.
- Halide bounds inference: see `src/BoundsInference.cpp`, `src/Func.cpp`.
- Existing intrinsic recognition: see `src/FindIntrinsics.cpp`.
- `define_extern`: see `src/Func.cpp` and related apps.
- `IRMatcher` and Simplify pattern matching: see `src/IRMatch.h`,
  `src/Simplify_*.cpp`.
- `compute_with` semantics (relevant to multi-output instructions): see
  `src/ScheduleFunctions.cpp` and `Func::compute_with` documentation.

## 10. Companion documents

- [`IMPLEMENTATION_STATUS.md`](IMPLEMENTATION_STATUS.md) — current phase,
  what's landed, what's blocked. Updated at the end of every session.
- [`DECISIONS.md`](DECISIONS.md) — resolved open questions and design
  decisions made under uncertainty, with reasoning. The authoritative
  record of *why* the implementation looks the way it does.

## 11. Changelog

Format: `YYYY-MM-DD (session N)` — short summary of what changed.

- **2026-05-25 (session 8, Phase 4 remaining case studies):** All
  three remaining case studies from the Phase 4 plan land as
  test-only additions to `implement_with_phase4.cpp` (no source
  changes — sessions 5-7's matcher infrastructure covers them):
  (1) `test_case_study_sdot_gemv_4x4` (DESIGN.md §3.4) — single-
  output reduction form of the four-broadcasting-SDOTs pattern;
  two-stage `out` with an RDom k(0, 4) reducing `cast<int32>(A(i,
  k)) * cast<int32>(b(k))`. Matched at the update-stage's i-loop.
  (2) `test_case_study_hvx_mac_widening` (HVX target) — validates
  that `find_intrinsics` runs consistently inside the canonical-
  form prefix; both spec and user author the un-lifted Cast/Mul
  form, both get lifted to `widening_mul` + `widen_right_add`,
  matcher succeeds against the lifted form. (3)
  `test_case_study_ptx_gpu_mac` (host-cuda target) — validates
  matcher traversal of `ForType::GPUBlock` / `ForType::GPUThread`
  loops; canonical form is taken before `inject_gpu_offload`, so
  spec and user share GPU loop structure. Three documented
  authoring warts uncovered: direct FuncRef-to-FuncRef
  assignment of an undefined spec input bypasses auto-stub
  (workaround: `Expr c_val = c(i)`); auto-stubbed Funcs name
  their pure args from the call site, so `bound(_1, ...)`
  doesn't resolve; `gpu_tile` names post-tile block loops as
  `<func>.s1.<orig>.<orig>.block_id_<dim>`.

- **2026-05-25 (session 7, Phase 4 wildcards + Simplify + case
  study):** Three matcher-completion pieces landed plus the first
  Phase 4 case study. (1) Spec-input Func wildcards are now
  observable: `FuncRef::operator Expr()` schedules auto-stubbed
  spec input Funcs `compute_root()` so their bodies survive the
  canonical-form prefix as Realize / Allocate / Load chains,
  letting the matcher's `func_rename` bind the spec input names
  to user-side buffer / Func names. (2) `match_expr` now has a
  Simplify-equivalence fallback for scalar/vector integer Exprs:
  on structural-recursion failure, the matcher substitutes
  current `var_rename` bindings into the spec Expr and checks
  whether `simplify(spec_substituted - user)` is constant zero,
  so algebraically equal but lexically distinct indices match
  (e.g. `(i + 4) - 2` vs `j + 2` once `i -> j` is bound).
  (3) New public entry point
  `Internal::lower_pipeline_to_canonical_form(p, target)` mirrors
  `lower_spec_to_canonical_form` minus the spec-pattern
  assertion, enabling case studies that lower real user pipelines
  through the same prefix. (4) The vfmadd231ps_256 case study
  (`test_case_study_vfmadd231ps_256`) lowers both the §3.3 spec
  and a realistic Halide user pipeline (ImageParam inputs with
  mins pinned to 0), locates inner Fors, matches, and asserts
  For-name + Allocate-name + output-name bindings. Three case
  studies remain (SDOT, HVX/AMX, PTX MMA).

- **2026-05-25 (session 6, Phase 4 matcher):** Structural matcher
  for canonical-form IR landed. New `Internal::MatchResult`
  struct and `Internal::match_canonical_form(spec_loop, user_loop)`
  entry point in `src/ImplementWithMatcher.{h,cpp}`. The matcher is
  a parallel walker over paired Stmt/Expr trees with two binding
  maps: `var_rename` for Variables / For loop vars / Let{,Stmt}
  bound names; `func_rename` for buffer / Func / intrinsic names
  in Load, Store, Call, Provide, Realize, Allocate, Free,
  ProducerConsumer, Atomic, Prefetch, HoistedStorage. Commutative
  ops (Add, Mul, Min, Max) try both child orderings with
  snapshot-and-restore on the binding maps. Types, opcode kinds,
  Call::call_type, For::for_type/device_api/partition_policy,
  Allocate::memory_type/padding, Realize::memory_type,
  VectorReduce::op, and Shuffle::indices must match exactly. The
  matcher does not yet call `Simplify` on integer subexpressions,
  so lexically distinct but algebraically equal indices outside
  a commutative-op context will fail to match — that's a planned
  follow-up. Four new sub-tests in
  `test/correctness/implement_with_phase4.cpp` cover the
  identity-match, commutativity (handmade IR), op-mismatch
  failure, and the two-lowered-specs alpha-rename case.

  Known limitation: spec-pattern input Funcs auto-inline to their
  `0.0f` stub bodies during canonical-form lowering, so the
  matcher's `func_rename` wildcard binding for spec inputs
  isn't truly exercised yet — the body simplifies to a constant
  Store before any Load survives to bind. Resolving this is the
  next handoff item (see `IMPLEMENTATION_STATUS.md`).

- **2026-05-25 (session 5, Phase 4 in-progress):** Phase 4
  prerequisites landed: spec-pipeline lowering entry point and
  region locator. `Internal::lower_to_canonical_form` exposed via
  `src/Lower.h` (moved out of anonymous namespace; internal
  `LoweringLogger` parameter dropped). New file pair
  `src/ImplementWithMatcher.{h,cpp}` defines
  `Internal::lower_spec_to_canonical_form(spec, target)` — the
  spec-side counterpart of `lower_impl`'s pre-canonical-form
  prefix, omitting only `apply_implement_with_directives` (specs
  do not themselves carry instructions) — and
  `Internal::find_implement_with_loop(stmt, func, stage, var)`
  which locates a stage-qualified `For` node in canonical IR.
  Together these are the two matcher prerequisites: spec and
  use-site both reduce to canonical-form Stmt that the structural
  matcher (next session) will compare. New
  `test/correctness/implement_with_phase4.cpp` covers both with
  four sub-tests.

- **2026-05-25 (session 5, Phase 4 kick-off):** Phase 4 case-study
  list expanded to four entries. PTX MMA (NVPTX tensor cores) added
  as the LLVM-backed-GPU validation case — closes the
  "LLVM-backed backends first" line from OQ#5's analysis by
  exercising matching against pre-`inject_gpu_offload`
  `For::GPUBlock`/`For::GPUThread` loop structure. §8.2 Phase 4 and
  [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) updated to
  list the four case studies and what each one specifically validates.
  No source-code changes in this doc commit. See
  [DECISIONS.md](DECISIONS.md#phase-4-case-study-set).

- **2026-05-25 (session 4, pre-Phase-4):** OQ#5 (canonicalization
  prefix) resolved. New function
  `Internal::lower_to_canonical_form(outputs, env, order, fused_groups,
  target, requirements, pipeline_name, trace_pipeline, log)` in
  `src/Lower.cpp` owns the prefix; `lower_impl` calls it. The function
  is a no-behavior-change extraction of the existing pass sequence
  from `schedule_functions` through `strip_asserts`, cutting *before*
  user `custom_passes` and *before* backend offloading. Both the user
  pipeline and (in Phase 4+) the spec pipeline will be lowered through
  this single function so structural matching sees identical canonical
  IR on both sides. §4.4 rewritten to spell out the prefix in order.
  No stability promise in v1; in-tree intrinsic catalogs stay in sync
  by construction. See
  [DECISIONS.md](DECISIONS.md#oq5-canonicalization-prefix).

- **2026-05-25 (session 3, Phase 3):** Schedule transfer +
  constraint-installation pass landed. New file pair
  `src/ApplyImplementWith.{h,cpp}` defines
  `Internal::apply_implement_with_directives`, invoked from `lower_impl`
  after `lock_loop_levels` and before `wrap_func_calls`. The pass:
  errors on missing required Target features; calls `instr.spec()` to
  get a fresh spec Pipeline; identifies user Funcs by name (output =
  the user Func the directive sat on; inputs = same-name Funcs in
  env); and transfers `FuncSchedule::bounds()` with positional
  spec-arg → user-arg rename. Constant-bound conflicts are detected
  inline with a `user_assert` that names both values. The pass works
  on the deep-copied lowering env, so the user's pristine `Function`
  schedule is untouched.

  Scope decisions: single-output and `bounds()`-only — multi-output
  (`co_outputs`, Tuple-valued primaries) and non-bound directives
  (`vectorize`, `align_storage`, `unroll`, `reorder`, etc.) are
  deferred to Phase 4 where the matcher provides the spec-Var →
  user-Var rename map. Positional rename is provisional and replaced
  by the matcher's mapping in Phase 4. See
  [DECISIONS.md](DECISIONS.md#phase-3-scope-bounds-only-single-output)
  and [DECISIONS.md](DECISIONS.md#positional-spec-var--user-var-mapping-in-phase-3).

  Cross-cutting: `Serializer::serialize_stage_schedule` now hard-errors
  on non-empty `implement_with_directives` so the silent-drop hole that
  existed in Phases 1–2 cannot regress; see
  [DECISIONS.md](DECISIONS.md#serialization-hard-error-in-phase-3).
  OQ#1 (target-check location) resolved as lowering-time. The
  call-time early-warning half of the OQ#1 proposal is NOT
  implemented in v1.

- **2026-05-25 (session 2, Phase 2):** Spec-pattern Func mode landed.
  `Function::is_spec_pattern` flag, `Pipeline::compile_to_module` guard,
  `Instruction::spec()` marking, `FuncRef::operator Expr()` auto-stub for
  undefined typed input Funcs. OQ#4 resolved. Design note: the §3.3 example
  uses bare `Func a("a")` for spec inputs; Phase 2 requires
  `Func a(Float(32), 1, "a")` to provide the element type. Type-polymorphic
  specs (bare Func with no required_types) are v2. See
  [DECISIONS.md](DECISIONS.md#spec-input-func-type-declarations).

- **2026-05-25 (session 1, Phase 1):** Initial in-repo copy. Resolved
  OQ#2 (spec-Func naming via `Func("name")`). Renamed Builder method
  `.requires(...)` → `.require(...)` because `requires` is a reserved
  keyword in C++20+ and would prevent user code from compiling at that
  language level. All `.requires(...)` references in the examples
  (§3.3–§3.7) and §4.1 should be read as `.require(...)`; full doc-text
  update deferred to avoid mid-Phase-1 churn — see
  [DECISIONS.md](DECISIONS.md#require-vs-requires).
