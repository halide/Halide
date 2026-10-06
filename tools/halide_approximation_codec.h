#ifndef HALIDE_APPROXIMATION_CODEC_TOOL_H
#define HALIDE_APPROXIMATION_CODEC_TOOL_H

/** \file
 * Builds both halves of a codec from one Approximation -- encode a buffer of
 * values to its approximated form, and decode that form back -- for a
 * Generator to adopt as its ports in configure(). Header-only, like
 * halide_approximation_testing.h: include it alongside Halide.h.
 *
 * make_codec() runs the pattern from doc/Approximation.md ("Generator shape"):
 * an ImageParam of values, an identity consumer, approximate_by(), a default
 * schedule, Pipeline::sever() at the encoded Funcs, and stable port names. One
 * configure() body serves both directions:
 *
 * \code
 * void configure() {
 *     ApproximationCodec::Codec codec =
 *         ApproximationCodec::make_codec(scheme, Float(32), 1);
 *     if (direction == Direction::Encode) {
 *         codec.adopt_encoder(*this);  // values -> one output per encoded port
 *     } else {
 *         codec.adopt_decoder(*this);  // one input per encoded port -> decoded
 *     }
 * }
 * void generate() {}
 * \endcode
 *
 * Both directions build the same encoded Funcs, so they agree on the encoded
 * form by construction. Add bounds constraints (e.g.
 * `codec.values.dim(0).set_min(0)`) and schedules through the returned Codec
 * before or after adopting it.
 */

#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "Halide.h"

namespace Halide {
namespace ApproximationCodec {

/** How make_codec() names and schedules the codec. */
struct Options {
    /** The name of the values ImageParam (the encoder's input). */
    std::string values_name = "values";
    /** The name of the decoded Func (the decoder's output). As for the
     * encoded names, it is suffixed if another Func already has it. */
    std::string decoded_name = "decoded";
    /** The names of the encoded ports: both the encoder's outputs and the
     * decoder's inputs. If empty, they are the scheme's encoded port names,
     * with characters other than letters, digits and '_' replaced by '_', and
     * "encoded<i>" for an unnamed port. Func names are made unique, so if
     * another Func already has a requested name (e.g. a unit named a Func
     * after its port), the Func gets a suffixed name, and adopting it as a
     * Generator port is an error; pass other names here. */
    std::vector<std::string> encoded_names;
    /** If true, compute_root() every Func with an update definition (e.g. a
     * per-block reduction) and leave the rest inline, as by default. If
     * false, nothing is scheduled. */
    bool default_schedule = true;
};

/** A codec built by make_codec(). Each half is an ordinary Pipeline: the
 * encoder computes `encoded` from `values`, and the decoder computes
 * `decoded` from `encoded_inputs`. */
struct Codec {
    /** The encoder's input. */
    ImageParam values;
    /** The encoder's outputs: one named copy of each of result.encoded. */
    std::vector<Func> encoded;
    /** The decoder's inputs, parallel to `encoded` (same names, types and
     * dimensionalities). */
    std::vector<ImageParam> encoded_inputs;
    /** The decoder's output: decode(encoded_inputs), with the same type and
     * dimensionality as `values`. */
    Func decoded;
    /** The result of approximate_by(), for scheduling either half. Its
     * `encoded` Funcs belong to the encoder only: after sever(), the decode
     * side reads `encoded_inputs` instead. */
    ApproximationResult result;

    /** Declare the encoder's ports on `generator` (any type with
     * GeneratorBase's add_input(const ImageParam &) and
     * add_output(const Func &)). Only valid in configure(). */
    template<typename G>
    void adopt_encoder(G &generator) const {
        generator.add_input(values);
        for (const Func &f : encoded) {
            check_port_name(f.name());
            generator.add_output(f);
        }
    }

    /** Declare the decoder's ports on `generator`, as above. */
    template<typename G>
    void adopt_decoder(G &generator) const {
        for (const ImageParam &p : encoded_inputs) {
            generator.add_input(p);
        }
        check_port_name(decoded.name());
        generator.add_output(decoded);
    }

private:
    static void check_port_name(const std::string &name) {
        _halide_user_assert(name.find('$') == std::string::npos)
            << "ApproximationCodec: the Func for port \"" << name.substr(0, name.find('$'))
            << "\" was renamed to \"" << name << "\", since another Func already had that "
            << "name (e.g. one the scheme's units made); pass other names in Options\n";
    }
};

namespace Detail {

inline std::string sanitize_port_name(const std::string &name, size_t index) {
    std::string result;
    for (char c : name) {
        result += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    }
    if (result.empty() || std::isdigit((unsigned char)result[0])) {
        result = "encoded" + std::to_string(index) + (result.empty() ? "" : "_" + result);
    }
    return result;
}

inline std::vector<Var> make_vars(int dimensions, const std::string &prefix) {
    std::vector<Var> vars;
    for (int i = 0; i < dimensions; i++) {
        vars.emplace_back(prefix + std::to_string(i));
    }
    return vars;
}

}  // namespace Detail

/** Approximate a `dimensions`-dimensional buffer of `type` values by
 * `scheme`, and split the round trip into an encoder and a decoder. */
inline Codec make_codec(const Approximation &scheme, Type type, int dimensions,
                        const Options &options = {}) {
    Codec codec;
    codec.values = ImageParam(type, dimensions, options.values_name);
    std::vector<Var> args = Detail::make_vars(dimensions, "v");
    std::vector<Expr> arg_exprs(args.begin(), args.end());
    codec.decoded = Func(options.decoded_name);
    codec.decoded(args) = codec.values(arg_exprs);

    codec.result = Func(codec.values).approximate_by(scheme, {codec.decoded});
    const ApproximationResult &r = codec.result;

    if (options.default_schedule) {
        for (Func f : r.intermediates) {
            if (f.has_update_definition()) {
                f.compute_root();
            }
        }
    }

    std::vector<std::string> names = options.encoded_names;
    if (names.empty()) {
        for (size_t i = 0; i < r.encoded_ports.size(); i++) {
            names.push_back(Detail::sanitize_port_name(r.encoded_ports[i].name, i));
        }
    }
    _halide_user_assert(names.size() == r.encoded.size())
        << "make_codec: " << names.size() << " encoded names given, but the scheme encodes to "
        << r.encoded.size() << " Funcs\n";
    std::set<std::string> unique = {options.values_name, options.decoded_name};
    _halide_user_assert(unique.size() == 2)
        << "make_codec: the values and decoded names must differ, but both are \""
        << options.values_name << "\"\n";
    for (const std::string &n : names) {
        _halide_user_assert(unique.insert(n).second)
            << "make_codec: port name \"" << n << "\" is used twice; pass "
            << "Options::encoded_names to disambiguate\n";
    }

    // Name the outputs before sever() mints ImageParams of the same names, so
    // the Funcs keep their names exactly (Func names are made unique).
    for (size_t i = 0; i < r.encoded.size(); i++) {
        const Func &e = r.encoded[i];
        std::vector<Var> e_args = Detail::make_vars(e.dimensions(), "e");
        std::vector<Expr> e_exprs(e_args.begin(), e_args.end());
        Func out(names[i]);
        out(e_args) = e(e_exprs);
        codec.encoded.push_back(out);
    }
    codec.encoded_inputs = Pipeline({codec.decoded}).sever(r.encoded, names).online_inputs;
    return codec;
}

}  // namespace ApproximationCodec
}  // namespace Halide

#endif  // HALIDE_APPROXIMATION_CODEC_TOOL_H
