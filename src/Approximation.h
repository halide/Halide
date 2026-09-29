#ifndef HALIDE_APPROXIMATION_H
#define HALIDE_APPROXIMATION_H

/** \file
 * Defines Approximation, a type-erased handle for lossy, quantified
 * Func-to-Func transformations (e.g. a quantize/dequantize round trip), and
 * Compose/Apply/etc., which build larger Approximations out of smaller ones. See
 * Func::approximate_by(), which splices such a round trip into an existing
 * call graph, and doc/ApproximationDesign.md for the design rationale.
 */

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Func.h"

namespace Halide {

struct EncodeResult;
struct DecodeResult;
struct ApproximationResult;
struct ApproximationTraceNode;

/** Approximation is a value-semantic, type-erased handle (in the style of
 * std::function) to a lossy, quantified transformation of one or more Funcs'
 * values -- e.g. quantize-then-dequantize. Unlike an ordinary schedule
 * directive, an Approximation deliberately changes the *value* computed, not
 * just how or where it's computed: decode(encode(f)) is expected to
 * approximately reproduce f, not exactly reproduce it.
 *
 * Any type `T` providing const-callable `encode` and `decode` methods
 * implicitly converts to an Approximation; there is no base class to derive
 * from. Each direction independently may take either ONE of two forms:
 *
 * \code
 * // multi: a vector of Funcs in, a vector of Funcs out
 * std::vector<Func> encode(const std::vector<Func> &) const;
 * std::vector<Func> decode(const std::vector<Func> &) const;
 *
 * // single: one Func in, one Func out. Handing the unit any other number
 * // of inputs is an error.
 * Func encode(const Func &) const;
 * Func decode(const Func &) const;
 * \endcode
 *
 * The forms may be mixed (e.g. multi encode with single decode). If a type
 * offers both a vector and a Func overload for one direction, the vector
 * form is used. Types with a non-matching return type (or only one
 * direction) do not convert. A minimal unit:
 *
 * \code
 * struct Negate {
 *     Func encode(const Func &f) const {
 *         Func g("negated");
 *         g(_) = -f(_);
 *         return g;
 *     }
 *     Func decode(const Func &f) const {
 *         return encode(f);
 *     }
 * };
 * Approximation a = Negate{};
 * \endcode
 *
 * See also Pointwise for elementwise units. The handle stores a decayed copy
 * of the unit, so methods are always invoked on a const object. (Units
 * needing mutable state must hold it in `mutable` members or behind a
 * pointer.)
 *
 * The handle's own encode()/decode() always take and return a *vector* of
 * Funcs, even though the common case (a leaf Approximation like a plain
 * quantizer) only ever uses one. This is what makes Compose and Apply below
 * possible: a composed Approximation's inner stage can produce multiple Funcs
 * (e.g. a quantized-values Func plus a separate scale Func), and the next
 * stage needs to be able to consume all of them, or select just one to act
 * on.
 *
 * A unit only returns its output Funcs. The framework discovers the rest:
 * whatever intermediate Funcs the unit defined along the way (e.g. a
 * per-block reduction) are found by walking the outputs' definitions, and
 * are reported in EncodeResult::intermediates / DecodeResult::intermediates
 * (see Approximation::encode()). A unit that calls other Approximation
 * handles inside its own encode()/decode() (as Compose does) gets those
 * calls traced automatically.
 *
 * Identity semantics: a copy of a handle is the *same* stage (same_as() is
 * true), while converting a plain unit to an Approximation twice produces two
 * *distinct* stages, even if the units compare equal. Every stage invoked
 * through a handle -- including those invoked from inside another unit's
 * encode()/decode() -- is recorded automatically in the result's
 * `stage_outputs` (children first, then the stage itself), so to find a stage's outputs
 * later, hold onto a handle to it and hand copies of that handle to the
 * combinators:
 *
 * \code
 * Approximation qh = LittleEndianScalarPack<uint32_t>{};
 * Compose scheme{qh, BlockReshape{32}};
 * ApproximationResult r = f.approximate_by(scheme, {g});
 * Func bytes = r.decoded_by(qh);
 * \endcode
 *
 * An Approximation makes no claim about *where* or *when* encode/decode are
 * computed relative to the rest of a pipeline (offline vs fused inline,
 * compute_root vs compute_at) -- that is a scheduling decision, orthogonal
 * to the semantics defined here. Concretely: the same Approximation can be
 * used with encode() computed once, offline, ahead of any other stage (a
 * static weight quantizer) or fused into a producer's inner loop and
 * recomputed on every call (dynamic activation requantization) -- nothing
 * about the interface favors one over the other. See Func::approximate_by()
 * for splicing an Approximation into an existing call graph. */
class Approximation {
    struct Concept {
        virtual ~Concept() = default;
        virtual std::vector<Func> encode(const std::vector<Func> &inputs) const = 0;
        virtual std::vector<Func> decode(const std::vector<Func> &encoded) const = 0;
        virtual std::string default_label() const = 0;
    };

    // The shared identity of a stage: every copy of a handle points at one
    // State, so the label lives here and is seen by all copies.
    struct State {
        std::unique_ptr<const Concept> impl;
        std::string label;
    };

    /** A readable name for a unit type: demangled, with "Halide::" and
     * anonymous-namespace qualifiers stripped. */
    static std::string type_label(const std::type_info &type);

    template<typename T>
    struct Model;

    enum class Form { None,
                      Multi,
                      Single };

    template<typename T, typename Arg>
    using encode_call_t = decltype(std::declval<const T &>().encode(std::declval<Arg>()));
    template<typename T, typename Arg>
    using decode_call_t = decltype(std::declval<const T &>().decode(std::declval<Arg>()));

    template<typename Vector, typename Single, typename = void>
    struct form_of : std::integral_constant<Form, Form::None> {};

    // The vector-argument call is tried first, so a type with both vector
    // and Func overloads uses the vector form.
    template<typename Vector, typename Single>
    struct form_of<Vector, Single, std::enable_if_t<std::is_same_v<Vector, std::vector<Func>>>>
        : std::integral_constant<Form, Form::Multi> {};

    template<typename Vector, typename Single>
    struct form_of<Vector, Single,
                   std::enable_if_t<!std::is_same_v<Vector, std::vector<Func>> &&
                                    std::is_same_v<Single, Func>>>
        : std::integral_constant<Form, Form::Single> {};

    template<typename T, typename = void>
    struct detect_encode_vec {
        using type = void;
    };
    template<typename T>
    struct detect_encode_vec<T, std::void_t<encode_call_t<T, const std::vector<Func> &>>> {
        using type = std::decay_t<encode_call_t<T, const std::vector<Func> &>>;
    };
    template<typename T, typename = void>
    struct detect_encode_one {
        using type = void;
    };
    template<typename T>
    struct detect_encode_one<T, std::void_t<encode_call_t<T, const Func &>>> {
        using type = std::decay_t<encode_call_t<T, const Func &>>;
    };
    template<typename T, typename = void>
    struct detect_decode_vec {
        using type = void;
    };
    template<typename T>
    struct detect_decode_vec<T, std::void_t<decode_call_t<T, const std::vector<Func> &>>> {
        using type = std::decay_t<decode_call_t<T, const std::vector<Func> &>>;
    };
    template<typename T, typename = void>
    struct detect_decode_one {
        using type = void;
    };
    template<typename T>
    struct detect_decode_one<T, std::void_t<decode_call_t<T, const Func &>>> {
        using type = std::decay_t<decode_call_t<T, const Func &>>;
    };

    template<typename T, typename = void>
    struct has_name : std::false_type {};
    template<typename T>
    struct has_name<T, std::void_t<decltype(std::string(std::declval<const T &>().name()))>>
        : std::true_type {};

    template<typename T>
    static constexpr Form encode_form = form_of<typename detect_encode_vec<T>::type,
                                                typename detect_encode_one<T>::type>::value;
    template<typename T>
    static constexpr Form decode_form = form_of<typename detect_decode_vec<T>::type,
                                                typename detect_decode_one<T>::type>::value;

    template<typename T>
    using enable_if_unit = std::enable_if_t<
        !std::is_base_of_v<Approximation, std::decay_t<T>> &&
        encode_form<std::decay_t<T>> != Form::None && decode_form<std::decay_t<T>> != Form::None>;

    static void check_single_input(const std::vector<Func> &inputs, const char *direction);

public:
    /** Construct an undefined handle. */
    Approximation() = default;

    /** Wrap a copy of `unit` as a new stage. */
    template<typename T, typename = enable_if_unit<T>>
    Approximation(T &&unit);

    /** Wrap a copy of `unit` as a new stage with an explicit label. */
    template<typename T, typename = enable_if_unit<T>>
    Approximation(T &&unit, std::string label);

    bool defined() const {
        return state_ != nullptr;
    }

    /** Do these handles refer to the same stage? True for copies of one
     * handle; false for two separate conversions of equal units. */
    bool same_as(const Approximation &other) const {
        return state_ == other.state_;
    }

    /** Set this stage's label and return this handle. The label lives in the
     * state shared by every copy of the handle (they are all the same stage),
     * so this affects all existing copies too -- and the result is same_as()
     * this handle. Typical use: `Approximation q = Approximation(unit).labelled("q");`
     * or `Approximation q(unit, "q");`. */
    Approximation labelled(std::string label) const;

    /** A human-readable name for this stage, used when printing traces. It is
     * the label set by labelled() (or the constructor), if any; otherwise the
     * unit's `std::string name() const` if it has one; otherwise the unit's
     * type name with "Halide::" qualifiers stripped (e.g. "Compose",
     * "StorageCast<float, signed char>"). Empty for an undefined handle. */
    std::string label() const;

    /** Produce the encoded form of `inputs`. EncodeResult::encoded's
     * elements are not required to have the same type, dimensionality, or
     * count as `inputs` -- an Approximation is free to choose a packed
     * representation (a single opaque byte buffer, fields recovered via
     * reinterpret<>() inside decode) or a planar one (multiple typed Funcs,
     * one per field). Either is legitimate; the framework does not
     * decide.
     *
     * After the unit returns, the framework computes this stage's
     * `intermediates`: every Func reachable from the outputs' definitions
     * (pure, update, and extern-argument references) without passing through
     * one of `inputs`, excluding the inputs and outputs themselves, in
     * topological order (producers before consumers; ties broken by name).
     * This includes pure-only Funcs; callers filter as they see fit. Funcs
     * the unit references from outside (e.g. a shared lookup table Func
     * defined elsewhere) are reachable and not inputs, so they are reported
     * too.
     *
     * The outermost handle call on a thread opens a trace, and every handle
     * call nested inside it (directly or from inside any unit's
     * encode()/decode()) appends a record to it, children first, then the
     * stage itself. `stage_outputs` holds exactly the records appended
     * during this call. Encode and decode share one trace: a decode() call
     * made from inside an encode() (unusual) is recorded in that encode's
     * `stage_outputs`. */
    EncodeResult encode(const std::vector<Func> &inputs) const;

    /** Reconstruct an approximation of the original Func(s) from their
     * encoded form. See DecodeResult for the constraint on `decoded`'s
     * size, which depends on how this Approximation is used. */
    DecodeResult decode(const std::vector<Func> &encoded) const;

private:
    std::shared_ptr<State> state_;
};

/** One handle call (encode or decode) in an execution trace. `children` are
 * the handle calls that started and finished during this one, in the order
 * they were invoked; a node completes after all of its children. */
struct ApproximationTraceNode {
    Approximation stage;
    /** stage.label() when the call finished. */
    std::string label;
    /** The stage's output Funcs for this call. */
    std::vector<Func> ports;
    /** The Funcs discovered for this stage alone (see Approximation::encode). */
    std::vector<Func> intermediates;
    std::vector<ApproximationTraceNode> children;
};

/** The ports produced by one stage during encode or decode, plus the
 * intermediate Funcs discovered for that stage alone. This trace is
 * supplemental scheduling metadata; it does not alter the signature
 * contract. */
struct ApproximationStageOutputs {
    Approximation stage;
    std::vector<Func> ports;
    std::vector<Func> intermediates;
};

/** The result of Approximation::encode(): the Func(s) that make up the
 * signature contract other code is expected to consume, plus the
 * intermediate Funcs discovered between the inputs and those outputs (e.g.
 * per-block reduction Funcs), which have no meaning outside scheduling but
 * must still be scheduled by whoever calls encode(). */
struct EncodeResult {
    std::vector<Func> encoded;
    std::vector<Func> intermediates;
    /** The trace of this call as a flat list: the post-order flattening of
     * `trace` (children first, then the stage itself). */
    std::vector<ApproximationStageOutputs> stage_outputs;
    /** The trace of this call as a tree; its root is this call's stage. */
    ApproximationTraceNode trace;
};

/** The result of Approximation::decode(): decoded is the round-trip
 * replacement for whatever Func(s) were originally encoded, plus the
 * discovered scheduling-only intermediates. When an Approximation is used
 * directly with Func::approximate_by(), decoded must contain exactly one
 * Func; when it's used as one stage of a larger Compose/Apply chain,
 * decoded may contain however many Funcs the next stage down expects. */
struct DecodeResult {
    std::vector<Func> decoded;
    std::vector<Func> intermediates;
    std::vector<ApproximationStageOutputs> stage_outputs;
    ApproximationTraceNode trace;
};

template<typename T>
struct Approximation::Model final : Approximation::Concept {
    T unit;

    template<typename U>
    explicit Model(U &&u)
        : unit(std::forward<U>(u)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const override {
        if constexpr (encode_form<T> == Form::Multi) {
            return unit.encode(inputs);
        } else {
            check_single_input(inputs, "encode");
            return {unit.encode(inputs[0])};
        }
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const override {
        if constexpr (decode_form<T> == Form::Multi) {
            return unit.decode(encoded);
        } else {
            check_single_input(encoded, "decode");
            return {unit.decode(encoded[0])};
        }
    }

    std::string default_label() const override {
        if constexpr (has_name<T>::value) {
            return std::string(unit.name());
        } else {
            return type_label(typeid(T));
        }
    }
};

template<typename T, typename>
Approximation::Approximation(T &&unit)
    : Approximation(std::forward<T>(unit), std::string()) {
}

template<typename T, typename>
Approximation::Approximation(T &&unit, std::string label)
    : state_(std::make_shared<State>()) {
    state_->impl = std::make_unique<const Model<std::decay_t<T>>>(std::forward<T>(unit));
    state_->label = std::move(label);
}

/** The result of Func::approximate_by(): the primary replacement Func
 * (already spliced into every Func in `consumers`), plus every
 * intermediate Func discovered by encode()/decode() along the way that needs
 * scheduling (compute_root, compute_at, etc.) -- none of `intermediates` are
 * part of the Approximation's signature contract, but Halide still
 * requires Funcs with update definitions to be scheduled, and the fusion
 * patterns described on Approximation above (e.g. compute_at-ing the
 * encoded Func into a producer) are only possible if the caller has a
 * Func to schedule. `intermediates` is `encoded`, then the encode side's
 * discovered intermediates, then the decode side's, without duplicates, and
 * never contains the original Func or `replacement`. Like all discovered
 * intermediates, it may include Funcs the units referenced from outside. */
struct ApproximationResult {
    Func replacement;
    /** The Func(s) produced by encode() -- the signature-contract boundary
     * between the original values and their approximated form (e.g. a
     * quantizer's packed byte buffer). This is a subset of `intermediates` (kept
     * there too, so existing code that schedules everything in `intermediates`
     * doesn't need to change), broken out separately so callers can act on
     * exactly this boundary -- e.g. Pipeline::compute_offline(result.encoded)
     * -- without calling Approximation::encode() themselves. */
    std::vector<Func> encoded;
    std::vector<Func> intermediates;
    std::vector<ApproximationStageOutputs> encoded_stage_outputs;
    std::vector<ApproximationStageOutputs> decoded_stage_outputs;
    /** The encode and decode traces as trees; the roots are the stages
     * invoked by approximate_by() itself. */
    ApproximationTraceNode encode_trace;
    ApproximationTraceNode decode_trace;

    /** Return the given output port of `stage` (found by same_as()), or an
     * undefined Func if the port is out of range. It is an error if `stage`
     * was not invoked in this direction, or was invoked more than once (the
     * lookup would be ambiguous). */
    Func encoded_by(const Approximation &stage, size_t port = 0) const;
    Func decoded_by(const Approximation &stage, size_t port = 0) const;

    /** Every Func that is an output port of some stage in either direction,
     * deduplicated by name, in trace order (encode side first, each side
     * post-order: children before parents). Callers can use it to schedule
     * stage boundaries (e.g. compute_root them alongside reductions).
     * `replacement` -- the decode root's output, already spliced into the
     * consumers -- is excluded. A stage that passes an input through
     * (Identity, Apply) reports it as a port, so the original Func may appear
     * if such a stage sits at the very inside of the encode chain. */
    std::vector<Func> stage_ports() const;

    /** Is `f` (matched by name) one of stage_ports()? */
    bool is_stage_port(const Func &f) const;
};

/** Print a trace as an indented tree, one line per call: the label, then
 * `-> ` and the comma-separated port Func names. A non-empty intermediates
 * list follows on its own line as `intermediates: a, b`, indented under its
 * stage, then the children in invocation order. */
std::ostream &operator<<(std::ostream &stream, const ApproximationTraceNode &node);

/** Print both directions of an ApproximationResult under `encode:` and
 * `decode:` headers. */
std::ostream &operator<<(std::ostream &stream, const ApproximationResult &result);

/** Sequentially composes any number of Approximations into a pipeline:
 * encode() runs `stages` back-to-front (the last stage first, on the
 * original inputs), feeding each stage's encoded output to the one before
 * it; decode() runs the mirror image, front-to-back. So `stages[0]` is the
 * "outermost" stage -- the one whose encode() output is this Compose's own
 * encoded result, and whose decode() input is this Compose's own encoded
 * argument -- and `stages.back()` is "innermost", closest to the original
 * values.
 *
 * Each stage is held as an Approximation handle (plain units convert
 * implicitly), so pass a named handle for any stage you want to look up
 * later with ApproximationResult::encoded_by()/decoded_by():
 *
 * \code
 * Compose scheme{
 *     StructPack{...},
 *     Apply{1, 1, 1, Fp16Pack{}},
 *     SymmetricAffineQuantize{block_size, qmax, rounding, anchor},
 * };
 * \endcode
 */
struct Compose {
    explicit Compose(std::vector<Approximation> stages)
        : stages(std::move(stages)) {
    }

    template<typename A, typename B, typename... Rest,
             typename = std::enable_if_t<std::conjunction_v<std::is_convertible<A, Approximation>,
                                                            std::is_convertible<B, Approximation>,
                                                            std::is_convertible<Rest, Approximation>...>>>
    Compose(A &&a, B &&b, Rest &&...rest)
        : stages{Approximation(std::forward<A>(a)), Approximation(std::forward<B>(b)),
                 Approximation(std::forward<Rest>(rest))...} {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    std::vector<Approximation> stages;
};

/** Applies `inner` to just the sub-range `[idx, idx + arity)` of a Func
 * vector, passing every other element through unchanged -- e.g. applying a
 * quantizer to just the "shifted" component of an affine (shift + scale)
 * scheme's encoded output while leaving the shift amount itself untouched.
 * `encode_arity`/`decode_arity` (how many Funcs `inner` consumes at that
 * position for each direction) must be given explicitly, since C++ has no
 * way to infer them generically from `inner` itself. */
struct Apply {
    Apply(int idx, int encode_arity, int decode_arity, Approximation inner)
        : idx(idx), encode_arity(encode_arity), decode_arity(decode_arity),
          inner(std::move(inner)) {
    }

    Apply(int idx, Approximation inner)
        : Apply(idx, 1, 1, std::move(inner)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    /** "Apply[idx]" */
    std::string name() const;

    int idx, encode_arity, decode_arity;
    Approximation inner;
};

/** Routes encode() to one Approximation and decode() to another, taking each
 * direction from a *different* source. This is the deliberate backdoor out of
 * the structural guarantee Compose provides.
 *
 * Every Approximation is meant to be an approximate identity, factored into a
 * decode-after-encode pair (decode(encode(f)) ~= f). Compose preserves that by
 * construction: it interleaves its stages' encode()s and decode()s in mirror
 * order, so the composed round trip (d1 . d2) . (e2 . e1) is *guaranteed* to be
 * an approximate identity for the same structural reason each stage is -- the
 * two halves provably come from one stage list. TrustedInverse pairs an encode
 * and a decode from unrelated Approximations, so nothing structural guarantees
 * they compose to an identity: the caller is *trusted* to have supplied a true
 * inverse pair. Hence the name -- "trusted" as in "taken on trust", not "known
 * safe".
 *
 * The motivating case: a scheme whose forward map (quantize) is an opaque
 * offline black box -- a per-block codeword search, a transcendental scale fit,
 * typically an extern call -- that no composition of Halide Funcs reproduces
 * bit-for-bit, but whose reverse map (dequantize) *is* an ordinary Compose of
 * invertible primitives. Compose can't express that pairing; TrustedInverse
 * can, keeping the decode side a clean composition while the encode side is
 * whatever opaque Approximation actually produces the encoded form:
 *
 * \code
 * TrustedInverse{
 *     ExternQuantize{"q4_k_quantize_via_ggml"},   // encode(): values -> bytes
 *     Compose{                                      // decode(): bytes -> values
 *         StructPack{...}, Apply{...}, ..., BlockReshape{block_size},
 *     },
 * };
 * \endcode
 *
 * The unused half of each side is never called (here, the ExternQuantize's
 * decode() and the Compose's encode()); supplying an Approximation whose
 * relevant half is a stub is expected. */
struct TrustedInverse {
    TrustedInverse(Approximation encoder, Approximation decoder)
        : encoder(std::move(encoder)), decoder(std::move(decoder)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    Approximation encoder, decoder;
};

/** Picks one of two Approximations at construction time based on `cond`,
 * keeping only the chosen handle (so it can be looked up by that handle). */
struct Choose {
    Choose(bool cond, Approximation if_true, Approximation if_false)
        : chosen(cond ? std::move(if_true) : std::move(if_false)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    Approximation chosen;
};

/** An elementwise unit: encode() and decode() each map every value of a
 * single input Func through a user-supplied function, producing a Func with
 * the same dimensionality (`out(vs) = fn(in(vs))`).
 *
 * The functions take an Expr and return an Expr (the common case, for
 * single-valued Funcs), or take and return a `std::vector<Expr>` (one element
 * per Tuple output of the input Func, for Tuple-valued Funcs). Pass
 * lambdas with concrete parameter types, not `auto`. No type checking of the
 * input is done here; wrap Pointwise in a unit of your own for that.
 *
 * The output Funcs are named `name + "_encode"` and `name + "_decode"`; the
 * second constructor lets the caller pick both names exactly (and the
 * prefix of the pure Var names, which are numbered by dimension), so that a
 * unit built on Pointwise can keep its Func names stable.
 *
 * \code
 * Approximation a = Pointwise{"scale",
 *                            [](Expr x) { return x * 2; },
 *                            [](Expr x) { return x / 2; }};
 * \endcode
 *
 * Besides converting to Approximation, Pointwise's encode(Func) and
 * decode(Func) may be called directly. */
struct Pointwise {
    using ExprFn = std::function<Expr(Expr)>;
    using TupleFn = std::function<std::vector<Expr>(const std::vector<Expr> &)>;

    Pointwise(const std::string &name, ExprFn encode_fn, ExprFn decode_fn);
    Pointwise(const std::string &name, TupleFn encode_fn, TupleFn decode_fn);
    Pointwise(std::string encode_name, std::string decode_name, ExprFn encode_fn, ExprFn decode_fn,
              std::string var_prefix = "pw");
    Pointwise(std::string encode_name, std::string decode_name, TupleFn encode_fn, TupleFn decode_fn,
              std::string var_prefix = "pw");

    Func encode(const Func &input) const;
    Func decode(const Func &encoded) const;

private:
    std::string encode_name, decode_name, var_prefix;
    TupleFn encode_fn, decode_fn;
};

/** Passes Funcs through unchanged in both directions. */
struct Identity {
    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;
};

/** Reorders Funcs: encode() outputs `inputs[permutation[i]]` at position i,
 * and decode() inverts that. */
struct Permute {
    explicit Permute(std::vector<int> permutation)
        : forward(std::move(permutation)) {
        backward.resize(forward.size());
        for (int i = 0; i < (int)forward.size(); i++) {
            backward[forward[i]] = i;
        }
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    std::vector<int> forward, backward;
};

}  // namespace Halide

#endif
