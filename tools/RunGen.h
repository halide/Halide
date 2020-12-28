#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <vector>

namespace Halide {
namespace RunGen {

using ::Halide::Runtime::Buffer;

// Buffer<> uses "shape" to mean "array of halide_dimension_t", but doesn't
// provide a typedef for it (and doesn't use a vector for it in any event).
using Shape = std::vector<halide_dimension_t>;

// A ShapePromise is a function that returns a Shape. If the Promise can't
// return a valid Shape, it may fail. This allows us to defer error reporting
// for situations until the Shape is actually needed; in particular, it allows
// us to attempt doing bounds-query for the shape of input buffers early,
// but to ignore the error unless we actually need it... which we won't if an
// estimate is provided for the input in question.
using ShapePromise = std::function<Shape()>;

// Standard stream output for halide_type_t
inline std::ostream &operator<<(std::ostream &stream, const halide_type_t &type) {
    if (type.code == halide_type_uint && type.bits == 1) {
        stream << "bool";
    } else {
        assert(type.code >= 0 && type.code <= 3);
        static const char *const names[4] = {"int", "uint", "float", "handle"};
        stream << names[type.code] << (int)type.bits;
    }
    if (type.lanes > 1) {
        stream << "x" << (int)type.lanes;
    }
    return stream;
}

// Standard stream output for halide_dimension_t
inline std::ostream &operator<<(std::ostream &stream, const halide_dimension_t &d) {
    stream << "[" << d.min << "," << d.extent << "," << d.stride << "]";
    return stream;
}

// Standard stream output for vector<halide_dimension_t>
inline std::ostream &operator<<(std::ostream &stream, const Shape &shape) {
    stream << "[";
    bool need_comma = false;
    for (auto &d : shape) {
        if (need_comma) {
            stream << ",";
        }
        stream << d;
        need_comma = true;
    }
    stream << "]";
    return stream;
}

// Bottleneck all our logging so that client code can override any/all of them.
struct Logger {
    using LogFn = std::function<void(const std::string &)>;

    const LogFn out, info, warn, fail;

    Logger()
        : out(log_out), info(log_cerr), warn(log_cerr), fail(log_fail) {
    }
    Logger(LogFn o, LogFn i, LogFn w, LogFn f)
        : out(std::move(o)), info(std::move(i)), warn(std::move(w)), fail(std::move(f)) {
    }

private:
    static void log_out(const std::string &s) {
        std::cout << s;
    }

    static void log_cerr(const std::string &s) {
        std::cerr << s;
    }

    static void log_fail(const std::string &s) {
        log_cerr(s);
        abort();
    }
};

// Client code must provide a definition of Halide::Runtime::log();
// it is sufficient to merely return a default Logger instance.
extern Logger log();

// Gather up all output in a stringstream, emit in the dtor
struct LogEmitter {
    template<typename T>
    LogEmitter &operator<<(const T &x) {
        msg << x;
        return *this;
    }

    ~LogEmitter() {
        std::string s = msg.str();
        if (s.back() != '\n') {
            s += '\n';
        }
        f(s);
    }

protected:
    explicit LogEmitter(Logger::LogFn f)
        : f(std::move(f)) {
    }

private:
    const Logger::LogFn f;
    std::ostringstream msg;
};

// Emit ordinary non-error output that should never be suppressed (ie, stdout)
struct out : LogEmitter {
    out()
        : LogEmitter(log().out) {
    }
};

// Log detailed informational output
struct info : LogEmitter {
    info()
        : LogEmitter(log().info) {
    }
};

// Log warnings
struct warn : LogEmitter {
    warn()
        : LogEmitter(log().warn) {
    }
};

// Log unrecoverable errors, then abort
struct fail : LogEmitter {
    fail()
        : LogEmitter(log().fail) {
    }
};

// Replace the failure handlers from halide_image_io to fail()
inline bool IOCheckFail(bool condition, const char *msg) {
    if (!condition) {
        fail() << "Error in I/O: " << msg;
    }
    return condition;
}

inline std::vector<std::string> split_string(const std::string &source,
                                             const std::string &delim) {
    std::vector<std::string> elements;
    size_t start = 0;
    size_t found = 0;
    while ((found = source.find(delim, start)) != std::string::npos) {
        elements.push_back(source.substr(start, found - start));
        start = found + delim.size();
    }

    // If start is exactly source.size(), the last thing in source is a
    // delimiter, in which case we want to add an empty std::string to elements.
    if (start <= source.size()) {
        elements.push_back(source.substr(start, std::string::npos));
    }
    return elements;
}

// Must be constexpr to allow use in case clauses.
inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return (((int)code) << 8) | bits;
}

// dynamic_type_dispatch is a utility for functors that want to be able
// to dynamically dispatch a halide_type_t to type-specialized code.
// To use it, a functor must be a *templated* class, e.g.
//
//     template<typename T> class MyFunctor { int operator()(arg1, arg2...); };
//
// dynamic_type_dispatch() is called with a halide_type_t as the first argument,
// followed by the arguments to the Functor's operator():
//
//     auto result = dynamic_type_dispatch<MyFunctor>(some_halide_type, arg1, arg2);
//
// Note that this means that the functor must be able to instantiate its
// operator() for all the Halide scalar types; it also means that all those
// variants *will* be instantiated (increasing code size), so this approach
// should only be used when strictly necessary.
template<template<typename> class Functor, typename... Args>
auto dynamic_type_dispatch(const halide_type_t &type, Args &&... args) -> decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE)  \
    case halide_type_code(CODE, BITS): \
        return Functor<TYPE>()(std::forward<Args>(args)...);
    switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        HANDLE_CASE(halide_type_float, 32, float)
        HANDLE_CASE(halide_type_float, 64, double)
        HANDLE_CASE(halide_type_int, 8, int8_t)
        HANDLE_CASE(halide_type_int, 16, int16_t)
        HANDLE_CASE(halide_type_int, 32, int32_t)
        HANDLE_CASE(halide_type_int, 64, int64_t)
        HANDLE_CASE(halide_type_uint, 1, bool)
        HANDLE_CASE(halide_type_uint, 8, uint8_t)
        HANDLE_CASE(halide_type_uint, 16, uint16_t)
        HANDLE_CASE(halide_type_uint, 32, uint32_t)
        HANDLE_CASE(halide_type_uint, 64, uint64_t)
        HANDLE_CASE(halide_type_handle, 64, void *)
    default:
        fail() << "Unsupported type: " << type << "\n";
        using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
        return ReturnType();
    }
#undef HANDLE_CASE
}

// Functor to parse a string into one of the known Halide scalar types.
template<typename T>
struct ScalarParser {
    bool operator()(const std::string &str, halide_scalar_value_t *v) {
        std::istringstream iss(str);
        // std::setbase(0) means "infer base from input", and allows hex and octal constants
        iss >> std::setbase(0) >> *(T *)v;
        return !iss.fail() && iss.get() == EOF;
    }
};

// Override for int8 and uint8, to avoid parsing as char variants
template<>
inline bool ScalarParser<int8_t>::operator()(const std::string &str, halide_scalar_value_t *v) {
    std::istringstream iss(str);
    int i;
    iss >> std::setbase(0) >> i;
    if (!(!iss.fail() && iss.get() == EOF) || i < -128 || i > 127) {
        return false;
    }
    v->u.i8 = (int8_t)i;
    return true;
}

template<>
inline bool ScalarParser<uint8_t>::operator()(const std::string &str, halide_scalar_value_t *v) {
    std::istringstream iss(str);
    unsigned int u;
    iss >> std::setbase(0) >> u;
    if (!(!iss.fail() && iss.get() == EOF) || u > 255) {
        return false;
    }
    v->u.u8 = (uint8_t)u;
    return true;
}

// Override for bool, since istream just expects '1' or '0'.
template<>
inline bool ScalarParser<bool>::operator()(const std::string &str, halide_scalar_value_t *v) {
    if (str == "true") {
        v->u.b = true;
        return true;
    }
    if (str == "false") {
        v->u.b = false;
        return true;
    }
    return false;
}

// Override for handle, since we only accept "nullptr".
template<>
inline bool ScalarParser<void *>::operator()(const std::string &str, halide_scalar_value_t *v) {
    if (str == "nullptr") {
        v->u.handle = nullptr;
        return true;
    }
    return false;
}

// Parse a scalar when we know the corresponding C++ type at compile time.
template<typename T>
inline bool parse_scalar(const std::string &str, T *scalar) {
    return ScalarParser<T>()(str, (halide_scalar_value_t *)scalar);
}

// Dynamic-dispatch wrapper around ScalarParser.
inline bool parse_scalar(const halide_type_t &type,
                         const std::string &str,
                         halide_scalar_value_t *scalar) {
    return dynamic_type_dispatch<ScalarParser>(type, str, scalar);
}

// Parse an extent list, which should be of the form
//
//    [extent0, extent1...]
//
// Return a vector<halide_dimension_t> (aka a "shape") with the extents filled in,
// but with the min of each dimension set to zero and the stride set to the
// planar-default value.
inline Shape parse_extents(const std::string &extent_list) {
    if (extent_list.empty() || extent_list[0] != '[' || extent_list.back() != ']') {
        fail() << "Invalid format for extents: " << extent_list;
    }
    Shape result;
    if (extent_list == "[]") {
        return result;
    }
    std::vector<std::string> extents = split_string(extent_list.substr(1, extent_list.size() - 2), ",");
    for (size_t i = 0; i < extents.size(); i++) {
        const std::string &s = extents[i];
        const int stride = (i == 0) ? 1 : result[i - 1].stride * result[i - 1].extent;
        halide_dimension_t d = {0, 0, stride};
        if (!parse_scalar(s, &d.extent)) {
            fail() << "Invalid value for extents: " << s << " (" << extent_list << ")";
        }
        result.push_back(d);
    }
    return result;
}

// Parse the buffer_estimate list from a given argument's metadata into a Shape.
// If no valid buffer_estimate exists, return false.
inline bool try_parse_metadata_buffer_estimates(const halide_filter_argument_t *md, Shape *shape) {
    if (!md->buffer_estimates) {
        // zero-dimensional buffers don't have (or need) estimates, so don't fail.
        if (md->dimensions == 0) {
            *shape = Shape();
            return true;
        }
        return false;
    }
    Shape result(md->dimensions);
    int32_t stride = 1;
    for (int i = 0; i < md->dimensions; i++) {
        const int64_t *min = md->buffer_estimates[i * 2];
        const int64_t *extent = md->buffer_estimates[i * 2 + 1];
        if (!min || !extent) {
            return false;
            fail() << "Argument " << md->name << " was specified as 'estimate', but no estimate was provided for dimension " << i << " of " << md->dimensions;
        }
        result[i] = halide_dimension_t{(int32_t)*min, (int32_t)*extent, stride};
        stride *= result[i].extent;
    }
    *shape = result;
    return true;
};

// Parse the buffer_estimate list from a given argument's metadata into a Shape.
// If no valid buffer_estimate exists, fail.
inline Shape parse_metadata_buffer_estimates(const halide_filter_argument_t *md) {
    Shape shape;
    if (!try_parse_metadata_buffer_estimates(md, &shape)) {
        fail() << "Argument " << md->name << " was specified as 'estimate', but no valid estimates were provided.";
    }
    return shape;
};

// Given a Buffer<>, return its shape in the form of a vector<halide_dimension_t>.
// (Oddly, Buffer<> has no API to do this directly.)
inline Shape get_shape(const Buffer<> &b) {
    Shape s;
    for (int i = 0; i < b.dimensions(); ++i) {
        s.push_back(b.raw_buffer()->dim[i]);
    }
    return s;
}

// Given a type and shape, create a new Buffer<> but *don't* allocate allocate storage for it.
inline Buffer<> make_with_shape(const halide_type_t &type, const Shape &shape) {
    return Buffer<>(type, nullptr, (int)shape.size(), &shape[0]);
}

// Given a type and shape, create a new Buffer<> and allocate storage for it.
// (Oddly, Buffer<> has an API to do this with vector-of-extent, but not vector-of-halide_dimension_t.)
inline Buffer<> allocate_buffer(const halide_type_t &type, const Shape &shape) {
    Buffer<> b = make_with_shape(type, shape);
    if (b.number_of_elements() > 0) {
        b.check_overflow();
        b.allocate();
        b.set_host_dirty();
    }
    return b;
}

inline Shape choose_output_extents(int dimensions, const Shape &defaults) {
    Shape s(dimensions);
    for (int i = 0; i < dimensions; ++i) {
        if ((size_t)i < defaults.size()) {
            s[i] = defaults[i];
        } else {
            // If the defaults don't provide enough dimensions, make a guess.
            s[i].extent = (i < 2 ? 1000 : 4);
        }
    }
    return s;
}

inline void fix_chunky_strides(const Shape &constrained_shape, Shape *new_shape) {
    // Special-case Chunky: most "chunky" generators tend to constrain stride[0]
    // and stride[2] to exact values, leaving stride[1] unconstrained;
    // in practice, we must ensure that stride[1] == stride[0] * extent[0]
    // and stride[0] = extent[2] to get results that are not garbled.
    // This is unpleasantly hacky and will likely need aditional enhancements.
    // (Note that there are, theoretically, other stride combinations that might
    // need fixing; in practice, ~all generators that aren't planar tend
    // to be classically chunky.)
    if (new_shape->size() >= 3 &&
        (*new_shape)[0].extent > 1 &&
        (*new_shape)[1].extent > 1) {
        if (constrained_shape[2].stride == 1) {
            if (constrained_shape[0].stride >= 1) {
                // If we have stride[0] and stride[2] set to obviously-chunky,
                // then force extent[2] to match stride[0].
                (*new_shape)[2].extent = constrained_shape[0].stride;
            } else {
                // If we have stride[2] == 1 but stride[0] < 1,
                // force stride[0] = extent[2]
                (*new_shape)[0].stride = (*new_shape)[2].extent;
            }
            // Ensure stride[1] is reasonable.
            (*new_shape)[1].stride = (*new_shape)[0].extent * (*new_shape)[0].stride;
        }
    }
}

// Return true iff all of the dimensions in the range [first, last] have an extent of <= 1.
inline bool dims_in_range_are_trivial(const Buffer<> &b, int first, int last) {
    for (int d = first; d <= last; ++d) {
        if (b.dim(d).extent() > 1) {
            return false;
        }
    }
    return true;
}

// Add or subtract dimensions to the given buffer to match dims_needed,
// emitting warnings if we do so.
inline Buffer<> adjust_buffer_dims(const std::string &title, const std::string &name,
                                   const int dims_needed, Buffer<> b) {
    const int dims_actual = b.dimensions();
    if (dims_actual > dims_needed) {
        // Warn that we are ignoring dimensions, but only if at least one of the
        // ignored dimensions has extent > 1
        if (!dims_in_range_are_trivial(b, dims_needed, dims_actual - 1)) {
            warn() << "Image for " << title << " \"" << name << "\" has "
                   << dims_actual << " dimensions, but only the first "
                   << dims_needed << " were used; data loss may have occurred.";
        }
        auto old_shape = get_shape(b);
        while (b.dimensions() > dims_needed) {
            b = b.sliced(dims_needed);
        }
        info() << "Shape for " << name << " changed: " << old_shape << " -> " << get_shape(b);
    } else if (dims_actual < dims_needed) {
        warn() << "Image for " << title << " \"" << name << "\" has "
               << dims_actual << " dimensions, but this argument requires at least "
               << dims_needed << " dimensions: adding dummy dimensions of extent 1.";
        auto old_shape = get_shape(b);
        while (b.dimensions() < dims_needed) {
            b = b.embedded(b.dimensions(), 0);
        }
        info() << "Shape for " << name << " changed: " << old_shape << " -> " << get_shape(b);
    }
    return b;
}

// Load a buffer from a pathname, adjusting the type and dimensions to
// fit the metadata's requirements as needed.
inline Buffer<> load_input_from_file(const std::string &pathname,
                                     const halide_filter_argument_t &metadata) {
    Buffer<> b = Buffer<>(metadata.type, 0);
    info() << "Loading input " << metadata.name << " from " << pathname << " ...";
    if (!Halide::Tools::load<Buffer<>, IOCheckFail>(pathname, &b)) {
        fail() << "Unable to load input: " << pathname;
    }
    if (b.dimensions() != metadata.dimensions) {
        b = adjust_buffer_dims("Input", metadata.name, metadata.dimensions, b);
    }
    if (b.type() != metadata.type) {
        warn() << "Image loaded for argument \"" << metadata.name << "\" is type "
               << b.type() << " but this argument expects type "
               << metadata.type << "; data loss may have occurred.";
        b = Halide::Tools::ImageTypeConversion::convert_image(b, metadata.type);
    }
    return b;
}

template<typename T>
struct FillWithRandom {
public:
    void operator()(Buffer<> &b_dynamic, int seed) {
        Buffer<T> b = b_dynamic;
        std::mt19937 rng(seed);
        fill(b, rng);
    }

private:
    template<typename T2 = T,
             typename std::enable_if<std::is_integral<T2>::value && !std::is_same<T2, bool>::value && !std::is_same<T2, char>::value && !std::is_same<T2, signed char>::value && !std::is_same<T2, unsigned char>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<T2> dis;
        b.for_each_value([&rng, &dis](T2 &value) {
            value = dis(rng);
        });
    }

    template<typename T2 = T, typename std::enable_if<std::is_floating_point<T2>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_real_distribution<T2> dis(0.0, 1.0);
        b.for_each_value([&rng, &dis](T2 &value) {
            value = dis(rng);
        });
    }

    template<typename T2 = T, typename std::enable_if<std::is_same<T2, bool>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<int> dis(0, 1);
        b.for_each_value([&rng, &dis](T2 &value) {
            value = static_cast<T2>(dis(rng));
        });
    }

    // std::uniform_int_distribution<char> is UB in C++11,
    // so special-case to avoid compiler variation
    template<typename T2 = T, typename std::enable_if<std::is_same<T2, char>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<int> dis(-128, 127);
        b.for_each_value([&rng, &dis](T2 &value) {
            value = static_cast<T2>(dis(rng));
        });
    }

    // std::uniform_int_distribution<signed char> is UB in C++11,
    // so special-case to avoid compiler variation
    template<typename T2 = T, typename std::enable_if<std::is_same<T2, signed char>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<int> dis(-128, 127);
        b.for_each_value([&rng, &dis](T2 &value) {
            value = static_cast<T2>(dis(rng));
        });
    }

    // std::uniform_int_distribution<unsigned char> is UB in C++11,
    // so special-case to avoid compiler variation
    template<typename T2 = T, typename std::enable_if<std::is_same<T2, unsigned char>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<int> dis(0, 255);
        b.for_each_value([&rng, &dis](T2 &value) {
            value = static_cast<T2>(dis(rng));
        });
    }

    template<typename T2 = T, typename std::enable_if<std::is_pointer<T2>::value>::type * = nullptr>
    void fill(Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<intptr_t> dis;
        b.for_each_value([&rng, &dis](T2 &value) {
            value = reinterpret_cast<T2>(dis(rng));
        });
    }
};

template<typename T>
struct FillWithScalar {
public:
    void operator()(Buffer<> &b_dynamic, const halide_scalar_value_t &value) {
        Buffer<T> b = b_dynamic;
        b.fill(as_T(value));
    }

private:
    // Segregate into pointer and non-pointer clauses to avoid compiler warnings
    // about casting from (e.g.) int8 to void*
    template<typename T2 = T, typename std::enable_if<!std::is_pointer<T2>::value>::type * = nullptr>
    T as_T(const halide_scalar_value_t &value) {
        const halide_type_t type = halide_type_of<T>();
        switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        case halide_type_code(halide_type_int, 8):
            return (T)value.u.i8;
        case halide_type_code(halide_type_int, 16):
            return (T)value.u.i16;
        case halide_type_code(halide_type_int, 32):
            return (T)value.u.i32;
        case halide_type_code(halide_type_int, 64):
            return (T)value.u.i64;
        case halide_type_code(halide_type_uint, 1):
            return (T)value.u.b;
        case halide_type_code(halide_type_uint, 8):
            return (T)value.u.u8;
        case halide_type_code(halide_type_uint, 16):
            return (T)value.u.u16;
        case halide_type_code(halide_type_uint, 32):
            return (T)value.u.u32;
        case halide_type_code(halide_type_uint, 64):
            return (T)value.u.u64;
        case halide_type_code(halide_type_float, 32):
            return (T)value.u.f32;
        case halide_type_code(halide_type_float, 64):
            return (T)value.u.f64;
        default:
            fail() << "Can't convert value with type: " << (int)type.code << "bits: " << type.bits;
            return (T)0;
        }
    }

    template<typename T2 = T, typename std::enable_if<std::is_pointer<T2>::value>::type * = nullptr>
    T as_T(const halide_scalar_value_t &value) {
        const halide_type_t type = halide_type_of<T>();
        switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        case halide_type_code(halide_type_handle, 64):
            return (T)value.u.handle;
        default:
            fail() << "Can't convert value with type: " << (int)type.code << "bits: " << type.bits;
            return (T)0;
        }
    }
};

// This logic exists in Halide::Tools, but is Internal; we're going to replicate
// it here for now since we may want slightly different logic in some cases
// for this tool.
inline Halide::Tools::FormatInfo best_save_format(const Buffer<> &b, const std::set<Halide::Tools::FormatInfo> &info) {
    // Perfect score is zero (exact match).
    // The larger the score, the worse the match.
    int best_score = 0x7fffffff;
    Halide::Tools::FormatInfo best{};
    const halide_type_t type = b.type();
    const int dimensions = b.dimensions();
    for (auto &f : info) {
        int score = 0;
        // If format has too-few dimensions, that's very bad.
        score += std::abs(f.dimensions - dimensions) * 128;
        // If format has too-few bits, that's pretty bad.
        score += std::abs(f.type.bits - type.bits);
        // If format has different code, that's a little bad.
        score += (f.type.code != type.code) ? 1 : 0;
        if (score < best_score) {
            best_score = score;
            best = f;
        }
    }

    return best;
}

inline std::string scalar_to_string(const halide_type_t &type,
                                    const halide_scalar_value_t &value) {
    std::ostringstream o;
    switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
    case halide_type_code(halide_type_float, 32):
        o << value.u.f32;
        break;
    case halide_type_code(halide_type_float, 64):
        o << value.u.f64;
        break;
    case halide_type_code(halide_type_int, 8):
        o << (int)value.u.i8;
        break;
    case halide_type_code(halide_type_int, 16):
        o << value.u.i16;
        break;
    case halide_type_code(halide_type_int, 32):
        o << value.u.i32;
        break;
    case halide_type_code(halide_type_int, 64):
        o << value.u.i64;
        break;
    case halide_type_code(halide_type_uint, 1):
        o << (value.u.b ? "true" : "false");
        break;
    case halide_type_code(halide_type_uint, 8):
        o << (int)value.u.u8;
        break;
    case halide_type_code(halide_type_uint, 16):
        o << value.u.u16;
        break;
    case halide_type_code(halide_type_uint, 32):
        o << value.u.u32;
        break;
    case halide_type_code(halide_type_uint, 64):
        o << value.u.u64;
        break;
    case halide_type_code(halide_type_handle, 64):
        o << (uint64_t)value.u.handle;
        break;
    default:
        fail() << "Unsupported type: " << type << "\n";
        break;
    }
    return o.str();
}

struct ArgData {
    size_t index{0};
    std::string name;
    const halide_filter_argument_t *metadata{nullptr};
    std::string raw_string;
    halide_scalar_value_t scalar_value;
    Buffer<> buffer_value;

    ArgData() = default;

    ArgData(size_t index, const std::string &name, const halide_filter_argument_t *metadata)
        : index(index), name(name), metadata(metadata) {
    }

    Buffer<> load_buffer(ShapePromise shape_promise, const halide_filter_argument_t *argument_metadata) {
        const auto parse_optional_extents = [&](const std::string &s) -> Shape {
            if (s == "auto") {
                return shape_promise();
            }
            if (s == "estimate") {
                return parse_metadata_buffer_estimates(argument_metadata);
            }
            if (s == "estimate_then_auto") {
                Shape shape;
                if (!try_parse_metadata_buffer_estimates(argument_metadata, &shape)) {
                    info() << "Input " << argument_metadata->name << " has no estimates; using bounds-query result instead.";
                    shape = shape_promise();
                }
                return shape;
            }
            return parse_extents(s);
        };

        std::vector<std::string> v = split_string(raw_string, ":");
        if (v[0] == "zero") {
            if (v.size() != 2) {
                fail() << "Invalid syntax: " << raw_string;
            }
            auto shape = parse_optional_extents(v[1]);
            Buffer<> b = allocate_buffer(metadata->type, shape);
            memset(b.data(), 0, b.size_in_bytes());
            return b;
        } else if (v[0] == "constant") {
            if (v.size() != 3) {
                fail() << "Invalid syntax: " << raw_string;
            }
            halide_scalar_value_t value;
            if (!parse_scalar(metadata->type, v[1], &value)) {
                fail() << "Invalid value for constant value";
            }
            auto shape = parse_optional_extents(v[2]);
            Buffer<> b = allocate_buffer(metadata->type, shape);
            dynamic_type_dispatch<FillWithScalar>(metadata->type, b, value);
            return b;
        } else if (v[0] == "identity") {
            if (v.size() != 2) {
                fail() << "Invalid syntax: " << raw_string;
            }
            auto shape = parse_optional_extents(v[1]);
            // Make a binary buffer with diagonal elements set to true. Diagonal
            // elements are those whose first two dimensions are equal.
            Buffer<bool> b = allocate_buffer(halide_type_of<bool>(), shape);
            b.for_each_element([&b](const int *pos) {
                b(pos) = (b.dimensions() >= 2) ? (pos[0] == pos[1]) : (pos[0] == 0);
            });
            // Convert the binary buffer to the required type, so true becomes 1.
            return Halide::Tools::ImageTypeConversion::convert_image(b, metadata->type);
        } else if (v[0] == "random") {
            if (v.size() != 3) {
                fail() << "Invalid syntax: " << raw_string;
            }
            int seed;
            if (!parse_scalar(v[1], &seed)) {
                fail() << "Invalid value for seed";
            }
            auto shape = parse_optional_extents(v[2]);
            Buffer<> b = allocate_buffer(metadata->type, shape);
            dynamic_type_dispatch<FillWithRandom>(metadata->type, b, seed);
            return b;
        } else {
            return load_input_from_file(v[0], *metadata);
        }
    }

    Buffer<> load_buffer(const Shape &shape, const halide_filter_argument_t *argument_metadata) {
        ShapePromise promise = [shape]() -> Shape { return shape; };
        return load_buffer(promise, argument_metadata);
    }

    void adapt_input_buffer(const Shape &constrained_shape) {
        if (metadata->kind != halide_argument_kind_input_buffer) {
            return;
        }

        // Ensure that the input Buffer meets our constraints; if it doesn't, allcoate
        // and copy into a new Buffer.
        bool updated = false;
        Shape new_shape = get_shape(buffer_value);
        info() << "Input " << name << ": Shape is " << new_shape;
        if (new_shape.size() != constrained_shape.size()) {
            fail() << "Dimension mismatch; expected " << constrained_shape.size() << "dimensions";
        }
        for (size_t i = 0; i < constrained_shape.size(); ++i) {
            // If the constrained shape is not in bounds of the
            // buffer's current shape we need to use the constrained
            // shape.
            int current_min = new_shape[i].min;
            int current_max = new_shape[i].min + new_shape[i].extent - 1;
            int constrained_min = constrained_shape[i].min;
            int constrained_max = constrained_shape[i].min + constrained_shape[i].extent - 1;
            if (constrained_min < current_min || constrained_max > current_max) {
                new_shape[i].min = constrained_shape[i].min;
                new_shape[i].extent = constrained_shape[i].extent;
                updated = true;
            }
            // stride of nonzero means "required stride", stride of zero means "no constraints"
            if (constrained_shape[i].stride != 0 && new_shape[i].stride != constrained_shape[i].stride) {
                new_shape[i].stride = constrained_shape[i].stride;
                updated = true;
            }
        }
        if (updated) {
            fix_chunky_strides(constrained_shape, &new_shape);
            Buffer<> new_buf = allocate_buffer(buffer_value.type(), new_shape);
            new_buf.copy_from(buffer_value);
            buffer_value = new_buf;
        }

        info() << "Input " << name << ": BoundsQuery result is " << constrained_shape;
        if (updated) {
            info() << "Input " << name << ": Updated Shape is " << get_shape(buffer_value);
        }
    }

    void allocate_output_buffer(const Shape &constrained_shape) {
        if (metadata->kind != halide_argument_kind_output_buffer) {
            return;
        }

        // Given a constraint Shape (generally produced by a bounds query), create a new
        // Shape that can legally be used to create and allocate a new Buffer:
        // ensure that extents/strides aren't zero, do some reality checking
        // on planar vs interleaved, and generally try to guess at a reasonable result.
        Shape new_shape = constrained_shape;

        // Make sure that the extents and strides for these are nonzero.
        for (size_t i = 0; i < new_shape.size(); ++i) {
            if (!new_shape[i].extent) {
                // A bit of a hack: fill in unconstrained dimensions to 1... except
                // for probably-the-channels dimension, which we'll special-case to
                // fill in to 4 when possible (unless it appears to be chunky).
                // Stride will be fixed below.
                if (i == 2) {
                    if (constrained_shape[0].stride >= 1 && constrained_shape[2].stride == 1) {
                        // Definitely chunky, so make extent[2] match the chunk size
                        new_shape[i].extent = constrained_shape[0].stride;
                    } else {
                        // Not obviously chunky; let's go with 4 channels.
                        new_shape[i].extent = 4;
                    }
                } else {
                    new_shape[i].extent = 1;
                }
            }
        }

        fix_chunky_strides(constrained_shape, &new_shape);

        // If anything else is zero, just set strides to planar and hope for the best.
        bool any_strides_zero = false;
        for (size_t i = 0; i < new_shape.size(); ++i) {
            if (!new_shape[i].stride) {
                any_strides_zero = true;
            }
        }
        if (any_strides_zero) {
            // Planar
            new_shape[0].stride = 1;
            for (size_t i = 1; i < new_shape.size(); ++i) {
                new_shape[i].stride = new_shape[i - 1].stride * new_shape[i - 1].extent;
            }
        }

        buffer_value = allocate_buffer(metadata->type, new_shape);

        // allocate_buffer conservatively sets host dirty. Don't waste
        // time copying output buffers to device.
        buffer_value.set_host_dirty(false);

        info() << "Output " << name << ": BoundsQuery result is " << constrained_shape;
        info() << "Output " << name << ": Shape is " << get_shape(buffer_value);
    }
};

class RunGen {
public:
    using ArgvCall = int (*)(void **);

    RunGen(ArgvCall halide_argv_call,
           const struct halide_filter_metadata_t *halide_metadata)
        : halide_argv_call(halide_argv_call), md(halide_metadata) {
        if (md->version != halide_filter_metadata_t::VERSION) {
            fail() << "Unexpected metadata version " << md->version;
        }
        for (size_t i = 0; i < (size_t)md->num_arguments; ++i) {
            std::string name = md->arguments[i].name;
            if (name.size() > 2 && name[name.size() - 2] == '$' && isdigit(name[name.size() - 1])) {
                // If it ends in "$3" or similar, just lop it off
                name = name.substr(0, name.size() - 2);
            }
            ArgData arg(i, name, &md->arguments[i]);
            args[name] = arg;
        }
        halide_set_error_handler(rungen_halide_error);
        halide_set_custom_print(rungen_halide_print);
    }

    ArgvCall get_halide_argv_call() const {
        return halide_argv_call;
    }
    const struct halide_filter_metadata_t *get_halide_metadata() const {
        return md;
    }

    int argument_kind(const std::string &name) const {
        auto it = args.find(name);
        if (it == args.end()) {
            return -1;
        }
        return it->second.metadata->kind;
    }

    void parse_one(const std::string &name,
                   const std::string &value,
                   std::set<std::string> *seen_args) {
        if (value.empty()) {
            fail() << "Argument value is empty for: " << name;
        }
        seen_args->insert(name);
        auto it = args.find(name);
        if (it == args.end()) {
            // Don't fail, just return.
            return;
        }
        if (!it->second.raw_string.empty()) {
            fail() << "Argument value specified multiple times for: " << name;
        }
        it->second.raw_string = value;
    }

    void validate(const std::set<std::string> &seen_args,
                  const std::string &default_input_buffers,
                  const std::string &default_input_scalars,
                  bool ok_to_omit_outputs) {
        std::ostringstream o;
        for (auto &s : seen_args) {
            if (args.find(s) == args.end()) {
                o << "Unknown argument name: " << s << "\n";
            }
        }
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            if (arg.raw_string.empty()) {
                if (ok_to_omit_outputs && arg.metadata->kind == halide_argument_kind_output_buffer) {
                    continue;
                }
                if (!default_input_buffers.empty() &&
                    arg.metadata->kind == halide_argument_kind_input_buffer) {
                    arg.raw_string = default_input_buffers;
                    info() << "Using value of '" << arg.raw_string << "' for: " << arg.metadata->name;
                    continue;
                }
                if (!default_input_scalars.empty() &&
                    arg.metadata->kind == halide_argument_kind_input_scalar) {
                    arg.raw_string = default_input_scalars;
                    info() << "Using value of '" << arg.raw_string << "' for: " << arg.metadata->name;
                    continue;
                }
                o << "Argument value missing for: " << arg.metadata->name << "\n";
            }
        }
        if (!o.str().empty()) {
            fail() << o.str();
        }
    }

    // Parse all the input arguments, loading images as necessary.
    // (Don't handle outputs yet.)
    void load_inputs(const std::string &user_specified_output_shape_string) {
        assert(output_shapes.empty());

        Shape first_input_shape;
        std::map<std::string, ShapePromise> auto_input_shape_promises;

        // First, set all the scalar inputs: we need those to be correct
        // in order to get useful values from the bound-query for input buffers.
        for (auto &arg_pair : args) {
            auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_scalar: {
                if (!strcmp(arg.metadata->name, "__user_context")) {
                    arg.scalar_value.u.handle = nullptr;
                    info() << "Argument value for: __user_context is special-cased as: nullptr";
                    break;
                }
                std::vector<std::pair<const halide_scalar_value_t *, const char *>> values;
                // If this gets any more complex, smarten it up, but for now,
                // simpleminded code is fine.
                if (arg.raw_string == "default") {
                    values.emplace_back(arg.metadata->scalar_def, "default");
                } else if (arg.raw_string == "estimate") {
                    values.emplace_back(arg.metadata->scalar_estimate, "estimate");
                } else if (arg.raw_string == "default,estimate") {
                    values.emplace_back(arg.metadata->scalar_def, "default");
                    values.emplace_back(arg.metadata->scalar_estimate, "estimate");
                } else if (arg.raw_string == "estimate,default") {
                    values.emplace_back(arg.metadata->scalar_estimate, "estimate");
                    values.emplace_back(arg.metadata->scalar_def, "default");
                }
                if (!values.empty()) {
                    bool set = false;
                    for (auto &v : values) {
                        if (!v.first) {
                            continue;
                        }
                        info() << "Argument value for: " << arg.metadata->name << " is parsed from metadata (" << v.second << ") as: "
                               << scalar_to_string(arg.metadata->type, *v.first);
                        arg.scalar_value = *v.first;
                        set = true;
                        break;
                    }
                    if (!set) {
                        fail() << "Argument value for: " << arg.metadata->name << " was specified as '" << arg.raw_string << "', "
                               << "but no default and/or estimate was found in the metadata.";
                    }
                } else {
                    if (!parse_scalar(arg.metadata->type, arg.raw_string, &arg.scalar_value)) {
                        fail() << "Argument value for: " << arg_name << " could not be parsed as type "
                               << arg.metadata->type << ": "
                               << arg.raw_string;
                    }
                }
                break;
            }
            case halide_argument_kind_input_buffer:
            case halide_argument_kind_output_buffer:
                // Nothing yet
                break;
            }
        }

        if (!user_specified_output_shape_string.empty()) {
            // For now, we set all output shapes to be identical -- there's no
            // way on the command line to specify different shapes for each
            // output. Would be nice to try?
            for (auto &arg_pair : args) {
                auto &arg = arg_pair.second;
                if (arg.metadata->kind == halide_argument_kind_output_buffer) {
                    auto &arg_name = arg_pair.first;
                    if (user_specified_output_shape_string == "estimate") {
                        output_shapes[arg_name] = parse_metadata_buffer_estimates(arg.metadata);
                        info() << "Output " << arg_name << " is parsed from metadata as: " << output_shapes[arg_name];
                    } else {
                        output_shapes[arg_name] = parse_extents(user_specified_output_shape_string);
                        info() << "Output " << arg_name << " has user-specified Shape: " << output_shapes[arg_name];
                    }
                }
            }
            auto_input_shape_promises = bounds_query_input_shapes();
        }

        for (auto &arg_pair : args) {
            auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_buffer:
                arg.buffer_value = arg.load_buffer(auto_input_shape_promises[arg_name], arg.metadata);
                info() << "Input " << arg_name << ": Shape is " << get_shape(arg.buffer_value);
                if (first_input_shape.empty()) {
                    first_input_shape = get_shape(arg.buffer_value);
                }
                break;
            case halide_argument_kind_input_scalar:
                // Already handled.
                break;
            case halide_argument_kind_output_buffer:
                // Nothing yet
                break;
            }
        }

        if (user_specified_output_shape_string.empty() && !first_input_shape.empty()) {
            // If there was no output shape specified by the user, use the shape of
            // the first input buffer (if any). (This is a better-than-nothing guess
            // that is definitely not always correct, but is convenient and useful enough
            // to be worth doing.)
            for (auto &arg_pair : args) {
                auto &arg = arg_pair.second;
                if (arg.metadata->kind == halide_argument_kind_output_buffer) {
                    auto &arg_name = arg_pair.first;
                    output_shapes[arg_name] = first_input_shape;
                    info() << "Output " << arg_name << " assumes the shape of first input: " << first_input_shape;
                }
            }
        }
    }

    void save_outputs() {
        // Save the output(s), if necessary.
        for (auto &arg_pair : args) {
            auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            if (arg.metadata->kind != halide_argument_kind_output_buffer) {
                continue;
            }
            if (arg.raw_string.empty()) {
                info() << "(Output " << arg_name << " was not saved.)";
                continue;
            }

            info() << "Saving output " << arg_name << " to " << arg.raw_string << " ...";
            Buffer<> &b = arg.buffer_value;

            std::set<Halide::Tools::FormatInfo> savable_types;
            if (!Halide::Tools::save_query<Buffer<>, IOCheckFail>(arg.raw_string, &savable_types)) {
                fail() << "Unable to save output: " << arg.raw_string;
            }
            const Halide::Tools::FormatInfo best = best_save_format(b, savable_types);
            if (best.dimensions != b.dimensions()) {
                b = adjust_buffer_dims("Output", arg_name, best.dimensions, b);
            }
            if (best.type != b.type()) {
                warn() << "Image for argument \"" << arg_name << "\" is of type "
                       << b.type() << " but is being saved as type "
                       << best.type << "; data loss may have occurred.";
                b = Halide::Tools::ImageTypeConversion::convert_image(b, best.type);
            }
            if (!Halide::Tools::save<Buffer<const void>, IOCheckFail>(b.as<const void>(), arg.raw_string)) {
                fail() << "Unable to save output: " << arg.raw_string;
            }
        }
    }

    void device_sync_outputs() {
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            if (arg.metadata->kind == halide_argument_kind_output_buffer) {
                Buffer<> &b = arg.buffer_value;
                b.device_sync();
            }
        }
    }

    void copy_outputs_to_host() {
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            if (arg.metadata->kind == halide_argument_kind_output_buffer) {
                Buffer<> &b = arg.buffer_value;
                b.copy_to_host();
            }
        }
    }

    uint64_t pixels_out() const {
        uint64_t pixels_out = 0;
        for (const auto &arg_pair : args) {
            const auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_output_buffer: {
                // TODO: this assumes that most output is "pixel-ish", and counting the size of the first
                // two dimensions approximates the "pixel size". This is not, in general, a valid assumption,
                // but is a useful metric for benchmarking.
                Shape shape = get_shape(arg.buffer_value);
                if (shape.size() >= 2) {
                    pixels_out += shape[0].extent * shape[1].extent;
                } else if (!shape.empty()) {
                    pixels_out += shape[0].extent;
                } else {
                    pixels_out += 1;
                }
                break;
            }
            }
        }
        return pixels_out;
    }

    double megapixels_out() const {
        return (double)pixels_out() / (1024.0 * 1024.0);
    }

    uint64_t elements_out() const {
        uint64_t elements_out = 0;
        for (const auto &arg_pair : args) {
            const auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_output_buffer: {
                elements_out += arg.buffer_value.number_of_elements();
                break;
            }
            }
        }
        return elements_out;
    }

    uint64_t bytes_out() const {
        uint64_t bytes_out = 0;
        for (const auto &arg_pair : args) {
            const auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_output_buffer: {
                // size_in_bytes() is not necessarily the same, since
                // it may include unused space for padding.
                bytes_out += arg.buffer_value.number_of_elements() * arg.buffer_value.type().bytes();
                break;
            }
            }
        }
        return bytes_out;
    }

    // Run a bounds-query call with the given args, and return the shapes
    // to which we are constrained.
    std::vector<Shape> run_bounds_query() const {
        std::vector<void *> filter_argv(args.size(), nullptr);
        // These vectors are larger than needed, but simplifies logic downstream.
        std::vector<Buffer<>> bounds_query_buffers(args.size());
        std::vector<Shape> constrained_shapes(args.size());
        for (const auto &arg_pair : args) {
            const auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_scalar:
                filter_argv[arg.index] = const_cast<halide_scalar_value_t *>(&arg.scalar_value);
                break;
            case halide_argument_kind_input_buffer:
            case halide_argument_kind_output_buffer:
                Shape shape = (arg.metadata->kind == halide_argument_kind_input_buffer) ?
                                  get_shape(arg.buffer_value) :
                                  choose_output_extents(arg.metadata->dimensions, output_shapes.at(arg_name));
                bounds_query_buffers[arg.index] = make_with_shape(arg.metadata->type, shape);
                filter_argv[arg.index] = bounds_query_buffers[arg.index].raw_buffer();
                break;
            }
        }

        info() << "Running bounds query...";
        // Ignore result since our halide_error() should catch everything.
        (void)halide_argv_call(&filter_argv[0]);

        for (const auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_scalar:
                break;
            case halide_argument_kind_input_buffer:
            case halide_argument_kind_output_buffer:
                constrained_shapes[arg.index] = get_shape(bounds_query_buffers[arg.index]);
                break;
            }
        }
        return constrained_shapes;
    }

    void adapt_input_buffers(const std::vector<Shape> &constrained_shapes) {
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            arg.adapt_input_buffer(constrained_shapes[arg.index]);
        }
    }

    void allocate_output_buffers(const std::vector<Shape> &constrained_shapes) {
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            arg.allocate_output_buffer(constrained_shapes[arg.index]);
        }
    }

    void run_for_benchmark(double benchmark_min_time) {
        std::vector<void *> filter_argv = build_filter_argv();

        const auto benchmark_inner = [this, &filter_argv]() {
            // Ignore result since our halide_error() should catch everything.
            (void)halide_argv_call(&filter_argv[0]);
            // Ensure that all outputs are finished, otherwise we may just be
            // measuring how long it takes to do a kernel launch for GPU code.
            this->device_sync_outputs();
        };

        info() << "Benchmarking filter...";

        Halide::Tools::BenchmarkConfig config;
        config.min_time = benchmark_min_time;
        config.max_time = benchmark_min_time * 4;
        auto result = Halide::Tools::benchmark(benchmark_inner, config);

        if (!parsable_output) {
            out() << "Benchmark for " << md->name << " produces best case of " << result.wall_time << " sec/iter (over "
                  << result.samples << " samples, "
                  << result.iterations << " iterations, "
                  << "accuracy " << std::setprecision(2) << (result.accuracy * 100.0) << "%).\n"
                  << "Best output throughput is " << (megapixels_out() / result.wall_time) << " mpix/sec.\n";
        } else {
            out() << md->name << "  BEST_TIME_MSEC_PER_ITER  " << result.wall_time * 1000.f << "\n"
                  << md->name << "  SAMPLES                  " << result.samples << "\n"
                  << md->name << "  ITERATIONS               " << result.iterations << "\n"
                  << md->name << "  TIMING_ACCURACY          " << result.accuracy << "\n"
                  << md->name << "  THROUGHPUT_MPIX_PER_SEC  " << (megapixels_out() / result.wall_time) << "\n"
                  << md->name << "  HALIDE_TARGET            " << md->target << "\n";
        }
    }

    struct Output {
        std::string name;
        Buffer<> actual;
    };
    std::vector<Output> run_for_output() {
        std::vector<void *> filter_argv = build_filter_argv();

        info() << "Running filter...";
        // Ignore result since our halide_error() should catch everything.
        (void)halide_argv_call(&filter_argv[0]);

        std::vector<Output> v;
        for (auto &arg_pair : args) {
            const auto &arg_name = arg_pair.first;
            const auto &arg = arg_pair.second;
            if (arg.metadata->kind != halide_argument_kind_output_buffer) {
                continue;
            }
            v.push_back({arg_name, arg.buffer_value});
        }
        return v;
    }

    Buffer<> get_expected_output(const std::string &output) {
        auto it = args.find(output);
        if (it == args.end()) {
            fail() << "Unable to find output: " << output;
        }
        const auto &arg = it->second;
        return args.at(output).load_buffer(output_shapes.at(output), arg.metadata);
    }

    void describe() const {
        out() << "Filter name: \"" << md->name << "\"\n";
        for (size_t i = 0; i < (size_t)md->num_arguments; ++i) {
            std::ostringstream o;
            auto &a = md->arguments[i];
            bool is_input = a.kind != halide_argument_kind_output_buffer;
            bool is_scalar = a.kind == halide_argument_kind_input_scalar;
            o << "  " << (is_input ? "Input" : "Output") << " \"" << a.name << "\" is of type ";
            if (is_scalar) {
                o << a.type;
            } else {
                o << "Buffer<" << a.type << "> with " << a.dimensions << " dimensions";
            }
            out() << o.str();
        }
    }

    std::vector<void *> build_filter_argv() {
        std::vector<void *> filter_argv(args.size(), nullptr);
        for (auto &arg_pair : args) {
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_scalar:
                filter_argv[arg.index] = &arg.scalar_value;
                break;
            case halide_argument_kind_input_buffer:
            case halide_argument_kind_output_buffer:
                filter_argv[arg.index] = arg.buffer_value.raw_buffer();
                break;
            }
        }
        return filter_argv;
    }

    std::string name() const {
        return md->name;
    }

    void set_quiet(bool quiet = true) {
        halide_set_custom_print(quiet ? rungen_halide_print_quiet : rungen_halide_print);
    }

    void set_parsable_output(bool parsable_output = true) {
        this->parsable_output = parsable_output;
    }

private:
    static void rungen_ignore_error(void *user_context, const char *message) {
        // nothing
    }

    std::map<std::string, ShapePromise> bounds_query_input_shapes() const {
        assert(!output_shapes.empty());
        std::vector<void *> filter_argv(args.size(), nullptr);
        std::vector<Buffer<>> bounds_query_buffers(args.size());
        for (const auto &arg_pair : args) {
            auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            switch (arg.metadata->kind) {
            case halide_argument_kind_input_scalar:
                filter_argv[arg.index] = const_cast<halide_scalar_value_t *>(&arg.scalar_value);
                break;
            case halide_argument_kind_input_buffer:
                // Make a Buffer<> that has the right dimension count and extent=0 for all of them
                bounds_query_buffers[arg.index] = Buffer<>(arg.metadata->type, std::vector<int>(arg.metadata->dimensions, 0));
                filter_argv[arg.index] = bounds_query_buffers[arg.index].raw_buffer();
                break;
            case halide_argument_kind_output_buffer:
                bounds_query_buffers[arg.index] = make_with_shape(arg.metadata->type, output_shapes.at(arg_name));
                filter_argv[arg.index] = bounds_query_buffers[arg.index].raw_buffer();
                break;
            }
        }

        auto previous_error_handler = halide_set_error_handler(rungen_ignore_error);
        int result = halide_argv_call(&filter_argv[0]);
        halide_set_error_handler(previous_error_handler);

        std::map<std::string, ShapePromise> input_shape_promises;
        for (const auto &arg_pair : args) {
            auto &arg_name = arg_pair.first;
            auto &arg = arg_pair.second;
            if (arg.metadata->kind == halide_argument_kind_input_buffer) {
                if (result == 0) {
                    Shape shape = get_shape(bounds_query_buffers[arg.index]);
                    input_shape_promises[arg_name] = [shape]() -> Shape { return shape; };
                    info() << "Input " << arg_name << " has a bounds-query shape of " << shape;
                } else {
                    input_shape_promises[arg_name] = [arg_name]() -> Shape {
                        fail() << "Input " << arg_name << " could not calculate a shape satisfying bounds-query constraints.\n"
                               << "Try relaxing the constraints, or providing an explicit estimate for the input.\n";
                        return Shape();
                    };
                    info() << "Input " << arg_name << " failed bounds-query\n";
                }
            }
        }
        return input_shape_promises;
    }

    // Replace the standard Halide runtime function to capture print output to stdout
    static void rungen_halide_print(void *user_context, const char *message) {
        out() << "halide_print: " << message;
    }

    static void rungen_halide_print_quiet(void *user_context, const char *message) {
        // nothing
    }

    // Replace the standard Halide runtime function to capture Halide errors to fail()
    static void rungen_halide_error(void *user_context, const char *message) {
        fail() << "halide_error: " << message;
    }

    ArgvCall halide_argv_call;
    const struct halide_filter_metadata_t *const md;
    std::map<std::string, ArgData> args;
    std::map<std::string, Shape> output_shapes;
    bool parsable_output = false;
};

}  // namespace RunGen
}  // namespace Halide
