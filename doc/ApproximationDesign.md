# Approximation: a core concept for lossy, quantified Func substitution

Status: design draft, no implementation yet.

## Motivation

Two pieces of prior work motivate this:

1. `apps/ggml/halide/*_generators.cpp` hand-implements ~24 GGML quantized weight
   formats as pairs of Halide Generators (quantize, dequantize), each file
   independently encoding its own byte layout, scale/bias math, and (for
   K-quants and a few others) a scaffolded extern-stage call to GGML's own
   reference quantizer pending a native port. This works, but every type
   duplicates the same shape of logic (block layout, scale search, bit-packing)
   by hand, and the "quantize happens once offline, dequantize happens on every
   inference call" relationship between the two directions is enforced by
   nothing but convention and comments.

2. A private Python research prototype explores building the same set of formats
   compositionally instead: a small `Approximation` ABC (`encode`/`decode`, each
   operating on Halide `Func`s) with a handful of primitives (block layout
   reshaping, a linear integer quantizer, a shift-by-min helper for affine
   schemes, bit-packers) that compose to reconstruct all of Q2_K..Q8_K. It's
   Python-only, JIT-only, and only covers quantize/dequantize round trips (no
   vec_dot, no repack, no scheduling story).

This document proposes promoting the core of that prototype (`Approximation`)
into a first-class Halide C++ concept, plus two small pieces of surrounding API
(`approximate_by`, `compute_offline`) needed to wire an `Approximation` into a
real pipeline's call graph and, optionally, split it across a compile-time
boundary.

## Why this can't just be ordinary scheduling

Halide's algorithm/schedule separation depends on schedule directives being
meaning-preserving: `.compute_root()` vs `.compute_at()` never changes what a
pipeline computes, only how. An `Approximation` is the opposite by design — it
deliberately changes the *value* computed (a real weight becomes a
quantized-then-dequantized approximation of itself), in a bounded, quantified
way. Wiring that in by disguising it as an ordinary Func substitution (e.g. a
custom `.in()` wrapper with no other marking) would make a semantics-changing
operation look, to any future reader, like a semantics-preserving one. It needs
its own footing in the API.

## Core concept: `Approximation`

```cpp
// encode()/decode() each return (funcs, handles): the "public" Func(s) that
// participate in the signature contract, plus extra intermediate Funcs
// (reduction accumulators, per-block stats, etc.) that have no meaning
// outside scheduling but still need someone to schedule them -- see
// "approximate_by" below for why silently dropping handles is a real bug,
// not a simplification.
struct EncodeResult {
    std::vector<Func> encoded;  // the signature-contract output(s)
    std::vector<Func> handles;  // scheduling-only, no semantic meaning
};
struct DecodeResult {
    std::vector<Func> decoded;  // decoded[0] is the round-trip replacement
    std::vector<Func> handles;
};

class Approximation {
public:
    virtual ~Approximation() = default;

    // Both operate purely on Funcs -- no opinion about placement (compute
    // root vs fused, offline vs online). See "Scope: placement is not
    // semantics" below for why that split matters.
    virtual EncodeResult encode(Func f) = 0;
    virtual DecodeResult decode(std::vector<Func> encoded) = 0;
};
```

Virtual dispatch is used specifically because composed Approximations
(`Compose`/`Apply`, below) need to hold a runtime-heterogeneous list of
`Approximation` references and call `encode`/`decode` on each polymorphically.
(A templated/CRTP alternative was considered and rejected for this reason: it
would make composed, heterogeneous chains require type erasure some other way,
for no benefit here.)

**Signature contract.** `decode(encode(f).encoded).decoded[0]` must reproduce
`f`'s arg list and value type exactly — that's what makes it valid to splice
back into a call graph in place of `f`. This is not proposed to be enforced
generically by the base class in v1; each concrete `Approximation` is
responsible for it, treating round-trip error and convergence as a testable
property rather than a type-level guarantee. `approximate_by`'s substitution
step (see below) does do a shape/type check at the point of substitution, which
catches violations, just not at `Approximation`-definition time.

**`encode`'s output arity is scheme-dependent, and that's fine.** This follows
directly from a memory-layout choice each `Approximation` makes:

- *Packed*: one opaque `Buffer<uint8_t>`, fields recovered via
  `reinterpret<T>()` at fixed byte offsets inside `decode`. This is what every
  type in `apps/ggml/halide` already does today (`Buffer<uint8_t, 2>` dim 0 =
  byte-within-block), because Halide has no struct type to give a K-quant's
  `(d, dmin, scales[12], qs[128])` a real typed signature.
- *Planar*: multiple typed Funcs (e.g. a separate `float16_t` delta Func, a
  separate `int8_t` quants Func), more Halide-native and type-safe, but the
  Generator's public signature grows with the scheme's field count.

Both are legitimate; `Approximation` doesn't pick one. A Generator wrapping an
`Approximation` (see below) ends up with a scheme-dependent public signature
either way — accepted as a consequence of this choice, not something the
framework tries to paper over.

## `approximate_by`: wiring an `Approximation` into a call graph

### Rejected first approach: building on `Func::in`

`Func::in(g)` (`src/Func.cpp:2456`) looked like a natural fit initially: it
registers a wrapper Function in `f`'s `FuncSchedule::wrappers()` map, which
`WrapCalls.cpp::wrap_func_calls` later rewrites `g`'s `Call` nodes against. The
problem is *later*: that rewrite only happens during `lower()`, well after
`configure()`/`generate()`/`schedule()` have all already run. Anything that
needs to reason about "what does `g` actually call" before that point — in
particular, `compute_offline`'s `configure()`-time branching — would be looking
at stale, pre-substitution state. Using `.in()` here would make `approximate_by`
and `compute_offline` fundamentally unable to compose in the same pipeline.

### The actual mechanism: eager, destructive, like `rfactor`

`Stage::rfactor` (`src/Func.cpp:1001`) is the right precedent instead. It never
defers to a lowering pass: it builds a new `Func intm(...)`, calls
`intm.function().define_update(...)` immediately, and rewrites the *original*
Function's own definition via `substitute_self_reference` — all synchronously,
as part of the `.rfactor()` call itself. By the time `.rfactor()` returns, the
graph already reflects the change, which is why a caller can immediately turn
around and schedule `intm` in the same breath.

`approximate_by` should behave the same way, using the same class of primitive
`WrapCalls.cpp` already relies on internally —
`Function::substitute_calls(SubstitutionMap)` — but invoked immediately,
directly on an explicitly-given set of consumers, instead of registered for a
later pass:

`approximate_by` is a member of `Func` (`f.approximate_by(p, consumers)`), not a
free function — it's a graph-editing operation on `f` in exactly the same sense
`f.in(...)`/`f.rfactor(...)` are, so it should read like the rest of that family
instead of standing apart as a free function taking `f` as its first argument:

```cpp
struct ApproximationResult {
    Func replacement;           // decode's round-trip output; already
                                 // spliced into every Func in `consumers`
    std::vector<Func> handles;  // encode's output(s) + encode's handles +
                                 // decode's handles -- all need scheduling,
                                 // none are part of the signature contract
};

// Func.h: ApproximationResult approximate_by(Approximation &p, const std::vector<Func> &consumers);
ApproximationResult Func::approximate_by(Approximation &p, const std::vector<Func> &consumers) {
    EncodeResult enc = p.encode(*this);
    DecodeResult dec = p.decode(enc.encoded);
    Func round_trip = dec.decoded[0];  // signature contract: matches *this exactly

    for (const Func &g : consumers) {
        // eager, destructive -- happens now, not at lowering time
        g.function().substitute_calls(func, round_trip.function());
    }

    std::vector<Func> handles = enc.encoded;
    handles.insert(handles.end(), enc.handles.begin(), enc.handles.end());
    handles.insert(handles.end(), dec.handles.begin(), dec.handles.end());
    return {round_trip, handles};
}
```

(`func` is `Func`'s own private `Internal::Function` member; `g.function()` is
the public accessor for a different `Func`'s. `Function::substitute_calls`
already has a single-pair overload taking `(orig, substitute)` directly, so no
manual `SubstitutionMap` construction is needed.)

This needs **no new public primitive at all**, which is a smaller change than
either of the two things earlier drafts of this design proposed (a `Func::in`
overload, or a public wrapper around `substitute_calls`):
`Function::substitute_calls` is already an ordinary method on the internal
`Function` class, and `approximate_by` is implemented inside libHalide itself
(`Approximation.h`/`Func.cpp`), so it can call it directly — the "new public
entry point" concern only would have applied if this were being built as
external, non-core code.

**Returning `handles` is not optional.** Both `encode` and `decode` can
introduce intermediate Funcs with update definitions (per-block reductions, a
shift-by-min helper's own min-reduction, etc.). Left unscheduled, Halide doesn't
error on these — it computes them at the innermost valid loop level by default
(a Func can only fail to compile this way if something explicitly forces
`.compute_inline()` on it, which is illegal for a Func with an update
definition). But that default placement is exactly that, a default: the caller
has no way to override it, or to apply the fusion patterns from "Scope:
placement is not semantics" below (e.g. `compute_at`-ing `enc.encoded` into a
producer for dynamic activation requantization). The struct above bundles every
encode/decode handle together for exactly this reason — so the caller can
schedule all of them, not just the primary output.

### Consequence: consumers must already exist

Because the substitution is eager, `approximate_by` can only rewrite Funcs that
are already built at the point of the call — there is no equivalent of `.in()`'s
global mode (redirect *every* current and future consumer, including ones not
yet written). This is a real capability loss relative to `.in()`, but it's the
same scoping `rfactor` already lives with (it never retroactively touches
definitions that don't exist yet either), and it matches how Generator code is
actually written: within `generate()`, `f` and its consumer(s) are typically
built in the same breath, so passing `consumers` explicitly costs nothing there.
It only forecloses a genuinely different use case — transparently intercepting
calls inside some large, opaque, externally-authored algorithm without being
able to enumerate its call sites — which is out of scope for this design.

## Scope: placement is not semantics

An `Approximation`'s `encode`/`decode` never make any claim about *where* or
*when* they're computed relative to the rest of the pipeline. This was initially
proposed otherwise — an early draft of this design suggested that `encode`'s
output could just always be treated as "the offline half" — and was rejected on
a concrete counterexample: **dynamic activation requantization**.

Contrast:

- **Static weight quantization**: `encode` (e.g. Q4_0 quantize) runs exactly
  once, ever, fully decoupled from any inference call — a genuine
  compile-time/compilation-unit boundary. `decode` gets fused inline into the
  consumer's inner loop — already exactly what every `*VecDotGenerator` in
  `apps/ggml/halide` does (e.g. `ggml_halide::q8_0_value(y_blocks_, r)` called
  inline inside `sum()`, never materialized as its own Func).
- **Dynamic activation requantization**: `encode` (quantize a just-computed
  activation tile) needs to be fused into the *producer's* schedule — same
  granularity, same loop nest, no separate storage, recomputed every call.
  `decode` is fused into the consumer's tiles exactly as before.

Same `Approximation`, opposite treatment of where `encode` is computed. If
"encode ⇒ offline" were baked into the interface, the activation case would need
an escape hatch to override it — at which point the shortcut has bought nothing,
and worse, it would invite tooling to assume every quantize step is safe to
hoist to conversion time, which is a real correctness trap for anything computed
at inference time.

**Consequence, and a scope reduction**: the "fuse encode into producer" / "fuse
decode into consumer tiles" cases need *no new Halide feature at all*. Ordinary
`.compute_at()` / `.compute_inline()` on `ApproximationResult`'s `replacement`
and `handles` (in particular `enc.encoded`, the piece that needs to be fused
into the producer for the activation case) already achieves this, since they're
just regular Funcs sitting in the call graph. `compute_offline` (below) is
needed only for the strictly narrower case of actually severing the graph into
two separately-compiled artifacts — the static-weight case.

## `compute_offline`: v1 scope

**Decision: "seam exposure," not automatic pipeline splitting.** Given one Func
`f` that should become a compile-time boundary, `compute_offline`-style support
means:

- `f`'s computation exists in one compile, as a normal `Output` of a Generator
  (the "offline" artifact).
- A same-shaped `Input` (e.g. an opaque `Buffer<uint8_t>` for a packed layout)
  exists in another compile, standing in for `f` (the "online" pipeline).
- Both are ordinary, statically-declared Generator I/O — no dynamic discovery of
  new ports mid-`generate()`.

The alternative (true automatic splitting: one Generator definition, two
artifacts emitted automatically, no extra static I/O declared by the author) was
considered and rejected for v1: it would require a Generator to discover an
extra Input/Output *during* `generate()`, based on the structure of a Func graph
that doesn't exist yet when `configure()` runs and declares I/O — a
phase-ordering problem, not just an ergonomics one.

### Why this doesn't need new Generator machinery

Generators already support dynamic I/O declared *before* `generate()` runs:
`configure()` (`src/Generator.h:192`) exists specifically so `add_input<>()`/
`add_output<>()` (`src/Generator.h:3236` ff.) can be called based on
`GeneratorParam` values decided earlier. The only in-repo precedent
(`apps/hannk/halide/conv_generator.cpp:75-81`) uses `configure()` narrowly (to
pick a `Target`-dependent filter type), but the mechanism generalizes directly:
`compute_offline`'s "seam" is just another `GeneratorParam`-driven decision
about which ports to declare.

### Composability with `approximate_by`

Now that `approximate_by`'s substitution is eager (see above), this composes
cleanly: whichever order `compute_offline`'s `configure()`-time branching and an
`approximate_by` call run in, within the same `configure()`/`generate()`, the
graph state at every point *is* the true state — no later lowering pass can
silently change what a Func calls out from under code that already ran. In
practice, a Generator authoring an op from scratch (the shape below) usually
doesn't need `approximate_by` at all — it can just build `enc`/`dec` and
reference `dec.decoded[0]` directly wherever the op's math needs the value,
since it's writing the consumer fresh anyway. `approximate_by` earns its keep
when the consumer already exists as written code the author doesn't want to edit
by hand (e.g. a shared reduction reused across several ops) — and because it's
now eager, nothing stops using it *inside* the same `generate()` that also does
`compute_offline`-style branching, in either order.

### Non-goal for v1: provenance checking

Nothing proposed here guarantees that the `Approximation` used to produce the
offline artifact in one compile is *actually* the same one the online compile
expects when decoding it — correctness rests entirely on both sides referencing
the same registry name (see the Generator shape below). Building real guarantees
on top of that (e.g. embedding a scheme fingerprint in the artifact, checked at
load time) is explicitly deferred to a future version; v1 accepts this as a user
obligation, the same way `apps/ggml/halide`'s quantize/dequantize split already
relies on convention today.

## Generator shape

There's very little in-repo precedent for a Generator whose `configure()` does
real work beyond a `Target`/`GeneratorParam` type tweak (`conv_generator.cpp` is
the only example). The proposed shape leans on the existing three-phase
lifecycle (`call_configure()` → `call_generate()` → `call_schedule()`,
`src/Generator.h:3826-3829`) much harder than any existing Generator does:

- **`configure()`** is the real "compiler driver": it looks up `Approximation`
  objects by name from an independent, non-Generator registry
  (`make_approximation(name)`, a lookup table of named schemes), decides which
  Inputs/Outputs to declare based on `GeneratorParam`s (per-argument scheme
  choice, a `quantize_only` mode, a `weight_precomputed` flag selecting between
  the split and combined-artifact shapes), and stores the selected
  `Approximation`s as members.
- **`generate()`** must exist (`static_assert(has_generate_method_v<T>)`,
  `src/Generator.h:3901`) but can be a thin pass-through: it builds the actual
  op math using the `Approximation`s selected in `configure()` (weight value via
  `decode`, activation `encode`d and fused at whatever granularity, the op's own
  reduction) and assigns the result to the `Output<>` members declared in
  `configure()`.
- **`schedule()`** is a thin dispatch reading further `GeneratorParam`s to pick
  a named tiling/fusion strategy, rather than scheduling being written inline in
  `generate()` (the style every existing Generator in this repo uses).

```cpp
class MatMulOp : public Generator<MatMulOp> {
public:
    GeneratorParam<std::string> weight_scheme_{"weight_scheme", "q4_0"};
    GeneratorParam<std::string> activation_scheme_{"activation_scheme", "q8_0"};
    GeneratorParam<std::string> quantize_only_{"quantize_only", ""};  // "" | "weight" | "activation"
    GeneratorParam<bool> weight_precomputed_{"weight_precomputed", true};

    void configure() {
        weight_approx_ = make_approximation(weight_scheme_);
        activation_approx_ = make_approximation(activation_scheme_);

        if (!quantize_only_.value().empty()) {
            x_ = add_input<Buffer<float>>("x", 1);
            EncodeResult enc = (quantize_only_ == "weight" ? weight_approx_ : activation_approx_)->encode(Func(*x_));
            for (size_t i = 0; i < enc.encoded.size(); i++) {
                *add_output<Buffer<void>>("packed_" + std::to_string(i), enc.encoded[i].dimensions()) = enc.encoded[i];
            }
            // enc.handles (e.g. per-block reduction Funcs) still need scheduling here too.
            return;
        }

        if (weight_precomputed_) {
            weight_packed_ = add_input<Buffer<void>>("weight_packed", 2);
        } else {
            weight_fp32_ = add_input<Buffer<float>>("weight_fp32", 2);
        }
        activation_ = add_input<Buffer<float>>("activation", 1);
        result_ = add_output<Buffer<float>>("result", 1);
    }

    void generate() {
        Func weight_value = weight_precomputed_
            ? weight_approx_->decode({Func(*weight_packed_)}).decoded[0]
            : weight_approx_->decode(weight_approx_->encode(Func(*weight_fp32_)).encoded).decoded[0];
        EncodeResult activation_enc = activation_approx_->encode(Func(*activation_));
        // activation_enc.encoded / .handles get fused at whatever granularity
        // schedule() picks -- see "Scope: placement is not semantics" above.
        *result_ = /* the actual matmul reduction, calling weight_value / activation_enc.encoded inline */;
    }

    void schedule() {
        // e.g. pick a named tiling strategy based on another GeneratorParam.
    }

private:
    std::unique_ptr<Approximation> weight_approx_, activation_approx_;
    Input<Buffer<float>> *x_ = nullptr, *weight_fp32_ = nullptr, *activation_ = nullptr;
    Input<Buffer<void>> *weight_packed_ = nullptr;
    Output<Buffer<float>> *result_ = nullptr;
};
```

Known rough edge, accepted for now: `add_input`/`add_output` return raw
pointers, needing member-pointer bookkeeping (`Input<Buffer<float>> *x_`) that
the static `Input<>`/`Output<>` member style used elsewhere in this repo doesn't
need. Revisiting this ergonomics gap is out of scope until the rest of this
design is validated.

## Summary of decisions and open items

| Item                                                                                        | Status                                                                                                       |
| ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Approximation`: virtual-dispatch abstract class, operates on `Func`s only                  | Decided                                                                                                      |
| `Approximation` makes no placement claims (offline vs fused)                                | Decided                                                                                                      |
| `encode`/`decode` return `(funcs, handles)`, not bare `vector<Func>`                        | Decided; handles are scheduling-only intermediates, kept separate from the signature-contract output         |
| `decode(encode(f).encoded).decoded[0]` signature contract                                   | Decided; enforced only at `approximate_by`'s substitution time in v1, not at `Approximation`-definition time |
| `encode`'s output arity/layout (packed vs planar)                                           | Left to each `Approximation`; not decided by the framework                                                   |
| `approximate_by`: eager, destructive `substitute_calls`, not `Func::in`                     | Decided; needed so it composes with `compute_offline` — see rejected-`Func::in` writeup above                |
| `approximate_by` is a `Func` member (`f.approximate_by(p, consumers)`), not a free function | Decided; reads like the rest of the `in()`/`rfactor()`/`clone_in()` graph-editing family                     |
| `approximate_by` requires explicit, already-existing `consumers`                            | Decided trade-off; no more "global" wrapper covering not-yet-written Funcs, same scoping as `rfactor`        |
| `compute_offline`: seam exposure via static, `GeneratorParam`-driven `configure()`-time I/O | Decided for v1                                                                                               |
| `compute_offline`: true automatic pipeline splitting                                        | Rejected for v1 (phase-ordering conflict with `configure()`/`generate()`)                                    |
| `compute_offline`: cross-compile provenance checking                                        | Explicitly deferred to v2; v1 relies on "same registry name"                                                 |
| `compute_offline` + `approximate_by` used together in one pipeline                          | Supported — both are eager/destructive, sequenced by ordinary program order, like `rfactor` + scheduling     |
| Fusing `encode`/`decode` into neighboring stages (activation requantization)                | No new mechanism needed — ordinary `.compute_at()`/`.compute_inline()`                                       |
| Generator I/O ergonomics (`add_input`/`add_output` pointer bookkeeping)                     | Accepted rough edge, deferred                                                                                |

## Prior art referenced

- `apps/ggml/halide/*_generators.cpp` — the manually-duplicated
  quantize/dequantize/vec_dot implementations this design generalizes.
- A private Python research prototype exploring the same compositional
  `Approximation` idea, including its `(funcs, handles)` return convention — not
  a public artifact, referenced here only for context.
- `apps/hannk/halide/conv_generator.cpp` — sole in-repo precedent for a
  non-trivial `configure()`.
- `src/Func.cpp: Stage::rfactor` — the eager, destructive graph-editing
  precedent `approximate_by` follows instead of `.in()`.
- `src/Func.cpp`, `src/Function.cpp`, `src/WrapCalls.cpp` — origin of
  `Function::substitute_calls`, the primitive `approximate_by` calls directly
  and eagerly instead of through the deferred wrapper map.
- `src/Generator.h` — the `configure()`/`generate()`/`schedule()` lifecycle
  `compute_offline` and the Generator shape build on.
