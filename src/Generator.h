#ifndef HALIDE_GENERATOR_H_
#define HALIDE_GENERATOR_H_

/** \file
 *
 * Generator is a class used to encapsulate the building of Funcs in user pipelines.
 * A Generator is agnostic to JIT vs AOT compilation; it can be used for either
 * purpose, but is especially convenient to use for AOT compilation.
 *
 * A Generator automatically detects the run-time parameters (Param/ImageParams)
 * associated with the Func and (for AOT code) produces a function signature
 * with the correct params in the correct order.
 *
 * A Generator can also be customized via compile-time parameters (GeneratorParams),
 * which affect code generation.
 *
 * GeneratorParams, ImageParams, and Params are (by convention)
 * always public and always declared at the top of the Generator class,
 * in the order
 *
 *    GeneratorParam(s)
 *    ImageParam(s)
 *    Param(s)
 *
 * Preferred style is to use C++11 in-class initialization style, e.g.
 * \code
 *    GeneratorParam<int> magic{"magic", 42};
 * \endcode
 *
 * Note that the ImageParams/Params will appear in the C function
 * call in the order they are declared. (GeneratorParams are always
 * referenced by name, not position, so their order is irrelevant.)
 *
 * All Param variants declared as Generator members must have explicit
 * names, and all such names must match the regex [A-Za-z][A-Za-z_0-9]*
 * (i.e., essentially a C/C++ variable name, with some extra restrictions
 * on underscore use). By convention, the name should match the member-variable name.
 *
 * Generators are usually added to a global registry to simplify AOT build mechanics;
 * this is done by simply defining an instance of RegisterGenerator at static
 * scope:
 * \code
 *    RegisterGenerator<ExampleGen> register_jit_example{"jit_example"};
 * \endcode
 *
 * The registered name of the Generator is provided as an argument
 * (which must match the same rules as Param names, above).
 *
 * (If you are jitting, you may not need to bother registering your Generator,
 * but it's considered best practice to always do so anyway.)
 *
 * Most Generator classes will only need to provide a build() method
 * that the base class will call, and perhaps declare a Param and/or
 * GeneratorParam:
 *
 * \code
 *  class XorImage : public Generator<XorImage> {
 *  public:
 *      GeneratorParam<int> channels{"channels", 3};
 *      ImageParam input{UInt(8), 3, "input"};
 *      Param<uint8_t> mask{"mask"};
 *
 *      Func build() {
 *          Var x, y, c;
 *          Func f;
 *          f(x, y, c) = input(x, y, c) ^ mask;
 *          f.bound(c, 0, bound).reorder(c, x, y).unroll(c);
 *          return f;
 *      }
 *  };
 *  RegisterGenerator<XorImage> reg_xor{"xor_image"};
 * \endcode
 *
 * By default, this code schedules itself for 3-channel (RGB) images;
 * by changing the value of the "channels" GeneratorParam before calling
 * build() we can produce code suited for different channel counts.
 *
 * Note that a Generator is always executed with a specific Target
 * assigned to it, that you can access via the get_target() method.
 * (You should *not* use the global get_target_from_environment(), etc.
 * methods provided in Target.h)
 *
 * Your build() method will usually return a Func. If you have a
 * pipeline that outputs multiple Funcs, you can also return a
 * Pipeline object.
 */

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Func.h"
#include "ObjectInstanceRegistry.h"
#include "Introspection.h"
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
std::string halide_type_to_c_source(const Type &t);

// Convert a Halide Type into a string representation of its C Source.
// e.g., Int(32) -> "int32_t"
std::string halide_type_to_c_type(const Type &t);

/** generate_filter_main() is a convenient wrapper for GeneratorRegistry::create() +
 * compile_to_files();
 * it can be trivially wrapped by a "real" main() to produce a command-line utility
 * for ahead-of-time filter compilation. */
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
    explicit GeneratorParamImpl(const std::string &name, const T &value) : GeneratorParamBase(name), value_(value) {}

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
    explicit GeneratorParam_Target(const std::string &name, const T &value) : GeneratorParamImpl<T>(name, value) {}

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
        return "Halide::Target";
    }
};

template<typename T>
class GeneratorParam_Arithmetic : public GeneratorParamImpl<T> {
public:
    explicit GeneratorParam_Arithmetic(const std::string &name, 
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
    explicit GeneratorParam_Bool(const std::string &name, const T &value) : GeneratorParam_Arithmetic<T>(name, value) {}

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
    explicit GeneratorParam_Enum(const std::string &name, const T &value, const std::map<std::string, T> &enum_map)
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
    explicit GeneratorParam_Type(const std::string &name, const T &value)
        : GeneratorParam_Enum<T>(name, value, get_halide_type_enum_map()) {}

    std::string call_to_string(const std::string &v) const override {
        return "Halide::Internal::halide_type_to_enum_string(" + v + ")";
    }

    std::string get_c_type() const override {
        return "Halide::Type";
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
    explicit GeneratorParam_LoopLevel(const std::string &name, const std::string &def) 
        : GeneratorParam_Enum<T>(name, enum_from_string(get_halide_looplevel_enum_map(), def), get_halide_looplevel_enum_map()), def(def) {}

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << "Halide::Internal::halide_looplevel_to_enum_string(" << v << ")";
        return oss.str();
    }
    std::string get_c_type() const override {
        return "Halide::LoopLevel";
    }

    std::string get_default_value() const override {
        if (def == "undefined") return "Halide::Internal::get_halide_undefined_looplevel()";
        if (def == "root") return "Halide::LoopLevel::root()";
        if (def == "inline") return "Halide::LoopLevel()";
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

class FuncOrExpr {
    IOKind kind_;
    Halide::Func func_;
    Halide::Expr expr_;
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

template <typename T>
std::vector<FuncOrExpr> to_func_or_expr_vector(const T &t) {
    return { FuncOrExpr(t) };
}

template <typename T>
std::vector<FuncOrExpr> to_func_or_expr_vector(const std::vector<T> &v) {
    std::vector<FuncOrExpr> r;
    std::copy(v.begin(), v.end(), std::back_inserter(r));
    return r;
}

void verify_same_funcs(Func a, Func b);
void verify_same_funcs(const std::vector<Func>& a, const std::vector<Func>& b);

class GIOBase {
public:
    bool array_size_defined() const;
    size_t array_size() const;
    virtual bool is_array() const;

    const std::string &name() const;
    IOKind kind() const;

    bool types_defined() const;
    const std::vector<Type> &types() const;
    Type type() const;

    bool dimensions_defined() const;
    int dimensions() const;

    const std::vector<Func> &funcs() const;
    const std::vector<Expr> &exprs() const;

protected:
    GIOBase(size_t array_size, 
            const std::string &name, 
            IOKind kind,
            const std::vector<Type> &types,
            int dimensions);
    virtual ~GIOBase();

    friend class GeneratorBase;

    int array_size_;           // always 1 if is_array() == false. -1 if is_array() == true but unspecified.

    const std::string name_;
    const IOKind kind_;
    std::vector<Type> types_;  // empty if type is unspecified
    int dimensions_;           // -1 if dim is unspecified

    // Exactly one will have nonzero length
    std::vector<Func> funcs_;
    std::vector<Expr> exprs_;

    std::string array_name(size_t i) const;

    virtual void verify_internals() const;

    void check_matching_array_size(size_t size);
    void check_matching_type_and_dim(const std::vector<Type> &t, int d);

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
    GeneratorInputBase(size_t array_size,
                       const std::string &name, 
                       IOKind kind, 
                       const std::vector<Type> &t, 
                       int d);

    GeneratorInputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
      : GeneratorInputBase(1, name, kind, t, d) {}

    ~GeneratorInputBase() override;

    friend class GeneratorBase;

    std::vector<Parameter> parameters_;

    void init_internals();
    void set_inputs(const std::vector<FuncOrExpr> &inputs);

    virtual void set_def_min_max() {
        // nothing
    }

    void verify_internals() const override;

private:
    void init_parameters();
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

    operator class Func() const { 
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
    //template <typename T2 = T, typename std::enable_if<std::is_same<TBase, Func>::value>::type * = nullptr>
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
    GeneratorOutputBase(size_t array_size, 
                        const std::string &name, 
                        const std::vector<Type> &t, 
                        int d);

    GeneratorOutputBase(const std::string &name, 
                        const std::vector<Type> &t, 
                        int d)
      : GeneratorOutputBase(1, name, t, d) {}

    ~GeneratorOutputBase() override;

    friend class GeneratorBase;

    void init_internals();
    void resize(size_t size);
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
    operator class Func() const { 
        return get_values<ValueType>().at(0); 
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

class NamesInterface {
    // Names in this class are only intended for use in derived classes.
protected:
    // Import a consistent list of Halide names that can be used in
    // Halide generators without qualification.
    using Expr = Halide::Expr;
    using ExternFuncArgument = Halide::ExternFuncArgument;
    using Func = Halide::Func;
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
    template <typename T = void, int D = 4> using Image = Halide::Image<T, D>;
    template <typename T> using Param = Halide::Param<T>;
    static inline Type Bool(int lanes = 1) { return Halide::Bool(lanes); }
    static inline Type Float(int bits, int lanes = 1) { return Halide::Float(bits, lanes); }
    static inline Type Int(int bits, int lanes = 1) { return Halide::Int(bits, lanes); }
    static inline Type UInt(int bits, int lanes = 1) { return Halide::UInt(bits, lanes); }
};

class GeneratorContext {
public:
    virtual Target get_target() const = 0;
};

class JITGeneratorContext : public GeneratorContext {
public:
    explicit JITGeneratorContext(const Target &t) : target(t) {}
    Target get_target() const override { return target; }
private:
    const Target target;
};

namespace Internal {

class GeneratorStub;
class SimpleGeneratorFactory;
template <class T> class RegisterGeneratorAndStub;

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

    const GeneratorContext& context() const { return *this; }

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
    template<typename T> friend class RegisterGeneratorAndStub;

    const size_t size;
    std::vector<Internal::Parameter *> filter_params;
    std::vector<Internal::GeneratorInputBase *> filter_inputs;
    std::vector<Internal::GeneratorOutputBase *> filter_outputs;
    std::vector<Internal::GeneratorParamBase *> generator_params;
    bool params_built{false};
    bool generator_params_set{false};
    bool schedule_params_set{false};
    bool inputs_set{false};
    std::string cpp_stub_class_name;

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

    void set_cpp_stub_class_name(const std::string &n) {
        internal_assert(cpp_stub_class_name.empty());
        cpp_stub_class_name = n;
    }

    EXPORT void set_inputs(const std::vector<std::vector<FuncOrExpr>> &inputs);

    GeneratorBase(const GeneratorBase &) = delete;
    void operator=(const GeneratorBase &) = delete;
};

class GeneratorFactory {
public:
    virtual ~GeneratorFactory() {}
    virtual std::unique_ptr<GeneratorBase> create(const std::map<std::string, std::string> &params) const = 0;
};

typedef std::unique_ptr<Internal::GeneratorBase> (*GeneratorCreateFunc)();

class SimpleGeneratorFactory : public GeneratorFactory {
public:
    SimpleGeneratorFactory(GeneratorCreateFunc create_func, const std::string &cpp_stub_class_name) 
        : create_func(create_func), cpp_stub_class_name(cpp_stub_class_name) {
        internal_assert(create_func != nullptr);
    }

    std::unique_ptr<Internal::GeneratorBase> create(const std::map<std::string, std::string> &params) const override {
        auto g = create_func();
        internal_assert(g.get() != nullptr);
        g->set_cpp_stub_class_name(cpp_stub_class_name);
        g->set_generator_param_values(params);
        return g;
    }
private:
    const GeneratorCreateFunc create_func;
    const std::string cpp_stub_class_name;
};

class GeneratorRegistry {
public:
    EXPORT static void register_factory(const std::string &name, std::unique_ptr<GeneratorFactory> factory);
    EXPORT static void unregister_factory(const std::string &name);
    EXPORT static std::vector<std::string> enumerate();
    EXPORT static std::string get_cpp_stub_class_name(const std::string &name);
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
    // so we use a little SFINAE to detect if there is a method-shaped member named 'schedule'.
    template<typename> 
    struct type_sink { typedef void type; };
    
    template<typename T2, typename = void> 
    struct has_schedule_method : std::false_type {}; 
    
    template<typename T2> 
    struct has_schedule_method<T2, typename type_sink<decltype(std::declval<T2>().schedule())>::type> : std::true_type {};

    template <typename T2 = T,
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::build)>::value>::type * = nullptr>
    Pipeline build_pipeline_impl() {
        internal_assert(!build_pipeline_called);
        static_assert(!has_schedule_method<T2>::value, "The schedule() method is ignored if you define a build() method; use generate() instead.");
        pre_build();
        Pipeline p = ((T *)this)->build();
        build_pipeline_called = true;
        return p;
    }
    template <typename T2 = T,
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::generate)>::value>::type * = nullptr>
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
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::build)>::value>::type * = nullptr>
    void call_generate_impl() {
        user_error << "Unimplemented";
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::generate)>::value>::type * = nullptr>
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
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::build)>::value>::type * = nullptr>
    void call_schedule_impl() {
        user_error << "Unimplemented";
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_member_function_pointer<decltype(&T2::schedule)>::value>::type * = nullptr>
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
        return build_pipeline_impl();
    }

    void call_generate() override {
        call_generate_impl();
    }

    void call_schedule() override {
        call_schedule_impl();
    }
private:
    friend class Internal::SimpleGeneratorFactory;

    Generator(const Generator &) = delete;
    void operator=(const Generator &) = delete;
};

namespace Internal {

template <class StubClass> 
class RegisterGeneratorAndStub {
private:
    static GeneratorCreateFunc init_create_func(GeneratorCreateFunc f) {
        // This is initialized on the first call; subsequent code flows return existing value
        static GeneratorCreateFunc create_func_storage = f;
        return create_func_storage;
    }

    static const char *init_cpp_stub_class_name(const char *n) {
        // This is initialized on the first call; subsequent code flows return existing value
        static const char *init_cpp_stub_class_name_storage = n;
        return init_cpp_stub_class_name_storage;
    }

public:
    static std::unique_ptr<Internal::GeneratorBase> create(const std::map<std::string, std::string> &params) {
        GeneratorCreateFunc f = init_create_func(nullptr);
        user_assert(f != nullptr) << "RegisterGeneratorAndStub was not initialized; this is probably a wrong value for cpp_stub_class_name.\n";
        auto g = f();
        g->set_cpp_stub_class_name(init_cpp_stub_class_name(nullptr));
        g->set_generator_param_values(params);
        return g;
    }

    RegisterGeneratorAndStub(GeneratorCreateFunc create_func, const char *registry_name, const char *cpp_stub_class_name) {
        (void) init_create_func(create_func);
        (void) init_cpp_stub_class_name(cpp_stub_class_name);
        std::unique_ptr<Internal::SimpleGeneratorFactory> f(new Internal::SimpleGeneratorFactory(create_func, cpp_stub_class_name));
        Internal::GeneratorRegistry::register_factory(registry_name, std::move(f));
    }
};

}  // namespace Internal

template <class GeneratorClass> class RegisterGenerator {
public:
    RegisterGenerator(const char* name) {
        std::unique_ptr<Internal::SimpleGeneratorFactory> f(new Internal::SimpleGeneratorFactory(GeneratorClass::create, ""));
        Internal::GeneratorRegistry::register_factory(name, std::move(f));
    }
};

namespace Internal {

class GeneratorStub {
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

    template <typename... Args>
    GeneratorStub(const GeneratorContext &context,
            GeneratorFactory generator_factory,
            const std::map<std::string, std::string> &generator_params,
            const std::vector<std::vector<Internal::FuncOrExpr>> &inputs) {
        generator = generator_factory(generator_params);
        generator->target.set(context.get_target());
        generator->set_inputs(inputs);
        generator->call_generate();
    }

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

// Use a little variadic macro hacking to allow two or three arguments.
// This is suboptimal, but allows us more flexibility to mutate registration in
// the future with less impact on existing code.
#define _HALIDE_REGISTER_GENERATOR2(GEN_CLASS_NAME, GEN_REGISTRY_NAME) \
    Halide::RegisterGenerator<GEN_CLASS_NAME>(GEN_REGISTRY_NAME); 

#define _HALIDE_REGISTER_GENERATOR3(GEN_CLASS_NAME, GEN_REGISTRY_NAME, FULLY_QUALIFIED_STUB_NAME) \
    Halide::Internal::RegisterGeneratorAndStub<::FULLY_QUALIFIED_STUB_NAME>(GEN_CLASS_NAME::create, GEN_REGISTRY_NAME, #FULLY_QUALIFIED_STUB_NAME); 

#define _HALIDE_REGISTER_GENERATOR_CHOOSER(_1, _2, _3, NAME, ...) NAME

#define HALIDE_REGISTER_GENERATOR(...) \
    _HALIDE_REGISTER_GENERATOR_CHOOSER(__VA_ARGS__, _HALIDE_REGISTER_GENERATOR3, _HALIDE_REGISTER_GENERATOR2)(__VA_ARGS__)


#endif  // HALIDE_GENERATOR_H_
