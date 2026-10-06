#ifndef HALIDE_APPROXIMATION_H
#define HALIDE_APPROXIMATION_H

/** \file
 * Defines Approximation, a type-erased handle for lossy, quantified
 * Func-to-Func transformations (e.g. a quantize/dequantize round trip), and
 * Compose/Parallel/etc., which build larger Approximations out of smaller ones. See
 * Func::approximate_by(), which splices such a round trip into an existing
 * call graph, and doc/Approximation.md for the design rationale.
 */

#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Func.h"

namespace Halide {

struct EncodeResult;
struct DecodeResult;
struct ApproximationResult;
struct ApproximationTraceNode;

/** A closed interval [lo, hi] of values, with constant double endpoints. This
 * is deliberately simpler than Halide::Interval (which holds Exprs and can be
 * unbounded on either side): declared value ranges are compile-time constants
 * used for documentation and testing, never for code generation. Integers up
 * to 2^53 in magnitude are represented exactly. */
struct ApproximationRange {
    double lo = 0, hi = 0;

    ApproximationRange() = default;
    ApproximationRange(double lo, double hi)
        : lo(lo), hi(hi) {
    }

    bool contains(double v) const {
        return lo <= v && v <= hi;
    }
    /** Is every value of `other` in this range? */
    bool contains(const ApproximationRange &other) const {
        return lo <= other.lo && other.hi <= hi;
    }
    bool operator==(const ApproximationRange &other) const {
        return lo == other.lo && hi == other.hi;
    }
};

/** A named slot in an Approximation's interface: one Func that flows into or
 * out of a stage. The name identifies the port (for lookups and for
 * name-based combinators like Parallel); `type` and `dimensions`, when set,
 * are checked against the actual Func at run time. A port with a multi-valued
 * (Tuple) Func never has a `type`.
 *
 * A name identifies a *wire*, not a stage: the port names that reach a stage
 * from upstream (or from the caller) are the names it sees, and a stage's
 * declared input names are only defaults for when none flow in. The
 * declaration is checked (types, dimensions) but never substituted. See
 * Approximation::encode().
 *
 * `range`, when set, is a declared bound on the port's *values*. Its meaning
 * depends on the direction the port is used in:
 *
 * - on an *input* port of a stage's encode (or a stage's declared inputs):
 *   a precondition -- the range the stage requires for its declared
 *   properties (losslessness, error bounds) to hold;
 * - on an *output* port of encode: a guarantee -- every encoded value lies
 *   in the range (e.g. a symmetric int8 quantizer's codes are in [-127, 127]).
 *
 * Ranges are never checked on the normal encode/decode path, so they cost
 * nothing in generated code. They are checked statically (see
 * check_ranges() and Approximation::describe()) by comparing each producer's
 * guaranteed output range with the consumer's required input range, and
 * dynamically by the helpers in tools/halide_approximation_testing.h. */
struct ApproximationPort {
    std::string name;
    std::optional<Type> type;
    std::optional<int> dimensions;
    std::optional<ApproximationRange> range;

    ApproximationPort(std::string name, std::optional<Type> type = std::nullopt,
                      std::optional<int> dimensions = std::nullopt,
                      std::optional<ApproximationRange> range = std::nullopt)
        : name(std::move(name)), type(type), dimensions(dimensions), range(range) {
    }
    ApproximationPort(const char *name, std::optional<Type> type = std::nullopt,
                      std::optional<int> dimensions = std::nullopt,
                      std::optional<ApproximationRange> range = std::nullopt)
        : name(name), type(type), dimensions(dimensions), range(range) {
    }
};

using ApproximationPorts = std::vector<ApproximationPort>;

/** The declared interface of an Approximation, in the encode direction:
 * `inputs` are consumed by encode() and `outputs` are what it produces.
 * Decode is the reverse: it consumes `outputs` and produces `inputs`.
 *
 * A signature may be *unknown* (`known == false`), meaning the arity of the
 * unit could not be determined without running it (e.g. an undeclared
 * unit with a multi-Func encode). An unknown signature's `inputs` echo the
 * context it was resolved in and its `outputs` are empty. */
struct ApproximationSignature {
    ApproximationPorts inputs, outputs;
    bool known = true;

    static ApproximationSignature unknown(ApproximationPorts inputs = {}) {
        ApproximationSignature s;
        s.inputs = std::move(inputs);
        s.known = false;
        return s;
    }
};

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
 * Each direction may additionally take a *port-aware* form, which also
 * receives the ports (names and constraints) of the Funcs that encode()
 * consumed -- the original values. In decode() that is the context in which
 * the stage's encode ran, not the ports of `encoded`:
 *
 * \code
 * std::vector<Func> encode(const std::vector<Func> &inputs,
 *                          const ApproximationPorts &input_ports) const;
 * std::vector<Func> decode(const std::vector<Func> &encoded,
 *                          const ApproximationPorts &input_ports) const;
 * \endcode
 *
 * The port-aware form is preferred when present. Combinators use it so that
 * they can look ports up by name and hand each child its own context.
 *
 * A unit may declare its interface with ONE of:
 *
 * \code
 * // static: the same ports whatever the context
 * ApproximationSignature signature() const;
 * // contextual: given the resolved input ports, return the full signature.
 * // `.inputs` normally echoes the given ports, possibly with more constraints.
 * // It is called with an empty vector when the inputs are not known.
 * ApproximationSignature signature(const ApproximationPorts &inputs) const;
 * \endcode
 *
 * A unit with neither is *undeclared*. Its output ports are named at run
 * time: if the output count equals the input count, output i takes input i's
 * name (so a single-Func unit preserves its input's name); otherwise the
 * outputs are named positionally, "0", "1", .... Declared ports are validated
 * against the actual Funcs on every call (see encode()).
 *
 * Decode never needs to declare anything more: for every stage, the ports of
 * decode()'s outputs are named like the ports of its encode()'s inputs, and
 * the ports of decode()'s inputs like the ports of encode()'s outputs.
 *
 * A unit may also list the Approximations it is built from, for describe():
 *
 * \code
 * std::vector<Approximation> children() const;
 * // optional: the input ports each child would receive when this unit's
 * // encode receives `inputs`, parallel to children(); it is what lets
 * // describe() and check_ranges() resolve the children's signatures
 * std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const;
 * \endcode
 *
 * A unit may declare how accurate its round trip is, with ONE of:
 *
 * \code
 * // A per-element bound on |decode(encode(x)) - x|, as a Func with the same
 * // arguments as inputs[0] (any numeric type). `encoded` are the Funcs that
 * // encode() produced from `inputs`.
 * Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const;
 * // The round trip is exact (the bound is zero).
 * bool lossless() const;
 * \endcode
 *
 * Both are claims that hold *when every input port's declared range (its
 * precondition) is respected*; see Approximation::error_bound() and
 * lossless(). They are never enforced at run time.
 *
 * See also Pointwise for elementwise units. The handle stores a decayed copy
 * of the unit, so methods are always invoked on a const object. (Units
 * needing mutable state must hold it in `mutable` members or behind a
 * pointer.)
 *
 * The handle's own encode()/decode() always take and return a *vector* of
 * Funcs, even though the common case (a leaf Approximation like a plain
 * quantizer) only ever uses one. This is what makes Compose and Parallel below
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
 * Compose scheme{BlockReshape{32}, qh};
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
    enum class SignatureForm { None,
                               Static,
                               Contextual };

    struct Concept {
        virtual ~Concept() = default;
        virtual std::vector<Func> encode(const std::vector<Func> &inputs,
                                         const ApproximationPorts &input_ports) const = 0;
        virtual std::vector<Func> decode(const std::vector<Func> &encoded,
                                         const ApproximationPorts &input_ports) const = 0;
        virtual std::string default_label() const = 0;
        virtual SignatureForm signature_form() const = 0;
        virtual ApproximationSignature declared_signature(const ApproximationPorts &inputs) const = 0;
        virtual bool encode_is_single() const = 0;
        virtual std::vector<Approximation> children() const = 0;
        virtual std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const = 0;
        virtual Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const = 0;
        virtual bool lossless() const = 0;
    };

    // The shared identity of a stage: every copy of a handle points at one
    // State, so the label lives here and is seen by all copies.
    struct State {
        std::unique_ptr<const Concept> impl;
        std::string label;
    };

    /** A readable name for a unit type: demangled, with "Halide::" and
     * anonymous-namespace qualifiers stripped. */
    static std::string type_label(const char *pretty_function);

    // Captures the compiler's pretty function name, which spells out T; this
    // avoids depending on RTTI.
    template<typename T>
    static const char *pretty_type_name() {
#if defined(_MSC_VER) && !defined(__clang__)
        return __FUNCSIG__;
#else
        return __PRETTY_FUNCTION__;
#endif
    }

    template<typename T>
    struct Model;

    enum class Form { None,
                      Ported,
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

    template<typename T, typename = void>
    struct is_ported_encode : std::false_type {};
    template<typename T>
    struct is_ported_encode<T, std::enable_if_t<std::is_same_v<
                                   std::decay_t<decltype(std::declval<const T &>().encode(
                                       std::declval<const std::vector<Func> &>(),
                                       std::declval<const ApproximationPorts &>()))>,
                                   std::vector<Func>>>> : std::true_type {};
    template<typename T, typename = void>
    struct is_ported_decode : std::false_type {};
    template<typename T>
    struct is_ported_decode<T, std::enable_if_t<std::is_same_v<
                                   std::decay_t<decltype(std::declval<const T &>().decode(
                                       std::declval<const std::vector<Func> &>(),
                                       std::declval<const ApproximationPorts &>()))>,
                                   std::vector<Func>>>> : std::true_type {};

    template<typename T, typename = void>
    struct has_static_signature : std::false_type {};
    template<typename T>
    struct has_static_signature<T, std::enable_if_t<std::is_same_v<
                                       std::decay_t<decltype(std::declval<const T &>().signature())>,
                                       ApproximationSignature>>> : std::true_type {};
    template<typename T, typename = void>
    struct has_contextual_signature : std::false_type {};
    template<typename T>
    struct has_contextual_signature<T, std::enable_if_t<std::is_same_v<
                                           std::decay_t<decltype(std::declval<const T &>().signature(
                                               std::declval<const ApproximationPorts &>()))>,
                                           ApproximationSignature>>> : std::true_type {};

    template<typename T, typename = void>
    struct has_children : std::false_type {};
    template<typename T>
    struct has_children<T, std::enable_if_t<std::is_same_v<
                               std::decay_t<decltype(std::declval<const T &>().children())>,
                               std::vector<Approximation>>>> : std::true_type {};
    template<typename T, typename = void>
    struct has_child_inputs : std::false_type {};
    template<typename T>
    struct has_child_inputs<T, std::enable_if_t<std::is_same_v<
                                   std::decay_t<decltype(std::declval<const T &>().child_inputs(
                                       std::declval<const ApproximationPorts &>()))>,
                                   std::vector<ApproximationPorts>>>> : std::true_type {};

    template<typename T, typename = void>
    struct has_error_bound : std::false_type {};
    template<typename T>
    struct has_error_bound<T, std::enable_if_t<std::is_same_v<
                                  std::decay_t<decltype(std::declval<const T &>().error_bound(
                                      std::declval<const std::vector<Func> &>(),
                                      std::declval<const std::vector<Func> &>()))>,
                                  Func>>> : std::true_type {};
    template<typename T, typename = void>
    struct has_lossless : std::false_type {};
    template<typename T>
    struct has_lossless<T, std::enable_if_t<std::is_convertible_v<
                               decltype(std::declval<const T &>().lossless()), bool>>> : std::true_type {};

    template<typename T>
    static constexpr Form encode_form =
        is_ported_encode<T>::value ? Form::Ported :
                                     form_of<typename detect_encode_vec<T>::type,
                                             typename detect_encode_one<T>::type>::value;
    template<typename T>
    static constexpr Form decode_form =
        is_ported_decode<T>::value ? Form::Ported :
                                     form_of<typename detect_decode_vec<T>::type,
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
     * "LittleEndianScalarPack<unsigned int>"). Empty for an undefined handle. */
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
     * `stage_outputs`.
     *
     * Ports: `input_ports` name the `inputs`, and a name that flows in like
     * this is the one the stage uses, whatever its declared signature says.
     * If `input_ports` is non-empty, its size must equal `inputs.size()`. If
     * empty, the unit's declared signature supplies default names (when its
     * input count matches), else they are positional: "0", "1", .... Every
     * input Func is then checked against its port's `type` and `dimensions`
     * (when set) -- both the given ports and the unit's declared inputs -- and
     * a mismatch is a user_error naming this stage, the direction, the port,
     * and expected vs actual. The output ports are the declared signature's
     * outputs, resolved for these inputs (which must match the output count,
     * and are validated like the inputs) or, for an undeclared or
     * unknown-signature unit, follow the naming rule described on
     * Approximation. They are returned in EncodeResult::encoded_ports, with
     * unset types and dimensions filled in from the actual Funcs, so that
     * they can be handed to the next stage. */
    EncodeResult encode(const std::vector<Func> &inputs,
                        const ApproximationPorts &input_ports = {}) const;

    /** Reconstruct an approximation of the original Func(s) from their
     * encoded form. See DecodeResult for the constraint on `decoded`'s
     * size, which depends on how this Approximation is used.
     *
     * `input_ports` are the ports encode() was given (or empty, if it was
     * given none, as when a decode runs on its own after sever
     * severs the encode). They are the *context*: the encoded ports and the
     * decoded ports are both derived from them statically, through the
     * declared signature (as describe() does), so that a stand-alone decode
     * agrees with the encode that produced its inputs. Concretely, the
     * `encoded` Funcs are checked against, and named after, the outputs of
     * signature(input_ports), and the decoded Funcs are named after
     * `input_ports` (the defaults, if empty): decode's output port i is named
     * like encode's input port i. For an undeclared unit whose signature is
     * unknown, the encoded ports are named by the naming rule and the
     * decoded ports follow `input_ports` if their count matches. */
    DecodeResult decode(const std::vector<Func> &encoded,
                        const ApproximationPorts &input_ports = {}) const;

    /** The signature of this stage in the encode direction, resolved for
     * inputs named `inputs` (empty if unknown). Nothing is run.
     *
     * - static signature: returned as is, except that when `inputs` is
     *   non-empty and has the declared size, the input ports take the names
     *   of `inputs` (the declared names are defaults, never substitutes);
     * - contextual signature: computed from `inputs`, with the same rule
     *   for the input names;
     * - undeclared single-Func unit: one input (`inputs`, or "0" if none
     *   were given) and one output with the same name;
     * - undeclared multi-Func unit: unknown (see ApproximationSignature). */
    ApproximationSignature signature(const ApproximationPorts &inputs = {}) const;

    /** A per-element upper bound on |decode(encode(x)) - x|, given the
     * `inputs` handed to encode() and the `encoded` Funcs it returned, valid
     * when the inputs respect the declared input ranges. The result has the
     * same arguments as inputs[0] and a numeric type. It comes from the
     * unit's `error_bound()` if it has one, else a zero Func if the unit is
     * lossless(), else an undefined Func (no bound is declared). The bound
     * is a claim, not enforced; see tools/halide_approximation_testing.h to check it. */
    Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const;

    /** Does the unit declare its round trip exact (under its input
     * preconditions)? A unit that only has an `error_bound()` is not
     * lossless() even if that bound happens to be zero. */
    bool lossless() const;

    /** Render this stage's structure without running anything: one line per
     * stage, `label (inputs) -> (outputs)`, where a port prints as
     * `name: type xN in [lo, hi]` (`N` being its dimensionality; unset parts
     * are left out; the range is a precondition on inputs and a guarantee on
     * outputs), followed by the stage's children (see the `children()` unit
     * method), indented by two spaces. `inputs` is the context in which the
     * signature is resolved. When a stage's context is known, each input
     * whose declared range is not guaranteed by the context is flagged on the
     * following line, e.g. `! input 'value': [0, 15] not within [0, 7]`
     * (or `... not guaranteed` when the context declares no range). */
    std::string describe(const ApproximationPorts &inputs = {}) const;

private:
    ApproximationPorts resolve_inputs(const std::vector<Func> &inputs, const ApproximationPorts &given) const;
    ApproximationPorts resolve_encoded(const std::vector<Func> &encoded, const ApproximationSignature &sig,
                                       const ApproximationPorts &input_ports) const;
    ApproximationPorts output_ports(const std::vector<Func> &outputs, const ApproximationSignature &sig,
                                    const ApproximationPorts &input_ports, bool encode_direction) const;
    void describe_to(std::string &out, const ApproximationPorts &inputs, int depth) const;
    void range_issues_to(std::vector<std::string> &out, const ApproximationPorts &inputs,
                         const std::string &path) const;
    friend std::vector<std::string> check_ranges(const Approximation &, const ApproximationPorts &);

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
    /** The names of `ports` (parallel to it). */
    std::vector<std::string> port_names;
    /** The Funcs discovered for this stage alone (see Approximation::encode). */
    std::vector<Func> intermediates;
    std::vector<ApproximationTraceNode> children;
    /** The Funcs this call consumed (the stage's inputs) and their port
     * names (parallel to it). */
    std::vector<Func> inputs;
    std::vector<std::string> input_names;
};

/** The ports produced by one stage during encode or decode, plus the
 * intermediate Funcs discovered for that stage alone. This trace is
 * supplemental scheduling metadata; it does not alter the signature
 * contract. */
struct ApproximationStageOutputs {
    Approximation stage;
    std::vector<Func> ports;
    /** The names of `ports` (parallel to it). */
    std::vector<std::string> port_names;
    std::vector<Func> intermediates;
};

/** The result of Approximation::encode(): the Func(s) that make up the
 * signature contract other code is expected to consume, plus the
 * intermediate Funcs discovered between the inputs and those outputs (e.g.
 * per-block reduction Funcs), which have no meaning outside scheduling but
 * must still be scheduled by whoever calls encode(). */
struct EncodeResult {
    std::vector<Func> encoded;
    /** The ports of `encoded` (parallel to it), ready to hand to the next
     * stage's encode() or to decode(). */
    ApproximationPorts encoded_ports;
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
 * Func; when it's used as one stage of a larger Compose/Parallel chain,
 * decoded may contain however many Funcs the next stage down expects. */
struct DecodeResult {
    std::vector<Func> decoded;
    /** The ports of `decoded` (parallel to it). */
    ApproximationPorts decoded_ports;
    std::vector<Func> intermediates;
    std::vector<ApproximationStageOutputs> stage_outputs;
    ApproximationTraceNode trace;
};

template<typename T>
struct Approximation::Model final : Approximation::Concept {
    T unit;

    template<typename U,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Model>>>
    explicit Model(U &&u)
        : unit(std::forward<U>(u)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs,
                             const ApproximationPorts &input_ports) const override {
        if constexpr (encode_form<T> == Form::Ported) {
            return unit.encode(inputs, input_ports);
        } else if constexpr (encode_form<T> == Form::Multi) {
            return unit.encode(inputs);
        } else {
            check_single_input(inputs, "encode");
            return {unit.encode(inputs[0])};
        }
    }

    std::vector<Func> decode(const std::vector<Func> &encoded,
                             const ApproximationPorts &input_ports) const override {
        if constexpr (decode_form<T> == Form::Ported) {
            return unit.decode(encoded, input_ports);
        } else if constexpr (decode_form<T> == Form::Multi) {
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
            return type_label(pretty_type_name<T>());
        }
    }

    SignatureForm signature_form() const override {
        if constexpr (has_contextual_signature<T>::value) {
            return SignatureForm::Contextual;
        } else if constexpr (has_static_signature<T>::value) {
            return SignatureForm::Static;
        } else {
            return SignatureForm::None;
        }
    }

    ApproximationSignature declared_signature(const ApproximationPorts &inputs) const override {
        if constexpr (has_contextual_signature<T>::value) {
            return unit.signature(inputs);
        } else if constexpr (has_static_signature<T>::value) {
            return unit.signature();
        } else {
            return ApproximationSignature::unknown(inputs);
        }
    }

    bool encode_is_single() const override {
        return encode_form<T> == Form::Single;
    }

    std::vector<Approximation> children() const override {
        if constexpr (has_children<T>::value) {
            return unit.children();
        } else {
            return {};
        }
    }

    std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const override {
        if constexpr (has_child_inputs<T>::value) {
            return unit.child_inputs(inputs);
        } else {
            return std::vector<ApproximationPorts>(children().size());
        }
    }

    Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const override {
        if constexpr (has_error_bound<T>::value) {
            return unit.error_bound(inputs, encoded);
        } else {
            return Func();
        }
    }

    bool lossless() const override {
        if constexpr (has_lossless<T>::value) {
            return unit.lossless();
        } else {
            return false;
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
     * exactly this boundary -- e.g. Pipeline::sever(result.encoded)
     * -- without calling Approximation::encode() themselves. */
    std::vector<Func> encoded;
    /** The ports of `encoded` (parallel to it). */
    ApproximationPorts encoded_ports;
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

    /** As above, but find the port by name. It is an error if `stage` has no
     * port of that name in this direction (the message lists the ports it
     * has), or if the name is ambiguous. */
    Func encoded_by(const Approximation &stage, const std::string &port) const;
    Func decoded_by(const Approximation &stage, const std::string &port) const;

    /** Every Func that is an output port of some stage in either direction,
     * deduplicated by name, in trace order (encode side first, each side
     * post-order: children before parents). Callers can use it to schedule
     * stage boundaries (e.g. compute_root them alongside reductions).
     * `replacement` -- the decode root's output, already spliced into the
     * consumers -- is excluded. A stage that passes an input through
     * (Identity, or Parallel around one) reports it as a port, so the original Func may appear
     * if such a stage sits at the very inside of the encode chain. */
    std::vector<Func> stage_ports() const;

    /** Is `f` (matched by name) one of stage_ports()? */
    bool is_stage_port(const Func &f) const;
};

/** Statically check declared value ranges through `a`, without running
 * anything. For every stage whose input context is known (the root's is
 * `inputs`, which is skipped when empty; a child's comes from
 * `child_inputs()`), each input port with a declared range (a precondition)
 * is compared with the range the context guarantees:
 *
 * - if both are declared and the guarantee is not within the precondition,
 *   the diagnostic reads `path: input 'p': [0, 15] not within [0, 7]`;
 * - if the guarantee is undeclared, `path: input 'p': requires [0, 7], but
 *   the producer declares no range`.
 *
 * `path` is the chain of stage labels from the root. An empty result means
 * every declared precondition is statically guaranteed. Ranges only flow
 * through units that declare them, so unknown is common; unknown is reported,
 * never assumed satisfied. */
std::vector<std::string> check_ranges(const Approximation &a, const ApproximationPorts &inputs = {});

/** Print a trace as an indented tree, one line per call: the label, then
 * `-> ` and the comma-separated ports as `port name=Func name`. A non-empty intermediates
 * list follows on its own line as `intermediates: a, b`, indented under its
 * stage, then the children in invocation order. */
std::ostream &operator<<(std::ostream &stream, const ApproximationTraceNode &node);

/** Print Approximation::describe(). */
std::ostream &operator<<(std::ostream &stream, const Approximation &approximation);

/** Print both directions of an ApproximationResult under `encode:` and
 * `decode:` headers. */
std::ostream &operator<<(std::ostream &stream, const ApproximationResult &result);

/** Sequentially composes any number of Approximations into a pipeline. The
 * stages are listed in *encode order*, innermost first: encode() runs them
 * first to last, on the original inputs, feeding each stage's encoded output
 * to the next; decode() runs the mirror image, last to first. So `stages[0]`
 * is closest to the original values, and `stages.back()` is the one whose
 * encode() output is this Compose's own encoded result, and whose decode()
 * input is this Compose's own encoded argument.
 *
 * Each stage is held as an Approximation handle (plain units convert
 * implicitly), so pass a named handle for any stage you want to look up
 * later with ApproximationResult::encoded_by()/decoded_by():
 *
 * \code
 * Compose scheme{
 *     BlockReshape{block_size},
 *     SymmetricAffineQuantize{block_size, qmax, rounding, anchor},
 *     Parallel{{"codes", Fp8Pack{}}, {"scale", Fp16Pack{}}},
 *     StructPack{...},
 * };
 * \endcode
 *
 * Each stage sees the port names that the previous stage's encode produced,
 * and (in decode) the ports are derived statically from the context in the
 * same way, so a stage's decode output ports are named like its encode
 * input ports. */
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

    /** Each stage receives the ports the previous stage produced. */
    std::vector<Func> encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const;

    /** Each stage receives the context that its encode had, found by
     * threading the stages' signatures from `input_ports`. */
    std::vector<Func> decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const;

    /** Chains the stages' signatures from the first. Unknown if any stage's
     * signature is unknown. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** The stages in encode order, each with the ports it would receive. */
    std::vector<Approximation> children() const;
    std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const;

    /** A composition of lossless stages is lossless (under their
     * preconditions, see check_ranges()). No error bound is declared for
     * lossy compositions: bounds do not compose without knowing how errors
     * propagate. */
    bool lossless() const;

    std::vector<Approximation> stages;
};

/** A product combinator: applies different Approximations to different parts
 * of a vector of Funcs, side by side. It comes in two forms.
 *
 * *Positional*: `Parallel{a, b, c}`. Child i is applied to a consecutive
 * slice of the Funcs, whose width is the number of inputs in the child's
 * signature in encode() (and of outputs in decode(), i.e. the number of
 * encoded ports), or one if the child's signature is unknown. The slices
 * must exactly cover the Funcs, or it is an error. Use Identity to pass one
 * Func through.
 *
 * *Named*: `Parallel{{"codes", a}, {"scale", b}}`. Each entry routes the port
 * called `"codes"` to its child (an error if there is no such port, or more
 * than one, or if two entries name the same port). Ports that are not
 * mentioned pass through unchanged, in place. A child's outputs replace its
 * port in place, so a child may expand one port into several in encode(), and
 * in decode() collapses them back into one. Naming the ports requires the
 * context to have names: a by-name Parallel has no known signature without
 * input ports, which is the case for the first stage of a Compose only if
 * given ports explicitly.
 *
 * The two forms cannot be mixed. In both, the Funcs the children produce,
 * in order, are Parallel's outputs, and the children see (and produce) port
 * names as they are, so names flow through untouched.
 *
 * Parallel is lossless if all of its children are. It declares no error bound,
 * since it would have to know how the children's errors combine. */
struct Parallel {
    /** One by-name entry. */
    struct Entry {
        Entry(std::string port, Approximation child)
            : port(std::move(port)), child(std::move(child)) {
        }
        std::string port;
        Approximation child;
    };

    /** Positional. */
    explicit Parallel(std::vector<Approximation> children)
        : children_(std::move(children)) {
    }

    template<typename A, typename B, typename... Rest,
             typename = std::enable_if_t<std::conjunction_v<std::is_convertible<A, Approximation>,
                                                            std::is_convertible<B, Approximation>,
                                                            std::is_convertible<Rest, Approximation>...>>>
    Parallel(A &&a, B &&b, Rest &&...rest)
        : children_{Approximation(std::forward<A>(a)), Approximation(std::forward<B>(b)),
                    Approximation(std::forward<Rest>(rest))...} {
    }

    /** Named. */
    Parallel(std::initializer_list<Entry> entries);

    std::vector<Func> encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const;
    std::vector<Func> decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const;

    /** Unknown if the children cannot be located in `inputs` or any of their
     * signatures is unknown. Without `inputs`, a positional Parallel uses
     * each child's own defaults. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** The children, with the ports each would receive. */
    std::vector<Approximation> children() const;
    std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const;

    /** Lossless if every child is. */
    bool lossless() const;

private:
    // How the encode-side ports are divided among the children, in port
    // order: `child` (an index into children_, or -1 for a port that passes
    // through) takes [begin, begin + width) of the ports, with `context` as
    // their ports (empty if unknown), and produces `outputs` encoded ports.
    struct Segment {
        int child;
        size_t begin, width;
        ApproximationPorts context;
        size_t outputs;
    };
    bool plan(const ApproximationPorts &inputs, std::vector<Segment> &segments, std::string *problem) const;

    std::vector<Approximation> children_;
    std::vector<std::string> ports_;
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
 *         BlockReshape{block_size}, ..., Parallel{...}, StructPack{...},
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

    std::vector<Func> encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const;
    std::vector<Func> decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const;

    /** The encoder's signature, in both directions: the encoder defines the
     * encoded representation, and the decoder is trusted to consume it. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** The encoder, then the decoder (which is described without context,
     * since it runs in the other direction). */
    std::vector<Approximation> children() const;
    std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const;

    Approximation encoder, decoder;
};

/** Picks one of two Approximations at construction time based on `cond`,
 * keeping only the chosen handle (so it can be looked up by that handle). */
struct Choose {
    Choose(bool cond, Approximation if_true, Approximation if_false)
        : chosen(cond ? std::move(if_true) : std::move(if_false)) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const;
    std::vector<Func> decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const;

    /** The chosen stage's signature. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** Just the chosen stage. */
    std::vector<Approximation> children() const;
    std::vector<ApproximationPorts> child_inputs(const ApproximationPorts &inputs) const;

    /** Lossless if the chosen stage is. */
    bool lossless() const;

    Approximation chosen;
};

/** An elementwise unit: encode() and decode() each map every value of a
 * single input Func through a user-supplied function, producing a Func with
 * the same dimensionality (`out(vs) = fn(in(vs))`).
 *
 * The functions take an Expr and return an Expr (the common case, for
 * single-valued Funcs), or take and return a `std::vector<Expr>` (one element
 * per Tuple output of the input Func, for Tuple-valued Funcs). Pass
 * lambdas with concrete parameter types, not `auto`. The input is not type
 * checked unless with_types() declares its type.
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
 * A cast is a one-liner:
 *
 * \code
 * Approximation to_f16 = Pointwise{"f16",
 *                                 [](Expr x) { return cast<float16_t>(x); },
 *                                 [](Expr x) { return cast<float>(x); }}
 *                            .with_types(Float(32), Float(16));
 * \endcode
 *
 * By default a Pointwise declares only its arity: one Func in, one out, with
 * the name and dimensionality of the input port. The with_*() methods
 * declare more of the signature (see Approximation::signature()), and
 * claim losslessness.
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

    /** A copy that also declares an error bound: `bound(x)` is an upper
     * bound on |decode(encode(x)) - x| as a function of the *original* value
     * x (single-valued Funcs only). Without this, error_bound() returns an
     * undefined Func. */
    Pointwise with_error_bound(ExprFn bound) const;
    Func error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const;

    /** A copy that declares the type of its input (what encode consumes) and
     * of its output (what encode produces); both are checked against the
     * actual Funcs. */
    Pointwise with_types(Type input_type, Type output_type) const;

    /** A copy that declares value ranges: `input_range` is the precondition
     * on the input for the unit's declared properties to hold (e.g.
     * losslessness), and `output_range` is a guarantee on the encoded
     * values (see ApproximationPort::range). */
    Pointwise with_ranges(ApproximationRange input_range, ApproximationRange output_range) const;

    /** A copy that claims to be lossless (for inputs within the declared
     * input range). */
    Pointwise with_lossless(bool is_lossless = true) const;

    /** The declared signature; see the with_*() methods. Undeclared parts are
     * left unknown. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    bool lossless() const {
        return lossless_;
    }

private:
    std::string encode_name, decode_name, var_prefix;
    TupleFn encode_fn, decode_fn;
    ExprFn bound_fn;
    std::optional<Type> input_type, output_type;
    std::optional<ApproximationRange> input_range, output_range;
    bool lossless_ = false;
};

/** Passes Funcs (and their port names) through unchanged in both directions. */
struct Identity {
    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    /** Echoes `inputs`; unknown if there are none. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    bool lossless() const {
        return true;
    }
};

/** Reorders Funcs: encode() outputs `inputs[permutation[i]]` at position i,
 * and decode() inverts that. The output port names are permuted the same
 * way. Without a context (no input ports), the inputs are named
 * positionally. */
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

    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    bool lossless() const {
        return true;
    }

    std::vector<int> forward, backward;
};

/** Losslessly reshape a flat row into fixed-size records. In block-indexed
 * mode the flat side is `(within, record)` rather than a single flat index.
 * Any further (batch) dimensions pass through unchanged, after the record
 * dimension: `(k, rest...)` <-> `(within..., record, rest...)`, so one
 * BlockReshape applies to a single row and to a matrix of rows alike. */
struct BlockReshape {
    explicit BlockReshape(int block_size, bool block_indexed = false)
        : extents_{block_size}, block_indexed_(block_indexed) {
    }
    explicit BlockReshape(std::vector<int> extents, bool block_indexed = false)
        : extents_(std::move(extents)), block_indexed_(block_indexed) {
    }

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    /** values (flat) <-> blocks (one extra leading dimension per extent).
     * The dimensionalities come from the context: without one (or without
     * its dimensionality) they are left unknown. The element type and any
     * declared range are passed through. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** Pure re-indexing: exact for any values (given the flat extent is a
     * multiple of the block size). */
    bool lossless() const {
        return true;
    }

private:
    std::vector<int> extents_;
    bool block_indexed_;

    int block_size() const;
    std::vector<Var> block_vars() const;
};

/** Map consecutive logical Func slots to named fields of an exact struct
 * type. Scalar fields have the record's dimensions; array fields have an
 * additional leading element dimension. The record's dimensionality is
 * `record_dimensions` if given, and otherwise whatever the inputs have (all
 * inputs must agree), so one StructLayout applies to a single row of
 * records and to a matrix of them alike. */
struct StructLayout {
    StructLayout(Type record_type, std::vector<std::string> logical_fields,
                 std::optional<int> record_dimensions = std::nullopt);

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    /** One input per logical field, named after it, and one `record` output.
     * Without `record_dimensions`, the dimensionalities come from the
     * context, and are unknown without one. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** Fields are stored bit-for-bit. */
    bool lossless() const {
        return true;
    }

private:
    Type record_type_;
    std::vector<std::string> logical_fields_;
    std::optional<int> record_dimensions_;

    size_t logical_slot(const std::string &name) const;
    const StructField &physical_field(const std::string &name) const;
};

namespace Internal {
std::vector<Func> little_endian_scalar_encode(Type word_type, const std::vector<Func> &inputs);
std::vector<Func> little_endian_scalar_decode(Type word_type, const std::vector<Func> &encoded);
ApproximationSignature little_endian_scalar_signature(Type word_type, const ApproximationPorts &inputs);
}  // namespace Internal

/** Convert a scalar integral word per record to/from a leading little-endian
 * byte dimension. Decode deliberately uses concat_bits so struct lowering and
 * ordinary byte buffers share the same wide-load optimization path. */
template<typename Word>
struct LittleEndianScalarPack {
    std::vector<Func> encode(const std::vector<Func> &inputs) const {
        return Internal::little_endian_scalar_encode(type_of<Word>(), inputs);
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        return Internal::little_endian_scalar_decode(type_of<Word>(), encoded);
    }

    /** A word per record <-> its bytes in a new leading dimension. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const {
        return Internal::little_endian_scalar_signature(type_of<Word>(), inputs);
    }

    /** Every bit of the word is kept. */
    bool lossless() const {
        return true;
    }
};

/** Exact fixed-width planar packing. For `(field_bits, positions)`, one byte
 * contains `8/field_bits` planes, each plane spanning `positions` consecutive
 * elements. This component applies no recentering and no lookup policy. Any
 * dimensions after the record dimension pass through unchanged. */
struct PlanarFieldPack {
    PlanarFieldPack(int field_bits, int positions);

    std::vector<Func> encode(const std::vector<Func> &inputs) const;
    std::vector<Func> decode(const std::vector<Func> &encoded) const;

    /** (element, record, rest...) fields <-> (position, record, rest...)
     * bytes. The fields must fit in `field_bits`: encode masks each one to
     * that width, so values outside [0, 2^field_bits - 1] are silently
     * truncated. The dimensionality comes from the context, and is unknown
     * without one. */
    ApproximationSignature signature(const ApproximationPorts &inputs) const;

    /** Exact for fields in the declared input range. */
    bool lossless() const {
        return true;
    }

private:
    int field_bits_, positions_, planes_;
};

}  // namespace Halide

#endif
