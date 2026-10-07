# Approximation: a core concept for lossy, quantified Func substitution

This is a design document: it explains the concepts and the reasoning behind
them. "Summary of decisions and open items" at the end lists what is still
undecided.

## Motivation

Two pieces of prior work motivate this:

1. An out-of-tree GGML application hand-implements ~24 quantized weight formats
   as pairs of Halide Generators (quantize, dequantize). Each file independently
   encodes its own byte layout, scale/bias math, and (for K-quants and a few
   others) a call to GGML's own reference quantizer. Every type duplicates the
   same shape of logic (block layout, scale search, bit-packing) by hand, and
   the "quantize happens once offline, dequantize happens on every inference
   call" relationship between the two directions is enforced by nothing but
   convention and comments.

2. A private Python research prototype builds the same formats compositionally:
   a small `Approximation` ABC (`encode`/`decode`, each operating on Halide
   `Func`s) with a handful of primitives (block reshaping, a linear integer
   quantizer, a shift-by-min helper, bit-packers) that compose to reconstruct
   the K-quant family. It is Python-only and JIT-only, and covers only
   quantize/dequantize round trips.

This document describes the C++ realization of that idea as a first-class Halide
concept, `Approximation`, plus the surrounding API needed to wire one into a
real pipeline: `Func::approximate_by()`, which splices an approximation's round
trip into an existing call graph; `Pipeline::sever()`, which optionally splits
the result across a compile-time boundary; and
`tools/halide_approximation_testing.h`, which checks what an approximation
claims.

## Why this can't just be ordinary scheduling

Halide's algorithm/schedule separation depends on schedule directives being
meaning-preserving: `.compute_root()` vs `.compute_at()` never changes what a
pipeline computes, only how. An `Approximation` is the opposite by design: it
deliberately changes the *value* computed (a real weight becomes a
quantized-then-dequantized approximation of itself), in a bounded, quantified
way. Wiring that in by disguising it as an ordinary Func substitution (e.g. a
custom `.in()` wrapper with no other marking) would make a semantics-changing
operation look, to any future reader, like a semantics-preserving one. It needs
its own footing in the API.

## Core concept: `Approximation`

An `Approximation` is a value-semantic, type-erased handle (in the style of
`std::function`) to a lossy transformation of one or more Funcs' values:
`decode(encode(f))` approximately reproduces `f`. It operates purely on `Func`s
and makes no claim about *where* or *when* either half is computed (see "Scope:
placement is not semantics").

```cpp
class Approximation {
public:
    Approximation();                                  // undefined
    template<typename T> Approximation(T &&unit);     // duck-typed, implicit
    template<typename T> Approximation(T &&unit, std::string label);

    bool defined() const;
    bool same_as(const Approximation &other) const;
    Approximation labelled(std::string label) const;
    std::string label() const;

    EncodeResult encode(const std::vector<Func> &inputs,
                        const ApproximationPorts &input_ports = {}) const;
    DecodeResult decode(const std::vector<Func> &encoded,
                        const ApproximationPorts &input_ports = {}) const;

    ApproximationSignature signature(const ApproximationPorts &inputs = {}) const;
    Func error_bound(const std::vector<Func> &inputs,
                     const std::vector<Func> &encoded) const;
    bool lossless() const;
    std::string describe(const ApproximationPorts &inputs = {}) const;
};
```

The handle's `encode()`/`decode()` always take and return *vectors* of Funcs,
even though a plain quantizer only uses one. That is what makes the combinators
possible: an inner stage can produce several Funcs (a codes Func and a separate
scale Func), and the next stage needs to consume all of them, or pick one to act
on. There is no base class; the type-erasure is what lets `Compose` and
`Parallel` hold a runtime-heterogeneous list of stages.

**Identity.** A copy of a handle is the *same* stage (`same_as()` is true);
converting a plain unit to an `Approximation` twice makes two *distinct* stages,
even if the units compare equal. Every stage invoked through a handle, including
from inside another unit's `encode`/`decode`, is recorded in the result, so a
caller finds a stage's Funcs by keeping a handle to it and passing copies of
that handle to the combinators (see "Introspection").

**Labels.** Each handle has a label for display: `labelled("x")` (or the
two-argument constructor) sets it, else the unit's `std::string name() const` if
it has one, else the unit's type name with `Halide::` qualifiers stripped
(`Compose`, `LittleEndianScalarPack<unsigned int>`). The label lives in state
shared by all copies of the handle, so `labelled()` affects every copy and
returns a handle that is `same_as` the original, even though the method is
`const`.

### Defining a unit

A unit is any type with const-callable `encode` and `decode` methods. Each
direction independently takes one of three forms:

| Form       | Signature                                                                               |
| ---------- | --------------------------------------------------------------------------------------- |
| single     | `Func encode(const Func &) const`                                                       |
| multi      | `std::vector<Func> encode(const std::vector<Func> &) const`                             |
| port-aware | `std::vector<Func> encode(const std::vector<Func> &, const ApproximationPorts &) const` |

`decode` is the same with `encoded` in place of `inputs`. The forms may be mixed
across directions. A single-form direction handed any other number of Funcs is
an error. If a type offers both a vector and a `Func` overload for one
direction, the vector form is used; the port-aware form is preferred over both.
Types whose return types do not match, or with only one direction, do not
convert. The handle stores a decayed copy of the unit and only calls const
methods, so units needing mutable state must hold it in `mutable` members or
behind a pointer.

A unit only *returns its outputs*. It does not declare or register its
intermediate Funcs: after the unit returns, the framework walks the outputs'
definitions (pure, update, and extern-argument references), stopping at the
stage's inputs, and reports every other Func it reaches in topological order
(producers before consumers, ties broken by name). That includes pure-only
Funcs, and Funcs the unit references from outside (e.g. a shared lookup table);
callers filter. A unit that calls other `Approximation` handles inside its own
`encode`/`decode` (as `Compose` does) needs no extra bookkeeping: those calls
are traced automatically.

The port-aware form additionally receives the ports (names and constraints) of
the encode-side inputs: `encode(inputs, input_ports)` and
`decode(encoded, input_ports)`. It exists so that combinators can route by port
name and forward context to their children; leaf units rarely need it.

A unit may additionally provide any of the following optional members.

- `ApproximationSignature signature() const` (static: the same ports whatever
  the context) *or* `signature(const ApproximationPorts &inputs) const`
  (contextual: given the resolved input ports, empty if unknown, return the full
  signature; used by combinators and shape-polymorphic units).
- `std::string name() const`: the default label.
- `std::vector<Approximation> children() const`, and optionally
  `std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const`
  (the ports each child would receive, parallel to `children()`): the stages
  this unit is built from, used by `describe()` and `check_ranges()`. Only the
  unit knows how it routes ports to children, so this cannot be derived.
- `Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const`:
  a per-element bound on `abs(decode(encode(x)) - x)`, as a Func over
  `inputs[0]`'s arguments.
- `bool lossless() const`: the round trip is exact (a bound of zero).

If a unit provides both signature forms, the contextual one is used. A unit with
only an `error_bound()` is not `lossless()` even when the bound happens to be
zero. Both accuracy declarations are *claims that hold when every input port's
declared range (its precondition) is respected*. They are never enforced at run
time; see "Verification" for how they are checked.

`Approximation::error_bound(inputs, encoded)` returns the unit's bound, else a
zero-valued Func if the unit is `lossless()`, else an undefined Func.

### Worked example

Elementwise units need no struct: `Pointwise{name, encode_fn, decode_fn}` builds
one from a pair of `Expr -> Expr` lambdas (or `std::vector<Expr>` ones for
Tuple-valued Funcs; pass lambdas with concrete parameter types, not `auto`). A
cast or an offset is one inline `Pointwise`; the optional `with_types`,
`with_ranges` and `with_lossless` declare (and let tests check) more than its
arity.

```cpp
// Drops the low bit of an integer: lossy, with error at most 1.
Approximation drop_lsb =
    Pointwise{"drop_lsb",
              [](Expr x) { return x >> 1; },
              [](Expr x) { return x << 1; }}
        .with_error_bound([](Expr) { return Expr(1); });

// Signed 4-bit codes to stored nibbles: exact for codes in [-8, 7].
Approximation offset =
    Pointwise{"offset",
              [](Expr x) { return cast<uint8_t>(x + 8); },
              [](Expr x) { return cast<int8_t>(cast<int>(x) - 8); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-8, 7), ApproximationRange(0, 15))
        .with_lossless();
```

Anything less regular is a small struct. This int8 quantizer with one scale per
block uses the multi form, declares a signature with a range on its codes, and
declares an error bound. Its `amax` reduction is found by the framework; the
unit never mentions it.

```cpp
struct BlockQ8 {
    static constexpr int kBlock = 32;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0];
        Var i("i"), b("b");
        RDom r(0, kBlock, "r");
        Func amax("amax");
        amax(b) = 0.0f;
        amax(b) = max(amax(b), abs(x(b * kBlock + r)));
        Func scale("scale");
        scale(b) = amax(b) / 127.0f;
        Func codes("codes");
        Expr inv = select(scale(i / kBlock) != 0.0f, 1.0f / scale(i / kBlock), 0.0f);
        codes(i) = cast<int8_t>(clamp(round(x(i) * inv), -127, 127));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &enc) const {
        Var i("i");
        Func out("dequantized");
        out(i) = cast<float>(enc[0](i)) * enc[1](i / kBlock);
        return {out};
    }

    // Written in the encode direction: decode consumes `outputs` and
    // produces `inputs`.
    ApproximationSignature signature() const {
        return {{{"values", Float(32), 1}},
                {{"codes", Int(8), 1, ApproximationRange(-127, 127)},
                 {"scale", Float(32), 1}}};
    }

    // Half a step, plus a little slack for float rounding.
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &encoded) const {
        Var i("i");
        Func bound("bound");
        bound(i) = abs(encoded[1](i / kBlock)) * 0.5001f;
        return bound;
    }
};

Approximation q = BlockQ8{};
```

## Composition

### Core units

`Approximation.h` provides the combinators and a few generic leaf units for
layout and bit packing. All are plain structs that convert to `Approximation`,
and all declare signatures. Domain-specific quantizers (block scales, rounding
policies, code alphabets) are not part of the core: they live in client code,
like `BlockQ8` above or the Q4_0 quantizer in lesson 25 (GGML's own quantizers
are defined in the GGML app).

| Unit                                                       | Description                                                                                                                                                                                                                                                                                                                                         |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Compose{stages...}`                                       | Sequential composition, in encode order: `encode` runs the stages first to last, `decode` runs them last to first. The last stage's encoded output is the Compose's own. Lossless iff all stages are; no error bound is declared for lossy compositions, because bounds do not compose without knowing how errors propagate.                        |
| `Parallel{children...}` / `Parallel{{"port", child}, ...}` | Product: applies each child to its own share of the Funcs. See below.                                                                                                                                                                                                                                                                               |
| `TrustedInverse{encoder, decoder}`                         | Takes `encode` from one approximation and `decode` from another. The escape hatch out of `Compose`'s structural guarantee (see below).                                                                                                                                                                                                              |
| `Choose{cond, if_true, if_false}`                          | Keeps whichever handle `cond` selects at construction, so it can be found by that handle.                                                                                                                                                                                                                                                           |
| `Identity{}`, `Permute{permutation}`                       | Pass Funcs (and their port names) through unchanged / reordered. Both are lossless.                                                                                                                                                                                                                                                                 |
| `Pointwise{...}`                                           | Elementwise `out(vs) = fn(in(vs))`; the output Funcs are named `name + "_encode"` and `name + "_decode"` (the four-argument constructors choose both names and the pure Var prefix). `with_types`, `with_ranges`, `with_lossless` and `with_error_bound` declare the signature, precondition and guarantee ranges, losslessness and an error bound. |
| `BlockReshape`                                             | Flat row to fixed-size records, or (`BlockReshape::tiles({b0, b1, ...})`) several leading dimensions to dense tiles; lossless.                                                                                                                                                                                                                      |
| `StructLayout`                                             | Logical Funcs to a struct-typed record Func, one field each; lossless. The record dimensionality is the inputs' unless given to the constructor.                                                                                                                                                                                                    |
| `LittleEndianScalarPack<Word>`                             | A word per record to and from a leading byte dimension; lossless.                                                                                                                                                                                                                                                                                   |
| `PlanarFieldPack`                                          | Fixed-width fields packed into bytes; lossless for inputs in its declared range.                                                                                                                                                                                                                                                                    |

The layout units are rank-polymorphic: dimensions after the ones they act on
(`BlockReshape`: the flat index; `PlanarFieldPack`: `(element, record)`;
`StructLayout` and `LittleEndianScalarPack`: all of them, as records) pass
through unchanged, so one scheme value approximates a row `w(k)` and a matrix
`W(k, n)` alike. `BlockReshape` maps `(k, rest...)` to
`(within, block, rest...)`. `BlockReshape::tiles({b0, b1, ...})` splits each of
the leading dimensions by its own block, within-tile indices first:
`(x0, x1, rest...)` to `(x0 % b0, x1 % b1, x0 / b0, x1 / b1, rest...)`, so e.g.
`tiles({2, 2})` stores a matrix as dense 2x2 tiles that a schedule can fuse and
vectorize (`tiles({b})` is `BlockReshape(b)`). As with the flat form, each tiled
extent must be a multiple of its block; like other shape requirements, this is
documented rather than declared in the signature. The layout units' signatures
take the dimensionalities from the context, so they are unknown when there is
none (e.g. in `describe()` without inputs) and checked at run time otherwise. A
quantizer of your own composes with them on a matrix if it is rank-polymorphic
too, e.g. written with implicit Vars (`codes(j, b, _) = ...`).

A four-bit scheme, built from these and a quantizer of your own (`quant`, here
one whose codes are declared to lie in `[-8, 7]`), reads in the order `encode`
runs it: reshape to blocks, quantize, then shift the codes to `[0, 15]` (the
`offset` above) and pack them.

```cpp
Approximation pack = PlanarFieldPack{4, 8};
Approximation scheme = Compose{
    BlockReshape{16}, quant,
    Parallel{{"codes", Compose{offset, pack}}}};
```

**`Compose` and `TrustedInverse`.** Every `Approximation` is meant to be an
approximate identity factored into a `decode`-after-`encode` pair. `Compose`
preserves that structurally: it interleaves its stages' `encode`s and `decode`s
in mirror order, so both halves provably come from one stage list.
`TrustedInverse` pairs an `encode` and a `decode` from unrelated approximations,
so nothing structural guarantees they compose to an identity; the caller is
*trusted* to have supplied a true inverse pair. The motivating case is a scheme
whose forward map is an opaque offline black box (a per-block codeword search,
typically an extern call) that no composition of Funcs reproduces bit-for-bit,
but whose reverse map is an ordinary `Compose`. The unused half of each side is
never called.

**`Parallel`.** A product combinator with two forms; they cannot be mixed.

- *Positional*: `Parallel{a, b, ...}`. Child `i` gets a consecutive slice of the
  Funcs. On encode its width is the number of inputs of its signature, on decode
  the number of outputs (its encoded ports); one if the signature is unknown.
  The slices must exactly cover the Funcs, or it is an error stating both
  counts. `Identity{}` passes one Func through.
- *Named*: `Parallel{{"codes", a}, {"scale", b}}`. Each entry routes the one
  port of that name to its child. Ports not mentioned pass through unchanged, in
  place. A child's outputs replace its port in place, so a child may expand a
  port into several on encode; the mirror collapses them on decode, where the
  child must yield exactly one Func. A name that is missing or ambiguous, or
  routed twice, is an error. Without input ports (no context) the signature is
  unknown.

`Parallel` is lossless iff all children are and declares no error bound. Its
children appear in `describe()` and `check_ranges()`, and are traced like any
other combinator's.

### Ports and naming

Every Func a stage consumes or produces is a *port*:

```cpp
struct ApproximationPort {
    std::string name;
    std::optional<Type> type;
    std::optional<int> dimensions;
    std::optional<ApproximationRange> range;  // constant [lo, hi], see below
};
using ApproximationPorts = std::vector<ApproximationPort>;
```

`type` and `dimensions`, when set, are checked against the actual Func at run
time; a port with a Tuple-valued Func never has a `type`. `range` is a declared
bound on the port's *values*, never checked on the normal encode/decode path
(see "Verification").

A port name identifies one wire, in both directions. Declared names are
*checked, not substituted*.

- **Names flow in.** The names of the input ports come from, in order: the ports
  the caller (or the upstream stage) passed, else the unit's declared signature
  inputs, else positional `"0"`, `"1"`, .... A declared input name is only a
  default, used when no name flows in; it never renames a flowing wire. Types
  and dimensions are still checked against the declared ones.
- **Output ports** are the declared signature's outputs (their count must match
  the number of Funcs the unit returned) or, for an undeclared unit or one whose
  signature is unknown, follow the *naming rule*: if the output count equals the
  input count, output `i` takes input `i`'s name (so a single-Func unit
  preserves its input's name); otherwise the outputs are named positionally.
- The resolved output ports, with unset types and dimensions filled in from the
  actual Funcs, are returned as `EncodeResult::encoded_ports` /
  `DecodeResult::decoded_ports`, ready to hand to the next stage.
- **Mirror invariant.** For every stage, decode output `i` has the name of
  encode input `i`, and decode inputs have the names of the encode outputs. So a
  by-name `Parallel` routes the same port names in both directions, even across
  `StructLayout`, and decode ports never collide.
- **Stand-alone decode.** `decode(encoded, input_ports)` takes the encode-side
  context, computed statically: with none given, it is threaded from the root's
  default input names via signatures, exactly as `describe()` does. So a decode
  run on its own (e.g. after `sever` severs the encode) names its ports as an
  encode+decode would.
- `Func::approximate_by()` passes no names: the root's declared input names (or
  `"0"`) are the defaults.
- **Limit.** If a stage in a `Compose` has an unknown signature (an undeclared
  multi-Func unit), the contexts after it are unknown and fall back to defaults,
  so mirror naming past it is best-effort.

### Signatures and validation

```cpp
struct ApproximationSignature {
    ApproximationPorts inputs, outputs;  // encode direction
    bool known = true;
    static ApproximationSignature unknown(ApproximationPorts inputs = {});
};
```

The signature is written in the encode direction: `encode` consumes `inputs` and
produces `outputs`; `decode` consumes `outputs` and produces `inputs`. An
*undeclared* unit has no signature of its own, but
`Approximation::signature(inputs)` still resolves what it can without running
anything:

- a static signature is returned as is;
- a contextual signature is computed from `inputs`;
- an undeclared unit with a single-Func `encode` has one input (`inputs`, or
  `"0"` if none were given) and one output with the same name;
- anything else (an undeclared multi-Func unit) is *unknown* (`known == false`;
  `inputs` echoes the context and `outputs` is empty).

Combinators derive their signatures from their children's, and are unknown
whenever a child is: `Compose` chains its stages' signatures in encode order,
`Parallel` splices its children's ports into the slices they handle,
`TrustedInverse` and `Choose` report the encoder's and the chosen stage's,
`Identity` echoes its inputs (unknown if there are none), and `Permute` permutes
them.

On every `encode`/`decode` call, the handle checks each input Func against its
port's `type` and `dimensions` (both the ports the caller gave and the unit's
declared ones), and each output Func against the declared output ports. A
mismatch is a `user_error` naming the stage, the direction, the port, and
expected vs actual. Declared signatures are how a unit's Func-level contract
(e.g. "one float32 Func of dimension 1") gets checked at the point where a
scheme is assembled, rather than deep inside Halide's own lowering.

## Introspection

**Intermediates.** Each call to `encode`/`decode` reports, in `intermediates`,
every Func reachable from the stage's outputs without passing through one of its
inputs, excluding the inputs and outputs themselves (see "Defining a unit"). The
Funcs with update definitions among them need scheduling by whoever calls
`encode`; see `approximate_by` below.

**Trace.** The outermost handle call on a thread opens a trace, and every handle
call nested inside it appends a record, children first, then the stage itself.
Encode and decode share one trace (a `decode` called from inside an `encode`
appears in that `encode`'s trace). The trace is a tree,

```cpp
struct ApproximationTraceNode {
    Approximation stage;
    std::string label;                       // stage.label() when the call finished
    std::vector<Func> ports;                 // the stage's outputs
    std::vector<std::string> port_names;     // parallel to `ports`
    std::vector<Func> intermediates;         // discovered for this stage alone
    std::vector<ApproximationTraceNode> children;  // in invocation order
    std::vector<Func> inputs;                // what the call consumed
    std::vector<std::string> input_names;    // parallel to `inputs`
};
```

exposed as `EncodeResult::trace`, `DecodeResult::trace`, and
`ApproximationResult::encode_trace`/`decode_trace`. The flat `stage_outputs`
lists (`ApproximationStageOutputs{stage, ports, port_names, intermediates}`) are
its post-order flattening. `operator<<` prints a trace node, or a whole
`ApproximationResult` (under `encode:` and `decode:`), as an indented tree of
`label -> port=Func`, with each stage's intermediates on an `intermediates:`
line:

```
encode:
  BlockQ8 -> codes=codes, scale=scale
    intermediates: amax
decode:
  BlockQ8 -> values=dequantized
```

**Looking up a stage.** Stages are found by handle, not by position or path:

```cpp
Func encoded_by(const Approximation &stage, size_t port = 0) const;
Func decoded_by(const Approximation &stage, size_t port = 0) const;
Func encoded_by(const Approximation &stage, const std::string &port) const;
Func decoded_by(const Approximation &stage, const std::string &port) const;
```

on `ApproximationResult`. It is an error if `stage` was not invoked in that
direction, or was invoked more than once (the lookup would be ambiguous); an
out-of-range positional port yields an undefined Func, and an unknown or
duplicated port name is an error whose message lists the ports the stage has.

```cpp
Approximation qh = LittleEndianScalarPack<uint32_t>{};
Compose scheme{BlockReshape{32}, qh};
ApproximationResult r = f.approximate_by(scheme, {g});
Func bytes = r.decoded_by(qh);
```

**Stage ports.** `ApproximationResult::stage_ports()` returns every Func that is
an output port of some stage in either direction (deduplicated by name, encode
side first, each side in post-order), excluding `replacement`;
`is_stage_port(f)` tests membership. Callers use it to schedule stage boundaries
alongside reductions, e.g.
`if (f.has_update_definition() || r.is_stage_port(f)) f.compute_root();`.

**Decode chain.** `ApproximationResult::decode_funcs()` returns the decode
side's Funcs that `eager_inline()` would accept as their schedules stand now:
`replacement` and every Func it reaches without passing through `encoded` (the
decode trace root's intermediates), in dependency order (producers first,
`replacement` last). Funcs with update or extern definitions, or scheduled other
than inline (`compute_root()`, `compute_at()`, `vectorize()`, ...), are left
out. So after scheduling the encoded side and any decode Funcs that should stay
materialized, one call folds the rest of the decode chain into a consumer, which
then calls `encoded` directly, e.g. ahead of `rfactor()`:

```cpp
for (Func e : r.encoded) e.compute_root();
dot.update().eager_inline(r.decode_funcs());
```

**`describe()`.** `Approximation::describe(inputs = {})` (also `operator<<`)
renders a stage's structure without running anything: one
`label (inputs) -> (outputs)` line per stage, where a port prints as
`name: type xN in [lo, hi]` (unset parts are left out), followed by the stage's
children (`Compose`: encode order; `Parallel`: its children; `TrustedInverse`:
encoder then decoder; `Choose`: the chosen stage), indented by two spaces and
given the ports they would receive. For the four-bit scheme above (with the
quantizer from lesson 25):

```
Compose (values) -> (bytes: uint8 x2, scale: float32 x1)
  BlockReshape (values) -> (blocks)
  Q4_0Quantizer (blocks: float32 x2) -> (codes: int8 x2 in [-8, 7], scale: float32 x1)
  Parallel (codes: int8 x2 in [-8, 7], scale: float32 x1) -> (bytes: uint8 x2, scale: float32 x1)
    Compose (codes: int8 x2 in [-8, 7]) -> (bytes: uint8 x2)
      offset (codes: int8 x2 in [-8, 7]) -> (codes: uint8 x2 in [0, 15])
      PlanarFieldPack (codes: uint8 x2 in [0, 15]) -> (bytes: uint8 x2)
```

An unknown signature prints as `(unknown signature)`. Where a stage's context is
known, each input whose declared range is not guaranteed by its producer is
flagged on the following line, e.g.
`! input 'fields': [0, 16] not within [0, 15]` (or `not guaranteed` when the
producer declares no range).

## `approximate_by`: wiring an `Approximation` into a call graph

```cpp
ApproximationResult Func::approximate_by(const Approximation &p,
                                         const std::vector<Func> &consumers);

struct ApproximationResult {
    Func replacement;                  // decode's round-trip output; already
                                       // spliced into every Func in `consumers`
    std::vector<Func> encoded;         // the Funcs encode() produced
    ApproximationPorts encoded_ports;  // parallel to `encoded`
    std::vector<Func> intermediates;
    std::vector<ApproximationStageOutputs> encoded_stage_outputs,
                                           decoded_stage_outputs;
    ApproximationTraceNode encode_trace, decode_trace;
    // encoded_by(), decoded_by(), stage_ports(), is_stage_port(),
    // decode_funcs(): see above
};
```

`approximate_by` runs `p.encode({*this})` and `p.decode(encoded)`, requires the
decode to yield exactly one Func whose dimensionality and types match `*this`
(*the signature contract*: `decode(encode(f))` reproduces `f`'s arg list and
value type exactly, which is what makes it valid to splice back in), and then
replaces every call to `*this` inside each Func of `consumers` with a call to
that Func. A Func cannot be its own consumer. `intermediates` is `encoded`, then
the encode side's discovered intermediates, then the decode side's, without
duplicates, and never contains the original Func or `replacement`. Like all
discovered intermediates it may include Funcs the units referenced from outside.

The contract is not enforced generically at the `Approximation` level: each
concrete unit is responsible for it, and round-trip error and convergence are
treated as testable properties (see "Verification") rather than type-level
guarantees. `approximate_by`'s check at the point of substitution catches
violations of the shape contract, just not at definition time.

### Why not `Func::in`

The targeted form `g.in(f)` is eager: it rewrites `f` to call a new wrapper
immediately, so the graph reflects the change as soon as the call returns. But
`in()` always substitutes an *identity* wrapper; `approximate_by` must
substitute a different computation, `decode(encode(f))`. The global form
`f.in()` is not an option either, because it is deferred: it registers a wrapper
that `wrap_func_calls` applies during `lower()`, after
`configure()`/`generate()`/`schedule()` have run. Anything that reasons about
what a consumer actually calls before then (in particular, `sever`'s
`configure()`-time split) would see stale, pre-substitution state.

### The mechanism: eager and destructive, like `rfactor`

`Stage::rfactor` is the right precedent. It never defers to a lowering pass: it
builds a new `Func`, calls `define_update` on it immediately, and rewrites the
original Function's own definition, all synchronously, as part of the
`.rfactor()` call itself. By the time it returns, the graph already reflects the
change, which is why a caller can immediately turn around and schedule the new
Func.

`approximate_by` behaves the same way, using the same class of primitive
`WrapCalls.cpp` already relies on internally,
`Function::substitute_calls(orig, substitute)`, but invoked immediately, on an
explicitly given set of consumers, instead of registered for a later pass. It is
a member of `Func` (`f.approximate_by(p, consumers)`), not a free function,
because it is a graph-editing operation on `f` in exactly the sense that
`f.in(...)`, `f.clone_in(...)` and `f.rfactor(...)` are. No new internal
primitive was needed; `Function::substitute_calls` is already an ordinary
method, and `approximate_by` lives inside libHalide.

**Reporting `intermediates` is not optional.** Both `encode` and `decode` can
introduce Funcs with update definitions (per-block reductions, a shift-by-min
helper's own min-reduction). Left unscheduled, Halide computes them at the
innermost valid loop level by default. But that is only a default: the caller
has no way to override it, or to apply the fusion patterns from "Scope:
placement is not semantics" (e.g. `compute_at`-ing `encoded` into a producer for
dynamic activation requantization), unless it has the Funcs in hand. Because
units cannot be trusted to declare them, the framework discovers them (see
"Introspection") and bundles them into `intermediates` so the caller can
schedule all of them, not just the primary output.

### Consequence: consumers must already exist

Because the substitution is eager, `approximate_by` can only rewrite Funcs that
are already built at the point of the call. There is no equivalent of the global
`.in()` (redirect *every* current and future consumer). This is a real
capability loss, but it is the same scoping `rfactor` lives with, and it matches
how Generator code is written: within `generate()` (or `configure()`), `f` and
its consumers are typically built together, so passing `consumers` explicitly
costs nothing. It only forecloses transparently intercepting calls inside a
large, opaque, externally-authored algorithm whose call sites cannot be
enumerated, which is out of scope.

## Scope: placement is not semantics

An `Approximation`'s `encode`/`decode` never make any claim about *where* or
*when* they are computed relative to the rest of the pipeline. An early draft
proposed otherwise, that `encode`'s output could always be treated as "the
offline half", and was rejected on a concrete counterexample: **dynamic
activation requantization**.

- **Static weight quantization**: `encode` (e.g. Q4_0 quantize) runs exactly
  once, ever, fully decoupled from any inference call, a genuine
  compile-time/compilation-unit boundary. `decode` is fused inline into the
  consumer's inner loop and never materialized as its own Func.
- **Dynamic activation requantization**: `encode` (quantize a just-computed
  activation tile) needs to be fused into the *producer's* schedule: same
  granularity, same loop nest, no separate storage, recomputed every call.
  `decode` is fused into the consumer's tiles exactly as before.

Same `Approximation`, opposite treatment of where `encode` is computed. If
"encode implies offline" were baked into the interface, the activation case
would need an escape hatch to override it, at which point the shortcut has
bought nothing; and it would invite tooling to assume every quantize step is
safe to hoist to conversion time, a correctness trap for anything computed at
inference time.

**Consequence, and a scope reduction**: fusing `encode` into a producer or
`decode` into a consumer needs no new Halide feature. Ordinary `.compute_at()` /
`.compute_inline()` on `ApproximationResult`'s `replacement`, `encoded`,
`intermediates` and `stage_ports()` already achieves it, since they are regular
Funcs in the call graph (and `eager_inline(r.decode_funcs())` folds the decode
chain in at schedule time). `sever` (below) is needed only for the strictly
narrower case of actually severing the graph into two separately-compiled
artifacts, the static-weight case.

## `sever`: scope

`Pipeline::sever()` is deliberately independent of `Approximation`: it operates
on Funcs.

```cpp
SeverResult sever(const std::vector<Func> &to_sever);
SeverResult sever(const std::vector<Func> &to_sever,
                                     const std::vector<ImageParam> &bind_to);
SeverResult sever(const std::vector<Func> &to_sever,
                                     const std::vector<std::string> &names);

struct SeverResult {
    Pipeline offline;                       // computes to_sever's true values
    std::vector<ImageParam> online_inputs;  // one per to_sever, same order
};
```

It rewrites every call to each Func in `to_sever`, anywhere in the pipeline's
transitive call graph, to call an `ImageParam` of matching type and
dimensionality instead (a fresh one, one named by `names`, or the caller's
`bind_to`). Anything reachable only from `to_sever` (a per-block reduction, say)
belongs to the offline half and keeps its true computation. Like `rfactor` it is
eager and destructive. `offline` is a `Pipeline` whose outputs are `to_sever`;
realize it once (JIT), or compile it as its own artifact (AOT), and feed the
result to `online_inputs` before realizing the original pipeline.

**Decision: "seam exposure," not automatic pipeline splitting.** Given Funcs
that should become a compile-time boundary, the result is:

- their computation exists in one compile, as ordinary outputs (the "offline"
  artifact);
- same-shaped inputs exist in another compile, standing in for them (the
  "online" pipeline);
- both are ordinary, statically declared Generator I/O; there is no dynamic
  discovery of new ports mid-`generate()`.

True automatic splitting (one Generator definition, two artifacts emitted
automatically, no extra static I/O declared by the author) was considered and
rejected: it would require a Generator to discover an extra Input/Output
*during* `generate()`, based on the structure of a Func graph that does not yet
exist when `configure()` declares I/O. That is a phase-ordering problem, not
just an ergonomics one.

**Restriction:** each severed Func must be single-valued (no Tuples).

### Composability with `approximate_by`

Both operations are eager and destructive, so they compose in program order. The
graph state at every point *is* the true state; no later lowering pass can
silently change what a Func calls out from under code that already ran. The
`encoded` Funcs of an `ApproximationResult` are exactly what to hand to `sever`:
they are the Funcs the consumers' rewritten call graph depends on.

```cpp
ApproximationResult r = f.approximate_by(scheme, {consumer});
SeverResult split = Pipeline({consumer}).sever(r.encoded);
```

A Generator authoring an op from scratch usually does not need `approximate_by`
at all: it can build `encode`/`decode` itself and use `decoded[0]` wherever the
math needs the value, since it is writing the consumer fresh anyway.
`approximate_by` earns its keep when the consumer already exists as written code
the author does not want to edit by hand.

### Non-goal: provenance checking

Nothing here guarantees that the `Approximation` used to produce the offline
artifact in one compile is *actually* the same one the online compile expects
when decoding it. Correctness rests on both sides building the same scheme.
Embedding a scheme fingerprint in the artifact and checking it at load time is
deferred; this is a user obligation, as any hand-written quantize/dequantize
split already does.

## Generator shape

Generators already support dynamic I/O declared before `generate()` runs:
`configure()` exists so that `add_input<>()`/`add_output<>()` can be called
based on `GeneratorParam` values decided earlier. These additions let
`configure()` adopt the halves of a `sever` split as ports:

```cpp
template<typename T = Buffer<>> GeneratorInput<T> *add_input(const ImageParam &existing);
template<typename T = Buffer<>> GeneratorOutput<T> *add_output(const Func &existing);
template<typename T = Buffer<>> GeneratorOutput<T> *add_output(const std::string &name,
                                                               const Func &existing);
```

Each declares a port backed directly by the existing object and named after it
(or `name`); `T` may be given explicitly (`add_input<Buffer<int8_t, 1>>(q_in)`)
to check the object's type and dimensionality and give the stub statically typed
buffers. Both may only be called from `configure()`. `configure()` runs once per
target in a multi-target build, and `Func` names are made unique within a
process, so a `Func` created there is renamed (`y$1`) on the second run; name
the output port explicitly when that matters. (`ImageParam` names are exact.)

This lets one `configure()` build a whole round trip, split it, and adopt
whichever half applies, leaving `generate()` an empty stub. The quantize and
dequantize Generators share the body; only the ports differ:

```cpp
enum class Direction { Quantize, Dequantize };

class Codec : public Generator<Codec> {
public:
    GeneratorParam<Direction> direction{
        "direction", Direction::Quantize,
        {{"quantize", Direction::Quantize}, {"dequantize", Direction::Dequantize}}};

    void configure() {
        Approximation scheme = BlockQ8{};  // e.g. selected from GeneratorParams

        ImageParam x(Float(32), 1, "x");
        Var i("i");
        Func y("y");
        y(i) = x(i);  // stands in for whatever consumes the value

        ApproximationResult r = Func(x).approximate_by(scheme, {y});
        for (Func f : r.intermediates) {
            if (f.has_update_definition() || r.is_stage_port(f)) {
                f.compute_root();
            }
        }

        std::vector<std::string> names;
        for (const ApproximationPort &p : r.encoded_ports) {
            names.push_back(p.name + "_in");
        }
        SeverResult split =
            Pipeline({y}).sever(r.encoded, names);

        if (direction == Direction::Quantize) {
            add_input(x);
            std::vector<Func> encoded = split.offline.outputs();
            for (size_t i = 0; i < encoded.size(); i++) {
                add_output(r.encoded_ports[i].name, encoded[i]);
            }
        } else {
            for (const ImageParam &in : split.online_inputs) {
                add_input(in);
            }
            add_output("y", y);
        }
    }

    void generate() {}
};
```

`tools/halide_approximation_codec.h` (namespace `Halide::ApproximationCodec`,
header-only, not part of `Halide.h`) packages this body.
`make_codec(scheme, type, dimensions, options = {})` builds the round trip for a
`dimensions`-dimensional ImageParam, schedules it (`compute_root()` for Funcs
with update definitions; the rest stays inline), severs it, and returns a
`Codec`: `values` and `encoded` (the encoder's ports), `encoded_inputs` and
`decoded` (the decoder's), and the `ApproximationResult`. Port names are stable:
`values`, `decoded`, and one per encoded port named after it, the same in both
directions (`Options` overrides them). Bounds constraints and schedules are
added through the `Codec`:

```cpp
void configure() {
    ApproximationCodec::Codec codec =
        ApproximationCodec::make_codec(scheme, Float(32), 1);
    if (direction == Direction::Quantize) {
        codec.values.dim(0).set_min(0);
        codec.adopt_encoder(*this);  // values -> codes, scale
    } else {
        codec.adopt_decoder(*this);  // codes, scale -> decoded
    }
}
```

It is a helper over the public API rather than a library feature: a codec
Generator's shape (one ImageParam in, a severed identity consumer, names, which
side is scheduled how) is a convention, and Generators that need another shape
use the pattern above directly.

The encoded form's arity and layout are whatever the scheme chose, so the
Generator's public signature is scheme-dependent. That follows from a layout
choice each `Approximation` makes, and the framework does not paper over it:

- *Packed*: one opaque byte buffer (or one struct-typed Func), fields recovered
  inside `decode`.
- *Planar*: multiple typed Funcs (a `float16` delta Func, an `int8` quants
  Func), more Halide-native and type-safe, but the public signature grows with
  the scheme's field count.

Known rough edge, accepted for now: `add_input`/`add_output` return raw
pointers, so a Generator that wants to keep them needs member-pointer
bookkeeping that the static `Input<>`/`Output<>` member style does not.

## Verification

`tools/halide_approximation_testing.h` (namespace
`Halide::ApproximationTesting`, header-only, like `halide_image_io.h`; not part
of `Halide.h`) checks what a scheme claims. It is a test-time helper: nothing is
added to generated pipelines. Everything that runs Halide code uses the JIT,
with every stage boundary `compute_root`'d and traced so its values can be read
back; it does not change how a pipeline you build yourself is scheduled.

**Declared facts.**

- `ApproximationPort::range = ApproximationRange{lo, hi}`: constant `double`
  bounds (integers are exact up to 2^53). On an encode *input* port it is a
  *precondition*: the range the stage requires for its declared properties to
  hold (`PlanarFieldPack` with 4-bit fields needs `[0, 15]`). On an encode
  *output* port it is a *guarantee* (symmetric int8 codes lie in `[-127, 127]`).
- `Approximation::describe()` and `check_ranges(a, inputs = {})` compare, for
  every stage whose input context is known, each producer's guaranteed range
  with the consumer's precondition, without running anything. `check_ranges`
  returns a diagnostic per failure, e.g.
  `path: input 'p': [0, 15] not within [0, 7]`, or
  `path: input 'p': requires [0, 7], but the producer declares no range`; an
  empty result means every declared precondition is statically guaranteed.
  Ranges only flow through units that declare them, so unknown is common;
  unknown is reported, never assumed satisfied.
- `error_bound()` and `lossless()` (see "Defining a unit") declare accuracy.
  `BlockQ8` above declares half a step; a unit that declares none is treated as
  having no bound.

Ranges are never enforced by `encode`/`decode`, so they cost nothing in
generated code.

**Distributions.** A `Distribution` is a plain value describing how to fill a
buffer; `generate(dist, type, extents, seed)` draws it with a built-in
xoshiro256\*\* PRNG using only integer arithmetic and IEEE `+ - *`, so data is
identical on every platform (`normal()` is therefore an Irwin-Hall
approximation, not Box-Muller). Elements are generated in memory order
(dimension 0 fastest), and a *block* is a run of that many consecutive elements,
so for a `(within, block)` layout blocks line up with dimension 0.

| Constructor                                      | Values                                                                                                         |
| ------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| `uniform(lo, hi)`, `uniform_int(lo, hi)`         | Uniform floats in `[lo, hi)` / integers in `[lo, hi]`.                                                         |
| `normal(mean, stddev)`, `constant(v)`, `zeros()` | Approximately normal (support `mean +- 6 stddev`); constant.                                                   |
| `blockwise_constant(block, base)`                | One value from `base` per block.                                                                               |
| `outliers(base, block, magnitude)`               | `base`, except one random element per block (random sign) is `magnitude`.                                      |
| `extremes(type)`                                 | Only the extreme values of `type`.                                                                             |
| `special_floats()`                               | `+-0`, denormals, smallest normal, `+-inf`, NaN. Never part of another distribution.                           |
| `mixture(parts, block = 1)`                      | Each run of `block` elements drawn from one randomly chosen part.                                              |
| `from_port(port, type)`                          | Uniform over the port's declared range; else the whole range of an integer type, or `normal(0, 1)` for floats. |

**Round trips.** `verify_round_trip(a, inputs | input | dist, ...)` runs
`decode(encode(x))` and returns a `RoundTripReport`: value count, maximum
absolute and relative error (`|y - x| / max(|x|, floor)`), RMSE, exact-match
count, the worst coordinate and its values, whether a bound is declared and how
many values of the first input exceed it, and the seed and distribution as
provenance.

**Properties.** A `Property` is a named check on a `PropertyContext` (the
inputs, the decoded values, the encoded values and ports, and a lazily computed
re-encoding). The library:

| Property                                 | Checks                                                                                    |
| ---------------------------------------- | ----------------------------------------------------------------------------------------- |
| `lossless()`                             | `decode(encode(x))` is bit-for-bit `x`.                                                   |
| `bounded_error(abs)`                     | `abs(decode(encode(x)) - x) <= abs`.                                                      |
| `within_declared_bound()`                | Same, against the unit's `error_bound()`; fails if none is declared.                      |
| `idempotent_requantize(rel_tol = 1e-6)`  | `encode(decode(e)) == e` for `e = encode(x)` (floating-point encodings within `rel_tol`). |
| `zero_preserving()`, `sign_preserving()` | Zero decodes to zero; the round trip never flips a sign.                                  |
| `outputs_within_declared_ranges()`       | Every encoded value lies in its port's declared output range.                             |

`check_property(a, prop, ...)` runs `prop` over `trials` seeded trials (default
8), each on freshly generated inputs, and stops at the first failure. Inputs
come from a `std::vector<InputSpec>{type, extents, dist}`, from a single
`(dist, type, extents)`, or, with only `extents`, from the root's declared input
ports via `Distribution::from_port()`, so the property is exercised exactly
where its preconditions hold. Trial `i` uses `derive_seed(seed, i)` with trial 0
using `seed`, so `trials = 1, seed = failing_seed` reproduces a failure. The
`PropertyResult` carries the property, stage label, trials run, failing seed and
coordinate, and message.

**Preconditions.** The values entering the checked unit are compared with its
declared input ranges. By default a trial with a value outside them is *not run*
and fails with `PropertyResult::precondition_violated` set: the property is only
claimed where its preconditions hold, so a generator (or upstream stage) that
breaks them is reported as such. `PropertyOptions::check_preconditions = false`
runs the property anyway, which is how to demonstrate that it fails outside
them.

**Conditioning on upstream guarantees.** `prop.at(stage)` runs the whole scheme
on the generated inputs, takes the values that actually arrive at `stage`'s
encode inputs, and checks the property for that stage alone (encode, then
decode) on them. `stage` must be invoked exactly once by the scheme, so hold on
to its handle. This shows whether an upstream quantizer really establishes the
precondition of the packing stage below it:

```cpp
check_ranges(scheme);  // empty: the ranges line up statically
check_property(scheme, lossless().at(pack), Distribution::normal(0, 1),
               Float(32), {64}).passed;  // pack is exact on what it receives
```

**Mechanics.** Stage-boundary values are read back by tracing stores (a JIT
custom trace handler) rather than by realizing the Funcs, since their extents
are inferred from the consumers. Each trial is a full round trip; idempotence
re-runs it on the decoded values in a separate pipeline. Funcs that are not
single-valued scalars (Tuple-valued or struct-typed) cannot be read back: their
`encoded()` Buffers are undefined, and a property that needs one (e.g.
`idempotent_requantize`, or `outputs_within_declared_ranges` on a port with a
range) fails with a message saying so, as does `prop.at(stage)` for a stage with
such an input. Properties over the decoded values (`lossless()`,
`bounded_error()`, ...) are unaffected.

## Summary of decisions and open items

| Item                                                                                         | Status                                                                          |
| -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `Approximation`: value-semantic, type-erased, duck-typed handle; operates on `Func`s only    | Decided                                                                         |
| Copies of a handle are one stage; separate conversions are distinct stages                   | Decided; lookups (`encoded_by`/`decoded_by`) are by handle                      |
| Units return only their outputs; intermediates are discovered by walking definitions         | Decided; scheduling-only, kept separate from the signature-contract outputs     |
| Three unit forms per direction (single, multi, port-aware)                                   | Decided; see API surface below                                                  |
| Named ports, optional declared signatures (static or contextual), run-time validation        | Decided                                                                         |
| `decode(encode(f))` reproduces `f`'s arg list and type                                       | Decided; checked only at `approximate_by`'s substitution point, not generically |
| `Approximation` makes no placement claims (offline vs fused)                                 | Decided                                                                         |
| `encode`'s output arity/layout (packed vs planar)                                            | Left to each `Approximation`                                                    |
| `approximate_by`: eager, destructive `substitute_calls`, not `Func::in`; a `Func` member     | Decided; same scoping as `rfactor`, explicit already-existing `consumers`       |
| `sever`: seam exposure on `Pipeline`, adopted via `add_input(ImageParam)`/`add_output(Func)` | Decided; single-valued Funcs only                                               |
| `sever`: true automatic pipeline splitting                                                   | Rejected (phase-ordering conflict with `configure()`/`generate()`)              |
| `sever`: cross-compile provenance checking                                                   | Deferred; relies on both sides building the same scheme                         |
| Fusing `encode`/`decode` into neighboring stages (activation requantization)                 | No new mechanism; ordinary `.compute_at()`/`.compute_inline()`                  |
| Declared ranges, `error_bound()`, `lossless()`; property-based testing in `tools/`           | Decided; claims are checked in tests, never enforced or used in codegen         |
| Generator I/O ergonomics (`add_input`/`add_output` pointer bookkeeping)                      | Accepted rough edge, deferred                                                   |
| Codec Generators (configure()/sever()/adopt boilerplate)                                     | Header-only helper in `tools/` (`make_codec`); no library API                   |

Open items:

- **API surface.** The unit interface has many optional hooks. Candidates for
  pruning if the surface proves too large: the port-aware `encode`/`decode`
  forms (needed only by combinators that route by name or forward context),
  contextual `signature(inputs)` (needed only by shape-polymorphic units and
  combinators), and `child_inputs()` (only refines
  `describe()`/`check_ranges()`).
- **Path-based stage lookup.** Stages are found only by handle. Selecting a
  stage by its path in the trace tree (e.g. by label chain) is not implemented;
  it would remove the need to name handles in the common case, at the cost of
  making lookups depend on labels.
- **Trace scope.** The trace collector is thread-local; handle calls made from
  another thread inside a unit are not attached to the enclosing call.
- **Bounds composition.** `Compose` declares no `error_bound` for lossy stages,
  and ranges flow only through units that declare them.
- **Provenance checking** across an offline/online split, as above.

## Prior art referenced

- An out-of-tree GGML application's hand-written quantize/dequantize/vec_dot
  Generators, the implementations this design generalizes, and the first user of
  this API.
- A private Python research prototype exploring the same compositional
  `Approximation` idea. It is not a public artifact and is referenced only for
  context.
- `apps/hannk/halide/conv_generator.cpp`: the in-repo precedent for a
  `configure()` that does more than tweak a type.
- `src/Func.cpp: Stage::rfactor`: the eager, destructive graph-editing precedent
  `approximate_by` follows instead of `.in()`.
- `src/Func.cpp`, `src/Function.cpp`, `src/WrapCalls.cpp`: origin of
  `Function::substitute_calls`, the primitive `approximate_by` calls directly
  and eagerly instead of through the deferred wrapper map.
- `src/Generator.h`: the `configure()`/`generate()`/`schedule()` lifecycle that
  `sever` and the Generator shape build on.
