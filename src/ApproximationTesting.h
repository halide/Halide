#ifndef HALIDE_APPROXIMATION_TESTING_H
#define HALIDE_APPROXIMATION_TESTING_H

/** \file
 * Property-based testing and round-trip verification for Approximations.
 * Everything here is header-only and lives in Halide::ApproximationTesting
 * (kept out of namespace Halide proper because names like Distribution and
 * Property are too generic for the monolithic Halide.h).
 *
 * The pieces:
 *
 * - Distribution and generate(): seeded, platform-reproducible input
 *   generators, including adversarial ones (see below).
 * - verify_round_trip(): run decode(encode(x)) on concrete inputs and report
 *   error statistics, checked against the unit's declared error bound if it
 *   has one.
 * - Property and check_property(): named checks (lossless(), bounded_error(),
 *   idempotent_requantize(), ...) run over several seeded trials. A property
 *   is *conditioned* on where its inputs come from in two ways:
 *   (a) explicitly, by choosing the Distribution the inputs are drawn from
 *       (by default one that respects the root's declared input ranges);
 *   (b) by stage: `prop.at(stage)` runs the whole approximation on generated
 *       inputs, takes the values that actually arrive at `stage`'s encode
 *       inputs, and checks the property for that stage alone on those
 *       values. A bit-packing stage that is only exact for inputs that fit
 *       in its field can thus be tested on what an upstream quantizer really
 *       hands it.
 *
 * Everything that runs Halide code uses the JIT, with every stage boundary
 * compute_root'd and traced so its values can be read back; nothing here
 * changes how a pipeline you build yourself is scheduled.
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Approximation.h"
#include "Buffer.h"
#include "Func.h"
#include "Pipeline.h"

namespace Halide {
namespace ApproximationTesting {

// ---------------------------------------------------------------------------
// Random numbers
// ---------------------------------------------------------------------------

/** One step of the splitmix64 generator: advances `state` and returns the
 * next output. (splitmix64(seed = 0) yields 0xe220a8397b1dcdaf first.) */
inline uint64_t splitmix64(uint64_t &state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/** xoshiro256** seeded through splitmix64. Only integer operations, so a
 * given seed produces the same stream on every platform and standard
 * library (unlike std::uniform_*_distribution). */
class Rng {
public:
    explicit Rng(uint64_t seed) {
        for (uint64_t &word : s_) {
            word = splitmix64(seed);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    /** Uniform in [0, 1), with 53 random bits. */
    double next_double() {
        return (double)(next() >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    static uint64_t rotl(uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }
    uint64_t s_[4];
};

/** The seed of the `index`th trial or input derived from `seed`. Index 0 is
 * `seed` itself, so re-running with `seed = failing_seed` and one trial
 * reproduces a failing trial exactly. */
inline uint64_t derive_seed(uint64_t seed, uint64_t index) {
    if (index == 0) {
        return seed;
    }
    uint64_t state = seed ^ (index * 0xd1342543de82ef95ULL);
    splitmix64(state);
    return splitmix64(state);
}

namespace Detail {

// A product kept out of fused multiply-adds, whose different rounding on
// different compilers would otherwise break bit-reproducibility.
inline double product(double a, double b) {
    volatile double p = a * b;
    return p;
}

struct FloatInfo {
    double max, min_normal, denorm_min;
};

inline FloatInfo float_info(Type t) {
    if (t.is_bfloat()) {
        return {3.3895313892515355e38, 1.1754943508222875e-38, 9.1835496157991212e-41};
    }
    switch (t.bits()) {
    case 16:
        return {65504.0, 6.103515625e-05, 5.9604644775390625e-08};
    case 32:
        return {3.4028234663852886e38, 1.1754943508222875e-38, 1.401298464324817e-45};
    default:
        return {std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
                std::numeric_limits<double>::denorm_min()};
    }
}

inline bool is_floating(Type t) {
    return t.is_float() || t.is_bfloat();
}

inline bool is_integral(Type t) {
    return t.is_int() || t.is_uint();
}

// The lowest and highest value of an arithmetic type, as doubles (64-bit
// integer limits round to +-2^63 / 2^64).
inline std::pair<double, double> type_limits(Type t) {
    if (is_floating(t)) {
        double m = float_info(t).max;
        return {-m, m};
    }
    if (t.is_bool() || t.bits() == 1) {
        return {0, 1};
    }
    if (t.is_uint()) {
        return {0, std::ldexp(1.0, t.bits()) - (t.bits() < 53 ? 1 : 0)};
    }
    return {-std::ldexp(1.0, t.bits() - 1), std::ldexp(1.0, t.bits() - 1) - (t.bits() < 54 ? 1 : 0)};
}

template<typename T>
T saturate(double v) {
    if (!(v == v)) {
        return 0;
    }
    if (v <= (double)std::numeric_limits<T>::lowest()) {
        return std::numeric_limits<T>::lowest();
    }
    if (v >= (double)std::numeric_limits<T>::max()) {
        return std::numeric_limits<T>::max();
    }
    return (T)v;
}

inline void store_value(void *p, Type t, double v) {
    if (t.is_float()) {
        if (t.bits() == 64) {
            *(double *)p = v;
        } else if (t.bits() == 32) {
            *(float *)p = (float)v;
        } else {
            *(float16_t *)p = float16_t(v);
        }
    } else if (t.is_bfloat()) {
        *(bfloat16_t *)p = bfloat16_t(v);
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8:
            *(int8_t *)p = saturate<int8_t>(v);
            break;
        case 16:
            *(int16_t *)p = saturate<int16_t>(v);
            break;
        case 32:
            *(int32_t *)p = saturate<int32_t>(v);
            break;
        default:
            *(int64_t *)p = saturate<int64_t>(v);
            break;
        }
    } else if (t.is_uint() || t.is_bool()) {
        switch (t.bits()) {
        case 1:
        case 8:
            *(uint8_t *)p = t.bits() == 1 ? (v != 0) : saturate<uint8_t>(v);
            break;
        case 16:
            *(uint16_t *)p = saturate<uint16_t>(v);
            break;
        case 32:
            *(uint32_t *)p = saturate<uint32_t>(v);
            break;
        default:
            *(uint64_t *)p = saturate<uint64_t>(v);
            break;
        }
    } else {
        user_error << "ApproximationTesting: unsupported element type " << t << "\n";
    }
}

inline double load_value(const void *p, Type t) {
    if (t.is_float()) {
        if (t.bits() == 64) {
            return *(const double *)p;
        } else if (t.bits() == 32) {
            return *(const float *)p;
        }
        return (double)*(const float16_t *)p;
    } else if (t.is_bfloat()) {
        return (double)*(const bfloat16_t *)p;
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8:
            return *(const int8_t *)p;
        case 16:
            return *(const int16_t *)p;
        case 32:
            return *(const int32_t *)p;
        default:
            return (double)*(const int64_t *)p;
        }
    } else if (t.is_uint() || t.is_bool()) {
        switch (t.bits()) {
        case 1:
        case 8:
            return *(const uint8_t *)p;
        case 16:
            return *(const uint16_t *)p;
        case 32:
            return *(const uint32_t *)p;
        default:
            return (double)*(const uint64_t *)p;
        }
    }
    user_error << "ApproximationTesting: unsupported element type " << t << "\n";
    return 0;
}

inline uint8_t *element_ptr(const Buffer<> &b, const int *pos) {
    const halide_buffer_t *raw = b.raw_buffer();
    int64_t offset = 0;
    for (int d = 0; d < raw->dimensions; d++) {
        offset += (int64_t)(pos[d] - raw->dim[d].min) * raw->dim[d].stride;
    }
    return raw->host + offset * raw->type.bytes();
}

inline double get(const Buffer<> &b, const int *pos) {
    return load_value(element_ptr(b, pos), b.type());
}

inline int64_t element_count(const Buffer<> &b) {
    int64_t n = 1;
    for (int d = 0; d < b.dimensions(); d++) {
        n *= b.dim(d).extent();
    }
    return n;
}

// Visit every coordinate of `b`, dimension 0 varying fastest.
template<typename Fn>
void for_each_coord(const Buffer<> &b, Fn &&fn) {
    if (element_count(b) == 0) {
        return;
    }
    std::vector<int> pos(b.dimensions());
    for (int d = 0; d < b.dimensions(); d++) {
        pos[d] = b.dim(d).min();
    }
    while (true) {
        fn((const int *)pos.data());
        int d = 0;
        for (; d < b.dimensions(); d++) {
            if (++pos[d] < b.dim(d).min() + b.dim(d).extent()) {
                break;
            }
            pos[d] = b.dim(d).min();
        }
        if (d == b.dimensions()) {
            return;
        }
    }
}

inline std::string coord_string(const std::vector<int> &c) {
    std::ostringstream s;
    s << "(";
    for (size_t i = 0; i < c.size(); i++) {
        s << (i ? ", " : "") << c[i];
    }
    s << ")";
    return s.str();
}

inline std::string number_string(double v, int precision = 9) {
    std::ostringstream s;
    s.precision(precision);
    s << v;
    return s.str();
}

}  // namespace Detail

// ---------------------------------------------------------------------------
// Distributions
// ---------------------------------------------------------------------------

/** A description of how to fill a buffer with values. Distributions are
 * plain values; nothing is drawn until generate() is called with a seed.
 *
 * Elements are generated in memory order over the flattened buffer
 * (dimension 0 varies fastest), and a *block* is a run of that many
 * consecutive elements in this order -- so with dimension 0 the within-block
 * index, as in SymmetricBlockQuantize's (within, block) layout, blocks of
 * size `block` line up with dimension 0's extent.
 *
 * All draws use Rng, and use only integer arithmetic and IEEE +, -, *, so a
 * given (distribution, type, extents, seed) yields the same bytes on every
 * platform. (normal() is therefore an Irwin-Hall approximation, not
 * Box-Muller, which would depend on the platform's libm.) Values are computed
 * as doubles and then converted to the element type, saturating for
 * integers; 64-bit integer endpoints are therefore only accurate to a
 * double's 53 bits. */
class Distribution {
public:
    /** Uniform floating-point values in [lo, hi). */
    static Distribution uniform(double lo, double hi) {
        Distribution d(Kind::Uniform);
        d.a_ = lo;
        d.b_ = hi;
        return d;
    }

    /** Uniform integers in [lo, hi], both inclusive. */
    static Distribution uniform_int(int64_t lo, int64_t hi) {
        Distribution d(Kind::UniformInt);
        d.a_ = (double)lo;
        d.b_ = (double)hi;
        return d;
    }

    /** Approximately normal: the sum of twelve uniforms, centered and
     * scaled, so its support is mean +- 6 * stddev. */
    static Distribution normal(double mean, double stddev) {
        Distribution d(Kind::Normal);
        d.a_ = mean;
        d.b_ = stddev;
        return d;
    }

    static Distribution constant(double v) {
        Distribution d(Kind::Constant);
        d.a_ = v;
        return d;
    }

    static Distribution zeros() {
        return constant(0);
    }

    /** Every block of `block` elements is one constant value, drawn once per
     * block from `base`. */
    static Distribution blockwise_constant(int block, Distribution base) {
        user_assert(block > 0) << "blockwise_constant: block must be positive\n";
        Distribution d(Kind::Blockwise);
        d.block_ = block;
        d.parts_ = {std::move(base)};
        return d;
    }

    /** Values from `base`, except that one randomly chosen element of every
     * block of `block` elements (with a random sign) is `magnitude`. */
    static Distribution outliers(Distribution base, int block, double magnitude) {
        user_assert(block > 0) << "outliers: block must be positive\n";
        Distribution d(Kind::Outliers);
        d.block_ = block;
        d.a_ = magnitude;
        d.parts_ = {std::move(base)};
        return d;
    }

    /** Each element is one of the extremes of `type`: for integers its
     * minimum and maximum; for floats its lowest and highest finite values
     * and +-its smallest normal value. */
    static Distribution extremes(Type type) {
        Distribution d(Kind::Extremes);
        d.type_ = type;
        return d;
    }

    /** Each element is one of +-0, +-denormals, +-the smallest normal, +-inf
     * or NaN. Floating-point types only. Deliberately never part of any other
     * distribution: use it only when the unit under test is meant to cope. */
    static Distribution special_floats() {
        return Distribution(Kind::SpecialFloats);
    }

    /** Draw each run of `block` consecutive elements entirely from one
     * component, chosen uniformly at random (so the default, block = 1, mixes
     * per element; a larger block makes whole blocks homogeneous, which is
     * what block quantizers are sensitive to). A component generates its
     * segment on its own: its own block structure restarts at the segment's
     * start. */
    static Distribution mixture(std::vector<Distribution> parts, int block = 1) {
        user_assert(!parts.empty() && block > 0) << "mixture: needs components and a positive block\n";
        Distribution d(Kind::Mixture);
        d.block_ = block;
        d.parts_ = std::move(parts);
        return d;
    }

    /** A distribution respecting a port's declared value range (used as the
     * default generator for a root's inputs): uniform over the range if the
     * port declares one; otherwise over the whole range of an integer type,
     * or normal(0, 1) for a floating-point type (whose full range is
     * useless for exercising a quantizer). `type` must be the type inputs
     * will be generated in. */
    static Distribution from_port(const ApproximationPort &port, Type type) {
        if (Detail::is_floating(type)) {
            return port.range ? uniform(port.range->lo, port.range->hi) : normal(0, 1);
        }
        if (port.range) {
            return uniform_int((int64_t)std::ceil(port.range->lo), (int64_t)std::floor(port.range->hi));
        }
        auto limits = Detail::type_limits(type);
        return uniform_int((int64_t)limits.first, (int64_t)limits.second);
    }

    /** A short description, e.g. `uniform(-1, 1)`, used as provenance. */
    std::string to_string() const {
        using Detail::number_string;
        switch (kind_) {
        case Kind::Uniform:
            return "uniform(" + number_string(a_) + ", " + number_string(b_) + ")";
        case Kind::UniformInt:
            return "uniform_int(" + number_string(a_) + ", " + number_string(b_) + ")";
        case Kind::Normal:
            return "normal(" + number_string(a_) + ", " + number_string(b_) + ")";
        case Kind::Constant:
            return a_ == 0 && !std::signbit(a_) ? "zeros()" : "constant(" + number_string(a_) + ")";
        case Kind::Blockwise:
            return "blockwise_constant(" + std::to_string(block_) + ", " + parts_[0].to_string() + ")";
        case Kind::Outliers:
            return "outliers(" + parts_[0].to_string() + ", " + std::to_string(block_) + ", " +
                   number_string(a_) + ")";
        case Kind::Extremes: {
            std::ostringstream s;
            s << "extremes(" << type_ << ")";
            return s.str();
        }
        case Kind::SpecialFloats:
            return "special_floats()";
        case Kind::Mixture: {
            std::string s = "mixture({";
            for (size_t i = 0; i < parts_.size(); i++) {
                s += (i ? ", " : "") + parts_[i].to_string();
            }
            return s + "}, " + std::to_string(block_) + ")";
        }
        }
        return "";
    }

    /** Fill out[0, n) with values drawn for elements of type `type`. */
    void fill(Rng &rng, Type type, double *out, size_t n) const {
        switch (kind_) {
        case Kind::Constant:
            for (size_t i = 0; i < n; i++) {
                out[i] = a_;
            }
            break;
        case Kind::Uniform:
            for (size_t i = 0; i < n; i++) {
                out[i] = a_ + Detail::product(b_ - a_, rng.next_double());
            }
            break;
        case Kind::UniformInt:
            for (size_t i = 0; i < n; i++) {
                double v = a_ + std::floor(Detail::product(b_ - a_ + 1, rng.next_double()));
                out[i] = v > b_ ? b_ : v;
            }
            break;
        case Kind::Normal:
            for (size_t i = 0; i < n; i++) {
                double sum = 0;
                for (int k = 0; k < 12; k++) {
                    sum += rng.next_double();
                }
                out[i] = a_ + Detail::product(b_, sum - 6.0);
            }
            break;
        case Kind::Blockwise:
            for (size_t start = 0; start < n; start += block_) {
                double v;
                parts_[0].fill(rng, type, &v, 1);
                for (size_t i = start; i < std::min(n, start + (size_t)block_); i++) {
                    out[i] = v;
                }
            }
            break;
        case Kind::Outliers:
            parts_[0].fill(rng, type, out, n);
            for (size_t start = 0; start < n; start += block_) {
                size_t len = std::min(n - start, (size_t)block_);
                size_t pos = start + rng.next() % len;
                out[pos] = (rng.next() & 1) ? -a_ : a_;
            }
            break;
        case Kind::Extremes: {
            std::vector<double> values;
            if (Detail::is_floating(type_)) {
                Detail::FloatInfo info = Detail::float_info(type_);
                values = {-info.max, info.max, info.min_normal, -info.min_normal};
            } else {
                auto limits = Detail::type_limits(type_);
                values = {limits.first, limits.second};
            }
            for (size_t i = 0; i < n; i++) {
                out[i] = values[rng.next() % values.size()];
            }
            break;
        }
        case Kind::SpecialFloats: {
            user_assert(Detail::is_floating(type)) << "special_floats() requires a floating-point type\n";
            Detail::FloatInfo info = Detail::float_info(type);
            const double inf = std::numeric_limits<double>::infinity();
            const std::vector<double> values = {
                0.0, -0.0, info.denorm_min, -info.denorm_min,
                info.min_normal - info.denorm_min, -(info.min_normal - info.denorm_min),
                info.min_normal, -info.min_normal, inf, -inf,
                std::numeric_limits<double>::quiet_NaN()};
            for (size_t i = 0; i < n; i++) {
                out[i] = values[rng.next() % values.size()];
            }
            break;
        }
        case Kind::Mixture:
            for (size_t start = 0; start < n; start += block_) {
                size_t len = std::min(n - start, (size_t)block_);
                parts_[rng.next() % parts_.size()].fill(rng, type, out + start, len);
            }
            break;
        }
    }

private:
    enum class Kind { Constant,
                      Uniform,
                      UniformInt,
                      Normal,
                      Blockwise,
                      Outliers,
                      Extremes,
                      SpecialFloats,
                      Mixture };

    explicit Distribution(Kind kind)
        : kind_(kind), type_(Float(32)) {
    }

    Kind kind_;
    double a_ = 0, b_ = 0;
    int block_ = 1;
    Type type_;
    std::vector<Distribution> parts_;
};

/** Generate a buffer of element type `type` and the given extents (min 0)
 * from `dist`, deterministically from `seed`. */
inline Buffer<> generate(const Distribution &dist, Type type, std::vector<int> extents, uint64_t seed) {
    Buffer<> buf(type, extents);
    std::vector<double> values((size_t)Detail::element_count(buf));
    Rng rng(seed);
    dist.fill(rng, type, values.data(), values.size());
    size_t i = 0;
    Detail::for_each_coord(buf, [&](const int *pos) {
        Detail::store_value(Detail::element_ptr(buf, pos), type, values[i++]);
    });
    return buf;
}

template<typename T>
Buffer<T> generate(const Distribution &dist, std::vector<int> extents, uint64_t seed) {
    return generate(dist, type_of<T>(), std::move(extents), seed).template as<T>();
}

/** How to generate one input of an approximation. */
struct InputSpec {
    Type type;
    std::vector<int> extents;
    Distribution dist;
};

// ---------------------------------------------------------------------------
// Running a round trip
// ---------------------------------------------------------------------------

namespace Detail {

struct Capture {
    Type type;
    int dimensions = 0;
    std::map<std::vector<int>, std::vector<uint8_t>> data;
};

struct CaptureContext : JITUserContext {
    std::map<std::string, Capture> *captures = nullptr;
};

inline int32_t capture_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
    if (e->event != halide_trace_store) {
        return 0;
    }
    auto &captures = *static_cast<CaptureContext *>(ctx)->captures;
    auto it = captures.find(e->func);
    if (it == captures.end()) {
        return 0;
    }
    const int lanes = e->lanes, dims = e->dimensions, bytes = e->type.bytes();
    for (int l = 0; l < lanes; l++) {
        std::vector<int> coord(dims);
        for (int d = 0; d < dims; d++) {
            coord[d] = e->coordinates[d * lanes + l];
        }
        const uint8_t *src = (const uint8_t *)e->value + (size_t)l * bytes;
        it->second.data[coord].assign(src, src + bytes);
    }
    return 0;
}

// Assemble captured stores into a buffer covering their bounding box.
inline Buffer<> to_buffer(const Capture &c) {
    const int dims = c.dimensions;
    std::vector<int> lo(dims, std::numeric_limits<int>::max()), hi(dims, std::numeric_limits<int>::min());
    for (const auto &[coord, bytes] : c.data) {
        for (int d = 0; d < dims; d++) {
            lo[d] = std::min(lo[d], coord[d]);
            hi[d] = std::max(hi[d], coord[d]);
        }
    }
    std::vector<int> extents(dims, 0);
    if (!c.data.empty()) {
        for (int d = 0; d < dims; d++) {
            extents[d] = hi[d] - lo[d] + 1;
        }
    }
    Buffer<> buf(c.type, extents);
    if (!c.data.empty()) {
        buf.set_min(lo);
    }
    std::memset(buf.raw_buffer()->host, 0, (size_t)element_count(buf) * c.type.bytes());
    for (const auto &[coord, bytes] : c.data) {
        std::memcpy(element_ptr(buf, coord.data()), bytes.data(), bytes.size());
    }
    return buf;
}

inline Func wrap_buffer(const Buffer<> &b, const std::string &name) {
    std::vector<Var> vars;
    std::vector<Expr> args;
    for (int d = 0; d < b.dimensions(); d++) {
        vars.emplace_back("tv" + std::to_string(d));
        args.emplace_back(vars.back());
    }
    Func f(name);
    f(vars) = b(args);
    return f;
}

inline void collect_trace_funcs(const ApproximationTraceNode &node, std::vector<Func> &out) {
    for (const ApproximationTraceNode &child : node.children) {
        collect_trace_funcs(child, out);
    }
    out.insert(out.end(), node.inputs.begin(), node.inputs.end());
    out.insert(out.end(), node.ports.begin(), node.ports.end());
}

inline const ApproximationTraceNode *find_node(const ApproximationTraceNode &node, const Approximation &stage,
                                               int &count) {
    const ApproximationTraceNode *found = nullptr;
    if (node.stage.same_as(stage)) {
        found = &node;
        count++;
    }
    for (const ApproximationTraceNode &child : node.children) {
        if (const ApproximationTraceNode *f = find_node(child, stage, count)) {
            found = f;
        }
    }
    return found;
}

// The outcome of running decode(encode(inputs)) with every stage boundary
// captured.
struct RoundTripRun {
    EncodeResult enc;
    DecodeResult dec;
    std::vector<Buffer<>> inputs, encoded, decoded;
    // Every captured stage-boundary Func's values, by Func name.
    std::map<std::string, Buffer<>> captured;
    std::map<std::string, int> input_wrappers;
    Buffer<> bound;
    bool has_bound = false;

    // The realized values of a Func from the encode side.
    Buffer<> value_of(const Func &f) const {
        auto w = input_wrappers.find(f.name());
        if (w != input_wrappers.end()) {
            return inputs[w->second];
        }
        auto c = captured.find(f.name());
        user_assert(c != captured.end())
            << "ApproximationTesting: the values of '" << f.name() << "' were not captured\n";
        return c->second;
    }
};

inline std::string next_name(const char *base) {
    static int counter = 0;
    return std::string(base) + "_" + std::to_string(counter++);
}

// Run a's round trip on `inputs`. Only the encoded Funcs are captured unless
// `capture_all`, which captures every stage's inputs and outputs.
inline RoundTripRun run_round_trip(const Approximation &a, const std::vector<Buffer<>> &inputs,
                                   const std::vector<std::string> &input_names, bool capture_all) {
    user_assert(a.defined() && !inputs.empty()) << "ApproximationTesting: an approximation and inputs are required\n";
    RoundTripRun run;
    run.inputs = inputs;

    std::vector<Func> in_funcs;
    ApproximationPorts ports;
    for (size_t i = 0; i < inputs.size(); i++) {
        in_funcs.push_back(wrap_buffer(inputs[i], next_name("approx_test_input")));
        run.input_wrappers[in_funcs.back().name()] = (int)i;
        if (i < input_names.size()) {
            ports.emplace_back(input_names[i]);
        }
    }
    if (ports.size() != inputs.size()) {
        ports.clear();
    }

    run.enc = a.encode(in_funcs, ports);
    run.dec = a.decode(run.enc.encoded, run.enc.encoded_ports);
    user_assert(run.dec.decoded.size() == inputs.size())
        << "ApproximationTesting: '" << a.label() << "' decoded " << run.dec.decoded.size()
        << " Funcs from " << inputs.size() << " inputs\n";

    // Boundary Funcs to read back.
    std::vector<Func> wanted = run.enc.encoded;
    if (capture_all) {
        collect_trace_funcs(run.enc.trace, wanted);
    }
    std::map<std::string, Capture> captures;
    for (const Func &f : wanted) {
        if (f.defined() && f.outputs() == 1 && f.types()[0].is_scalar() && !f.types()[0].is_struct() &&
            !run.input_wrappers.count(f.name()) &&
            captures.emplace(f.name(), Capture{f.types()[0], f.dimensions(), {}}).second) {
            Func(f).compute_root().trace_stores();
        }
    }
    for (const auto *funcs : {&run.enc.intermediates, &run.dec.intermediates}) {
        for (const Func &f : *funcs) {
            if (f.has_update_definition()) {
                Func(f).compute_root();
            }
        }
    }

    std::vector<Func> outputs = run.dec.decoded;
    std::vector<Buffer<>> out_buffers;
    for (size_t i = 0; i < inputs.size(); i++) {
        const Func &d = run.dec.decoded[i];
        user_assert(d.outputs() == 1 && d.dimensions() == inputs[i].dimensions())
            << "ApproximationTesting: decoded Func '" << d.name() << "' does not match input " << i << "\n";
        std::vector<int> extents, mins;
        for (int k = 0; k < inputs[i].dimensions(); k++) {
            extents.push_back(inputs[i].dim(k).extent());
            mins.push_back(inputs[i].dim(k).min());
        }
        out_buffers.emplace_back(d.types()[0], extents);
        out_buffers.back().set_min(mins);
    }
    Func bound = a.error_bound(in_funcs, run.enc.encoded);
    if (bound.defined()) {
        user_assert(bound.dimensions() == inputs[0].dimensions() && bound.outputs() == 1)
            << "ApproximationTesting: the declared error bound must be a single-valued Func over the "
            << "first input's dimensions\n";
        std::vector<Var> vars;
        std::vector<Expr> args;
        for (int k = 0; k < bound.dimensions(); k++) {
            vars.emplace_back("bv" + std::to_string(k));
            args.emplace_back(vars.back());
        }
        Func as_double("approximation_bound_double");
        as_double(vars) = cast<double>(bound(args));
        outputs.push_back(as_double);
        std::vector<int> extents, mins;
        for (int k = 0; k < inputs[0].dimensions(); k++) {
            extents.push_back(inputs[0].dim(k).extent());
            mins.push_back(inputs[0].dim(k).min());
        }
        out_buffers.emplace_back(Float(64), extents);
        out_buffers.back().set_min(mins);
        run.has_bound = true;
    }

    CaptureContext ctx;
    ctx.captures = &captures;
    ctx.handlers.custom_trace = &capture_trace;
    Pipeline(outputs).realize(&ctx, Realization(out_buffers));

    for (const auto &[name, c] : captures) {
        run.captured[name] = to_buffer(c);
    }
    for (size_t i = 0; i < inputs.size(); i++) {
        run.decoded.push_back(out_buffers[i]);
    }
    if (run.has_bound) {
        run.bound = out_buffers.back();
    }
    // Encoded Funcs that cannot be read back (e.g. struct-typed) are left as undefined Buffers.
    for (const Func &f : run.enc.encoded) {
        bool readable = run.input_wrappers.count(f.name()) || run.captured.count(f.name());
        run.encoded.push_back(readable ? run.value_of(f) : Buffer<>());
    }
    return run;
}

// Do two same-shaped values agree exactly? Same-typed buffers are compared
// bitwise; otherwise as doubles.
inline bool same_value(const Buffer<> &a, const Buffer<> &b, const int *pos, bool bitwise) {
    if (bitwise && a.type() == b.type()) {
        return std::memcmp(element_ptr(a, pos), element_ptr(b, pos), a.type().bytes()) == 0;
    }
    double x = get(a, pos), y = get(b, pos);
    return x == y || (std::isnan(x) && std::isnan(y));
}

}  // namespace Detail

// ---------------------------------------------------------------------------
// Round-trip verification
// ---------------------------------------------------------------------------

/** Error statistics for decode(encode(x)) against x, over all inputs and
 * elements. Errors are absolute differences, computed in double; two equal
 * values (including two NaNs or two infinities of the same sign) differ by
 * zero, and any other difference involving a NaN or infinity is infinite. */
struct RoundTripReport {
    /** The number of values compared. */
    int64_t count = 0;
    double max_abs_error = 0;
    /** max over values of |y - x| / max(|x|, RoundTripOptions::relative_floor). */
    double max_rel_error = 0;
    /** sqrt of the mean squared error. */
    double rmse = 0;
    /** The number of values that came back equal (==, or both NaN). */
    int64_t exact_count = 0;
    /** Where the largest absolute error is, in which input, and the input
     * and decoded values there. */
    std::vector<int> worst_abs_coord;
    int worst_input_index = 0;
    double worst_input = 0, worst_output = 0;
    /** Whether the approximation declares an error bound (Approximation::
     * error_bound()), and how many values of the first input exceed it. */
    bool bound_declared = false;
    int64_t bound_violations = 0;
    std::vector<int> first_violation_coord;
    /** Provenance, for reproducing the run. */
    uint64_t seed = 0;
    std::string distribution;
};

struct RoundTripOptions {
    /** The floor of the denominator of the relative error. If negative, it
     * is 1 for integer inputs and the smallest normal float32 otherwise. */
    double relative_floor = -1;
};

inline std::ostream &operator<<(std::ostream &s, const RoundTripReport &r) {
    using Detail::coord_string;
    s << "RoundTripReport: " << r.count << " values, distribution " << r.distribution << ", seed " << r.seed << "\n";
    s << "  max abs error " << r.max_abs_error << " at " << coord_string(r.worst_abs_coord) << " of input "
      << r.worst_input_index << " (" << r.worst_input << " -> " << r.worst_output << ")\n";
    s << "  max rel error " << r.max_rel_error << ", rmse " << r.rmse << ", exact " << r.exact_count << "/" << r.count
      << "\n";
    if (r.bound_declared) {
        s << "  declared bound: " << r.bound_violations << " violation(s)";
        if (r.bound_violations) {
            s << ", first at " << coord_string(r.first_violation_coord);
        }
        s << "\n";
    } else {
        s << "  declared bound: none\n";
    }
    return s;
}

/** Run `a` on `inputs` (one Buffer per input port, all of which the round
 * trip must reproduce) and compare decode(encode(inputs)) with the inputs.
 * The declared error bound, if any, is compared against the first input.
 * `seed` and `distribution` are recorded in the report as provenance only. */
inline RoundTripReport verify_round_trip(const Approximation &a, const std::vector<Buffer<>> &inputs,
                                         const RoundTripOptions &options = {}, uint64_t seed = 0,
                                         const std::string &distribution = "<user-supplied buffers>") {
    Detail::RoundTripRun run = Detail::run_round_trip(a, inputs, {}, false);
    RoundTripReport report;
    report.seed = seed;
    report.distribution = distribution;
    report.bound_declared = run.has_bound;
    double sum_sq = 0;
    for (size_t i = 0; i < inputs.size(); i++) {
        const Type type = inputs[i].type();
        const double floor = options.relative_floor >= 0 ? options.relative_floor :
                             Detail::is_integral(type)   ? 1.0 :
                                                           1.1754943508222875e-38;
        Detail::for_each_coord(inputs[i], [&](const int *pos) {
            const double x = Detail::get(inputs[i], pos), y = Detail::get(run.decoded[i], pos);
            double err;
            if (x == y || (std::isnan(x) && std::isnan(y))) {
                err = 0;
                report.exact_count++;
            } else {
                err = std::fabs(y - x);
                if (std::isnan(err)) {
                    err = std::numeric_limits<double>::infinity();
                }
            }
            if (report.count == 0 || err > report.max_abs_error) {
                report.max_abs_error = err;
                report.worst_abs_coord.assign(pos, pos + inputs[i].dimensions());
                report.worst_input_index = (int)i;
                report.worst_input = x;
                report.worst_output = y;
            }
            report.max_rel_error = std::max(report.max_rel_error, err / std::max(std::fabs(x), floor));
            sum_sq += err * err;
            report.count++;
            if (i == 0 && run.has_bound && !(err <= Detail::get(run.bound, pos))) {
                if (report.bound_violations++ == 0) {
                    report.first_violation_coord.assign(pos, pos + inputs[i].dimensions());
                }
            }
        });
    }
    report.rmse = report.count ? std::sqrt(sum_sq / (double)report.count) : 0;
    return report;
}

inline RoundTripReport verify_round_trip(const Approximation &a, const Buffer<> &input,
                                         const RoundTripOptions &options = {}) {
    return verify_round_trip(a, std::vector<Buffer<>>{input}, options);
}

/** Generate one input of type `type` and the given extents from `dist` and
 * `seed`, and verify the round trip on it. */
inline RoundTripReport verify_round_trip(const Approximation &a, const Distribution &dist, Type type,
                                         std::vector<int> extents, uint64_t seed,
                                         const RoundTripOptions &options = {}) {
    Buffer<> input = generate(dist, type, std::move(extents), seed);
    return verify_round_trip(a, std::vector<Buffer<>>{input}, options, seed, dist.to_string());
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

/** The outcome of one property check on one trial. */
struct PropertyOutcome {
    bool passed = true;
    std::string message;
    /** The (first, in memory order) failing coordinate, if the failure has
     * one. */
    std::vector<int> coord;
};

/** What a property is checked against: the approximation under test (the
 * root, or a stage for a stage-targeted property), the inputs it received,
 * and its realized round trip. */
struct PropertyContext {
    Approximation approximation;
    std::vector<Buffer<>> inputs;
    std::vector<std::string> input_names;
    Detail::RoundTripRun run;

    /** The Buffers the encode side produced (one per encoded Func). */
    const std::vector<Buffer<>> &encoded() const {
        return run.encoded;
    }
    /** The decoded Buffers (one per input). */
    const std::vector<Buffer<>> &decoded() const {
        return run.decoded;
    }
    /** The ports of encoded(), including declared value ranges. */
    const ApproximationPorts &encoded_ports() const {
        return run.enc.encoded_ports;
    }

    /** The round trip run again on decoded(), for properties about
     * re-encoding. Computed on first use. */
    const Detail::RoundTripRun &requantized() const {
        if (!requantized_) {
            requantized_ = Detail::run_round_trip(approximation, run.decoded, input_names, false);
        }
        return *requantized_;
    }

private:
    mutable std::optional<Detail::RoundTripRun> requantized_;
};

/** A named check of an approximation on realized values. Build one with the
 * factory functions below (or from a function of a PropertyContext), and
 * retarget it at an inner stage with at(). */
class Property {
public:
    using Check = std::function<PropertyOutcome(const PropertyContext &)>;

    Property(std::string name, Check check)
        : name_(std::move(name)), check_(std::move(check)) {
    }

    const std::string &name() const {
        return name_;
    }
    const Check &check() const {
        return check_;
    }

    /** The stage a stage-targeted property is about, or an undefined handle. */
    const Approximation &stage() const {
        return stage_;
    }

    /** A copy of this property that check_property() evaluates for `stage`
     * alone, on the values that actually arrive at its encode inputs when
     * the whole approximation runs on the generated inputs. `stage` must be
     * invoked exactly once by the approximation under test (hold on to the
     * handle you composed it with). The arriving values are also checked
     * against the stage's declared input ranges; see PropertyOptions. */
    Property at(const Approximation &stage) const {
        Property p = *this;
        p.stage_ = stage;
        return p;
    }

private:
    std::string name_;
    Check check_;
    Approximation stage_;
};

namespace Detail {

inline PropertyOutcome fail(const std::string &message, const std::vector<int> &coord = {}) {
    PropertyOutcome o;
    o.passed = false;
    o.message = message;
    o.coord = coord;
    return o;
}

inline std::string describe_pair(double x, double y) {
    return number_string(x, 9) + " -> " + number_string(y, 9);
}

// Check `test(x, y, in_buffer, pos)` over every input/decoded pair; report the first failure.
template<typename Test>
PropertyOutcome check_pairs(const PropertyContext &c, const char *what, Test &&test) {
    for (size_t i = 0; i < c.inputs.size(); i++) {
        PropertyOutcome result;
        for_each_coord(c.inputs[i], [&](const int *pos) {
            if (!result.passed) {
                return;
            }
            std::string problem = test(get(c.inputs[i], pos), get(c.decoded()[i], pos), c.inputs[i], c.decoded()[i], pos);
            if (!problem.empty()) {
                result = fail("input " + std::to_string(i) + " at " +
                                  coord_string(std::vector<int>(pos, pos + c.inputs[i].dimensions())) + ": " +
                                  what + " (" + problem + ")",
                              std::vector<int>(pos, pos + c.inputs[i].dimensions()));
            }
        });
        if (!result.passed) {
            return result;
        }
    }
    return {};
}

}  // namespace Detail

/** decode(encode(x)) is bit-for-bit x. (-0.0 versus 0.0 counts as a
 * difference; NaNs with equal bits do not.) Losslessness typically holds
 * only for inputs within the declared input ranges. */
inline Property lossless() {
    return Property("lossless", [](const PropertyContext &c) {
        PropertyOutcome o;
        for (size_t i = 0; i < c.inputs.size() && o.passed; i++) {
            Detail::for_each_coord(c.inputs[i], [&](const int *pos) {
                if (o.passed && !Detail::same_value(c.inputs[i], c.decoded()[i], pos, true)) {
                    std::vector<int> coord(pos, pos + c.inputs[i].dimensions());
                    o = Detail::fail("input " + std::to_string(i) + " at " + Detail::coord_string(coord) +
                                         ": round trip changed the value (" +
                                         Detail::describe_pair(Detail::get(c.inputs[i], pos), Detail::get(c.decoded()[i], pos)) + ")",
                                     coord);
                }
            });
        }
        return o;
    });
}

/** |decode(encode(x)) - x| <= abs everywhere. Values that come back equal
 * always pass; a NaN or infinity that does not come back equal always fails. */
inline Property bounded_error(double abs) {
    return Property("bounded_error(" + Detail::number_string(abs) + ")", [abs](const PropertyContext &c) {
        return Detail::check_pairs(c, "error exceeds the bound", [&](double x, double y, const Buffer<> &, const Buffer<> &, const int *) {
            if (x == y || (std::isnan(x) && std::isnan(y))) {
                return std::string();
            }
            double err = std::fabs(y - x);
            return err <= abs ? std::string() : Detail::describe_pair(x, y) + ", error " + Detail::number_string(err);
        });
    });
}

/** |decode(encode(x)) - x| <= the unit's declared error_bound(), per element
 * of the first input. Fails if the approximation declares no bound. */
inline Property within_declared_bound() {
    return Property("within_declared_bound", [](const PropertyContext &c) {
        if (!c.run.has_bound) {
            return Detail::fail("'" + c.approximation.label() + "' declares no error bound");
        }
        PropertyOutcome o;
        Detail::for_each_coord(c.inputs[0], [&](const int *pos) {
            if (!o.passed) {
                return;
            }
            double x = Detail::get(c.inputs[0], pos), y = Detail::get(c.decoded()[0], pos);
            double err = (x == y || (std::isnan(x) && std::isnan(y))) ? 0 : std::fabs(y - x);
            double bound = Detail::get(c.run.bound, pos);
            if (!(err <= bound)) {
                std::vector<int> coord(pos, pos + c.inputs[0].dimensions());
                o = Detail::fail("at " + Detail::coord_string(coord) + ": error " + Detail::number_string(err) +
                                     " exceeds the declared bound " + Detail::number_string(bound) + " (" +
                                     Detail::describe_pair(x, y) + ")",
                                 coord);
            }
        });
        return o;
    });
}

/** Re-encoding a decoded value reproduces the encoding: encode(decode(e))
 * == e, for e = encode(x), elementwise on every encoded Func. Integer
 * encodings must match exactly. Floating-point encodings (e.g. a
 * quantizer's scale, recomputed as `qmax * scale / qmax`) may differ in the
 * last bits, so they match if within `float_rel_tol` relative to the larger
 * magnitude. */
inline Property idempotent_requantize(double float_rel_tol = 1e-6) {
    return Property("idempotent_requantize", [float_rel_tol](const PropertyContext &c) {
        const Detail::RoundTripRun &again = c.requantized();
        for (size_t p = 0; p < c.encoded().size(); p++) {
            const Buffer<> &e1 = c.encoded()[p], &e2 = again.encoded[p];
            if (e1.dimensions() != e2.dimensions() || e1.type() != e2.type()) {
                return Detail::fail("encoded Func " + std::to_string(p) + " changed shape or type on re-encoding");
            }
            PropertyOutcome o;
            Detail::for_each_coord(e1, [&](const int *pos) {
                if (!o.passed) {
                    return;
                }
                bool ok;
                double x = Detail::get(e1, pos), y = Detail::get(e2, pos);
                if (Detail::is_floating(e1.type())) {
                    ok = x == y || (std::isnan(x) && std::isnan(y)) ||
                         std::fabs(x - y) <= float_rel_tol * std::max(std::fabs(x), std::fabs(y));
                } else {
                    ok = x == y;
                }
                if (!ok) {
                    std::vector<int> coord(pos, pos + e1.dimensions());
                    o = Detail::fail("encoded Func " + std::to_string(p) + " at " + Detail::coord_string(coord) +
                                         " changed on re-encoding (" + Detail::describe_pair(x, y) + ")",
                                     coord);
                }
            });
            if (!o.passed) {
                return o;
            }
        }
        return PropertyOutcome();
    });
}

/** Inputs that are zero decode to zero (== 0; -0.0 counts as zero). */
inline Property zero_preserving() {
    return Property("zero_preserving", [](const PropertyContext &c) {
        return Detail::check_pairs(c, "zero did not round-trip to zero", [](double x, double y, const Buffer<> &, const Buffer<> &, const int *) {
            return x == 0 && y != 0 ? Detail::describe_pair(x, y) : std::string();
        });
    });
}

/** The round trip never flips a sign: a positive input decodes to a value >= 0
 * and a negative one to a value <= 0 (so flushing to zero is allowed). NaNs
 * are ignored. */
inline Property sign_preserving() {
    return Property("sign_preserving", [](const PropertyContext &c) {
        return Detail::check_pairs(c, "sign flipped", [](double x, double y, const Buffer<> &, const Buffer<> &, const int *) {
            return (x > 0 && y < 0) || (x < 0 && y > 0) ? Detail::describe_pair(x, y) : std::string();
        });
    });
}

/** Every value of every encoded Func lies in that port's declared output
 * range (a guarantee, see ApproximationPort::range). Ports with no declared
 * range are not checked. */
inline Property outputs_within_declared_ranges() {
    return Property("outputs_within_declared_ranges", [](const PropertyContext &c) {
        for (size_t p = 0; p < c.encoded().size() && p < c.encoded_ports().size(); p++) {
            const ApproximationPort &port = c.encoded_ports()[p];
            if (!port.range) {
                continue;
            }
            PropertyOutcome o;
            Detail::for_each_coord(c.encoded()[p], [&](const int *pos) {
                double v = Detail::get(c.encoded()[p], pos);
                if (o.passed && !port.range->contains(v)) {
                    std::vector<int> coord(pos, pos + c.encoded()[p].dimensions());
                    o = Detail::fail("output '" + port.name + "' at " + Detail::coord_string(coord) + " is " +
                                         Detail::number_string(v) + ", outside the declared range [" +
                                         Detail::number_string(port.range->lo) + ", " +
                                         Detail::number_string(port.range->hi) + "]",
                                     coord);
                }
            });
            if (!o.passed) {
                return o;
            }
        }
        return PropertyOutcome();
    });
}

// ---------------------------------------------------------------------------
// check_property
// ---------------------------------------------------------------------------

/** The outcome of check_property(). On failure, `failing_seed` is the seed of
 * the failing trial: `check_property(..., trials = 1, seed = failing_seed)`
 * reproduces it exactly. */
struct PropertyResult {
    bool passed = true;
    std::string property;
    /** The label of the stage checked (the root's label if not stage-targeted). */
    std::string stage_label;
    /** The number of trials run, including the failing one. */
    int trials_run = 0;
    uint64_t failing_seed = 0;
    /** The failing coordinate in the values checked (empty if the failure
     * has none). */
    std::vector<int> failing_coord;
    std::string message;
    /** True if the trial failed because its inputs lay outside the declared
     * input ranges of the stage checked, rather than because the property
     * itself failed. */
    bool precondition_violated = false;
};

inline std::ostream &operator<<(std::ostream &s, const PropertyResult &r) {
    s << "property " << r.property << " on '" << r.stage_label << "': ";
    if (r.passed) {
        return s << "passed (" << r.trials_run << " trial" << (r.trials_run == 1 ? "" : "s") << ")\n";
    }
    s << "FAILED in trial " << r.trials_run << " (seed " << r.failing_seed << ")";
    if (!r.failing_coord.empty()) {
        s << " at " << Detail::coord_string(r.failing_coord);
    }
    return s << ": " << r.message << "\n";
}

struct PropertyOptions {
    /** Preconditions: the values reaching the stage checked (the generated
     * inputs, or for `.at(stage)` the arriving values) are compared with that
     * stage's declared input ranges. If true (the default), a trial with a
     * value outside them is *not run* and fails with
     * PropertyResult::precondition_violated set: the property is only claimed
     * where its preconditions hold, so a generator (or upstream stage) that
     * breaks them is reported as such. If false, the property runs anyway,
     * which is how to demonstrate that it fails outside its preconditions. */
    bool check_preconditions = true;
};

using InputGenerator = std::function<std::vector<Buffer<>>(uint64_t seed)>;

namespace Detail {

// Compare values with declared preconditions; the empty string if they hold.
inline std::string precondition_problem(const ApproximationPorts &required, const std::vector<Buffer<>> &values,
                                        std::vector<int> &coord) {
    for (size_t i = 0; i < required.size() && i < values.size(); i++) {
        if (!required[i].range) {
            continue;
        }
        const ApproximationRange &range = *required[i].range;
        std::string problem;
        for_each_coord(values[i], [&](const int *pos) {
            double v = get(values[i], pos);
            if (problem.empty() && !range.contains(v)) {
                coord.assign(pos, pos + values[i].dimensions());
                problem = "precondition not met: input '" + required[i].name + "' is " + number_string(v) +
                          " at " + coord_string(coord) + ", outside the declared range [" +
                          number_string(range.lo) + ", " + number_string(range.hi) + "]";
            }
        });
        if (!problem.empty()) {
            return problem;
        }
    }
    return "";
}

}  // namespace Detail

/** Check `prop` on `trials` trials, each on freshly generated inputs (from
 * `gen`, given the trial's seed, which is derive_seed(seed, trial)), and stop
 * at the first failure.
 *
 * For a plain property, the property is about `a` on the generated inputs.
 * For prop.at(stage), `a` is run on the generated inputs, the values that
 * arrive at `stage`'s encode inputs are read back, and the property is
 * evaluated for `stage` alone (encode, then decode) on those values. Either
 * way the values that go into the checked unit are first compared with its
 * declared input ranges (see PropertyOptions). */
inline PropertyResult check_property(const Approximation &a, const Property &prop, const InputGenerator &gen,
                                     int trials, uint64_t seed, const PropertyOptions &options = {}) {
    PropertyResult result;
    result.property = prop.name();
    const Approximation &target = prop.stage().defined() ? prop.stage() : a;
    result.stage_label = target.label();

    for (int trial = 0; trial < trials; trial++) {
        const uint64_t trial_seed = Halide::ApproximationTesting::derive_seed(seed, trial);
        result.trials_run = trial + 1;
        std::vector<Buffer<>> inputs = gen(trial_seed);

        PropertyContext ctx;
        std::vector<Buffer<>> stage_inputs;
        std::vector<std::string> stage_names;
        if (prop.stage().defined()) {
            Detail::RoundTripRun root = Detail::run_round_trip(a, inputs, {}, true);
            int count = 0;
            const ApproximationTraceNode *node = Detail::find_node(root.enc.trace, prop.stage(), count);
            user_assert(count == 1) << "check_property: the stage '" << prop.stage().label() << "' was invoked "
                                    << count << " times by the encode of '" << a.label() << "' (expected once)\n";
            for (const Func &f : node->inputs) {
                stage_inputs.push_back(root.value_of(f));
            }
            stage_names = node->input_names;
        } else {
            stage_inputs = inputs;
        }

        if (options.check_preconditions) {
            ApproximationPorts context;
            for (const std::string &name : stage_names) {
                context.emplace_back(name);
            }
            ApproximationSignature sig = target.signature(context);
            std::vector<int> coord;
            std::string problem = sig.known ? Detail::precondition_problem(sig.inputs, stage_inputs, coord) : "";
            if (!problem.empty()) {
                result.passed = false;
                result.failing_seed = trial_seed;
                result.failing_coord = coord;
                result.message = problem;
                result.precondition_violated = true;
                return result;
            }
        }

        ctx.approximation = target;
        ctx.inputs = stage_inputs;
        ctx.input_names = stage_names;
        ctx.run = Detail::run_round_trip(target, stage_inputs, stage_names, false);
        PropertyOutcome outcome = prop.check()(ctx);
        if (!outcome.passed) {
            result.passed = false;
            result.failing_seed = trial_seed;
            result.failing_coord = outcome.coord;
            result.message = outcome.message;
            return result;
        }
    }
    return result;
}

/** As above, generating one buffer per InputSpec (input k is seeded with
 * derive_seed(trial_seed, k)). */
inline PropertyResult check_property(const Approximation &a, const Property &prop,
                                     const std::vector<InputSpec> &specs, int trials = 8, uint64_t seed = 0,
                                     const PropertyOptions &options = {}) {
    InputGenerator gen = [specs](uint64_t s) {
        std::vector<Buffer<>> inputs;
        for (size_t k = 0; k < specs.size(); k++) {
            inputs.push_back(generate(specs[k].dist, specs[k].type, specs[k].extents,
                                      Halide::ApproximationTesting::derive_seed(s, k)));
        }
        return inputs;
    };
    return check_property(a, prop, gen, trials, seed, options);
}

/** As above, for a single input of type `type`, drawn from `dist`. */
inline PropertyResult check_property(const Approximation &a, const Property &prop, const Distribution &dist,
                                     Type type, std::vector<int> extents, int trials = 8, uint64_t seed = 0,
                                     const PropertyOptions &options = {}) {
    return check_property(a, prop, std::vector<InputSpec>{{type, std::move(extents), dist}}, trials, seed, options);
}

/** The default generator: one input per declared root input port, all with
 * the given extents, each drawn from Distribution::from_port() -- uniform
 * within the port's declared range, so the property is exercised exactly
 * where its preconditions hold. An input port without a declared type is
 * generated as float32. The approximation must have a known signature. */
inline PropertyResult check_property(const Approximation &a, const Property &prop, std::vector<int> extents,
                                     int trials = 8, uint64_t seed = 0, const PropertyOptions &options = {}) {
    ApproximationSignature sig = a.signature();
    user_assert(sig.known && !sig.inputs.empty())
        << "check_property: '" << a.label() << "' has no declared input ports to generate from; pass InputSpecs\n";
    std::vector<InputSpec> specs;
    for (const ApproximationPort &port : sig.inputs) {
        Type type = port.type.value_or(Float(32));
        specs.push_back({type, extents, Distribution::from_port(port, type)});
    }
    return check_property(a, prop, specs, trials, seed, options);
}

}  // namespace ApproximationTesting
}  // namespace Halide

#endif
