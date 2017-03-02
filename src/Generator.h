#ifndef HALIDE_GENERATOR_H_
#define HALIDE_GENERATOR_H_

/** \file
 *
 * Generator is a class used to encapsulate the building of Funcs in user
 * pipelines. A Generator is agnostic to JIT vs AOT compilation; it can be used for
 * either purpose, but is especially convenient to use for AOT compilation.
 *
 * A Generator explicitly declares the Inputs and Outputs associated for a given
 * pipeline, and separates the code for constructing the outputs from the code from
 * scheduling them. For instance:
 *
 * \code
 *     class Blur : public Generator<Blur> {
 *     public:
 *         Input<Func> input{"input", UInt(16), 2};
 *         Output<Func> output{"output", UInt(16), 2};
 *         void generate() {
 *             blur_x(x, y) = (input(x, y) + input(x+1, y) + input(x+2, y))/3;
 *             blur_y(x, y) = (blur_x(x, y) + blur_x(x, y+1) + blur_x(x, y+2))/3;
 *             output(x, y) = blur(x, y);
 *         }
 *         void schedule() {
 *             blur_y.split(y, y, yi, 8).parallel(y).vectorize(x, 8);
 *             blur_x.store_at(blur_y, y).compute_at(blur_y, yi).vectorize(x, 8);
 *         }
 *     private:
 *         Var x, y, xi, yi;
 *         Func blur_x, blur_y;
 *     };
 * \endcode
 *
 * Halide can compile a Generator into the correct pipeline by introspecting these
 * values and constructing an appropriate signature based on them.
 *
 * A Generator must provide implementations of two methods:
 *
 *   - generate(), which must fill in all Output Func(s), but should not do any
 * scheduling
 *   - schedule(), which should do scheduling for any intermediate and
 * output Funcs
 *
 * Inputs can be any C++ scalar type:
 *
 * \code
 *     Input<float> radius{"radius"};
 *     Input<int32_t> increment{"increment"};
 * \endcode
 *
 * An Input<Func> is (essentially) like an ImageParam, except that it may (or may
 * not) not be backed by an actual buffer, and thus has no defined extents.
 *
 * \code
 *     Input<Func> input{"input", Float(32), 2};
 * \endcode
 *
 * You can optionally make the type and/or dimensions of Input<Func> unspecified,
 * in which case the value is simply inferred from the actual Funcs passed to them.
 * Of course, if you specify an explicit Type or Dimension, we still require the
 * input Func to match, or a compilation error results.
 *
 * \code
 *     Input<Func> input{ "input", 3 };  // require 3-dimensional Func,
 *                                       // but leave Type unspecified
 * \endcode
 *
 * A Generator must explicitly list the output(s) it produces:
 *
 * \code
 *     Output<Func> output{"output", Float(32), 2};
 * \endcode
 *
 * You can specify an output that returns a Tuple by specifying a list of Types:
 *
 * \code
 *     class Tupler : Generator<Tupler> {
 *       Input<Func> input{"input", Int(32), 2};
 *       Output<Func> output{"output", {Float(32), UInt(8)}, 2};
 *       void generate() {
 *         Var x, y;
 *         Expr a = cast<float>(input(x, y));
 *         Expr b = cast<uint8_t>(input(x, y));
 *         output(x, y) = Tuple(a, b);
 *       }
 *     };
 * \endcode
 *
 * You can also specify Output<X> for any scalar type (except for Handle types);
 * this is merely syntactic sugar on top of a zero-dimensional Func, but can be
 * quite handy, especially when used with multiple outputs:
 *
 * \code
 *     Output<float> sum{"sum"};  // equivalent to Output<Func> {"sum", Float(32), 0}
 * \endcode
 *
 * As with Input<Func>, you can optionally make the type and/or dimensions of an
 * Output<Func> unspecified; any unspecified types must be resolved via an
 * implicit GeneratorParam in order to use top-level compilation.
 *
 * You can also declare an *array* of Input or Output, by using an array type
 * as the type parameter:
 *
 * \code
 *     // Takes exactly 3 images and outputs exactly 3 sums.
 *     class SumRowsAndColumns : Generator<SumRowsAndColumns> {
 *       Input<Func[3]> inputs{"inputs", Float(32), 2};
 *       Input<int32_t[2]> extents{"extents"};
 *       Output<Func[3]> sums{"sums", Float(32), 1};
 *       void generate() {
 *         assert(inputs.size() == sums.size());
 *         // assume all inputs are same extent
 *         Expr width = extent[0];
 *         Expr height = extent[1];
 *         for (size_t i = 0; i < inputs.size(); ++i) {
 *           RDom r(0, width, 0, height);
 *           sums[i]() = 0.f;
 *           sums[i]() += inputs[i](r.x, r.y);
 *          }
 *       }
 *     };
 *
 * You can also leave array size unspecified, in which case it will be inferred
 * from the input vector, or (optionally) explicitly specified via a resize()
 * method:
 *
 * \code
 *     class Pyramid : public Generator<Pyramid> {
 *     public:
 *         GeneratorParam<int32_t> levels{"levels", 10};
 *         Input<Func> input{ "input", Float(32), 2 };
 *         Output<Func[]> pyramid{ "pyramid", Float(32), 2 };
 *         void generate() {
 *             pyramid.resize(levels);
 *             pyramid[0](x, y) = input(x, y);
 *             for (int i = 1; i < pyramid.size(); i++) {
 *                 pyramid[i](x, y) = (pyramid[i-1](2*x, 2*y) +
 *                                    pyramid[i-1](2*x+1, 2*y) +
 *                                    pyramid[i-1](2*x, 2*y+1) +
 *                                    pyramid[i-1](2*x+1, 2*y+1))/4;
 *             }
 *         }
 *     };
 * \endcode
 *
 * A Generator can also be customized via compile-time parameters (GeneratorParams
 * or ScheduleParams), which affect code generation. While a GeneratorParam can
 * be used from anywhere inside a Generator (either the generate() or
 * schedule() method), ScheduleParam should be accessed only within the
 * schedule() method. (This is not currently a compile-time error but may become
 * one in the future.)
 *
 * GeneratorParams, ScheduleParams, Inputs, and Outputs are (by convention) always
 * public and always declared at the top of the Generator class, in the order
 *
 * \code
 *     GeneratorParam(s)
 *     ScheduleParam(s)
 *     Input<Func>(s)
 *     Input<non-Func>(s)
 *     Output<Func>(s)
 * \endcode
 *
 * Note that the Inputs and Outputs will appear in the C function call in the order
 * they are declared. All Input<Func> and Output<Func> are represented as buffer_t;
 * all other Input<> are the appropriate C++ scalar type. (GeneratorParams are
 * always referenced by name, not position, so their order is irrelevant.)
 *
 * All Inputs and Outputs must have explicit names, and all such names must match
 * the regex [A-Za-z][A-Za-z_0-9]* (i.e., essentially a C/C++ variable name, with
 * some extra restrictions on underscore use). By convention, the name should match
 * the member-variable name.
 *
 * Generators are added to a global registry to simplify AOT build mechanics; this
 * is done by simply using the HALIDE_REGISTER_GENERATOR macro at global scope:
 *
 * \code
 *      HALIDE_REGISTER_GENERATOR(ExampleGen, "jit_example")
 * \endcode
 *
 * The registered name of the Generator is provided must match the same rules as
 * Input names, above.
 *
 * Note that a Generator is always executed with a specific Target assigned to it,
 * that you can access via the get_target() method. (You should *not* use the
 * global get_target_from_environment(), etc. methods provided in Target.h)
 *
 * (Note that there are older variations of Generator that differ from what's
 * documented above; these are still supported but not described here. See
 * https://github.com/halide/Halide/wiki/Old-Generator-Documentation for
 * more information.)
 */

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Func.h"
#include "Introspection.h"
#include "ObjectInstanceRegistry.h"
#include "Target.h"

namespace Halide {

template<typename T> class Buffer;

namespace Internal {

EXPORT void generator_test();

/**
 * ValueTracker is an internal utility class that attempts to track and flag certain
 * obvious Stub-related errors at Halide compile time: it tracks the constraints set
 * on any Parameter-based argument (i.e., Input<Buffer> and Output<Buffer>) to
 * ensure that incompatible values aren't set.
 *
 * e.g.: if a Generator A requires stride[0] == 1,
 * and Generator B uses Generator A via stub, but requires stride[0] == 4,
 * we should be able to detect this at Halide compilation time, and fail immediately,
 * rather than producing code that fails at runtime and/or runs slowly due to
 * vectorization being unavailable.
 *
 * We do this by tracking the active values at entrance and exit to all user-provided
 * Generator methods (build()/generate()/schedule()); if we ever find more than two unique
 * values active, we know we have a potential conflict. ("two" here because the first
 * value is the default value for a given constraint.)
 *
 * Note that this won't catch all cases:
 * -- JIT compilation has no way to check for conflicts at the top-level
 * -- constraints that match the default value (e.g. if dim(0).set_stride(1) is the
 * first value seen by the tracker) will be ignored, so an explicit requirement set
 * this way can be missed
 *
 * Nevertheless, this is likely to be much better than nothing when composing multiple
 * layers of Stubs in a single fused result.
 */
class ValueTracker {
private:
    std::map<std::string, std::vector<std::vector<Expr>>> values_history;
    const size_t max_unique_values;
public:
    explicit ValueTracker(size_t max_unique_values = 2) : max_unique_values(max_unique_values) {}
    EXPORT void track_values(const std::string &name, const std::vector<Expr> &values);
};

EXPORT std::vector<Expr> parameter_constraints(const Parameter &p);

template <typename T>
NO_INLINE std::string enum_to_string(const std::map<std::string, T> &enum_map, const T& t) {
    for (auto key_value : enum_map) {
        if (t == key_value.second) {
            return key_value.first;
        }
    }
    user_error << "Enumeration value not found.\n";
    return "";
}

template <typename T>
T enum_from_string(const std::map<std::string, T> &enum_map, const std::string& s) {
    auto it = enum_map.find(s);
    user_assert(it != enum_map.end()) << "Enumeration value not found: " << s << "\n";
    return it->second;
}

EXPORT extern const std::map<std::string, Halide::Type> &get_halide_type_enum_map();
inline std::string halide_type_to_enum_string(const Type &t) {
    return enum_to_string(get_halide_type_enum_map(), t);
}

EXPORT extern Halide::LoopLevel get_halide_undefined_looplevel();
EXPORT extern const std::map<std::string, Halide::LoopLevel> &get_halide_looplevel_enum_map();
inline std::string halide_looplevel_to_enum_string(const LoopLevel &loop_level){
    return enum_to_string(get_halide_looplevel_enum_map(), loop_level);
}

// Convert a Halide Type into a string representation of its C source.
// e.g., Int(32) -> "Halide::Int(32)"
EXPORT std::string halide_type_to_c_source(const Type &t);

// Convert a Halide Type into a string representation of its C Source.
// e.g., Int(32) -> "int32_t"
EXPORT std::string halide_type_to_c_type(const Type &t);

/** generate_filter_main() is a convenient wrapper for GeneratorRegistry::create() +
 * compile_to_files(); it can be trivially wrapped by a "real" main() to produce a
 * command-line utility for ahead-of-time filter compilation. */
EXPORT int generate_filter_main(int argc, char **argv, std::ostream &cerr);

// select_type<> is to std::conditional as switch is to if:
// it allows a multiway compile-time type definition via the form
//
//    select_type<cond<condition1, type1>,
//                cond<condition2, type2>,
//                ....
//                cond<conditionN, typeN>>::type
//
// Note that the conditions are evaluated in order; the first evaluating to true
// is chosen.
//
// Note that if no conditions evaluate to true, the resulting type is illegal
// and will produce a compilation error. (You can provide a default by simply
// using cond<true, SomeType> as the final entry.)
template<bool B, typename T>
struct cond {
    static constexpr bool value = B;
    using type = T;
};

template <typename First, typename... Rest>
struct select_type : std::conditional<First::value, typename First::type, typename select_type<Rest...>::type> { };

template<typename First>
struct select_type<First> { using type = typename std::conditional<First::value, typename First::type, void>::type; };

class GeneratorBase;

class GeneratorParamBase {
public:
    EXPORT explicit GeneratorParamBase(const std::string &name);
    EXPORT virtual ~GeneratorParamBase();

    const std::string name;

    // overload the set() function to call the right virtual method based on type.
    // This allows us to attempt to set a GeneratorParam via a 
    // plain C++ type, even if we don't know the specific templated
    // subclass. Attempting to set the wrong type will assert.
    // Notice that there is no typed setter for Enums, for obvious reasons;
    // setting enums in an unknown type must fallback to using set_from_string.
    //
    // It's always a bit iffy to use macros for this, but IMHO it clarifies the situation here.
#define HALIDE_GENERATOR_PARAM_TYPED_SETTER(TYPE) \
    virtual void set(const TYPE &new_value) = 0;

    HALIDE_GENERATOR_PARAM_TYPED_SETTER(bool)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int8_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int16_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int32_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int64_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint8_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint16_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint32_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint64_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(float)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(double)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(LoopLevel)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Target)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Type)

#undef HALIDE_GENERATOR_PARAM_TYPED_SETTER

    // Add overloads for string and char*
    void set(const std::string &new_value) { set_from_string(new_value); }
    void set(const char *new_value) { set_from_string(std::string(new_value)); }

protected:
    friend class GeneratorBase;
    friend class StubEmitter;

    EXPORT void check_value_readable() const;
    EXPORT void check_value_writable() const;

    // All GeneratorParams are settable from string.
    virtual void set_from_string(const std::string &value_string) = 0;

    virtual std::string to_string() const = 0;
    virtual std::string call_to_string(const std::string &v) const = 0;
    virtual std::string get_c_type() const = 0;

    virtual std::string get_type_decls() const {
        return "";
    }

    virtual std::string get_default_value() const {
        return to_string();
    }

    virtual std::string get_template_type() const {
        return get_c_type();
    }

    virtual std::string get_template_value() const {
        return get_default_value();
    }

    virtual bool is_schedule_param() const {
        return false;
    }

    virtual bool is_looplevel_param() const {
        return false;
    }

    virtual bool is_synthetic_param() const {
        return false;
    }

    EXPORT void fail_wrong_type(const char *type);

private:
    explicit GeneratorParamBase(const GeneratorParamBase &) = delete;
    void operator=(const GeneratorParamBase &) = delete;

    // Generator which owns this GeneratorParam. Note that this will be null
    // initially; the GeneratorBase itself will set this field when it initially
    // builds its info about params. However, since it (generally) isn't
    // appropriate for GeneratorParam<> to be declared outside of a Generator,
    // all reasonable non-testing code should expect this to be non-null.
    GeneratorBase *generator{nullptr};
};

template<typename T>
class GeneratorParamImpl : public GeneratorParamBase {
public:
    GeneratorParamImpl(const std::string &name, const T &value) : GeneratorParamBase(name), value_(value) {}

    T value() const { check_value_readable(); return value_; }

    operator T() const { return this->value(); }

    operator Expr() const { return make_const(type_of<T>(), this->value()); }

#define HALIDE_GENERATOR_PARAM_TYPED_SETTER(TYPE) \
    void set(const TYPE &new_value) override { typed_setter_impl<TYPE>(new_value, #TYPE); }

    HALIDE_GENERATOR_PARAM_TYPED_SETTER(bool)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int8_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int16_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int32_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(int64_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint8_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint16_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint32_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(uint64_t)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(float)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(double)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(LoopLevel)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Target)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Type)

#undef HALIDE_GENERATOR_PARAM_TYPED_SETTER

protected:
    bool is_looplevel_param() const override {
        return std::is_same<T, LoopLevel>::value;
    }

    virtual void set_impl(const T &new_value) { check_value_writable(); value_ = new_value; }

private:
    T value_;

    template <typename T2, typename std::enable_if<std::is_convertible<T, T2>::value>::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const T2 &t2, const char * msg) {
        // Arithmetic types must roundtrip losslessly.
        if (!std::is_same<T, T2>::value && 
            std::is_arithmetic<T>::value && 
            std::is_arithmetic<T2>::value) {
            const T t = t2;
            const T2 t2a = t;
            if (t2a != t2) {
                fail_wrong_type(msg);
            }
        }
        value_ = t2;
    }

    template <typename T2, typename std::enable_if<!std::is_convertible<T, T2>::value>::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const T2 &, const char *msg) {
        fail_wrong_type(msg);
    }
};

// Stubs for type-specific implementations of GeneratorParam, to avoid
// many complex enable_if<> statements that were formerly spread through the
// implementation. Note that not all of these need to be templated classes,
// (e.g. for GeneratorParam_Target, T == Target always), but are declared
// that way for symmetry of declaration.
template<typename T>
class GeneratorParam_Target : public GeneratorParamImpl<T> {
public:
    GeneratorParam_Target(const std::string &name, const T &value) : GeneratorParamImpl<T>(name, value) {}

    void set_from_string(const std::string &new_value_string) override {
        this->set(Target(new_value_string));
    }

    std::string to_string() const override {
        return this->value().to_string();
    }

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << v << ".to_string()";
        return oss.str();
    }

    std::string get_c_type() const override {
        return "Target";
    }
};

template<typename T>
class GeneratorParam_Arithmetic : public GeneratorParamImpl<T> {
public:
    GeneratorParam_Arithmetic(const std::string &name,
                              const T &value,
                              const T &min = std::numeric_limits<T>::lowest(),
                              const T &max = std::numeric_limits<T>::max())
        : GeneratorParamImpl<T>(name, value), min(min), max(max) {
        // call set() to ensure value is clamped to min/max
        this->set(value);
    }

    void set_impl(const T &new_value) override {
        user_assert(new_value >= min && new_value <= max) << "Value out of range: " << new_value;
        GeneratorParamImpl<T>::set_impl(new_value);
    }

    void set_from_string(const std::string &new_value_string) override {
        std::istringstream iss(new_value_string);
        T t;
        iss >> t;
        user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << new_value_string;
        this->set(t);
    }

    std::string to_string() const override {
        std::ostringstream oss;
        oss << this->value();
        if (std::is_same<T, float>::value) {
            // If the constant has no decimal point ("1")
            // we must append one before appending "f"
            if (oss.str().find(".") == std::string::npos) {
                oss << ".";
            }
            oss << "f";
        }
        return oss.str();
    }

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << "std::to_string(" << v << ")";
        return oss.str();
    }

    std::string get_c_type() const override {
        std::ostringstream oss;
        if (std::is_same<T, float>::value) {
            return "float";
        } else if (std::is_same<T, double>::value) {
            return "double";
        } else if (std::is_integral<T>::value) {
            if (std::is_unsigned<T>::value) oss << 'u';
            oss << "int" << (sizeof(T) * 8) << "_t";
            return oss.str();
        } else {
            user_error << "Unknown arithmetic type\n";
            return "";
        }
    }

private:
    const T min, max;
};

template<typename T>
class GeneratorParam_Bool : public GeneratorParam_Arithmetic<T> {
public:
    GeneratorParam_Bool(const std::string &name, const T &value) : GeneratorParam_Arithmetic<T>(name, value) {}

    void set_from_string(const std::string &new_value_string) override {
        bool v = false;
        if (new_value_string == "true") {
            v = true;
        } else if (new_value_string == "false") {
            v = false;
        } else {
            user_assert(false) << "Unable to parse bool: " << new_value_string;
        }
        this->set(v);
    }

    std::string to_string() const override {
        return this->value() ? "true" : "false";
    }

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << "(" << v << ") ? \"true\" : \"false\"";
        return oss.str();
    }

    std::string get_c_type() const override {
        return "bool";
    }
};

template<typename T>
class GeneratorParam_Enum : public GeneratorParamImpl<T> {
public:
    GeneratorParam_Enum(const std::string &name, const T &value, const std::map<std::string, T> &enum_map)
        : GeneratorParamImpl<T>(name, value), enum_map(enum_map) {}

    void set_from_string(const std::string &new_value_string) override {
        auto it = enum_map.find(new_value_string);
        user_assert(it != enum_map.end()) << "Enumeration value not found: " << new_value_string;
        this->set_impl(it->second);
    }

    std::string to_string() const override {
        return enum_to_string(enum_map, this->value());
    }

    std::string call_to_string(const std::string &v) const override {
        return "Enum_" + this->name + "_map().at(" + v + ")";
    }

    std::string get_c_type() const override {
        return "Enum_" + this->name;
    }

    std::string get_default_value() const override {
        return "Enum_" + this->name + "::" + enum_to_string(enum_map, this->value());
    }

    std::string get_type_decls() const override {
        std::ostringstream oss;
        oss << "enum class Enum_" << this->name << " {\n";
        for (auto key_value : enum_map) {
            oss << "  " << key_value.first << ",\n";
        }
        oss << "};\n";
        oss << "\n";

        // TODO: since we generate the enums, we could probably just use a vector (or array!) rather than a map,
        // since we can ensure that the enum values are a nice tight range.
        oss << "inline NO_INLINE const std::map<Enum_" << this->name << ", std::string>& Enum_" << this->name << "_map() {\n";
        oss << "  static const std::map<Enum_" << this->name << ", std::string> m = {\n";
        for (auto key_value : enum_map) {
            oss << "    { Enum_" << this->name << "::" << key_value.first << ", \"" << key_value.first << "\"},\n";
        }
        oss << "  };\n";
        oss << "  return m;\n";
        oss << "};\n";
        return oss.str();
    }

private:
    const std::map<std::string, T> enum_map;
};

template<typename T>
class GeneratorParam_Type : public GeneratorParam_Enum<T> {
public:
    GeneratorParam_Type(const std::string &name, const T &value)
        : GeneratorParam_Enum<T>(name, value, get_halide_type_enum_map()) {}

    std::string call_to_string(const std::string &v) const override {
        return "Halide::Internal::halide_type_to_enum_string(" + v + ")";
    }

    std::string get_c_type() const override {
        return "Type";
    }

    std::string get_template_type() const override {
        return "typename";
    }

    std::string get_template_value() const override {
        return halide_type_to_c_type(this->value());
    }

    std::string get_default_value() const override {
        return halide_type_to_c_source(this->value());
    }

    std::string get_type_decls() const override {
        return "";
    }
};

template<typename T>
class GeneratorParam_LoopLevel : public GeneratorParam_Enum<T> {
public:
    GeneratorParam_LoopLevel(const std::string &name, const std::string &def)
        : GeneratorParam_Enum<T>(name, enum_from_string(get_halide_looplevel_enum_map(), def), get_halide_looplevel_enum_map()), def(def) {}

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << "Halide::Internal::halide_looplevel_to_enum_string(" << v << ")";
        return oss.str();
    }
    std::string get_c_type() const override {
        return "LoopLevel";
    }

    std::string get_default_value() const override {
        if (def == "undefined") return "Halide::Internal::get_halide_undefined_looplevel()";
        if (def == "root") return "LoopLevel::root()";
        if (def == "inline") return "LoopLevel()";
        user_error << "LoopLevel value " << def << " not found.\n";
        return "";
    }

    std::string get_type_decls() const override {
        return "";
    }

    bool defined() const {
        return this->value() != get_halide_undefined_looplevel();
    }

private:
    const std::string def;
};

template<typename T>
using GeneratorParamImplBase =
    typename select_type<
        cond<std::is_same<T, Target>::value,    GeneratorParam_Target<T>>,
        cond<std::is_same<T, Type>::value,      GeneratorParam_Type<T>>,
        cond<std::is_same<T, LoopLevel>::value, GeneratorParam_LoopLevel<T>>,
        cond<std::is_same<T, bool>::value,      GeneratorParam_Bool<T>>,
        cond<std::is_arithmetic<T>::value,      GeneratorParam_Arithmetic<T>>,
        cond<std::is_enum<T>::value,            GeneratorParam_Enum<T>>
    >::type;

}  // namespace Internal

/** GeneratorParam is a templated class that can be used to modify the behavior
 * of the Generator at code-generation time. GeneratorParams are commonly
 * specified in build files (e.g. Makefile) to customize the behavior of
 * a given Generator, thus they have a very constrained set of types to allow
 * for efficient specification via command-line flags. A GeneratorParam can be:
 *   - any float or int type.
 *   - bool
 *   - enum
 *   - Halide::Target
 *   - Halide::Type
 * All GeneratorParams have a default value. Arithmetic types can also
 * optionally specify min and max. Enum types must specify a string-to-value
 * map.
 *
 * Halide::Type is treated as though it were an enum, with the mappings:
 *
 *   "int8"     Halide::Int(8)
 *   "int16"    Halide::Int(16)
 *   "int32"    Halide::Int(32)
 *   "uint8"    Halide::UInt(8)
 *   "uint16"   Halide::UInt(16)
 *   "uint32"   Halide::UInt(32)
 *   "float32"  Halide::Float(32)
 *   "float64"  Halide::Float(64)
 *
 * No vector Types are currently supported by this mapping.
 *
 */
template <typename T>
class GeneratorParam : public Internal::GeneratorParamImplBase<T> {
public:
    GeneratorParam(const std::string &name, const T &value)
        : Internal::GeneratorParamImplBase<T>(name, value) {}

    GeneratorParam(const std::string &name, const T &value, const T &min, const T &max)
        : Internal::GeneratorParamImplBase<T>(name, value, min, max) {}

    GeneratorParam(const std::string &name, const T &value, const std::map<std::string, T> &enum_map)
        : Internal::GeneratorParamImplBase<T>(name, value, enum_map) {}

    GeneratorParam(const std::string &name, const std::string &value)
        : Internal::GeneratorParamImplBase<T>(name, value) {}
};

/** ScheduleParam is similar to a GeneratorParam, with two important differences:
 *
 * (1) ScheduleParams are intended for use only within a Generator's schedule()
 * method (if any); if a Generator has no schedule() method, it should also have no
 * ScheduleParams
 *
 * (2) ScheduleParam can represent a LoopLevel, while GeneratorParam cannot.
 */
template <typename T>
class ScheduleParam : public GeneratorParam<T> {
public:
    ScheduleParam(const std::string &name, const T &value)
        : GeneratorParam<T>(name, value) {}

    ScheduleParam(const std::string &name, const T &value, const T &min, const T &max)
        : GeneratorParam<T>(name, value, min, max) {}

    ScheduleParam(const std::string &name, const std::string &value)
        : GeneratorParam<T>(name, value) {}

protected:
    bool is_schedule_param() const override { return true; }
};

/** Addition between GeneratorParam<T> and any type that supports operator+ with T.
 * Returns type of underlying operator+. */
// @{
template <typename Other, typename T>
decltype((Other)0 + (T)0) operator+(const Other &a, const GeneratorParam<T> &b) { return a + (T)b; }
template <typename Other, typename T>
decltype((T)0 + (Other)0) operator+(const GeneratorParam<T> &a, const Other & b) { return (T)a + b; }
// @}

/** Subtraction between GeneratorParam<T> and any type that supports operator- with T.
 * Returns type of underlying operator-. */
// @{
template <typename Other, typename T>
decltype((Other)0 - (T)0) operator-(const Other & a, const GeneratorParam<T> &b) { return a - (T)b; }
template <typename Other, typename T>
decltype((T)0 - (Other)0)  operator-(const GeneratorParam<T> &a, const Other & b) { return (T)a - b; }
// @}

/** Multiplication between GeneratorParam<T> and any type that supports operator* with T.
 * Returns type of underlying operator*. */
// @{
template <typename Other, typename T>
decltype((Other)0 * (T)0) operator*(const Other &a, const GeneratorParam<T> &b) { return a * (T)b; }
template <typename Other, typename T>
decltype((Other)0 * (T)0) operator*(const GeneratorParam<T> &a, const Other &b) { return (T)a * b; }
// @}

/** Division between GeneratorParam<T> and any type that supports operator/ with T.
 * Returns type of underlying operator/. */
// @{
template <typename Other, typename T>
decltype((Other)0 / (T)1) operator/(const Other &a, const GeneratorParam<T> &b) { return a / (T)b; }
template <typename Other, typename T>
decltype((T)0 / (Other)1) operator/(const GeneratorParam<T> &a, const Other &b) { return (T)a / b; }
// @}

/** Modulo between GeneratorParam<T> and any type that supports operator% with T.
 * Returns type of underlying operator%. */
// @{
template <typename Other, typename T>
decltype((Other)0 % (T)1) operator%(const Other &a, const GeneratorParam<T> &b) { return a % (T)b; }
template <typename Other, typename T>
decltype((T)0 % (Other)1) operator%(const GeneratorParam<T> &a, const Other &b) { return (T)a % b; }
// @}

/** Greater than comparison between GeneratorParam<T> and any type that supports operator> with T.
 * Returns type of underlying operator>. */
// @{
template <typename Other, typename T>
decltype((Other)0 > (T)1) operator>(const Other &a, const GeneratorParam<T> &b) { return a > (T)b; }
template <typename Other, typename T>
decltype((T)0 > (Other)1) operator>(const GeneratorParam<T> &a, const Other &b) { return (T)a > b; }
// @}

/** Less than comparison between GeneratorParam<T> and any type that supports operator< with T.
 * Returns type of underlying operator<. */
// @{
template <typename Other, typename T>
decltype((Other)0 < (T)1) operator<(const Other &a, const GeneratorParam<T> &b) { return a < (T)b; }
template <typename Other, typename T>
decltype((T)0 < (Other)1) operator<(const GeneratorParam<T> &a, const Other &b) { return (T)a < b; }
// @}

/** Greater than or equal comparison between GeneratorParam<T> and any type that supports operator>= with T.
 * Returns type of underlying operator>=. */
// @{
template <typename Other, typename T>
decltype((Other)0 >= (T)1) operator>=(const Other &a, const GeneratorParam<T> &b) { return a >= (T)b; }
template <typename Other, typename T>
decltype((T)0 >= (Other)1) operator>=(const GeneratorParam<T> &a, const Other &b) { return (T)a >= b; }
// @}

/** Less than or equal comparison between GeneratorParam<T> and any type that supports operator<= with T.
 * Returns type of underlying operator<=. */
// @{
template <typename Other, typename T>
decltype((Other)0 <= (T)1) operator<=(const Other &a, const GeneratorParam<T> &b) { return a <= (T)b; }
template <typename Other, typename T>
decltype((T)0 <= (Other)1) operator<=(const GeneratorParam<T> &a, const Other &b) { return (T)a <= b; }
// @}

/** Equality comparison between GeneratorParam<T> and any type that supports operator== with T.
 * Returns type of underlying operator==. */
// @{
template <typename Other, typename T>
decltype((Other)0 == (T)1) operator==(const Other &a, const GeneratorParam<T> &b) { return a == (T)b; }
template <typename Other, typename T>
decltype((T)0 == (Other)1) operator==(const GeneratorParam<T> &a, const Other &b) { return (T)a == b; }
// @}

/** Inequality comparison between between GeneratorParam<T> and any type that supports operator!= with T.
 * Returns type of underlying operator!=. */
// @{
template <typename Other, typename T>
decltype((Other)0 != (T)1) operator!=(const Other &a, const GeneratorParam<T> &b) { return a != (T)b; }
template <typename Other, typename T>
decltype((T)0 != (Other)1) operator!=(const GeneratorParam<T> &a, const Other &b) { return (T)a != b; }
// @}

/** Logical and between between GeneratorParam<T> and any type that supports operator&& with T.
 * Returns type of underlying operator&&. */
// @{
template <typename Other, typename T>
decltype((Other)0 && (T)1) operator&&(const Other &a, const GeneratorParam<T> &b) { return a && (T)b; }
template <typename Other, typename T>
decltype((T)0 && (Other)1) operator&&(const GeneratorParam<T> &a, const Other &b) { return (T)a && b; }
// @}

/** Logical or between between GeneratorParam<T> and any type that supports operator&& with T.
 * Returns type of underlying operator||. */
// @{
template <typename Other, typename T>
decltype((Other)0 || (T)1) operator||(const Other &a, const GeneratorParam<T> &b) { return a || (T)b; }
template <typename Other, typename T>
decltype((T)0 || (Other)1) operator||(const GeneratorParam<T> &a, const Other &b) { return (T)a || b; }
// @}

/* min and max are tricky as the language support for these is in the std
 * namespace. In order to make this work, forwarding functions are used that
 * are declared in a namespace that has std::min and std::max in scope.
 */
namespace Internal { namespace GeneratorMinMax {

using std::max;
using std::min;

template <typename Other, typename T>
decltype(min((Other)0, (T)1)) min_forward(const Other &a, const GeneratorParam<T> &b) { return min(a, (T)b); }
template <typename Other, typename T>
decltype(min((T)0, (Other)1)) min_forward(const GeneratorParam<T> &a, const Other &b) { return min((T)a, b); }

template <typename Other, typename T>
decltype(max((Other)0, (T)1)) max_forward(const Other &a, const GeneratorParam<T> &b) { return max(a, (T)b); }
template <typename Other, typename T>
decltype(max((T)0, (Other)1)) max_forward(const GeneratorParam<T> &a, const Other &b) { return max((T)a, b); }

}}

/** Compute minimum between GeneratorParam<T> and any type that supports min with T.
 * Will automatically import std::min. Returns type of underlying min call. */
// @{
template <typename Other, typename T>
auto min(const Other &a, const GeneratorParam<T> &b) -> decltype(Internal::GeneratorMinMax::min_forward(a, b)) {
    return Internal::GeneratorMinMax::min_forward(a, b);
}
template <typename Other, typename T>
auto min(const GeneratorParam<T> &a, const Other &b) -> decltype(Internal::GeneratorMinMax::min_forward(a, b)) {
    return Internal::GeneratorMinMax::min_forward(a, b);
}
// @}

/** Compute the maximum value between GeneratorParam<T> and any type that supports max with T.
 * Will automatically import std::max. Returns type of underlying max call. */
// @{
template <typename Other, typename T>
auto max(const Other &a, const GeneratorParam<T> &b) -> decltype(Internal::GeneratorMinMax::max_forward(a, b)) {
    return Internal::GeneratorMinMax::max_forward(a, b);
}
template <typename Other, typename T>
auto max(const GeneratorParam<T> &a, const Other &b) -> decltype(Internal::GeneratorMinMax::max_forward(a, b)) {
    return Internal::GeneratorMinMax::max_forward(a, b);
}
// @}

/** Not operator for GeneratorParam */
template <typename T>
decltype(!(T)0) operator!(const GeneratorParam<T> &a) { return !(T)a; }

namespace Internal {

template<typename T2> class GeneratorInput_Buffer;

enum class IOKind { Scalar, Function, Buffer };

/**
 * StubInputBuffer is the placeholder that a Stub uses when it requires
 * a Buffer for an input (rather than merely a Func or Expr). It is constructed
 * to allow only two possible sorts of input:
 * -- Assignment of an Input<Buffer<>>, with compatible type and dimensions,
 * essentially allowing us to pipe a parameter from an enclosing Generator to an internal Stub.
 * -- Assignment of a Buffer<>, with compatible type and dimensions,
 * causing the Input<Buffer<>> to become a precompiled buffer in the generated code.
 */
template<typename T = void>
class StubInputBuffer {
    friend class StubInput;
    template<typename T2> friend class GeneratorInput_Buffer;

    Parameter parameter_;

    NO_INLINE explicit StubInputBuffer(const Parameter &p) : parameter_(p) {
        // Create an empty 1-element buffer with the right runtime typing and dimensions,
        // which we'll use only to pass to can_convert_from() to verify this
        // Parameter is compatible with our constraints.
        Buffer<> other(p.type(), nullptr, std::vector<int>(p.dimensions(), 1));
        internal_assert((Buffer<T>::can_convert_from(other)));
    }

    template<typename T2>
    NO_INLINE static Parameter parameter_from_buffer(const Buffer<T2> &b) {
        user_assert((Buffer<T>::can_convert_from(b)));
        Parameter p(b.type(), true, b.dimensions());
        p.set_buffer(b);
        return p;
    }

public:
    StubInputBuffer() {}

    // *not* explicit -- this ctor should only be used when you want
    // to pass a literal Buffer<> for a Stub Input; this Buffer<> will be
    // compiled into the Generator's product, rather than becoming
    // a runtime Parameter.
    template<typename T2>
    StubInputBuffer(const Buffer<T2> &b) : parameter_(parameter_from_buffer(b)) {}
};

class StubOutputBufferBase {
protected:
    Func f;
    std::shared_ptr<GeneratorBase> generator;

    EXPORT void check_scheduled(const char* m) const;
    EXPORT Target get_target() const;

    explicit StubOutputBufferBase(const Func &f, std::shared_ptr<GeneratorBase> generator) : f(f), generator(generator) {}
    StubOutputBufferBase() {}

public:
    Realization realize(std::vector<int32_t> sizes) {
        check_scheduled("realize");
        return f.realize(sizes, get_target());
    }

    template <typename... Args>
    Realization realize(Args&&... args) {
        check_scheduled("realize");
        return f.realize(std::forward<Args>(args)..., get_target());
    }

    template<typename Dst>
    void realize(Dst dst) {
        check_scheduled("realize");
        f.realize(dst, get_target());
    }
};

/**
 * StubOutputBuffer is the placeholder that a Stub uses when it requires
 * a Buffer for an output (rather than merely a Func). It is constructed
 * to allow only two possible sorts of things:
 * -- Assignment to an Output<Buffer<>>, with compatible type and dimensions,
 * essentially allowing us to pipe a parameter from the result of a Stub to an
 * enclosing Generator
 * -- Realization into a Buffer<>; this is useful only in JIT compilation modes
 * (and shouldn't be usable otherwise)
 *
 * It is deliberate that StubOutputBuffer is not (easily) convertible to Func.
 */
template<typename T = void>
class StubOutputBuffer : public StubOutputBufferBase {
    template<typename T2> friend class GeneratorOutput_Buffer;
    friend class GeneratorStub;
    explicit StubOutputBuffer(const Func &f, std::shared_ptr<GeneratorBase> generator) : StubOutputBufferBase(f, generator) {}
public:
    StubOutputBuffer() {}
};

// This is a union-like class that allows for convenient initialization of Stub Inputs
// via C++11 initializer-list syntax; it is only used in situations where the
// downstream consumer will be able to explicitly check that each value is
// of the expected/required kind.
class StubInput {
    const IOKind kind_;
    // Exactly one of the following fields should be defined:
    const Parameter parameter_;
    const Func func_;
    const Expr expr_;
public:
    // *not* explicit.
    template<typename T2>
    StubInput(const StubInputBuffer<T2> &b) : kind_(IOKind::Buffer), parameter_(b.parameter_) {}
    StubInput(const Func &f) : kind_(IOKind::Function), func_(f) {}
    StubInput(const Expr &e) : kind_(IOKind::Scalar), expr_(e) {}

private:
    friend class GeneratorInputBase;

    IOKind kind() const {
        return kind_;
    }

    Parameter parameter() const {
        internal_assert(kind_ == IOKind::Buffer);
        return parameter_;
    }

    Func func() const {
        internal_assert(kind_ == IOKind::Function);
        return func_;
    }

    Expr expr() const {
        internal_assert(kind_ == IOKind::Scalar);
        return expr_;
    }
};

class Constrainable {
public:
    virtual ~Constrainable() {}

    virtual Parameter parameter() const = 0;

    Dimension dim(int i) {
        return Dimension(parameter(), i);
    }

    const Dimension dim(int i) const {
        return Dimension(parameter(), i);
    }

    int host_alignment() const {
        return parameter().host_alignment();
    }

    Constrainable &set_host_alignment(int alignment) {
        parameter().set_host_alignment(alignment);
        return *this;
    }
};

/** GIOBase is the base class for all GeneratorInput<> and GeneratorOutput<>
 * instantiations; it is not part of the public API and should never be
 * used directly by user code.
 *
 * Every GIOBase instance can be either a single value or an array-of-values;
 * each of these values can be an Expr or a Func. (Note that for an
 * array-of-values, the types/dimensions of all values in the array must match.)
 *
 * A GIOBase can have multiple Types, in which case it represents a Tuple.
 * (Note that Tuples are currently only supported for GeneratorOutput, but
 * it is likely that GeneratorInput will be extended to support Tuple as well.)
 *
 * The array-size, type(s), and dimensions can all be left "unspecified" at
 * creation time, in which case they may assume values provided by a Stub.
 * (It is important to note that attempting to use a GIOBase with unspecified
 * values will assert-fail; you must ensure that all unspecified values are
 * filled in prior to use.)
 */
class GIOBase {
public:
    EXPORT bool array_size_defined() const;
    EXPORT size_t array_size() const;
    EXPORT virtual bool is_array() const;

    EXPORT const std::string &name() const;
    EXPORT IOKind kind() const;

    EXPORT bool types_defined() const;
    EXPORT const std::vector<Type> &types() const;
    EXPORT Type type() const;

    EXPORT bool dimensions_defined() const;
    EXPORT int dimensions() const;

    EXPORT const std::vector<Func> &funcs() const;
    EXPORT const std::vector<Expr> &exprs() const;

protected:
    EXPORT GIOBase(size_t array_size,
                   const std::string &name,
                   IOKind kind,
                   const std::vector<Type> &types,
                   int dimensions);
    EXPORT virtual ~GIOBase();

    friend class GeneratorBase;

    int array_size_;           // always 1 if is_array() == false.
                               // -1 if is_array() == true but unspecified.

    const std::string name_;
    const IOKind kind_;
    std::vector<Type> types_;  // empty if type is unspecified
    int dimensions_;           // -1 if dim is unspecified

    // Exactly one of these will have nonzero length
    std::vector<Func> funcs_;
    std::vector<Expr> exprs_;

    // Generator which owns this Input or Output. Note that this will be null
    // initially; the GeneratorBase itself will set this field when it initially
    // builds its info about params. However, since it isn't
    // appropriate for Input<> or Output<> to be declared outside of a Generator,
    // all reasonable non-testing code should expect this to be non-null.
    GeneratorBase *generator{nullptr};

    EXPORT std::string array_name(size_t i) const;

    EXPORT virtual void verify_internals() const;

    EXPORT void check_matching_array_size(size_t size);
    EXPORT void check_matching_type_and_dim(const std::vector<Type> &t, int d);

    template<typename ElemType>
    const std::vector<ElemType> &get_values() const;

    virtual bool allow_synthetic_generator_params() const {
        return true;
    }

    virtual Parameter parameter() const {
        internal_error << "Unimplemented";
        return Parameter();
    }

    virtual void check_value_writable() const = 0;

private:
    template<typename T> friend class GeneratorParam_Synthetic;

    explicit GIOBase(const GIOBase &) = delete;
    void operator=(const GIOBase &) = delete;
};

template<>
inline const std::vector<Expr> &GIOBase::get_values<Expr>() const {
    return exprs();
}

template<>
inline const std::vector<Func> &GIOBase::get_values<Func>() const {
    return funcs();
}

class GeneratorInputBase : public GIOBase {
protected:
    EXPORT GeneratorInputBase(size_t array_size,
                       const std::string &name,
                       IOKind kind,
                       const std::vector<Type> &t,
                       int d);

    EXPORT GeneratorInputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d);

    EXPORT ~GeneratorInputBase() override;

    friend class GeneratorBase;

    std::vector<Parameter> parameters_;

    EXPORT void init_internals();
    EXPORT void set_inputs(const std::vector<StubInput> &inputs);

    EXPORT virtual void set_def_min_max();

    EXPORT void verify_internals() const override;

    friend class StubEmitter;

    virtual std::string get_c_type() const = 0;

    EXPORT void check_value_writable() const override;
private:
    EXPORT void init_parameters();
};


template<typename T, typename ValueType>
class GeneratorInputImpl : public GeneratorInputBase {
protected:
    using TBase = typename std::remove_all_extents<T>::type;

    bool is_array() const override {
        return std::is_array<T>::value;
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2 not-an-array
        !std::is_array<T2>::value
    >::type * = nullptr>
    GeneratorInputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorInputBase(name, kind, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[kSomeConst]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && (std::extent<T2, 0>::value > 0)
    >::type * = nullptr>
    GeneratorInputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorInputBase(std::extent<T2, 0>::value, name, kind, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && std::extent<T2, 0>::value == 0
    >::type * = nullptr>
    GeneratorInputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorInputBase(-1, name, kind, t, d) {
    }

public:
    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    size_t size() const {
        return get_values<ValueType>().size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &operator[](size_t i) const {
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &at(size_t i) const {
        return get_values<ValueType>().at(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator begin() const {
        return get_values<ValueType>().begin();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator end() const {
        return get_values<ValueType>().end();
    }
};

template<typename T>
class GeneratorInput_Buffer : public GeneratorInputImpl<T, Func>, public Constrainable {
private:
    using Super = GeneratorInputImpl<T, Func>;

protected:
    using TBase = typename Super::TBase;

    bool allow_synthetic_generator_params() const override {
        return !T::has_static_halide_type();
    }

    std::string get_c_type() const override {
        if (T::has_static_halide_type()) {
            return "Halide::Internal::StubInputBuffer<" +
                halide_type_to_c_type(T::static_halide_type()) +
                ">";
        } else {
            return "Halide::Internal::StubInputBuffer<>";
        }
    }

    Parameter parameter() const override {
        internal_assert(this->parameters_.size() == 1);
        return this->parameters_.at(0);
    }

public:
    GeneratorInput_Buffer(const std::string &name)
        : Super(name, IOKind::Buffer,
                T::has_static_halide_type() ? std::vector<Type>{ T::static_halide_type() } : std::vector<Type>{},
                -1) {
    }

    GeneratorInput_Buffer(const std::string &name, const Type &t, int d = -1)
        : Super(name, IOKind::Buffer, {t}, d) {
        static_assert(!T::has_static_halide_type(), "Cannot use pass a Type argument for a Buffer with a non-void static type");
    }

    GeneratorInput_Buffer(const std::string &name, int d)
        : Super(name, IOKind::Buffer, T::has_static_halide_type() ? std::vector<Type>{ T::static_halide_type() } : std::vector<Type>{}, d) {
    }


    template <typename... Args>
    Expr operator()(Args&&... args) const {
        return this->funcs().at(0)(std::forward<Args>(args)...);
    }

    Expr operator()(std::vector<Expr> args) const {
        return this->funcs().at(0)(args);
    }

    template<typename T2>
    operator StubInputBuffer<T2>() const {
        return StubInputBuffer<T2>(parameter());
    }

    operator Func() const {
        return this->funcs().at(0);
    }
};


template<typename T>
class GeneratorInput_Func : public GeneratorInputImpl<T, Func> {
private:
    using Super = GeneratorInputImpl<T, Func>;

protected:
    using TBase = typename Super::TBase;

    std::string get_c_type() const override {
        return "Func";
    }

public:
    GeneratorInput_Func(const std::string &name, const Type &t, int d)
        : Super(name, IOKind::Function, {t}, d) {
    }

    // unspecified type
    GeneratorInput_Func(const std::string &name, int d)
        : Super(name, IOKind::Function, {}, d) {
    }

    // unspecified dimension
    GeneratorInput_Func(const std::string &name, const Type &t)
        : Super(name, IOKind::Function, {t}, -1) {
    }

    // unspecified type & dimension
    GeneratorInput_Func(const std::string &name)
        : Super(name, IOKind::Function, {}, -1) {
    }

    GeneratorInput_Func(size_t array_size, const std::string &name, const Type &t, int d)
        : Super(array_size, name, IOKind::Function, {t}, d) {
    }

    // unspecified type
    GeneratorInput_Func(size_t array_size, const std::string &name, int d)
        : Super(array_size, name, IOKind::Function, {}, d) {
    }

    // unspecified dimension
    GeneratorInput_Func(size_t array_size, const std::string &name, const Type &t)
        : Super(array_size, name, IOKind::Function, {t}, -1) {
    }

    // unspecified type & dimension
    GeneratorInput_Func(size_t array_size, const std::string &name)
        : Super(array_size, name, IOKind::Function, {}, -1) {
    }

    template <typename... Args>
    Expr operator()(Args&&... args) const {
        return this->funcs().at(0)(std::forward<Args>(args)...);
    }

    Expr operator()(std::vector<Expr> args) const {
        return this->funcs().at(0)(args);
    }

    operator Func() const {
        return this->funcs().at(0);
    }
};


template<typename T>
class GeneratorInput_Scalar : public GeneratorInputImpl<T, Expr> {
private:
    using Super = GeneratorInputImpl<T, Expr>;
protected:
    using TBase = typename Super::TBase;

    const TBase def_{TBase()};

protected:
    void set_def_min_max() override {
        for (Parameter &p : this->parameters_) {
            p.set_scalar<TBase>(def_);
        }
    }

    std::string get_c_type() const override {
        return "Expr";
    }

public:
    explicit GeneratorInput_Scalar(const std::string &name,
                                   const TBase &def = static_cast<TBase>(0))
        : Super(name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(def) {
    }

    GeneratorInput_Scalar(size_t array_size,
                          const std::string &name,
                          const TBase &def = static_cast<TBase>(0))
        : Super(array_size, name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(def) {
    }

    /** You can use this Input as an expression in a halide
     * function definition */
    operator Expr() const {
        return this->exprs().at(0);
    }

    /** Using an Input as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        return ExternFuncArgument(this->exprs().at(0));
    }


};

template<typename T>
class GeneratorInput_Arithmetic : public GeneratorInput_Scalar<T> {
private:
    using Super = GeneratorInput_Scalar<T>;
protected:
    using TBase = typename Super::TBase;

    const Expr min_, max_;

protected:
    void set_def_min_max() override {
        GeneratorInput_Scalar<T>::set_def_min_max();
        // Don't set min/max for bool
        if (!std::is_same<TBase, bool>::value) {
            for (Parameter &p : this->parameters_) {
                if (min_.defined()) p.set_min_value(min_);
                if (max_.defined()) p.set_max_value(max_);
            }
        }
    }

public:
    explicit GeneratorInput_Arithmetic(const std::string &name,
                                       const TBase &def = static_cast<TBase>(0))
        : Super(name, def), min_(Expr()), max_(Expr()) {
    }

    GeneratorInput_Arithmetic(size_t array_size,
                              const std::string &name,
                              const TBase &def = static_cast<TBase>(0))
        : Super(array_size, name, def), min_(Expr()), max_(Expr()) {
    }

    GeneratorInput_Arithmetic(const std::string &name,
                              const TBase &def,
                              const TBase &min,
                              const TBase &max)
        : Super(name, def), min_(min), max_(max) {
    }

    GeneratorInput_Arithmetic(size_t array_size,
                              const std::string &name,
                              const TBase &def,
                              const TBase &min,
                              const TBase &max)
        : Super(array_size, name, def), min_(min), max_(max) {
    }
};

template<typename>
struct type_sink { typedef void type; };

template<typename T2, typename = void>
struct has_static_halide_type_method : std::false_type {};

template<typename T2>
struct has_static_halide_type_method<T2, typename type_sink<decltype(T2::static_halide_type())>::type> : std::true_type {};

template<typename T, typename TBase = typename std::remove_all_extents<T>::type>
using GeneratorInputImplBase =
    typename select_type<
        cond<has_static_halide_type_method<TBase>::value, GeneratorInput_Buffer<T>>,
        cond<std::is_same<TBase, Func>::value,            GeneratorInput_Func<T>>,
        cond<std::is_arithmetic<TBase>::value,            GeneratorInput_Arithmetic<T>>,
        cond<std::is_scalar<TBase>::value,                GeneratorInput_Scalar<T>>
    >::type;

}  // namespace Internal

template <typename T>
class GeneratorInput : public Internal::GeneratorInputImplBase<T> {
private:
    using Super = Internal::GeneratorInputImplBase<T>;
protected:
    using TBase = typename Super::TBase;

    // Trick to avoid ambiguous ctor between Func-with-dim and int-with-default-value;
    // since we can't use std::enable_if on ctors, define the argument to be one that
    // can only be properly resolved for TBase=Func.
    struct Unused;
    using IntIfNonScalar =
        typename Internal::select_type<
            Internal::cond<Internal::has_static_halide_type_method<TBase>::value, int>,
            Internal::cond<std::is_same<TBase, Func>::value, int>,
            Internal::cond<true, Unused>
        >::type;

public:
    explicit GeneratorInput(const std::string &name)
        : Super(name) {
    }

    GeneratorInput(const std::string &name, const TBase &def)
        : Super(name, def) {
    }

    GeneratorInput(size_t array_size, const std::string &name, const TBase &def)
        : Super(array_size, name, def) {
    }

    GeneratorInput(const std::string &name,
                   const TBase &def, const TBase &min, const TBase &max)
        : Super(name, def, min, max) {
    }

    GeneratorInput(size_t array_size, const std::string &name,
                   const TBase &def, const TBase &min, const TBase &max)
        : Super(array_size, name, def, min, max) {
    }

    GeneratorInput(const std::string &name, const Type &t, int d)
        : Super(name, t, d) {
    }

    GeneratorInput(const std::string &name, const Type &t)
        : Super(name, t) {
    }

    // Avoid ambiguity between Func-with-dim and int-with-default
    GeneratorInput(const std::string &name, IntIfNonScalar d)
        : Super(name, d) {
    }

    GeneratorInput(size_t array_size, const std::string &name, const Type &t, int d)
        : Super(array_size, name, t, d) {
    }

    GeneratorInput(size_t array_size, const std::string &name, const Type &t)
        : Super(array_size, name, t) {
    }

    // Avoid ambiguity between Func-with-dim and int-with-default
    //template <typename T2 = T, typename std::enable_if<std::is_same<TBase, Func>::value>::type * = nullptr>
    GeneratorInput(size_t array_size, const std::string &name, IntIfNonScalar d)
        : Super(array_size, name, d) {
    }

    GeneratorInput(size_t array_size, const std::string &name)
        : Super(array_size, name) {
    }
};

namespace Internal {

class GeneratorOutputBase : public GIOBase {
protected:
    EXPORT GeneratorOutputBase(size_t array_size,
                        const std::string &name,
                        IOKind kind,
                        const std::vector<Type> &t,
                        int d);

    EXPORT GeneratorOutputBase(const std::string &name,
                               IOKind kind,
                               const std::vector<Type> &t,
                               int d);

    EXPORT ~GeneratorOutputBase() override;

    friend class GeneratorBase;
    friend class StubEmitter;

    EXPORT void init_internals();
    EXPORT void resize(size_t size);

    virtual std::string get_c_type() const {
        return "Func";
    }

    EXPORT void check_value_writable() const override;
};

template<typename T>
class GeneratorOutputImpl : public GeneratorOutputBase {
protected:
    using TBase = typename std::remove_all_extents<T>::type;
    using ValueType = Func;

    bool is_array() const override {
        return std::is_array<T>::value;
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2 not-an-array
        !std::is_array<T2>::value
    >::type * = nullptr>
    GeneratorOutputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(name, kind, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[kSomeConst]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && (std::extent<T2, 0>::value > 0)
    >::type * = nullptr>
    GeneratorOutputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(std::extent<T2, 0>::value, name, kind, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && std::extent<T2, 0>::value == 0
    >::type * = nullptr>
    GeneratorOutputImpl(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(-1, name, kind, t, d) {
    }

public:
    template <typename... Args, typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    FuncRef operator()(Args&&... args) const {
        return get_values<ValueType>().at(0)(std::forward<Args>(args)...);
    }

    template <typename ExprOrVar, typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    FuncRef operator()(std::vector<ExprOrVar> args) const {
        return get_values<ValueType>().at(0)(args);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    operator Func() const {
        return get_values<ValueType>().at(0);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    size_t size() const {
        return get_values<ValueType>().size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &operator[](size_t i) const {
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &at(size_t i) const {
        return get_values<ValueType>().at(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator begin() const {
        return get_values<ValueType>().begin();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator end() const {
        return get_values<ValueType>().end();
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && std::extent<T2, 0>::value == 0
    >::type * = nullptr>
    void resize(size_t size) {
        GeneratorOutputBase::resize(size);
    }
};

template<typename T>
class GeneratorOutput_Buffer : public GeneratorOutputImpl<T>, public Constrainable {
private:
    using Super = GeneratorOutputImpl<T>;

protected:
    using TBase = typename Super::TBase;

protected:
    GeneratorOutput_Buffer(const std::string &name)
        : Super(name, IOKind::Buffer,
                T::has_static_halide_type() ? std::vector<Type>{ T::static_halide_type() } : std::vector<Type>{},
                -1) {
    }

    GeneratorOutput_Buffer(const std::string &name, const std::vector<Type> &t, int d = -1)
        : Super(name, IOKind::Buffer,
                T::has_static_halide_type() ? std::vector<Type>{ T::static_halide_type() } : t,
                d) {
        if (T::has_static_halide_type()) {
            user_assert(t.empty()) << "Cannot use pass a Type argument for a Buffer with a non-void static type\n";
        } else {
            user_assert(t.size() <= 1) << "Output<Buffer<>>(" << name << ") requires at most one Type, but has " << t.size() << "\n";
        }
    }

    GeneratorOutput_Buffer(const std::string &name, int d)
        : Super(name, IOKind::Buffer, std::vector<Type>{ T::static_halide_type() }, d) {
        static_assert(T::has_static_halide_type(), "Must pass a Type argument for a Buffer with a static type of void");
    }

    NO_INLINE std::string get_c_type() const override {
        if (T::has_static_halide_type()) {
            return "Halide::Internal::StubOutputBuffer<" +
                halide_type_to_c_type(T::static_halide_type()) +
                ">";
        } else {
            return "Halide::Internal::StubOutputBuffer<>";
        }
    }

    Parameter parameter() const override {
        internal_assert(this->funcs().size() == 1);
        return this->funcs().at(0).output_buffer().parameter();
    }

public:

    // Allow assignment from a Buffer<> to an Output<Buffer<>>;
    // this allows us to use a statically-compiled buffer inside a Generator
    // to assign to an output.
    // TODO: This used to take the buffer as a const ref. This no longer works as
    // using it in a Pipeline might change the dev field so it is currently
    // not considered const. We should consider how this really ought to work.
    template<typename T2>
    NO_INLINE GeneratorOutput_Buffer<T> &operator=(Buffer<T2> &buffer) {
        this->check_value_writable();

        user_assert(T::can_convert_from(buffer))
            << "Cannot assign to the Output \"" << this->name()
            << "\": the expression is not convertible to the same Buffer type and/or dimensions.\n";

        if (this->types_defined()) {
            user_assert(Type(buffer.type()) == this->type())
                << "Output should have type=" << this->type() << " but saw type=" << Type(buffer.type()) << "\n";
        }
        if (this->dimensions_defined()) {
            user_assert(buffer.dimensions() == this->dimensions())
                << "Output should have dim=" << this->dimensions() << " but saw dim=" << buffer.dimensions() << "\n";
        }

        internal_assert(this->exprs_.empty() && this->funcs_.size() == 1);
        user_assert(!this->funcs_.at(0).defined());
        this->funcs_.at(0)(_) = buffer(_);

        return *this;
    }

    // Allow assignment from a StubOutputBuffer to an Output<Buffer>;
    // this allows us to pipeline the results of a Stub to the results
    // of the enclosing Generator.
    template<typename T2>
    NO_INLINE GeneratorOutput_Buffer<T> &operator=(const StubOutputBuffer<T2> &stub_output_buffer) {
        this->check_value_writable();

        const auto &f = stub_output_buffer.f;
        internal_assert(f.defined());

        const auto &output_types = f.output_types();
        user_assert(output_types.size() == 1)
            << "Output should have size=1 but saw size=" << output_types.size() << "\n";

        Buffer<> other(output_types.at(0), nullptr, std::vector<int>(f.dimensions(), 1));
        user_assert(T::can_convert_from(other))
            << "Cannot assign to the Output \"" << this->name()
            << "\": the expression is not convertible to the same Buffer type and/or dimensions.\n";

        if (this->types_defined()) {
            user_assert(output_types.at(0) == this->type())
                << "Output should have type=" << this->type() << " but saw type=" << output_types.at(0) << "\n";
        }
        if (this->dimensions_defined()) {
            user_assert(f.dimensions() == this->dimensions())
                << "Output should have dim=" << this->dimensions() << " but saw dim=" << f.dimensions() << "\n";
        }

        internal_assert(this->exprs_.empty() && this->funcs_.size() == 1);
        user_assert(!this->funcs_.at(0).defined());
        this->funcs_[0] = f;

        return *this;
    }
};


template<typename T>
class GeneratorOutput_Func : public GeneratorOutputImpl<T> {
private:
    using Super = GeneratorOutputImpl<T>;

    NO_INLINE Func &get_assignable_func_ref(size_t i) {
        internal_assert(this->exprs_.empty() && this->funcs_.size() > i);
        return this->funcs_.at(i);
    }

protected:
    using TBase = typename Super::TBase;

protected:
    GeneratorOutput_Func(const std::string &name, const std::vector<Type> &t, int d)
        : Super(name, IOKind::Function, t, d) {
    }

    GeneratorOutput_Func(size_t array_size, const std::string &name, const std::vector<Type> &t, int d)
        : Super(array_size, name, IOKind::Function, t, d) {
    }

public:
    // Allow Output<Func> = Func
    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutput_Func<T> &operator=(const Func &f) {
        this->check_value_writable();

        // Don't bother verifying the Func type, dimensions, etc., here: 
        // That's done later, when we produce the pipeline.
        get_assignable_func_ref(0) = f;
        return *this;
    }

    // Allow Output<Func[]> = Func
    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Func &operator[](size_t i) {
        this->check_value_writable();
        return get_assignable_func_ref(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const Func &operator[](size_t i) const {
        return Super::operator[](i);
    }
};


template<typename T>
class GeneratorOutput_Arithmetic : public GeneratorOutputImpl<T> {
private:
    using Super = GeneratorOutputImpl<T>;
protected:
    using TBase = typename Super::TBase;

protected:
    explicit GeneratorOutput_Arithmetic(const std::string &name)
        : Super(name, IOKind::Function, {type_of<TBase>()}, 0) {
    }

    GeneratorOutput_Arithmetic(size_t array_size, const std::string &name)
        : Super(array_size, name, IOKind::Function, {type_of<TBase>()}, 0) {
    }
};

template<typename T, typename TBase = typename std::remove_all_extents<T>::type>
using GeneratorOutputImplBase =
    typename select_type<
        cond<has_static_halide_type_method<TBase>::value, GeneratorOutput_Buffer<T>>,
        cond<std::is_same<TBase, Func>::value, GeneratorOutput_Func<T>>,
        cond<std::is_arithmetic<TBase>::value, GeneratorOutput_Arithmetic<T>>
    >::type;

}  // namespace Internal

template <typename T>
class GeneratorOutput : public Internal::GeneratorOutputImplBase<T> {
private:
    using Super = Internal::GeneratorOutputImplBase<T>;
protected:
    using TBase = typename Super::TBase;

public:
    explicit GeneratorOutput(const std::string &name)
        : Super(name) {
    }

    explicit GeneratorOutput(const char *name)
        : GeneratorOutput(std::string(name)) {
    }

    GeneratorOutput(size_t array_size, const std::string &name)
        : Super(array_size, name) {
    }

    GeneratorOutput(const std::string &name, int d)
        : Super(name, {}, d) {
    }

    GeneratorOutput(const std::string &name, const Type &t, int d)
        : Super(name, {t}, d) {
    }

    GeneratorOutput(const std::string &name, const std::vector<Type> &t, int d)
        : Super(name, t, d) {
    }

    GeneratorOutput(size_t array_size, const std::string &name, int d)
        : Super(array_size, name, {}, d) {
    }

    GeneratorOutput(size_t array_size, const std::string &name, const Type &t, int d)
        : Super(array_size, name, {t}, d) {
    }

    GeneratorOutput(size_t array_size, const std::string &name, const std::vector<Type> &t, int d)
        : Super(array_size, name, t, d) {
    }

    // TODO: This used to take the buffer as a const ref. This no longer works as
    // using it in a Pipeline might change the dev field so it is currently
    // not considered const. We should consider how this really ought to work.
    template <typename T2>
    GeneratorOutput<T> &operator=(Buffer<T2> &buffer) {
        Super::operator=(buffer);
        return *this;
    }

    template <typename T2>
    GeneratorOutput<T> &operator=(const Internal::StubOutputBuffer<T2> &stub_output_buffer) {
        Super::operator=(stub_output_buffer);
        return *this;
    }

    GeneratorOutput<T> &operator=(const Func &f) {
        Super::operator=(f);
        return *this;
    }
};

namespace Internal {

template<typename T>
T parse_scalar(const std::string &value) {
    std::istringstream iss(value);
    T t;
    iss >> t;
    user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << value;
    return t;
}

EXPORT std::vector<Type> parse_halide_type_list(const std::string &types);

// This is a type of GeneratorParam used internally to create 'synthetic' params
// (e.g. image.type, image.dim); it is not possible for user code to instantiate it.
template<typename T>
class GeneratorParam_Synthetic : public GeneratorParamImpl<T> {
public:
    void set_from_string(const std::string &new_value_string) override {
        set_from_string_impl<T>(new_value_string);
    }

    std::string to_string() const override {
        internal_error;
        return std::string();
    }

    std::string call_to_string(const std::string &v) const override {
        internal_error;
        return std::string();
    }

    std::string get_c_type() const override {
        internal_error;
        return std::string();
    }

    bool is_synthetic_param() const override {
        return true;
    }

private:
    friend class GeneratorBase;

    enum Which { Type, Dim, ArraySize };
    GeneratorParam_Synthetic(const std::string &name, GIOBase &gio, Which which) : GeneratorParamImpl<T>(name, T()), gio(gio), which(which) {}

    template <typename T2 = T, typename std::enable_if<std::is_same<T2, ::Halide::Type>::value>::type * = nullptr>
    void set_from_string_impl(const std::string &new_value_string) {
        internal_assert(which == Type);
        gio.types_ = parse_halide_type_list(new_value_string);
    }

    template <typename T2 = T, typename std::enable_if<std::is_integral<T2>::value>::type * = nullptr>
    void set_from_string_impl(const std::string &new_value_string) {
        if (which == Dim) {
            gio.dimensions_ = parse_scalar<T2>(new_value_string);
        } else if (which == ArraySize) {
            gio.array_size_ = parse_scalar<T2>(new_value_string);
        } else {
            internal_error;
        }
    }

    GIOBase &gio;
    const Which which;
};


class GeneratorStub;

}  // namespace Internal

/** GeneratorContext is an abstract interface that is used when constructing a Generator Stub;
 * it is used to allow the outer context (typically, either a Generator or "top-level" code)
 * to specify certain information to the inner context to ensure that inner and outer
 * Generators are compiled in a compatible way; at present, this is used to propagate
 * the outer Target to the inner Generator. */
class GeneratorContext {
public:
    virtual ~GeneratorContext() {};
    virtual Target get_target() const = 0;
protected:
    friend class Internal::GeneratorBase;
    virtual std::shared_ptr<Internal::ValueTracker> get_value_tracker() const = 0;
};

/** JITGeneratorContext is a utility implementation of GeneratorContext that
 * is intended for use when using Generator Stubs with the JIT; it simply
 * allows you to wrap a specific Target in a GeneratorContext for use with a stub,
 * often in conjunction with the Halide::get_target_from_environment() call:
 *
 * \code
 *   auto my_stub = MyStub(
 *       JITGeneratorContext(get_target_from_environment()),
 *       // inputs
 *       { ... },
 *       // generator params
 *       { ... }
 *   );
 */
class JITGeneratorContext : public GeneratorContext {
public:
    explicit JITGeneratorContext(const Target &t)
        : target(t)
        , value_tracker(std::make_shared<Internal::ValueTracker>()) {}
    Target get_target() const override { return target; }
protected:
    std::shared_ptr<Internal::ValueTracker> get_value_tracker() const override { return value_tracker; }
private:
    const Target target;
    const std::shared_ptr<Internal::ValueTracker> value_tracker;
};

class NamesInterface {
    // Names in this class are only intended for use in derived classes.
protected:
    // Import a consistent list of Halide names that can be used in
    // Halide generators without qualification.
    using Expr = Halide::Expr;
    using ExternFuncArgument = Halide::ExternFuncArgument;
    using Func = Halide::Func;
    using GeneratorContext = Halide::GeneratorContext;
    using ImageParam = Halide::ImageParam;
    using LoopLevel = Halide::LoopLevel;
    using Pipeline = Halide::Pipeline;
    using RDom = Halide::RDom;
    using TailStrategy = Halide::TailStrategy;
    using Target = Halide::Target;
    using Tuple = Halide::Tuple;
    using Type = Halide::Type;
    using Var = Halide::Var;
    using NameMangling = Halide::NameMangling;
    template <typename T> static Expr cast(Expr e) { return Halide::cast<T>(e); }
    static inline Expr cast(Halide::Type t, Expr e) { return Halide::cast(t, e); }
    template <typename T> using GeneratorParam = Halide::GeneratorParam<T>;
    template <typename T> using ScheduleParam = Halide::ScheduleParam<T>;
    template <typename T = void> using Buffer = Halide::Buffer<T>;
    template <typename T> using Param = Halide::Param<T>;
    static inline Type Bool(int lanes = 1) { return Halide::Bool(lanes); }
    static inline Type Float(int bits, int lanes = 1) { return Halide::Float(bits, lanes); }
    static inline Type Int(int bits, int lanes = 1) { return Halide::Int(bits, lanes); }
    static inline Type UInt(int bits, int lanes = 1) { return Halide::UInt(bits, lanes); }
};

namespace Internal {

template<typename ...Args>
struct NoRealizations : std::false_type {};

template<>
struct NoRealizations<> : std::true_type {};

template<typename T, typename ...Args>
struct NoRealizations<T, Args...> {
    static const bool value = !std::is_convertible<T, Realization>::value && NoRealizations<Args...>::value;
};

class GeneratorStub;
class SimpleGeneratorFactory;

class GeneratorBase : public NamesInterface, public GeneratorContext {
public:
    GeneratorParam<Target> target{ "target", Halide::get_host_target() };

    struct EmitOptions {
        bool emit_o, emit_h, emit_cpp, emit_assembly, emit_bitcode, emit_stmt, emit_stmt_html, emit_static_library, emit_cpp_stub;
        // This is an optional map used to replace the default extensions generated for
        // a file: if an key matches an output extension, emit those files with the
        // corresponding value instead (e.g., ".s" -> ".assembly_text"). This is
        // empty by default; it's mainly useful in build environments where the default
        // extensions are problematic, and avoids the need to rename output files
        // after the fact.
        std::map<std::string, std::string> substitutions;
        EmitOptions()
            : emit_o(false), emit_h(true), emit_cpp(false), emit_assembly(false),
              emit_bitcode(false), emit_stmt(false), emit_stmt_html(false), emit_static_library(true), emit_cpp_stub(false) {}
    };

    EXPORT virtual ~GeneratorBase();

    Target get_target() const override { return target; }

    EXPORT void set_generator_param(const std::string &name, const std::string &value);
    EXPORT void set_generator_param_values(const std::map<std::string, std::string> &params);

    template<typename T>
    GeneratorBase &set_generator_param(const std::string &name, const T &value) {
        find_generator_param_by_name(name).set(value);
        return *this;
    }

    template<typename T>
    GeneratorBase &set_schedule_param(const std::string &name, const T &value) {
        find_schedule_param_by_name(name).set(value);
        return *this;
    }

    EXPORT void set_schedule_param_values(const std::map<std::string, std::string> &params,
                                          const std::map<std::string, LoopLevel> &looplevel_params);

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for the current target. */
    int natural_vector_size(Halide::Type t) const {
        return get_target().natural_vector_size(t);
    }

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for the current target. */
    template <typename data_t>
    int natural_vector_size() const {
        return get_target().natural_vector_size<data_t>();
    }

    EXPORT void emit_cpp_stub(const std::string &stub_file_path);

    // Call build() and produce a Module for the result.
    // If function_name is empty, generator_name() will be used for the function.
    EXPORT Module build_module(const std::string &function_name = "",
                               const LoweredFunc::LinkageType linkage_type = LoweredFunc::External);

    Realization realize(std::vector<int32_t> sizes) {
        check_scheduled("realize");
        return produce_pipeline().realize(sizes, get_target());
    }

    // Only enable if none of the args are Realization; otherwise we can incorrectly
    // select this method instead of the Realization-as-outparam variant
    template <typename... Args, typename std::enable_if<NoRealizations<Args...>::value>::type * = nullptr>
    Realization realize(Args&&... args) {
        check_scheduled("realize");
        return produce_pipeline().realize(std::forward<Args>(args)..., get_target());
    }

    void realize(Realization r) {
        check_scheduled("realize");
        produce_pipeline().realize(r, get_target());
    }

protected:
    EXPORT GeneratorBase(size_t size, const void *introspection_helper);
    EXPORT void init_from_context(const Halide::GeneratorContext &context);

    EXPORT virtual Pipeline build_pipeline() = 0;
    EXPORT virtual void call_generate() = 0;
    EXPORT virtual void call_schedule() = 0;

    std::shared_ptr<ValueTracker> get_value_tracker() const override { return value_tracker; }

    EXPORT void track_parameter_values(bool include_outputs);

    EXPORT void pre_build();
    EXPORT void post_build();
    EXPORT void pre_generate();
    EXPORT void post_generate();
    EXPORT void pre_schedule();
    EXPORT void post_schedule();
    EXPORT Pipeline produce_pipeline();

    template<typename T>
    using Input = GeneratorInput<T>;

    template<typename T>
    using Output = GeneratorOutput<T>;

    // A Generator's creation and usage must go in a certain phase to ensure correctness;
    // the state machine here is advanced and checked at various points to ensure
    // this is the case.
    enum Phase {
        // Generator has just come into being.
        Created,

        // All Input<>/Param<> fields have been set. (Applicable only in JIT mode;
        // in AOT mode, this can be skipped, going Created->GenerateCalled directly.)
        InputsSet,

        // Generator has had its generate() method called. (For Generators with
        // a build() method instead of generate(), this phase will be skipped
        // and will advance directly to ScheduleCalled.)
        GenerateCalled,

        // Generator has had its schedule() method called.
        ScheduleCalled,
    } phase{Created};

    void check_exact_phase(Phase expected_phase) const;
    void check_min_phase(Phase expected_phase) const;
    void advance_phase(Phase new_phase);

private:
    friend void ::Halide::Internal::generator_test();
    friend class GeneratorParamBase;
    friend class GeneratorInputBase;
    friend class GeneratorOutputBase;
    friend class GeneratorStub;
    friend class SimpleGeneratorFactory;
    friend class StubOutputBufferBase;

    struct ParamInfo {
        EXPORT ParamInfo(GeneratorBase *generator, const size_t size);

        // Ordered-list  of non-null ptrs to GeneratorParam<> fields.
        std::vector<Internal::GeneratorParamBase *> generator_params;

        // Ordered-list  of non-null ptrs to Input<>/Output<> fields; empty if old-style Generator.
        std::vector<Internal::GeneratorInputBase *> filter_inputs;
        std::vector<Internal::GeneratorOutputBase *> filter_outputs;

        // Ordered-list of non-null ptrs to Param<> or ImageParam<> fields; empty if new-style Generator.
        std::vector<Internal::Parameter *> filter_params;

        // Convenience structure to look up GP/SP by name.
        std::map<std::string, Internal::GeneratorParamBase *> params_by_name;

    private:
        // list of synthetic GP's that we dynamically created; this list only exists to simplify
        // lifetime management, and shouldn't be accessed directly outside of our ctor/dtor,
        // regardless of friend access.
        std::vector<std::unique_ptr<Internal::GeneratorParamBase>> owned_synthetic_params;
    };

    const size_t size;
    // Lazily-allocated-and-inited struct with info about our various Params. 
    // Do not access directly: use the param_info() getter to lazy-init.
    std::unique_ptr<ParamInfo> param_info_ptr; 

    std::shared_ptr<Internal::ValueTracker> value_tracker;
    bool inputs_set{false};
    std::string generator_name;

    // Return our ParamInfo (lazy-initing as needed).
    EXPORT ParamInfo &param_info();

    EXPORT Internal::GeneratorParamBase &find_param_by_name(const std::string &name);
    Internal::GeneratorParamBase &find_generator_param_by_name(const std::string &name) {
        auto &p = find_param_by_name(name);
        internal_assert(!p.is_schedule_param());
        return p;
    }
    Internal::GeneratorParamBase &find_schedule_param_by_name(const std::string &name) {
        auto &p = find_param_by_name(name);
        internal_assert(p.is_schedule_param());
        return p;
    }

    EXPORT void check_scheduled(const char* m) const;

    EXPORT void build_params(bool force = false);

    // Provide private, unimplemented, wrong-result-type methods here
    // so that Generators don't attempt to call the global methods
    // of the same name by accident: use the get_target() method instead.
    void get_host_target();
    void get_jit_target_from_environment();
    void get_target_from_environment();

    EXPORT Func get_first_output();
    EXPORT Func get_output(const std::string &n);
    EXPORT std::vector<Func> get_output_vector(const std::string &n);

    void set_generator_name(const std::string &n) {
        internal_assert(generator_name.empty());
        generator_name = n;
    }

    EXPORT void set_inputs_vector(const std::vector<std::vector<StubInput>> &inputs);

    GeneratorBase(const GeneratorBase &) = delete;
    void operator=(const GeneratorBase &) = delete;
};

class GeneratorFactory {
public:
    virtual ~GeneratorFactory() {}
    // Note that this method must never return null:
    // if it cannot return a valid Generator, it should assert-fail.
    virtual std::unique_ptr<GeneratorBase> create(const GeneratorContext &context, 
                                                  const std::map<std::string, std::string> &params) const = 0;
};

using GeneratorCreateFunc = std::function<std::unique_ptr<Internal::GeneratorBase>(const GeneratorContext &context)>;

class SimpleGeneratorFactory : public GeneratorFactory {
public:
    SimpleGeneratorFactory(GeneratorCreateFunc create_func, const std::string &generator_name)
        : create_func(create_func), generator_name(generator_name) {
        internal_assert(create_func != nullptr);
    }

    std::unique_ptr<Internal::GeneratorBase> create(const GeneratorContext &context,
                                                    const std::map<std::string, std::string> &params) const override {
        auto g = create_func(context);
        internal_assert(g.get() != nullptr);
        g->set_generator_name(generator_name);
        g->set_generator_param_values(params);
        return g;
    }
private:
    const GeneratorCreateFunc create_func;
    const std::string generator_name;
};

class GeneratorRegistry {
public:
    EXPORT static void register_factory(const std::string &name, std::unique_ptr<GeneratorFactory> factory);
    EXPORT static void unregister_factory(const std::string &name);
    EXPORT static std::vector<std::string> enumerate();
    // Note that this method will never return null:
    // if it cannot return a valid Generator, it should assert-fail.
    EXPORT static std::unique_ptr<GeneratorBase> create(const std::string &name,
                                                        const GeneratorContext &context,
                                                        const std::map<std::string, std::string> &params);

private:
    using GeneratorFactoryMap = std::map<const std::string, std::unique_ptr<GeneratorFactory>>;

    GeneratorFactoryMap factories;
    std::mutex mutex;

    EXPORT static GeneratorRegistry &get_registry();

    GeneratorRegistry() {}
    GeneratorRegistry(const GeneratorRegistry &) = delete;
    void operator=(const GeneratorRegistry &) = delete;
};

}  // namespace Internal

template <class T> 
class Generator : public Internal::GeneratorBase {
protected:
    Generator() :
        Internal::GeneratorBase(sizeof(T),
                                Internal::Introspection::get_introspection_helper<T>()) {}

public:
    static std::unique_ptr<Internal::GeneratorBase> create(const Halide::GeneratorContext &context) {
        // We must have an object of type T (not merely GeneratorBase) to call a protected method,
        // because CRTP is a weird beast.
        T *t = new T;
        t->init_from_context(context);
        return std::unique_ptr<Internal::GeneratorBase>(t);
    }

private:
    // Implementations for build_pipeline_impl(), specialized on whether we
    // have build() or generate()/schedule() methods.

    // std::is_member_function_pointer will fail if there is no member of that name,
    // so we use a little SFINAE to detect if there are method-shaped members.
    template<typename>
    struct type_sink { typedef void type; };

    template<typename T2, typename = void>
    struct has_generate_method : std::false_type {};

    template<typename T2>
    struct has_generate_method<T2, typename type_sink<decltype(std::declval<T2>().generate())>::type> : std::true_type {};

    template<typename T2, typename = void>
    struct has_schedule_method : std::false_type {};

    template<typename T2>
    struct has_schedule_method<T2, typename type_sink<decltype(std::declval<T2>().schedule())>::type> : std::true_type {};

    template <typename T2 = T,
              typename std::enable_if<!has_generate_method<T2>::value>::type * = nullptr>
    Pipeline build_pipeline_impl() {
        static_assert(!has_schedule_method<T2>::value, "The schedule() method is ignored if you define a build() method; use generate() instead.");
        pre_build();
        Pipeline p = ((T *)this)->build();
        post_build();
        return p;
    }
    template <typename T2 = T,
              typename std::enable_if<has_generate_method<T2>::value>::type * = nullptr>
    Pipeline build_pipeline_impl() {
        ((T *)this)->call_generate_impl();
        ((T *)this)->call_schedule_impl();
        return produce_pipeline();
    }

    // Implementations for call_generate_impl(), specialized on whether we
    // have build() or generate()/schedule() methods.

    template <typename T2 = T,
              typename std::enable_if<!has_generate_method<T2>::value>::type * = nullptr>
    void call_generate_impl() {
        user_error << "Unimplemented";
    }

    template <typename T2 = T,
              typename std::enable_if<has_generate_method<T2>::value>::type * = nullptr>
    void call_generate_impl() {
        T *t = (T*)this;
        static_assert(std::is_void<decltype(t->generate())>::value, "generate() must return void");
        pre_generate();
        t->generate();
        post_generate();
    }

    // Implementations for call_schedule_impl(), specialized on whether we
    // have build() or generate()/schedule() methods.

    template <typename T2 = T,
              typename std::enable_if<!has_schedule_method<T2>::value>::type * = nullptr>
    void call_schedule_impl() {
        user_error << "Unimplemented";
    }

    template <typename T2 = T,
              typename std::enable_if<has_schedule_method<T2>::value>::type * = nullptr>
    void call_schedule_impl() {
        T *t = (T*)this;
        static_assert(std::is_void<decltype(t->schedule())>::value, "schedule() must return void");
        pre_schedule();
        t->schedule();
        post_schedule();
    }

protected:
    Pipeline build_pipeline() override {
        return this->build_pipeline_impl();
    }

    void call_generate() override {
        this->call_generate_impl();
    }

    void call_schedule() override {
        this->call_schedule_impl();
    }
private:
    friend void ::Halide::Internal::generator_test();
    friend class Internal::SimpleGeneratorFactory;
    friend void ::Halide::Internal::generator_test();

    Generator(const Generator &) = delete;
    void operator=(const Generator &) = delete;
};

template <class GeneratorClass> 
class RegisterGenerator {
public:
    RegisterGenerator(const char* generator_name) {
        std::unique_ptr<Internal::SimpleGeneratorFactory> f(new Internal::SimpleGeneratorFactory(GeneratorClass::create, generator_name));
        Internal::GeneratorRegistry::register_factory(generator_name, std::move(f));
    }
};

namespace Internal {

class GeneratorStub : public NamesInterface {
public:
    // default ctor
    GeneratorStub() = default;

    // move constructor
    GeneratorStub(GeneratorStub&& that) : generator(std::move(that.generator)) {}

    // move assignment operator
    GeneratorStub& operator=(GeneratorStub&& that) {
        generator = std::move(that.generator);
        return *this;
    }

    Target get_target() const { return generator->get_target(); }

    // schedule method
    EXPORT void schedule(const std::map<std::string, std::string> &schedule_params,
                         const std::map<std::string, LoopLevel> &schedule_params_looplevels);

    // Overloads for first output
    operator Func() const {
        return get_first_output();
    }

    template <typename... Args>
    FuncRef operator()(Args&&... args) const {
        return get_first_output()(std::forward<Args>(args)...);
    }

    template <typename ExprOrVar>
    FuncRef operator()(std::vector<ExprOrVar> args) const {
        return get_first_output()(args);
    }

    Realization realize(std::vector<int32_t> sizes) {
        return generator->realize(sizes);
    }

    // Only enable if none of the args are Realization; otherwise we can incorrectly
    // select this method instead of the Realization-as-outparam variant
    template <typename... Args, typename std::enable_if<NoRealizations<Args...>::value>::type * = nullptr>
    Realization realize(Args&&... args) {
        return generator->realize(std::forward<Args>(args)...);
    }

    void realize(Realization r) {
        generator->realize(r);
    }

    virtual ~GeneratorStub() {}

protected:
    typedef std::function<std::unique_ptr<GeneratorBase>(const GeneratorContext&, const std::map<std::string, std::string>&)> GeneratorFactory;

    EXPORT GeneratorStub(const GeneratorContext &context,
                  GeneratorFactory generator_factory,
                  const std::map<std::string, std::string> &generator_params,
                  const std::vector<std::vector<Internal::StubInput>> &inputs);

    // Output(s)
    // TODO: identify vars used
    Func get_output(const std::string &n) const {
        return generator->get_output(n);
    }

    template<typename T2>
    T2 get_output_buffer(const std::string &n) const {
        return T2(get_output(n), generator);
    }

    std::vector<Func> get_output_vector(const std::string &n) const {
        return generator->get_output_vector(n);
    }

    bool has_generator() const {
        return generator != nullptr;
    }

    template<typename Ratio>
    static double ratio_to_double() {
        return (double)Ratio::num / (double)Ratio::den;
    }

    static std::vector<StubInput> to_stub_input_vector(const Expr &e) {
        return { StubInput(e) };
    }

    static std::vector<StubInput> to_stub_input_vector(const Func &f) {
        return { StubInput(f) };
    }

    template<typename T = void>
    static std::vector<StubInput> to_stub_input_vector(const StubInputBuffer<T> &b) {
        return { StubInput(b) };
    }

    template <typename T>
    static std::vector<StubInput> to_stub_input_vector(const std::vector<T> &v) {
        std::vector<StubInput> r;
        std::copy(v.begin(), v.end(), std::back_inserter(r));
        return r;
    }

    EXPORT void verify_same_funcs(const Func &a, const Func &b);
    EXPORT void verify_same_funcs(const std::vector<Func>& a, const std::vector<Func>& b);

    template<typename T2>
    void verify_same_funcs(const StubOutputBuffer<T2> &a, const StubOutputBuffer<T2> &b) {
        verify_same_funcs(a.f, b.f);
    }

private:
    std::shared_ptr<GeneratorBase> generator;

    Func get_first_output() const {
        return generator->get_first_output();
    }
    explicit GeneratorStub(const GeneratorStub &) = delete;
    void operator=(const GeneratorStub &) = delete;
};

}  // namespace Internal


}  // namespace Halide

#define HALIDE_REGISTER_GENERATOR(GEN_CLASS_NAME, GEN_REGISTRY_NAME) \
    namespace ns_reg_gen { static auto reg_##GEN_CLASS_NAME = Halide::RegisterGenerator<GEN_CLASS_NAME>(GEN_REGISTRY_NAME); }


#endif  // HALIDE_GENERATOR_H_
