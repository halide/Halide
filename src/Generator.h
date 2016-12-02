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

namespace Internal {

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

class GeneratorParamBase {
public:
    EXPORT explicit GeneratorParamBase(const std::string &name);
    EXPORT virtual ~GeneratorParamBase();

    const std::string name;

protected:
    friend class GeneratorBase;
    friend class StubEmitter;

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

private:
    explicit GeneratorParamBase(const GeneratorParamBase &) = delete;
    void operator=(const GeneratorParamBase &) = delete;
};

template<typename T>
class GeneratorParamImpl : public GeneratorParamBase {
public:
    GeneratorParamImpl(const std::string &name, const T &value) : GeneratorParamBase(name), value_(value) {}

    T value() const { return value_; }

    operator T() const { return this->value(); }
    
    operator Expr() const { return make_const(type_of<T>(), this->value()); }

    virtual void set(const T &new_value) { value_ = new_value; }

protected:
    bool is_looplevel_param() const override { 
        return std::is_same<T, LoopLevel>::value; 
    }

private:
    T value_;
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

    void set(const T &new_value) override {
        user_assert(new_value >= min && new_value <= max) << "Value out of range: " << new_value;
        GeneratorParamImpl<T>::set(new_value);
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
        this->set(it->second);
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
decltype((Other)0 + (T)0) operator+(Other a, const GeneratorParam<T> &b) { return a + (T)b; }
template <typename Other, typename T>
decltype((T)0 + (Other)0) operator+(const GeneratorParam<T> &a, Other b) { return (T)a + b; }
// @}

/** Subtraction between GeneratorParam<T> and any type that supports operator- with T.
 * Returns type of underlying operator-. */
// @{
template <typename Other, typename T>
decltype((Other)0 - (T)0) operator-(Other a, const GeneratorParam<T> &b) { return a - (T)b; }
template <typename Other, typename T>
decltype((T)0 - (Other)0)  operator-(const GeneratorParam<T> &a, Other b) { return (T)a - b; }
// @}

/** Multiplication between GeneratorParam<T> and any type that supports operator* with T.
 * Returns type of underlying operator*. */
// @{
template <typename Other, typename T>
decltype((Other)0 * (T)0) operator*(Other a, const GeneratorParam<T> &b) { return a * (T)b; }
template <typename Other, typename T>
decltype((Other)0 * (T)0) operator*(const GeneratorParam<T> &a, Other b) { return (T)a * b; }
// @}

/** Division between GeneratorParam<T> and any type that supports operator/ with T.
 * Returns type of underlying operator/. */
// @{
template <typename Other, typename T>
decltype((Other)0 / (T)1) operator/(Other a, const GeneratorParam<T> &b) { return a / (T)b; }
template <typename Other, typename T>
decltype((T)0 / (Other)1) operator/(const GeneratorParam<T> &a, Other b) { return (T)a / b; }
// @}

/** Modulo between GeneratorParam<T> and any type that supports operator% with T.
 * Returns type of underlying operator%. */
// @{
template <typename Other, typename T>
decltype((Other)0 % (T)1) operator%(Other a, const GeneratorParam<T> &b) { return a % (T)b; }
template <typename Other, typename T>
decltype((T)0 % (Other)1) operator%(const GeneratorParam<T> &a, Other b) { return (T)a % b; }
// @}

/** Greater than comparison between GeneratorParam<T> and any type that supports operator> with T.
 * Returns type of underlying operator>. */
// @{
template <typename Other, typename T>
decltype((Other)0 > (T)1) operator>(Other a, const GeneratorParam<T> &b) { return a > (T)b; }
template <typename Other, typename T>
decltype((T)0 > (Other)1) operator>(const GeneratorParam<T> &a, Other b) { return (T)a > b; }
// @}

/** Less than comparison between GeneratorParam<T> and any type that supports operator< with T.
 * Returns type of underlying operator<. */
// @{
template <typename Other, typename T>
decltype((Other)0 < (T)1) operator<(Other a, const GeneratorParam<T> &b) { return a < (T)b; }
template <typename Other, typename T>
decltype((T)0 < (Other)1) operator<(const GeneratorParam<T> &a, Other b) { return (T)a < b; }
// @}

/** Greater than or equal comparison between GeneratorParam<T> and any type that supports operator>= with T.
 * Returns type of underlying operator>=. */
// @{
template <typename Other, typename T>
decltype((Other)0 >= (T)1) operator>=(Other a, const GeneratorParam<T> &b) { return a >= (T)b; }
template <typename Other, typename T>
decltype((T)0 >= (Other)1) operator>=(const GeneratorParam<T> &a, Other b) { return (T)a >= b; }
// @}

/** Less than or equal comparison between GeneratorParam<T> and any type that supports operator<= with T.
 * Returns type of underlying operator<=. */
// @{
template <typename Other, typename T>
decltype((Other)0 <= (T)1) operator<=(Other a, const GeneratorParam<T> &b) { return a <= (T)b; }
template <typename Other, typename T>
decltype((T)0 <= (Other)1) operator<=(const GeneratorParam<T> &a, Other b) { return (T)a <= b; }
// @}

/** Equality comparison between GeneratorParam<T> and any type that supports operator== with T.
 * Returns type of underlying operator==. */
// @{
template <typename Other, typename T>
decltype((Other)0 == (T)1) operator==(Other a, const GeneratorParam<T> &b) { return a == (T)b; }
template <typename Other, typename T>
decltype((T)0 == (Other)1) operator==(const GeneratorParam<T> &a, Other b) { return (T)a == b; }
// @}

/** Inequality comparison between between GeneratorParam<T> and any type that supports operator!= with T.
 * Returns type of underlying operator!=. */
// @{
template <typename Other, typename T>
decltype((Other)0 != (T)1) operator!=(Other a, const GeneratorParam<T> &b) { return a != (T)b; }
template <typename Other, typename T>
decltype((T)0 != (Other)1) operator!=(const GeneratorParam<T> &a, Other b) { return (T)a != b; }
// @}

/** Logical and between between GeneratorParam<T> and any type that supports operator&& with T.
 * Returns type of underlying operator&&. */
// @{
template <typename Other, typename T>
decltype((Other)0 && (T)1) operator&&(Other a, const GeneratorParam<T> &b) { return a && (T)b; }
template <typename Other, typename T>
decltype((T)0 && (Other)1) operator&&(const GeneratorParam<T> &a, Other b) { return (T)a && b; }
// @}

/** Logical or between between GeneratorParam<T> and any type that supports operator&& with T.
 * Returns type of underlying operator||. */
// @{
template <typename Other, typename T>
decltype((Other)0 || (T)1) operator||(Other a, const GeneratorParam<T> &b) { return a || (T)b; }
template <typename Other, typename T>
decltype((T)0 || (Other)1) operator||(const GeneratorParam<T> &a, Other b) { return (T)a || b; }
// @}

/* min and max are tricky as the language support for these is in the std
 * namespace. In order to make this work, forwarding functions are used that
 * are declared in a namespace that has std::min and std::max in scope.
 */
namespace Internal { namespace GeneratorMinMax {

using std::max;
using std::min;

template <typename Other, typename T>
decltype(min((Other)0, (T)1)) min_forward(Other a, const GeneratorParam<T> &b) { return min(a, (T)b); }
template <typename Other, typename T>
decltype(min((T)0, (Other)1)) min_forward(const GeneratorParam<T> &a, Other b) { return min((T)a, b); }

template <typename Other, typename T>
decltype(max((Other)0, (T)1)) max_forward(Other a, const GeneratorParam<T> &b) { return max(a, (T)b); }
template <typename Other, typename T>
decltype(max((T)0, (Other)1)) max_forward(const GeneratorParam<T> &a, Other b) { return max((T)a, b); }

}}

/** Compute minimum between GeneratorParam<T> and any type that supports min with T.
 * Will automatically import std::min. Returns type of underlying min call. */
// @{
template <typename Other, typename T>
auto min(Other a, const GeneratorParam<T> &b) -> decltype(Internal::GeneratorMinMax::min_forward(a, b)) {
    return Internal::GeneratorMinMax::min_forward(a, b);
}
template <typename Other, typename T>
auto min(const GeneratorParam<T> &a, Other b) -> decltype(Internal::GeneratorMinMax::min_forward(a, b)) {
    return Internal::GeneratorMinMax::min_forward(a, b);
}
// @}

/** Compute the maximum value between GeneratorParam<T> and any type that supports max with T.
 * Will automatically import std::max. Returns type of underlying max call. */
// @{
template <typename Other, typename T>
auto max(Other a, const GeneratorParam<T> &b) -> decltype(Internal::GeneratorMinMax::max_forward(a, b)) {
    return Internal::GeneratorMinMax::max_forward(a, b);
}
template <typename Other, typename T>
auto max(const GeneratorParam<T> &a, Other b) -> decltype(Internal::GeneratorMinMax::max_forward(a, b)) {
    return Internal::GeneratorMinMax::max_forward(a, b);
}
// @}

/** Not operator for GeneratorParam */
template <typename T>
decltype(!(T)0) operator!(const GeneratorParam<T> &a) { return !(T)a; }

namespace Internal {

enum class IOKind { Scalar, Function };

// This is a union class that allows for convenient initialization of Stub Inputs
// via C++11 initializer-list syntax; it is only used in situations where the
// downstream consumer will be able to explicitly check that each value is
// of the expected/required kind.
class FuncOrExpr {
    const IOKind kind_;
    const Halide::Func func_;
    const Halide::Expr expr_;
public:
    // *not* explicit 
    FuncOrExpr(const Func &f) : kind_(IOKind::Function), func_(f), expr_(Expr()) {}
    FuncOrExpr(const Expr &e) : kind_(IOKind::Scalar), func_(Func()), expr_(e) {}

    IOKind kind() const {
        return kind_;
    }

    Func func() const {
        internal_assert(kind_ == IOKind::Function) << "Expected Func, got Expr";
        return func_;
    }

    Expr expr() const {
        internal_assert(kind_ == IOKind::Scalar) << "Expected Expr, got Func";
        return expr_;
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

    bool is_stub_usage_{false};

    EXPORT std::string array_name(size_t i) const;

    EXPORT virtual void verify_internals() const;

    EXPORT void check_matching_array_size(size_t size);
    EXPORT void check_matching_type_and_dim(const std::vector<Type> &t, int d);

    template<typename ElemType>
    const std::vector<ElemType> &get_values() const;

private:
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

    EXPORT GeneratorInputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
      : GeneratorInputBase(1, name, kind, t, d) {}

    EXPORT ~GeneratorInputBase() override;

    friend class GeneratorBase;

    std::vector<Parameter> parameters_;

    EXPORT void init_internals();
    EXPORT void set_inputs(const std::vector<FuncOrExpr> &inputs);

    EXPORT virtual void set_def_min_max();

    EXPORT void verify_internals() const override;

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
    ValueType operator[](size_t i) const {
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    ValueType at(size_t i) const {
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
class GeneratorInput_Func : public GeneratorInputImpl<T, Func> {
private:
    using Super = GeneratorInputImpl<T, Func>;

protected:
    using TBase = typename Super::TBase;

    // TODO: this logic to create vars based on specific name
    // patterns is already replicated in several places across Halide;
    // we should really centralize it into one place, as it seems likely
    // to be fragile.
    Expr MakeInt32Var(size_t array_index, const char* c, int d) const {
        const auto &p = this->parameters_.at(array_index);
        std::ostringstream s;
        s << p.name() << c << d;
        return Variable::make(Int(32), s.str(), p);
    }

    void check_has_buffer() const {
        user_assert(has_buffer()) << "This operation requires an Input<Func> that is backed by a Buffer.\n";
    }

public:
    // This is a minimal definition of "Dimension" that is just enough to satisfy
    // the needs of a "func-like" in BoundaryConditions.
    struct Dimension {
        const Expr min_, extent_, stride_;

        Expr min() const { return min_; }
        Expr extent() const { return extent_; }
        Expr stride() const { return stride_; }
        Expr max() const { return min() + extent(); }
    };

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

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    int dimensions() const {
        return this->funcs().at(0).dimensions(); 
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_min_constraint(int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(0).set_min_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_extent_constraint(int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(0).set_extent_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_stride_constraint(int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(0).set_stride_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_host_alignment_constraint(int e) {
        check_has_buffer();
        this->parameters_.at(0).set_host_alignment(e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr min(int i) const {
        check_has_buffer();
        return MakeInt32Var(0, ".min.", i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr extent(int i) const {
        check_has_buffer();
        return MakeInt32Var(0, ".extent.", i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr stride(int i) const {
        check_has_buffer();
        return MakeInt32Var(0, ".stride.", i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr width() const {
        check_has_buffer();
        return extent(0);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr height() {
        check_has_buffer();
        return extent(1);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr channels() const {
        check_has_buffer();
        return extent(2);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    int host_alignment() const {
        check_has_buffer();
        return this->parameters_.at(0).host_alignment();
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    const Dimension dim(int i) const {
        check_has_buffer();
        return { min(i), extent(i), stride(i) };
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    int dimensions(size_t array_index) const {
        return this->funcs().at(array_index).dimensions(); 
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_min_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(array_index).set_min_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_extent_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(array_index).set_extent_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_stride_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        this->parameters_.at(array_index).set_stride_constraint(i, e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorInput_Func<T> &set_host_alignment_constraint(size_t array_index, int e) {
        check_has_buffer();
        this->parameters_.at(array_index).set_host_alignment(e);
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr min(size_t array_index, int i) const {
        check_has_buffer();
        return MakeInt32Var(array_index, ".min.", i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr extent(size_t array_index, int i) const {
        check_has_buffer();
        return MakeInt32Var(array_index, ".extent.", i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr stride(size_t array_index, int i) const {
        check_has_buffer();
        return MakeInt32Var(array_index, ".stride.", i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr width(size_t array_index) const {
        check_has_buffer();
        return extent(array_index, 0);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr height(size_t array_index) {
        check_has_buffer();
        return extent(array_index, 1);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr channels(size_t array_index) const {
        check_has_buffer();
        return extent(array_index, 2);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    int host_alignment(size_t array_index) const {
        check_has_buffer();
        return this->parameters_.at(array_index).host_alignment();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const Dimension dim(size_t array_index, int i) const {
        check_has_buffer();
        return { min(array_index, i), extent(array_index, i), stride(array_index, i) };
    }

    bool has_buffer() const {
        return !this->is_stub_usage_;
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

template<typename T, typename TBase = typename std::remove_all_extents<T>::type> 
using GeneratorInputImplBase =
    typename select_type<
        cond<std::is_same<TBase, Func>::value, GeneratorInput_Func<T>>,
        cond<std::is_arithmetic<TBase>::value, GeneratorInput_Arithmetic<T>>,
        cond<std::is_scalar<TBase>::value,     GeneratorInput_Scalar<T>>
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
    using IntIfFunc =
        typename Internal::select_type<
            Internal::cond<std::is_same<TBase, Func>::value, int>,
            Internal::cond<true,                             Unused>
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
    GeneratorInput(const std::string &name, IntIfFunc d)
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
    GeneratorInput(size_t array_size, const std::string &name, IntIfFunc d)
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
                        const std::vector<Type> &t, 
                        int d);

    EXPORT GeneratorOutputBase(const std::string &name, 
                               const std::vector<Type> &t, 
                               int d)
      : GeneratorOutputBase(1, name, t, d) {}

    EXPORT ~GeneratorOutputBase() override;

    friend class GeneratorBase;

    EXPORT void init_internals();
    EXPORT void resize(size_t size);
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
    GeneratorOutputImpl(const std::string &name, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(name, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[kSomeConst]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && (std::extent<T2, 0>::value > 0)
    >::type * = nullptr>
    GeneratorOutputImpl(const std::string &name, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(std::extent<T2, 0>::value, name, t, d) {
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && std::extent<T2, 0>::value == 0
    >::type * = nullptr>
    GeneratorOutputImpl(const std::string &name, const std::vector<Type> &t, int d)
        : GeneratorOutputBase(-1, name, t, d) {
    }

    void check_has_buffer() const {
        user_assert(has_buffer()) << "This operation requires an Output<Func> that is backed by a Buffer.\n";
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
    int dimensions(size_t array_index) const {
        return get_values<ValueType>().at(array_index).dimensions(); 
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    int dimensions() const {
        return get_values<ValueType>().at(0).dimensions(); 
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    size_t size() const {
        return get_values<ValueType>().size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    ValueType operator[](size_t i) const {
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    ValueType at(size_t i) const {
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

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_min_constraint(int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(0).output_buffers()) {
            ob.set_min(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_extent_constraint(int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(0).output_buffers()) {
            ob.set_extent(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_stride_constraint(int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(0).output_buffers()) {
            ob.set_stride(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_host_alignment_constraint(int e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(0).output_buffers()) {
            ob.set_host_alignment(e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr min(int i) const {
        check_has_buffer();
        return this->funcs().at(0).output_buffers().at(0).min(i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr extent(int i) const {
        check_has_buffer();
        return this->funcs().at(0).output_buffers().at(0).extent(i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    Expr stride(int i) const {
        check_has_buffer();
        return this->funcs().at(0).output_buffers().at(0).stride(i);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    int host_alignment() const {
        check_has_buffer();
        return this->funcs().at(0).output_buffers().at(0).host_alignment();
    }


    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_min_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(array_index).output_buffers()) {
            ob.set_min(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_extent_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(array_index).output_buffers()) {
            ob.set_extent(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_stride_constraint(size_t array_index, int i, Expr e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(array_index).output_buffers()) {
            ob.set_stride(i, e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutputImpl<T> &set_host_alignment_constraint(size_t array_index, int e) {
        check_has_buffer();
        for (auto ob : this->funcs().at(array_index).output_buffers()) {
            ob.set_host_alignment(e);
        }
        return *this;
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr min(size_t array_index, int i) const {
        check_has_buffer();
        return this->funcs().at(array_index).output_buffers().at(0).min(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr extent(size_t array_index, int i) const {
        check_has_buffer();
        return this->funcs().at(array_index).output_buffers().at(0).extent(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Expr stride(size_t array_index, int i) const {
        check_has_buffer();
        return this->funcs().at(array_index).output_buffers().at(0).stride(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    int host_alignment(size_t array_index) const {
        check_has_buffer();
        return this->funcs().at(array_index).output_buffers().at(0).host_alignment();
    }

    bool has_buffer() const {
        return !this->is_stub_usage_;
    }
};

template<typename T>
class GeneratorOutput_Func : public GeneratorOutputImpl<T> {
private:
    using Super = GeneratorOutputImpl<T>;

protected:
    using TBase = typename Super::TBase;

protected:
    GeneratorOutput_Func(const std::string &name, const std::vector<Type> &t, int d)
        : Super(name, t, d) {
    }

    GeneratorOutput_Func(size_t array_size, const std::string &name, const std::vector<Type> &t, int d)
        : Super(array_size, name, t, d) {
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
        : Super(name, {type_of<TBase>()}, 0) {
    }

    GeneratorOutput_Arithmetic(size_t array_size, const std::string &name) 
        : Super(array_size, name, {type_of<TBase>()}, 0) {
    }
};

template<typename T, typename TBase = typename std::remove_all_extents<T>::type> 
using GeneratorOutputImplBase =
    typename select_type<
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
};

/** GeneratorContext is an abstract interface that is used when constructing a Generator Stub;
 * it is used to allow the outer context (typically, either a Generator or "top-level" code)
 * to specify certain information to the inner context to ensure that inner and outer
 * Generators are compiled in a compatible way; at present, this is used to propagate
 * the outer Target to the inner Generator. */
class GeneratorContext {
public:
    virtual ~GeneratorContext() {};
    virtual Target get_target() const = 0;
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
    explicit JITGeneratorContext(const Target &t) : target(t) {}
    Target get_target() const override { return target; }
private:
    const Target target;
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
    template <typename T> static Expr cast(Expr e) { return Halide::cast<T>(e); }
    static inline Expr cast(Halide::Type t, Expr e) { return Halide::cast(t, e); }
    template <typename T> using GeneratorParam = Halide::GeneratorParam<T>;
    template <typename T> using ScheduleParam = Halide::ScheduleParam<T>;
    template <typename T = void, int D = 4> using Buffer = Halide::Buffer<T, D>;
    template <typename T> using Param = Halide::Param<T>;
    static inline Type Bool(int lanes = 1) { return Halide::Bool(lanes); }
    static inline Type Float(int bits, int lanes = 1) { return Halide::Float(bits, lanes); }
    static inline Type Int(int bits, int lanes = 1) { return Halide::Int(bits, lanes); }
    static inline Type UInt(int bits, int lanes = 1) { return Halide::UInt(bits, lanes); }
};

namespace Internal {

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

    EXPORT void set_generator_param_values(const std::map<std::string, std::string> &params);

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

protected:
    EXPORT GeneratorBase(size_t size, const void *introspection_helper);

    EXPORT virtual Pipeline build_pipeline() = 0;
    EXPORT virtual void call_generate() = 0;
    EXPORT virtual void call_schedule() = 0;

    EXPORT void pre_build();
    EXPORT void pre_generate();
    EXPORT Pipeline produce_pipeline();

    template<typename T>
    using Input = GeneratorInput<T>;

    template<typename T>
    using Output = GeneratorOutput<T>;

    bool build_pipeline_called{false};
    bool generate_called{false};
    bool schedule_called{false};

private:
    friend class GeneratorStub;
    friend class SimpleGeneratorFactory;

    const size_t size;
    std::vector<Internal::Parameter *> filter_params;
    std::vector<Internal::GeneratorInputBase *> filter_inputs;
    std::vector<Internal::GeneratorOutputBase *> filter_outputs;
    std::vector<Internal::GeneratorParamBase *> generator_params;
    bool params_built{false};
    bool generator_params_set{false};
    bool schedule_params_set{false};
    bool inputs_set{false};
    std::string generator_name;

    EXPORT void build_params(bool force = false);
    EXPORT void init_inputs_and_outputs();

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

    EXPORT void set_inputs(const std::vector<std::vector<FuncOrExpr>> &inputs);

    GeneratorBase(const GeneratorBase &) = delete;
    void operator=(const GeneratorBase &) = delete;
};

class GeneratorFactory {
public:
    virtual ~GeneratorFactory() {}
    // Note that this method must never return null: 
    // if it cannot return a valid Generator, it should assert-fail.
    virtual std::unique_ptr<GeneratorBase> create(const std::map<std::string, std::string> &params) const = 0;
};

typedef std::unique_ptr<Internal::GeneratorBase> (*GeneratorCreateFunc)();

class SimpleGeneratorFactory : public GeneratorFactory {
public:
    SimpleGeneratorFactory(GeneratorCreateFunc create_func, const std::string &generator_name) 
        : create_func(create_func), generator_name(generator_name) {
        internal_assert(create_func != nullptr);
    }

    std::unique_ptr<Internal::GeneratorBase> create(const std::map<std::string, std::string> &params) const override {
        auto g = create_func();
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

EXPORT void generator_test();

}  // namespace Internal

template <class T> class Generator : public Internal::GeneratorBase {
public:
    Generator() :
        Internal::GeneratorBase(sizeof(T),
                                Internal::Introspection::get_introspection_helper<T>()) {}

    static std::unique_ptr<Internal::GeneratorBase> create() {
        return std::unique_ptr<Internal::GeneratorBase>(new T());
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
        internal_assert(!build_pipeline_called);
        static_assert(!has_schedule_method<T2>::value, "The schedule() method is ignored if you define a build() method; use generate() instead.");
        pre_build();
        Pipeline p = ((T *)this)->build();
        build_pipeline_called = true;
        return p;
    }
    template <typename T2 = T,
              typename std::enable_if<has_generate_method<T2>::value>::type * = nullptr>
    Pipeline build_pipeline_impl() {
        internal_assert(!build_pipeline_called);
        ((T *)this)->call_generate_impl();
        ((T *)this)->call_schedule_impl();
        build_pipeline_called = true;
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
        user_assert(!generate_called) << "You may not call the generate() method more than once per instance.";
        typedef typename std::result_of<decltype(&T::generate)(T)>::type GenerateRetType;
        static_assert(std::is_void<GenerateRetType>::value, "generate() must return void");
        pre_generate();
        ((T *)this)->generate();
        generate_called = true;
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
        user_assert(generate_called) << "You must call the generate() method before calling the schedule() method.";
        user_assert(!schedule_called) << "You may not call the schedule() method more than once per instance.";
        typedef typename std::result_of<decltype(&T::schedule)(T)>::type ScheduleRetType;
        static_assert(std::is_void<ScheduleRetType>::value, "schedule() must return void");
        ((T *)this)->schedule();
        schedule_called = true;
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
    friend class Internal::SimpleGeneratorFactory;

    Generator(const Generator &) = delete;
    void operator=(const Generator &) = delete;
};

template <class GeneratorClass> class RegisterGenerator {
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
    GeneratorStub() {}

    // move constructor
    GeneratorStub(GeneratorStub&& that) : generator(std::move(that.generator)) {}

    // move assignment operator
    GeneratorStub& operator=(GeneratorStub&& that) {
        generator = std::move(that.generator);
        return *this;
    }

    Target get_target() const { return generator->get_target(); }

    // schedule method
    void schedule(const std::map<std::string, std::string> &schedule_params,
                  const std::map<std::string, LoopLevel> &schedule_params_looplevels) {
        generator->set_schedule_param_values(schedule_params, schedule_params_looplevels);
        generator->call_schedule();
    }

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
        check_scheduled("realize");
        return get_first_output().realize(sizes, get_target()); 
    }
    
    template <typename... Args> 
    Realization realize(Args&&... args) { 
        check_scheduled("realize");
        return get_first_output().realize(std::forward<Args>(args)..., get_target()); 
    }

    template<typename Dst> 
    void realize(Dst dst) { 
        check_scheduled("realize");
        get_first_output().realize(dst, get_target()); 
    }

    virtual ~GeneratorStub() {}

protected:
    typedef std::function<std::unique_ptr<GeneratorBase>(const std::map<std::string, std::string>&)> GeneratorFactory;

    EXPORT GeneratorStub(const GeneratorContext *context,
                  GeneratorFactory generator_factory,
                  const std::map<std::string, std::string> &generator_params,
                  const std::vector<std::vector<Internal::FuncOrExpr>> &inputs);

    // Output(s)
    // TODO: identify vars used
    Func get_output(const std::string &n) { 
        return generator->get_output(n); 
    }

    std::vector<Func> get_output_vector(const std::string &n) { 
        return generator->get_output_vector(n); 
    }

    bool has_generator() const { 
        return generator != nullptr; 
    }

    template<typename Ratio>
    static double ratio_to_double() {
        return (double)Ratio::num / (double)Ratio::den;
    }

    template <typename T>
    static std::vector<FuncOrExpr> to_func_or_expr_vector(const T &t) {
        return { FuncOrExpr(t) };
    }

    template <typename T>
    static std::vector<FuncOrExpr> to_func_or_expr_vector(const std::vector<T> &v) {
        std::vector<FuncOrExpr> r;
        std::copy(v.begin(), v.end(), std::back_inserter(r));
        return r;
    }

    EXPORT void verify_same_funcs(Func a, Func b);
    EXPORT void verify_same_funcs(const std::vector<Func>& a, const std::vector<Func>& b);

private:
    std::shared_ptr<GeneratorBase> generator;

    Func get_first_output() const { 
        return generator->get_first_output(); 
    }
    void check_scheduled(const char* m) const { 
        user_assert(generator->schedule_called) << "Must call schedule() before calling " << m << "()"; 
    }

    explicit GeneratorStub(const GeneratorStub &) = delete;
    void operator=(const GeneratorStub &) = delete;
};

}  // namespace Internal


}  // namespace Halide

#define HALIDE_REGISTER_GENERATOR(GEN_CLASS_NAME, GEN_REGISTRY_NAME) \
    namespace ns_reg_gen { static auto reg_##GEN_CLASS_NAME = Halide::RegisterGenerator<GEN_CLASS_NAME>(GEN_REGISTRY_NAME); }


#endif  // HALIDE_GENERATOR_H_
