#ifndef HALIDE_GENERATOR_H_
#define HALIDE_GENERATOR_H_

/** \file
 *
 * Generator is a class used to encapsulate the building of Funcs in user
 * pipelines. A Generator is agnostic to JIT vs AOT compilation; it can be used for
 * either purpose, but is especially convenient to use for AOT compilation.
 *
 * A Generator explicitly declares the Inputs and Outputs associated for a given
 * pipeline, and (optionally) separates the code for constructing the outputs from the code from
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
 * A Generator provides implementations of two methods:
 *
 *   - generate(), which must fill in all Output Func(s); it may optionally also do scheduling
 *   if no schedule() method is present.
 *   - schedule(), which (if present) should contain all scheduling code.
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
 * \endcode
 *
 * You can also leave array size unspecified, with some caveats:
 *   - For ahead-of-time compilation, Inputs must have a concrete size specified
 *     via a GeneratorParam at build time (e.g., pyramid.size=3)
 *   - For JIT compilation via a Stub, Inputs array sizes will be inferred
 *     from the vector passed.
 *   - For ahead-of-time compilation, Outputs may specify a concrete size
 *     via a GeneratorParam at build time (e.g., pyramid.size=3), or the
 *     size can be specified via a resize() method.
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
 * A Generator can also be customized via compile-time parameters (GeneratorParams),
 * which affect code generation.
 *
 * GeneratorParams, Inputs, and Outputs are (by convention) always
 * public and always declared at the top of the Generator class, in the order
 *
 * \code
 *     GeneratorParam(s)
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
 * You can dynamically add Inputs and Outputs to your Generator via adding a
 * configure() method; if present, it will be called before generate(). It can
 * examine GeneratorParams but it may not examine predeclared Inputs or Outputs;
 * the only thing it should do is call add_input<>() and/or add_output<>().
 * Added inputs will be appended (in order) after predeclared Inputs but before
 * any Outputs; added outputs will be appended after predeclared Outputs.
 *
 * Note that the pointers returned by add_input() and add_output() are owned
 * by the Generator and will remain valid for the Generator's lifetime; user code
 * should not attempt to delete or free them.
 *
 * \code
 *     class MultiSum : public Generator<MultiSum> {
 *     public:
 *         GeneratorParam<int32_t> input_count{"input_count", 10};
 *         Output<Func> output{ "output", Float(32), 2 };
 *
 *         void configure() {
 *             for (int i = 0; i < input_count; ++i) {
 *                 extra_inputs.push_back(
 *                     add_input<Func>("input_" + std::to_string(i), Float(32), 2);
 *             }
 *         }
 *
 *         void generate() {
 *             Expr sum = 0.f;
 *             for (int i = 0; i < input_count; ++i) {
 *                 sum += (*extra_inputs)[i](x, y);
 *             }
 *             output(x, y) = sum;
 *         }
 *     private:
 *         std::vector<Input<Func>* extra_inputs;
 *     };
 * \endcode
 *
 *  All Generators have three GeneratorParams that are implicitly provided
 *  by the base class:
 *
 *      GeneratorParam<Target> target{"target", Target()};
 *      GeneratorParam<bool> auto_schedule{"auto_schedule", false};
 *      GeneratorParam<MachineParams> machine_params{"machine_params", MachineParams::generic()};
 *
 *  - 'target' is the Halide::Target for which the Generator is producing code.
 *    It is read-only during the Generator's lifetime, and must not be modified;
 *    its value should always be filled in by the calling code: either the Halide
 *    build system (for ahead-of-time compilation), or ordinary C++ code
 *    (for JIT compilation).
 *  - 'auto_schedule' indicates whether the auto-scheduler should be run for this
 *    Generator:
 *      - if 'false', the Generator should schedule its Funcs as it sees fit.
 *      - if 'true', the Generator should only provide estimate()s for its Funcs,
 *        and not call any other scheduling methods.
 *  - 'machine_params' is only used if auto_schedule is true; it is ignored
 *    if auto_schedule is false. It provides details about the machine architecture
 *    being targeted which may be used to enhance the automatically-generated
 *    schedule.
 *
 * Generators are added to a global registry to simplify AOT build mechanics; this
 * is done by simply using the HALIDE_REGISTER_GENERATOR macro at global scope:
 *
 * \code
 *      HALIDE_REGISTER_GENERATOR(ExampleGen, jit_example)
 * \endcode
 *
 * The registered name of the Generator is provided must match the same rules as
 * Input names, above.
 *
 * Note that the class name of the generated Stub class will match the registered
 * name by default; if you want to vary it (typically, to include namespaces),
 * you can add it as an optional third argument:
 *
 * \code
 *      HALIDE_REGISTER_GENERATOR(ExampleGen, jit_example, SomeNamespace::JitExampleStub)
 * \endcode
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
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "ExternalCode.h"
#include "Func.h"
#include "ImageParam.h"
#include "Introspection.h"
#include "ObjectInstanceRegistry.h"
#include "Target.h"

namespace Halide {

template<typename T> class Buffer;

namespace Internal {

void generator_test();

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
    void track_values(const std::string &name, const std::vector<Expr> &values);
};

std::vector<Expr> parameter_constraints(const Parameter &p);

template <typename T>
HALIDE_NO_USER_CODE_INLINE std::string enum_to_string(const std::map<std::string, T> &enum_map, const T& t) {
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

extern const std::map<std::string, Halide::Type> &get_halide_type_enum_map();
inline std::string halide_type_to_enum_string(const Type &t) {
    return enum_to_string(get_halide_type_enum_map(), t);
}

// Convert a Halide Type into a string representation of its C source.
// e.g., Int(32) -> "Halide::Int(32)"
std::string halide_type_to_c_source(const Type &t);

// Convert a Halide Type into a string representation of its C Source.
// e.g., Int(32) -> "int32_t"
std::string halide_type_to_c_type(const Type &t);

/** generate_filter_main() is a convenient wrapper for GeneratorRegistry::create() +
 * compile_to_files(); it can be trivially wrapped by a "real" main() to produce a
 * command-line utility for ahead-of-time filter compilation.
 * Note that this function is not thread-safe because it resets global states.*/
int generate_filter_main(int argc, char **argv, std::ostream &cerr);

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
class GeneratorParamInfo;

class GeneratorParamBase {
public:
    explicit GeneratorParamBase(const std::string &name);
    virtual ~GeneratorParamBase();

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
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Target)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(MachineParams)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Type)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(LoopLevel)

#undef HALIDE_GENERATOR_PARAM_TYPED_SETTER

    // Add overloads for string and char*
    void set(const std::string &new_value) { set_from_string(new_value); }
    void set(const char *new_value) { set_from_string(std::string(new_value)); }

protected:
    friend class GeneratorBase;
    friend class GeneratorParamInfo;
    friend class StubEmitter;

    void check_value_readable() const;
    void check_value_writable() const;

    // All GeneratorParams are settable from string.
    virtual void set_from_string(const std::string &value_string) = 0;

    virtual std::string call_to_string(const std::string &v) const = 0;
    virtual std::string get_c_type() const = 0;

    virtual std::string get_type_decls() const {
        return "";
    }

    virtual std::string get_default_value() const = 0;

    virtual bool is_synthetic_param() const {
        return false;
    }

    virtual bool is_looplevel_param() const {
        return false;
    }

    void fail_wrong_type(const char *type);

private:
    // No copy
    GeneratorParamBase(const GeneratorParamBase &) = delete;
    void operator=(const GeneratorParamBase &) = delete;
    // No move
    GeneratorParamBase(GeneratorParamBase&&) = delete;
    void operator=(GeneratorParamBase&&) = delete;

    // Generator which owns this GeneratorParam. Note that this will be null
    // initially; the GeneratorBase itself will set this field when it initially
    // builds its info about params. However, since it (generally) isn't
    // appropriate for GeneratorParam<> to be declared outside of a Generator,
    // all reasonable non-testing code should expect this to be non-null.
    GeneratorBase *generator{nullptr};
};

// This is strictly some syntactic sugar to suppress certain compiler warnings.
template<typename FROM, typename TO>
struct Convert {
    template <typename TO2 = TO, typename std::enable_if<!std::is_same<TO2, bool>::value>::type * = nullptr>
    inline static TO2 value(const FROM &from) { return static_cast<TO2>(from); }

    template <typename TO2 = TO, typename std::enable_if<std::is_same<TO2, bool>::value>::type * = nullptr>
    inline static TO2 value(const FROM &from) { return from != 0; }
};

template<typename T>
class GeneratorParamImpl : public GeneratorParamBase {
public:
    using type = T;

    GeneratorParamImpl(const std::string &name, const T &value) : GeneratorParamBase(name), value_(value) {}

    T value() const { this->check_value_readable(); return value_; }

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
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Target)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(MachineParams)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(Type)
    HALIDE_GENERATOR_PARAM_TYPED_SETTER(LoopLevel)

#undef HALIDE_GENERATOR_PARAM_TYPED_SETTER

    // Overload for std::string.
    void set(const std::string &new_value) {
        check_value_writable();
        value_ = new_value;
    }

protected:
    virtual void set_impl(const T &new_value) { check_value_writable(); value_ = new_value; }

    // Needs to be protected to allow GeneratorParam<LoopLevel>::set() override
    T value_;

private:
    // If FROM->T is not legal, fail
    template <typename FROM, typename std::enable_if<
        !std::is_convertible<FROM, T>::value
    >::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const FROM &, const char *msg) {
        fail_wrong_type(msg);
    }

    // If FROM and T are identical, just assign
    template <typename FROM, typename std::enable_if<
        std::is_same<FROM, T>::value
    >::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const FROM &value, const char * msg) {
        check_value_writable();
        value_ = value;
    }

    // If both FROM->T and T->FROM are legal, ensure it's lossless
    template <typename FROM, typename std::enable_if<
        !std::is_same<FROM, T>::value &&
        std::is_convertible<FROM, T>::value &&
        std::is_convertible<T, FROM>::value
    >::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const FROM &value, const char * msg) {
        check_value_writable();
        const T t = Convert<FROM, T>::value(value);
        const FROM value2 = Convert<T, FROM>::value(t);
        if (value2 != value) {
            fail_wrong_type(msg);
        }
        value_ = t;
    }

    // If FROM->T is legal but T->FROM is not, just assign
    template <typename FROM, typename std::enable_if<
        !std::is_same<FROM, T>::value &&
        std::is_convertible<FROM, T>::value &&
        !std::is_convertible<T, FROM>::value
    >::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const FROM &value, const char * msg) {
        check_value_writable();
        value_ = value;
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

    std::string get_default_value() const override {
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
class GeneratorParam_MachineParams : public GeneratorParamImpl<T> {
public:
    GeneratorParam_MachineParams(const std::string &name, const T &value) : GeneratorParamImpl<T>(name, value) {}

    void set_from_string(const std::string &new_value_string) override {
        this->set(MachineParams(new_value_string));
    }

    std::string get_default_value() const override {
        return this->value().to_string();
    }

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << v << ".to_string()";
        return oss.str();
    }

    std::string get_c_type() const override {
        return "MachineParams";
    }
};

class GeneratorParam_LoopLevel : public GeneratorParamImpl<LoopLevel> {
public:
    GeneratorParam_LoopLevel(const std::string &name, const LoopLevel &value) : GeneratorParamImpl<LoopLevel>(name, value) {}

    using GeneratorParamImpl<LoopLevel>::set;

    void set(const LoopLevel &value) override {
        // Don't call check_value_writable(): It's OK to set a LoopLevel after generate().
        // check_value_writable();

        // This looks odd, but is deliberate:

        // First, mutate the existing contents to match the value passed in,
        // so that any existing usage of the LoopLevel now uses the newer value.
        // (Strictly speaking, this is really only necessary if this method
        // is called after generate(): before generate(), there is no usage
        // to be concerned with.)
        value_.set(value);

        // Then, reset the value itself so that it points to the same LoopLevelContents
        // as the value passed in. (Strictly speaking, this is really only
        // useful if this method is called before generate(): afterwards, it's
        // too late to alter the code to refer to a different LoopLevelContents.)
        value_ = value;
    }

    void set_from_string(const std::string &new_value_string) override {
        if (new_value_string == "root") {
            this->set(LoopLevel::root());
        } else if (new_value_string == "inlined") {
            this->set(LoopLevel::inlined());
        } else {
            user_error << "Unable to parse " << this->name << ": " << new_value_string;
        }
    }

    std::string get_default_value() const override {
        // This is dodgy but safe in this case: we want to
        // see what the value of our LoopLevel is *right now*,
        // so we make a copy and lock the copy so we can inspect it.
        // (Note that ordinarily this is a bad idea, since LoopLevels
        // can be mutated later on; however, this method is only
        // called by the Generator infrastructure, on LoopLevels that
        // will never be mutated, so this is really just an elaborate way
        // to avoid runtime assertions.)
        LoopLevel copy;
        copy.set(this->value());
        copy.lock();
        if (copy.is_inlined()) {
            return "LoopLevel::inlined()";
        } else if (copy.is_root()) {
            return "LoopLevel::root()";
        } else {
            internal_error;
            return "";
        }
    }

    std::string call_to_string(const std::string &v) const override {
        internal_error;
        return std::string();
    }

    std::string get_c_type() const override {
        return "LoopLevel";
    }

    bool is_looplevel_param() const override {
        return true;
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
        // All one-byte ints int8 and uint8 should be parsed as integers, not chars --
        // including 'char' itself. (Note that sizeof(bool) is often-but-not-always-1,
        // so be sure to exclude that case.)
        if (sizeof(T) == sizeof(char) && !std::is_same<T, bool>::value) {
            int i;
            iss >> i;
            t = (T) i;
        } else {
            iss >> t;
        }
        user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << new_value_string;
        this->set(t);
    }

    std::string get_default_value() const override {
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
        if (new_value_string == "true" || new_value_string == "True") {
            v = true;
        } else if (new_value_string == "false" || new_value_string == "False") {
            v = false;
        } else {
            user_assert(false) << "Unable to parse bool: " << new_value_string;
        }
        this->set(v);
    }

    std::string get_default_value() const override {
        return this->value() ? "true" : "false";
    }

    std::string call_to_string(const std::string &v) const override {
        std::ostringstream oss;
        oss << "std::string((" << v << ") ? \"true\" : \"false\")";
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

    // define a "set" that takes our specific enum (but don't hide the inherited virtual functions)
    using GeneratorParamImpl<T>::set;

    template <typename T2 = T, typename std::enable_if<!std::is_same<T2, Type>::value>::type * = nullptr>
    void set(const T &e) {
        this->set_impl(e);
    }

    void set_from_string(const std::string &new_value_string) override {
        auto it = enum_map.find(new_value_string);
        user_assert(it != enum_map.end()) << "Enumeration value not found: " << new_value_string;
        this->set_impl(it->second);
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
        oss << "inline HALIDE_NO_USER_CODE_INLINE const std::map<Enum_" << this->name << ", std::string>& Enum_" << this->name << "_map() {\n";
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

    std::string get_default_value() const override {
        return halide_type_to_c_source(this->value());
    }

    std::string get_type_decls() const override {
        return "";
    }
};

template<typename T>
class GeneratorParam_String : public Internal::GeneratorParamImpl<T> {
public:
    GeneratorParam_String(const std::string &name, const std::string &value)
        : GeneratorParamImpl<T>(name, value) {
    }
    void set_from_string(const std::string &new_value_string) override {
        this->set(new_value_string);
    }

    std::string get_default_value() const override {
        return "\"" + this->value() + "\"";
    }

    std::string call_to_string(const std::string &v) const override {
        return v;
    }

    std::string get_c_type() const override {
        return "std::string";
    }
};

template<typename T>
using GeneratorParamImplBase =
    typename select_type<
        cond<std::is_same<T, Target>::value,        GeneratorParam_Target<T>>,
        cond<std::is_same<T, MachineParams>::value, GeneratorParam_MachineParams<T>>,
        cond<std::is_same<T, LoopLevel>::value,     GeneratorParam_LoopLevel>,
        cond<std::is_same<T, std::string>::value,   GeneratorParam_String<T>>,
        cond<std::is_same<T, Type>::value,          GeneratorParam_Type<T>>,
        cond<std::is_same<T, bool>::value,          GeneratorParam_Bool<T>>,
        cond<std::is_arithmetic<T>::value,          GeneratorParam_Arithmetic<T>>,
        cond<std::is_enum<T>::value,                GeneratorParam_Enum<T>>
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
 *   - std::string
 * Please don't use std::string unless there's no way to do what you want with some
 * other type; in particular, don't use this if you can use enum instead.
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
    template <typename T2 = T, typename std::enable_if<!std::is_same<T2, std::string>::value>::type * = nullptr>
    GeneratorParam(const std::string &name, const T &value)
        : Internal::GeneratorParamImplBase<T>(name, value) {}

    GeneratorParam(const std::string &name, const T &value, const T &min, const T &max)
        : Internal::GeneratorParamImplBase<T>(name, value, min, max) {}

    GeneratorParam(const std::string &name, const T &value, const std::map<std::string, T> &enum_map)
        : Internal::GeneratorParamImplBase<T>(name, value, enum_map) {}

    GeneratorParam(const std::string &name, const std::string &value)
        : Internal::GeneratorParamImplBase<T>(name, value) {}
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
template <typename T>
decltype((T)0 && (T)1) operator&&(const GeneratorParam<T> &a, const  GeneratorParam<T> &b) { return (T)a && (T)b; }
// @}

/** Logical or between between GeneratorParam<T> and any type that supports operator|| with T.
 * Returns type of underlying operator||. */
// @{
template <typename Other, typename T>
decltype((Other)0 || (T)1) operator||(const Other &a, const GeneratorParam<T> &b) { return a || (T)b; }
template <typename Other, typename T>
decltype((T)0 || (Other)1) operator||(const GeneratorParam<T> &a, const Other &b) { return (T)a || b; }
template <typename T>
decltype((T)0 || (T)1) operator||(const GeneratorParam<T> &a, const  GeneratorParam<T> &b) { return (T)a || (T)b; }
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

    HALIDE_NO_USER_CODE_INLINE explicit StubInputBuffer(const Parameter &p) : parameter_(p) {
        // Create an empty 1-element buffer with the right runtime typing and dimensions,
        // which we'll use only to pass to can_convert_from() to verify this
        // Parameter is compatible with our constraints.
        Buffer<> other(p.type(), nullptr, std::vector<int>(p.dimensions(), 1));
        internal_assert((Buffer<T>::can_convert_from(other)));
    }

    template<typename T2>
    HALIDE_NO_USER_CODE_INLINE static Parameter parameter_from_buffer(const Buffer<T2> &b) {
        user_assert((Buffer<T>::can_convert_from(b)));
        Parameter p(b.type(), true, b.dimensions());
        p.set_buffer(b);
        return p;
    }

public:
    StubInputBuffer() = default;

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

    void check_scheduled(const char* m) const;
    Target get_target() const;

    explicit StubOutputBufferBase(const Func &f, std::shared_ptr<GeneratorBase> generator) : f(f), generator(generator) {}
    StubOutputBufferBase() = default;

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
    StubOutputBuffer() = default;
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
    StubInput(const Func &f) : kind_(IOKind::Function), parameter_(), func_(f) {}
    StubInput(const Expr &e) : kind_(IOKind::Scalar), parameter_(), expr_(e) {}

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
    bool array_size_defined() const;
    size_t array_size() const;
    virtual bool is_array() const;

    const std::string &name() const;
    IOKind kind() const;

    bool types_defined() const;
    const std::vector<Type> &types() const;
    Type type() const;

    bool dims_defined() const;
    int dims() const;

    const std::vector<Func> &funcs() const;
    const std::vector<Expr> &exprs() const;

    virtual ~GIOBase();

protected:
    GIOBase(size_t array_size,
            const std::string &name,
            IOKind kind,
            const std::vector<Type> &types,
            int dims);

    friend class GeneratorBase;
    friend class GeneratorParamInfo;

    mutable int array_size_;   // always 1 if is_array() == false.
                               // -1 if is_array() == true but unspecified.

    const std::string name_;
    const IOKind kind_;
    mutable std::vector<Type> types_;  // empty if type is unspecified
    mutable int dims_;                 // -1 if dim is unspecified

    // Exactly one of these will have nonzero length
    std::vector<Func> funcs_;
    std::vector<Expr> exprs_;

    // Generator which owns this Input or Output. Note that this will be null
    // initially; the GeneratorBase itself will set this field when it initially
    // builds its info about params. However, since it isn't
    // appropriate for Input<> or Output<> to be declared outside of a Generator,
    // all reasonable non-testing code should expect this to be non-null.
    GeneratorBase *generator{nullptr};

    std::string array_name(size_t i) const;

    virtual void verify_internals();

    void check_matching_array_size(size_t size) const;
    void check_matching_types(const std::vector<Type> &t) const;
    void check_matching_dims(int d) const;

    template<typename ElemType>
    const std::vector<ElemType> &get_values() const;

    void check_gio_access() const;

    virtual void check_value_writable() const = 0;

    virtual const char *input_or_output() const = 0;

private:
    template<typename T> friend class GeneratorParam_Synthetic;

    // No copy
    GIOBase(const GIOBase &) = delete;
    void operator=(const GIOBase &) = delete;
    // No move
    GIOBase(GIOBase&&) = delete;
    void operator=(GIOBase&&) = delete;
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

    GeneratorInputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d);

    friend class GeneratorBase;
    friend class GeneratorParamInfo;

    std::vector<Parameter> parameters_;

    Parameter parameter() const;

    void init_internals();
    void set_inputs(const std::vector<StubInput> &inputs);

    virtual void set_def_min_max();
    virtual Expr get_def_expr() const;

    void verify_internals() override;

    friend class StubEmitter;

    virtual std::string get_c_type() const = 0;

    void check_value_writable() const override;

    const char *input_or_output() const override { return "Input"; }

    void estimate_impl(Var var, Expr min, Expr extent);

public:
    ~GeneratorInputBase() override;
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
        this->check_gio_access();
        return get_values<ValueType>().size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &operator[](size_t i) const {
        this->check_gio_access();
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &at(size_t i) const {
        this->check_gio_access();
        return get_values<ValueType>().at(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator begin() const {
        this->check_gio_access();
        return get_values<ValueType>().begin();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator end() const {
        this->check_gio_access();
        return get_values<ValueType>().end();
    }
};

// When forwarding methods to ImageParam, Func, etc., we must take
// care with the return types: many of the methods return a reference-to-self
// (e.g., ImageParam&); since we create temporaries for most of these forwards,
// returning a ref will crater because it refers to a now-defunct section of the
// stack. Happily, simply removing the reference is solves this, since all of the
// types in question satisfy the property of copies referring to the same underlying
// structure (returning references is just an optimization). Since this is verbose
// and used in several places, we'll use a helper macro:
#define HALIDE_FORWARD_METHOD(Class, Method)                                \
    template<typename ...Args>                                              \
    inline auto Method(Args&&... args) ->                                   \
        typename std::remove_reference<decltype(std::declval<Class>().Method(std::forward<Args>(args)...))>::type { \
        return this->template as<Class>().Method(std::forward<Args>(args)...);          \
    }

#define HALIDE_FORWARD_METHOD_CONST(Class, Method)                          \
    template<typename ...Args>                                              \
    inline auto Method(Args&&... args) const ->                             \
        typename std::remove_reference<decltype(std::declval<Class>().Method(std::forward<Args>(args)...))>::type { \
        this->check_gio_access(); \
        return this->template as<Class>().Method(std::forward<Args>(args)...);          \
    }

template<typename T>
class GeneratorInput_Buffer : public GeneratorInputImpl<T, Func> {
private:
    using Super = GeneratorInputImpl<T, Func>;

protected:
    using TBase = typename Super::TBase;

    friend class ::Halide::Func;
    friend class ::Halide::Stage;

    std::string get_c_type() const override {
        if (TBase::has_static_halide_type) {
            return "Halide::Internal::StubInputBuffer<" +
                halide_type_to_c_type(TBase::static_halide_type()) +
                ">";
        } else {
            return "Halide::Internal::StubInputBuffer<>";
        }
    }

    template<typename T2>
    inline T2 as() const {
        return (T2) *this;
    }

public:
    GeneratorInput_Buffer(const std::string &name)
        : Super(name, IOKind::Buffer,
                TBase::has_static_halide_type ? std::vector<Type>{ TBase::static_halide_type() } : std::vector<Type>{},
                -1) {
    }

    GeneratorInput_Buffer(const std::string &name, const Type &t, int d = -1)
        : Super(name, IOKind::Buffer, {t}, d) {
        static_assert(!TBase::has_static_halide_type, "You can only specify a Type argument for Input<Buffer<T>> if T is void or omitted.");
    }

    GeneratorInput_Buffer(const std::string &name, int d)
        : Super(name, IOKind::Buffer, TBase::has_static_halide_type ? std::vector<Type>{ TBase::static_halide_type() } : std::vector<Type>{}, d) {
    }

    template <typename... Args>
    Expr operator()(Args&&... args) const {
        this->check_gio_access();
        return Func(*this)(std::forward<Args>(args)...);
    }

    Expr operator()(std::vector<Expr> args) const {
        this->check_gio_access();
        return Func(*this)(args);
    }

    template<typename T2>
    operator StubInputBuffer<T2>() const {
        user_assert(!this->is_array()) << "Cannot assign an array type to a non-array type for Input " << this->name();
        return StubInputBuffer<T2>(this->parameters_.at(0));
    }

    operator Func() const {
        this->check_gio_access();
        return this->funcs().at(0);
    }

    operator ExternFuncArgument() const {
        this->check_gio_access();
        return ExternFuncArgument(this->parameters_.at(0));
    }

    GeneratorInput_Buffer<T> &estimate(Var var, Expr min, Expr extent) {
        this->check_gio_access();
        this->estimate_impl(var, min, extent);
        return *this;
    }

    Func in() {
        this->check_gio_access();
        return Func(*this).in();
    }

    Func in(Func other) {
        this->check_gio_access();
        return Func(*this).in(other);
    }

    Func in(const std::vector<Func> &others) {
        this->check_gio_access();
        return Func(*this).in(others);
    }

    operator ImageParam() const {
        this->check_gio_access();
        user_assert(!this->is_array()) << "Cannot convert an Input<Buffer<>[]> to an ImageParam; use an explicit subscript operator: " << this->name();
        return ImageParam(this->parameters_.at(0), Func(*this));
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    size_t size() const {
        this->check_gio_access();
        return this->parameters_.size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    ImageParam operator[](size_t i) const {
        this->check_gio_access();
        return ImageParam(this->parameters_.at(i), this->funcs().at(i));
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    ImageParam at(size_t i) const {
        this->check_gio_access();
        return ImageParam(this->parameters_.at(i), this->funcs().at(i));
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ImageParam>::const_iterator begin() const {
        user_error << "Input<Buffer<>>::begin() is not supported.";
        return {};
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ImageParam>::const_iterator end() const {
        user_error << "Input<Buffer<>>::end() is not supported.";
        return {};
    }

    /** Forward methods to the ImageParam. */
    // @{
    HALIDE_FORWARD_METHOD(ImageParam, dim)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, dim)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, host_alignment)
    HALIDE_FORWARD_METHOD(ImageParam, set_host_alignment)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, dimensions)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, left)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, right)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, top)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, bottom)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, width)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, height)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, channels)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, trace_loads)
    HALIDE_FORWARD_METHOD_CONST(ImageParam, add_trace_tag)
    // }@
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

    template<typename T2>
    inline T2 as() const {
        return (T2) *this;
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
        this->check_gio_access();
        return this->funcs().at(0)(std::forward<Args>(args)...);
    }

    Expr operator()(std::vector<Expr> args) const {
        this->check_gio_access();
        return this->funcs().at(0)(args);
    }

    operator Func() const {
        this->check_gio_access();
        return this->funcs().at(0);
    }

    operator ExternFuncArgument() const {
        this->check_gio_access();
        return ExternFuncArgument(this->parameters_.at(0));
    }

    GeneratorInput_Func<T> &estimate(Var var, Expr min, Expr extent) {
        this->check_gio_access();
        this->estimate_impl(var, min, extent);
        return *this;
    }

    Func in() {
        this->check_gio_access();
        return Func(*this).in();
    }

    Func in(Func other) {
        this->check_gio_access();
        return Func(*this).in(other);
    }

    Func in(const std::vector<Func> &others) {
        this->check_gio_access();
        return Func(*this).in(others);
    }

    /** Forward const methods to the underlying Func. (Non-const methods
     * aren't available for Input<Func>.) */
    // @{
    HALIDE_FORWARD_METHOD_CONST(Func, args)
    HALIDE_FORWARD_METHOD_CONST(Func, defined)
    HALIDE_FORWARD_METHOD_CONST(Func, has_update_definition)
    HALIDE_FORWARD_METHOD_CONST(Func, num_update_definitions)
    HALIDE_FORWARD_METHOD_CONST(Func, output_types)
    HALIDE_FORWARD_METHOD_CONST(Func, outputs)
    HALIDE_FORWARD_METHOD_CONST(Func, rvars)
    HALIDE_FORWARD_METHOD_CONST(Func, update_args)
    HALIDE_FORWARD_METHOD_CONST(Func, update_value)
    HALIDE_FORWARD_METHOD_CONST(Func, update_values)
    HALIDE_FORWARD_METHOD_CONST(Func, value)
    HALIDE_FORWARD_METHOD_CONST(Func, values)
    // }@
};


template<typename T>
class GeneratorInput_Scalar : public GeneratorInputImpl<T, Expr> {
private:
    using Super = GeneratorInputImpl<T, Expr>;
protected:
    using TBase = typename Super::TBase;

    const TBase def_{TBase()};
    const Expr def_expr_;

protected:
    Expr get_def_expr() const override {
        return def_expr_;
    }

    void set_def_min_max() override {
        for (Parameter &p : this->parameters_) {
            p.set_scalar<TBase>(def_);
        }
    }

    std::string get_c_type() const override {
        return "Expr";
    }

    // Expr() doesn't accept a pointer type in its ctor; add a SFINAE adapter
    // so that pointer (aka handle) Inputs will get cast to uint64.
    template <typename TBase2 = TBase, typename std::enable_if<!std::is_pointer<TBase2>::value>::type * = nullptr>
    static Expr TBaseToExpr(const TBase2 &value) {
        return Expr(value);
    }

    template <typename TBase2 = TBase, typename std::enable_if<std::is_pointer<TBase2>::value>::type * = nullptr>
    static Expr TBaseToExpr(const TBase2 &value) {
        return Expr((uint64_t) value);
    }

public:
    explicit GeneratorInput_Scalar(const std::string &name)
        : Super(name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(static_cast<TBase>(0)), def_expr_(Expr()) {
    }

    GeneratorInput_Scalar(const std::string &name, const TBase &def)
        : Super(name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(def), def_expr_(TBaseToExpr(def)) {
    }

    GeneratorInput_Scalar(size_t array_size,
                          const std::string &name)
        : Super(array_size, name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(static_cast<TBase>(0)), def_expr_(Expr()) {
    }

    GeneratorInput_Scalar(size_t array_size,
                          const std::string &name,
                          const TBase &def)
        : Super(array_size, name, IOKind::Scalar, {type_of<TBase>()}, 0), def_(def), def_expr_(TBaseToExpr(def)) {
    }

    /** You can use this Input as an expression in a halide
     * function definition */
    operator Expr() const {
        this->check_gio_access();
        return this->exprs().at(0);
    }

    /** Using an Input as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        this->check_gio_access();
        return ExternFuncArgument(this->exprs().at(0));
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    void set_estimate(const TBase &value) {
        this->check_gio_access();
        Expr e = Expr(value);
        if (std::is_same<T2, bool>::value) {
          e = cast<bool>(e);
        }
        for (Parameter &p : this->parameters_) {
            p.set_estimate(e);
        }
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    void set_estimate(size_t index, const TBase &value) {
        this->check_gio_access();
        Expr e = Expr(value);
        if (std::is_same<T2, bool>::value) {
          e = cast<bool>(e);
        }
        this->parameters_.at(index).set_estimate(e);
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
        Super::set_def_min_max();
        // Don't set min/max for bool
        if (!std::is_same<TBase, bool>::value) {
            for (Parameter &p : this->parameters_) {
                if (min_.defined()) p.set_min_value(min_);
                if (max_.defined()) p.set_max_value(max_);
            }
        }
    }

public:
    explicit GeneratorInput_Arithmetic(const std::string &name)
        : Super(name), min_(Expr()), max_(Expr()) {
    }

    GeneratorInput_Arithmetic(const std::string &name,
                              const TBase &def)
        : Super(name, def), min_(Expr()), max_(Expr()) {
    }

    GeneratorInput_Arithmetic(size_t array_size,
                              const std::string &name)
        : Super(array_size, name), min_(Expr()), max_(Expr()) {
    }

    GeneratorInput_Arithmetic(size_t array_size,
                              const std::string &name,
                              const TBase &def)
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
    template<typename T2, typename std::enable_if<std::is_same<T2, Func>::value>::type * = nullptr>
    HALIDE_NO_USER_CODE_INLINE T2 as() const {
        static_assert(std::is_same<T2, Func>::value, "Only Func allowed here");
        internal_assert(kind() != IOKind::Scalar);
        internal_assert(exprs_.empty());
        user_assert(funcs_.size() == 1) << "Use [] to access individual Funcs in Output<Func[]>";
        return funcs_[0];
    }

public:
    /** Forward schedule-related methods to the underlying Func. */
    // @{
    HALIDE_FORWARD_METHOD(Func, add_trace_tag)
    HALIDE_FORWARD_METHOD(Func, align_bounds)
    HALIDE_FORWARD_METHOD(Func, align_storage)
    HALIDE_FORWARD_METHOD_CONST(Func, args)
    HALIDE_FORWARD_METHOD(Func, bound)
    HALIDE_FORWARD_METHOD(Func, bound_extent)
    HALIDE_FORWARD_METHOD(Func, compute_at)
    HALIDE_FORWARD_METHOD(Func, compute_inline)
    HALIDE_FORWARD_METHOD(Func, compute_root)
    HALIDE_FORWARD_METHOD(Func, compute_with)
    HALIDE_FORWARD_METHOD(Func, copy_to_device)
    HALIDE_FORWARD_METHOD(Func, copy_to_host)
    HALIDE_FORWARD_METHOD(Func, define_extern)
    HALIDE_FORWARD_METHOD_CONST(Func, defined)
    HALIDE_FORWARD_METHOD(Func, estimate)
    HALIDE_FORWARD_METHOD(Func, fold_storage)
    HALIDE_FORWARD_METHOD(Func, fuse)
    HALIDE_FORWARD_METHOD(Func, glsl)
    HALIDE_FORWARD_METHOD(Func, gpu)
    HALIDE_FORWARD_METHOD(Func, gpu_blocks)
    HALIDE_FORWARD_METHOD(Func, gpu_single_thread)
    HALIDE_FORWARD_METHOD(Func, gpu_threads)
    HALIDE_FORWARD_METHOD(Func, gpu_tile)
    HALIDE_FORWARD_METHOD_CONST(Func, has_update_definition)
    HALIDE_FORWARD_METHOD(Func, hexagon)
    HALIDE_FORWARD_METHOD(Func, in)
    HALIDE_FORWARD_METHOD(Func, memoize)
    HALIDE_FORWARD_METHOD_CONST(Func, num_update_definitions)
    HALIDE_FORWARD_METHOD_CONST(Func, output_types)
    HALIDE_FORWARD_METHOD_CONST(Func, outputs)
    HALIDE_FORWARD_METHOD(Func, parallel)
    HALIDE_FORWARD_METHOD(Func, prefetch)
    HALIDE_FORWARD_METHOD(Func, print_loop_nest)
    HALIDE_FORWARD_METHOD(Func, rename)
    HALIDE_FORWARD_METHOD(Func, reorder)
    HALIDE_FORWARD_METHOD(Func, reorder_storage)
    HALIDE_FORWARD_METHOD_CONST(Func, rvars)
    HALIDE_FORWARD_METHOD(Func, serial)
    HALIDE_FORWARD_METHOD(Func, shader)
    HALIDE_FORWARD_METHOD(Func, specialize)
    HALIDE_FORWARD_METHOD(Func, specialize_fail)
    HALIDE_FORWARD_METHOD(Func, split)
    HALIDE_FORWARD_METHOD(Func, store_at)
    HALIDE_FORWARD_METHOD(Func, store_root)
    HALIDE_FORWARD_METHOD(Func, tile)
    HALIDE_FORWARD_METHOD(Func, trace_stores)
    HALIDE_FORWARD_METHOD(Func, unroll)
    HALIDE_FORWARD_METHOD(Func, update)
    HALIDE_FORWARD_METHOD_CONST(Func, update_args)
    HALIDE_FORWARD_METHOD_CONST(Func, update_value)
    HALIDE_FORWARD_METHOD_CONST(Func, update_values)
    HALIDE_FORWARD_METHOD_CONST(Func, value)
    HALIDE_FORWARD_METHOD_CONST(Func, values)
    HALIDE_FORWARD_METHOD(Func, vectorize)
    // }@

#undef HALIDE_OUTPUT_FORWARD
#undef HALIDE_OUTPUT_FORWARD_CONST

protected:
    GeneratorOutputBase(size_t array_size,
                        const std::string &name,
                        IOKind kind,
                        const std::vector<Type> &t,
                        int d);

    GeneratorOutputBase(const std::string &name,
                        IOKind kind,
                        const std::vector<Type> &t,
                        int d);

    friend class GeneratorBase;
    friend class StubEmitter;

    void init_internals();
    void resize(size_t size);

    virtual std::string get_c_type() const {
        return "Func";
    }

    void check_value_writable() const override;

    const char *input_or_output() const override { return "Output"; }

public:
    ~GeneratorOutputBase() override;
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
        this->check_gio_access();
        return get_values<ValueType>().at(0)(std::forward<Args>(args)...);
    }

    template <typename ExprOrVar, typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    FuncRef operator()(std::vector<ExprOrVar> args) const {
        this->check_gio_access();
        return get_values<ValueType>().at(0)(args);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    operator Func() const {
        this->check_gio_access();
        return get_values<ValueType>().at(0);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    operator Stage() const {
        this->check_gio_access();
        return get_values<ValueType>().at(0);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    size_t size() const {
        this->check_gio_access();
        return get_values<ValueType>().size();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &operator[](size_t i) const {
        this->check_gio_access();
        return get_values<ValueType>()[i];
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const ValueType &at(size_t i) const {
        this->check_gio_access();
        return get_values<ValueType>().at(i);
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator begin() const {
        this->check_gio_access();
        return get_values<ValueType>().begin();
    }

    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    typename std::vector<ValueType>::const_iterator end() const {
        this->check_gio_access();
        return get_values<ValueType>().end();
    }

    template <typename T2 = T, typename std::enable_if<
        // Only allow T2[]
        std::is_array<T2>::value && std::rank<T2>::value == 1 && std::extent<T2, 0>::value == 0
    >::type * = nullptr>
    void resize(size_t size) {
        this->check_gio_access();
        GeneratorOutputBase::resize(size);
    }
};

template<typename T>
class GeneratorOutput_Buffer : public GeneratorOutputImpl<T> {
private:
    using Super = GeneratorOutputImpl<T>;

    HALIDE_NO_USER_CODE_INLINE void assign_from_func(const Func &f) {
        this->check_value_writable();

        internal_assert(f.defined());

        if (TBase::has_static_halide_type) {
            Buffer<> other(f.output_types().at(0), nullptr, std::vector<int>(f.dimensions(), 1));
            user_assert(T::can_convert_from(other))
                << "Cannot assign to the Output \"" << this->name()
                << "\": the expression is not convertible to the same Buffer type and/or dimensions.\n";
        }

        if (this->types_defined()) {
            const auto &my_types = this->types();
            user_assert(my_types.size() == f.output_types().size())
                << "Output " << this->name() << " requires a Func with " << my_types.size() << " type(s) but tried to assign one with " << f.output_types().size() << " type(s)\n";
            for (size_t i = 0; i < my_types.size(); i++) {
                user_assert(my_types[i] == f.output_types().at(i))
                    << "Output " << this->name() << " should have type[" << i << "]=" << my_types[i] << " but saw type[" << i << "]=" << f.output_types().at(i) << "\n";
            }
        }
        if (this->dims_defined()) {
            user_assert(f.dimensions() == this->dims())
                << "Output " << this->name() << " should have dim=" << this->dims() << " but saw dim=" << f.dimensions() << "\n";
        }

        internal_assert(this->exprs_.empty() && this->funcs_.size() == 1);
        user_assert(!this->funcs_.at(0).defined());
        this->funcs_[0] = f;
    }

protected:
    using TBase = typename Super::TBase;

    static std::vector<Type> my_types(const std::vector<Type> &t) {
        if (TBase::has_static_halide_type) {
            user_assert(t.empty()) << "Cannot pass a Type argument for an Output<Buffer> with a non-void static type\n";
            return std::vector<Type>{ TBase::static_halide_type() };
        }
        return t;
    }

protected:
    GeneratorOutput_Buffer(const std::string &name, const std::vector<Type> &t = {}, int d = -1)
        : Super(name, IOKind::Buffer, my_types(t), d) {
    }

    GeneratorOutput_Buffer(size_t array_size, const std::string &name, const std::vector<Type> &t = {}, int d = -1)
        : Super(array_size, name, IOKind::Buffer, my_types(t), d) {
    }

    HALIDE_NO_USER_CODE_INLINE std::string get_c_type() const override {
        if (TBase::has_static_halide_type) {
            return "Halide::Internal::StubOutputBuffer<" +
                halide_type_to_c_type(TBase::static_halide_type()) +
                ">";
        } else {
            return "Halide::Internal::StubOutputBuffer<>";
        }
    }

    template<typename T2, typename std::enable_if<!std::is_same<T2, Func>::value>::type * = nullptr>
    HALIDE_NO_USER_CODE_INLINE T2 as() const {
        return (T2) *this;
    }

public:

    // Allow assignment from a Buffer<> to an Output<Buffer<>>;
    // this allows us to use a statically-compiled buffer inside a Generator
    // to assign to an output.
    // TODO: This used to take the buffer as a const ref. This no longer works as
    // using it in a Pipeline might change the dev field so it is currently
    // not considered const. We should consider how this really ought to work.
    template<typename T2>
    HALIDE_NO_USER_CODE_INLINE GeneratorOutput_Buffer<T> &operator=(Buffer<T2> &buffer) {
        this->check_gio_access();
        this->check_value_writable();

        user_assert(T::can_convert_from(buffer))
            << "Cannot assign to the Output \"" << this->name()
            << "\": the expression is not convertible to the same Buffer type and/or dimensions.\n";

        if (this->types_defined()) {
            user_assert(Type(buffer.type()) == this->type())
                << "Output " << this->name() << " should have type=" << this->type() << " but saw type=" << Type(buffer.type()) << "\n";
        }
        if (this->dims_defined()) {
            user_assert(buffer.dimensions() == this->dims())
                << "Output " << this->name() << " should have dim=" << this->dims() << " but saw dim=" << buffer.dimensions() << "\n";
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
    GeneratorOutput_Buffer<T> &operator=(const StubOutputBuffer<T2> &stub_output_buffer) {
        this->check_gio_access();
        assign_from_func(stub_output_buffer.f);
        return *this;
    }

    // Allow assignment from a Func to an Output<Buffer>;
    // this allows us to use helper functions that return a plain Func
    // to simply set the output(s) without needing a wrapper Func.
    GeneratorOutput_Buffer<T> &operator=(const Func &f) {
        this->check_gio_access();
        assign_from_func(f);
        return *this;
    }

    operator OutputImageParam() const {
        this->check_gio_access();
        user_assert(!this->is_array()) << "Cannot convert an Output<Buffer<>[]> to an ImageParam; use an explicit subscript operator: " << this->name();
        internal_assert(this->exprs_.empty() && this->funcs_.size() == 1);
        return this->funcs_.at(0).output_buffer();
    }

    /** Forward methods to the OutputImageParam. */
    // @{
    HALIDE_FORWARD_METHOD(OutputImageParam, dim)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, dim)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, host_alignment)
    HALIDE_FORWARD_METHOD(OutputImageParam, set_host_alignment)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, dimensions)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, left)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, right)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, top)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, bottom)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, width)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, height)
    HALIDE_FORWARD_METHOD_CONST(OutputImageParam, channels)
    // }@
};


template<typename T>
class GeneratorOutput_Func : public GeneratorOutputImpl<T> {
private:
    using Super = GeneratorOutputImpl<T>;

    HALIDE_NO_USER_CODE_INLINE Func &get_assignable_func_ref(size_t i) {
        internal_assert(this->exprs_.empty() && this->funcs_.size() > i);
        return this->funcs_.at(i);
    }

protected:
    using TBase = typename Super::TBase;

protected:
    GeneratorOutput_Func(const std::string &name)
        : Super(name, IOKind::Function, std::vector<Type>{}, -1) {
    }

    GeneratorOutput_Func(const std::string &name, const std::vector<Type> &t, int d = -1)
        : Super(name, IOKind::Function, t, d) {
    }

    GeneratorOutput_Func(size_t array_size, const std::string &name, const std::vector<Type> &t, int d)
        : Super(array_size, name, IOKind::Function, t, d) {
    }

public:
    // Allow Output<Func> = Func
    template <typename T2 = T, typename std::enable_if<!std::is_array<T2>::value>::type * = nullptr>
    GeneratorOutput_Func<T> &operator=(const Func &f) {
        this->check_gio_access();
        this->check_value_writable();

        // Don't bother verifying the Func type, dimensions, etc., here:
        // That's done later, when we produce the pipeline.
        get_assignable_func_ref(0) = f;
        return *this;
    }

    // Allow Output<Func[]> = Func
    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    Func &operator[](size_t i) {
        this->check_gio_access();
        this->check_value_writable();
        return get_assignable_func_ref(i);
    }

    // Allow Func = Output<Func[]>
    template <typename T2 = T, typename std::enable_if<std::is_array<T2>::value>::type * = nullptr>
    const Func &operator[](size_t i) const {
        this->check_gio_access();
        return Super::operator[](i);
    }

    GeneratorOutput_Func<T> &estimate(Var var, Expr min, Expr extent) {
        this->check_gio_access();
        internal_assert(this->exprs_.empty() && this->funcs_.size() > 0);
        for (Func &f : this->funcs_) {
            f.estimate(var, min, extent);
        }
        return *this;
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

std::vector<Type> parse_halide_type_list(const std::string &types);

enum class SyntheticParamType { Type, Dim, ArraySize };

// This is a type of GeneratorParam used internally to create 'synthetic' params
// (e.g. image.type, image.dim); it is not possible for user code to instantiate it.
template<typename T>
class GeneratorParam_Synthetic : public GeneratorParamImpl<T> {
public:
    void set_from_string(const std::string &new_value_string) override {
        // If error_msg is not empty, this is unsettable:
        // display error_msg as a user error.
        if (!error_msg.empty()) {
            user_error << error_msg;
        }
        set_from_string_impl<T>(new_value_string);
    }

    std::string get_default_value() const override {
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
    friend class GeneratorParamInfo;

    static std::unique_ptr<Internal::GeneratorParamBase> make(
        GeneratorBase *generator,
        const std::string &generator_name,
        const std::string &gpname,
        GIOBase &gio,
        SyntheticParamType which,
        bool defined
    ) {
        std::string error_msg = defined ?
            "Cannot set the GeneratorParam " + gpname + " for " + generator_name + " because the value is explicitly specified in the C++ source." :
            "";
        return std::unique_ptr<GeneratorParam_Synthetic<T>>(
            new GeneratorParam_Synthetic<T>(gpname, gio, which, error_msg));
    }

    GeneratorParam_Synthetic(const std::string &name, GIOBase &gio, SyntheticParamType which, const std::string &error_msg = "") : GeneratorParamImpl<T>(name, T()), gio(gio), which(which), error_msg(error_msg) {}

    template <typename T2 = T, typename std::enable_if<std::is_same<T2, ::Halide::Type>::value>::type * = nullptr>
    void set_from_string_impl(const std::string &new_value_string) {
        internal_assert(which == SyntheticParamType::Type);
        gio.types_ = parse_halide_type_list(new_value_string);
    }

    template <typename T2 = T, typename std::enable_if<std::is_integral<T2>::value>::type * = nullptr>
    void set_from_string_impl(const std::string &new_value_string) {
        if (which == SyntheticParamType::Dim) {
            gio.dims_ = parse_scalar<T2>(new_value_string);
        } else if (which == SyntheticParamType::ArraySize) {
            gio.array_size_ = parse_scalar<T2>(new_value_string);
        } else {
            internal_error;
        }
    }

    GIOBase &gio;
    const SyntheticParamType which;
    const std::string error_msg;
};


class GeneratorStub;

}  // namespace Internal

/** GeneratorContext is a base class that is used when using Generators (or Stubs) directly;
 * it is used to allow the outer context (typically, either a Generator or "top-level" code)
 * to specify certain information to the inner context to ensure that inner and outer
 * Generators are compiled in a compatible way.
 *
 * If you are using this at "top level" (e.g. with the JIT), you can construct a GeneratorContext
 * with a Target:
 * \code
 *   auto my_stub = MyStub(
 *       GeneratorContext(get_target_from_environment()),
 *       // inputs
 *       { ... },
 *       // generator params
 *       { ... }
 *   );
 * \endcode
 *
 * Note that all Generators inherit from GeneratorContext, so if you are using a Stub
 * from within a Generator, you can just pass 'this' for the GeneratorContext:
 * \code
 *  struct SomeGen : Generator<SomeGen> {
 *   void generate() {
 *     ...
 *     auto my_stub = MyStub(
 *       this,  // GeneratorContext
 *       // inputs
 *       { ... },
 *       // generator params
 *       { ... }
 *     );
 *     ...
 *   }
 *  };
 * \endcode
 */
class GeneratorContext {
public:
    using ExternsMap = std::map<std::string, ExternalCode>;

    explicit GeneratorContext(const Target &t,
                              bool auto_schedule = false,
                              const MachineParams &machine_params = MachineParams::generic());
    virtual ~GeneratorContext();

    inline Target get_target() const { return target; }
    inline bool get_auto_schedule() const { return auto_schedule; }
    inline MachineParams get_machine_params() const { return machine_params; }

    /** Generators can register ExternalCode objects onto
     * themselves. The Generator infrastructure will arrange to have
     * this ExternalCode appended to the Module that is finally
     * compiled using the Generator. This allows encapsulating
     * functionality that depends on external libraries or handwritten
     * code for various targets. The name argument should match the
     * name of the ExternalCode block and is used to ensure the same
     * code block is not duplicated in the output. Halide does not do
     * anything other than to compare names for equality. To guarantee
     * uniqueness in public code, we suggest using a Java style
     * inverted domain name followed by organization specific
     * naming. E.g.:
     *     com.yoyodyne.overthruster.0719acd19b66df2a9d8d628a8fefba911a0ab2b7
     *
     * See test/generator/external_code_generator.cpp for example use. */
    inline std::shared_ptr<ExternsMap> get_externs_map() const {
        return externs_map;
    }

    template <typename T>
    inline std::unique_ptr<T> create() const {
        return T::create(*this);
    }

    template <typename T, typename... Args>
    inline std::unique_ptr<T> apply(const Args &...args) const {
        auto t = this->create<T>();
        t->apply(args...);
        return t;
    }

protected:
    GeneratorParam<Target> target;
    GeneratorParam<bool> auto_schedule;
    GeneratorParam<MachineParams> machine_params;
    std::shared_ptr<ExternsMap> externs_map;
    std::shared_ptr<Internal::ValueTracker> value_tracker;

    GeneratorContext() : GeneratorContext(Target()) {}

    virtual void init_from_context(const Halide::GeneratorContext &context);

    inline std::shared_ptr<Internal::ValueTracker> get_value_tracker() const {
        return value_tracker;
    }

    // No copy
    GeneratorContext(const GeneratorContext &) = delete;
    void operator=(const GeneratorContext &) = delete;
    // No move
    GeneratorContext(GeneratorContext&&) = delete;
    void operator=(GeneratorContext&&) = delete;
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
    using MemoryType = Halide::MemoryType;
    using NameMangling = Halide::NameMangling;
    using Pipeline = Halide::Pipeline;
    using PrefetchBoundStrategy = Halide::PrefetchBoundStrategy;
    using RDom = Halide::RDom;
    using RVar = Halide::RVar;
    using TailStrategy = Halide::TailStrategy;
    using Target = Halide::Target;
    using Tuple = Halide::Tuple;
    using Type = Halide::Type;
    using Var = Halide::Var;
    template <typename T> static Expr cast(Expr e) { return Halide::cast<T>(e); }
    static inline Expr cast(Halide::Type t, Expr e) { return Halide::cast(t, e); }
    template <typename T> using GeneratorParam = Halide::GeneratorParam<T>;
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

// Note that these functions must never return null:
// if they cannot return a valid Generator, they must assert-fail.
using GeneratorFactory = std::function<std::unique_ptr<GeneratorBase>(const GeneratorContext&)>;

struct StringOrLoopLevel {
    std::string string_value;
    LoopLevel loop_level;

    StringOrLoopLevel() = default;
    /*not-explicit*/ StringOrLoopLevel(const char *s) : string_value(s) {}
    /*not-explicit*/ StringOrLoopLevel(const std::string &s) : string_value(s) {}
    /*not-explicit*/ StringOrLoopLevel(const LoopLevel &loop_level) : loop_level(loop_level) {}
};
using GeneratorParamsMap = std::map<std::string, StringOrLoopLevel>;

class GeneratorParamInfo {
    // names used across all params, inputs, and outputs.
    std::set<std::string> names;

    // Ordered-list of non-null ptrs to GeneratorParam<> fields.
    std::vector<Internal::GeneratorParamBase *> filter_generator_params;

    // Ordered-list of non-null ptrs to Input<> fields.
    std::vector<Internal::GeneratorInputBase *> filter_inputs;

    // Ordered-list of non-null ptrs to Output<> fields; empty if old-style Generator.
    std::vector<Internal::GeneratorOutputBase *> filter_outputs;

    // list of synthetic GP's that we dynamically created; this list only exists to simplify
    // lifetime management, and shouldn't be accessed directly outside of our ctor/dtor,
    // regardless of friend access.
    std::vector<std::unique_ptr<Internal::GeneratorParamBase>> owned_synthetic_params;

    // list of dynamically-added inputs and outputs, here only for lifetime management.
    std::vector<std::unique_ptr<Internal::GIOBase>> owned_extras;

public:
    friend class GeneratorBase;

    GeneratorParamInfo(GeneratorBase *generator, const size_t size);

    const std::vector<Internal::GeneratorParamBase *> &generator_params() const { return filter_generator_params; }
    const std::vector<Internal::GeneratorInputBase *> &inputs() const { return filter_inputs; }
    const std::vector<Internal::GeneratorOutputBase *> &outputs() const { return filter_outputs; }
};


class GeneratorBase : public NamesInterface, public GeneratorContext {
public:
    struct EmitOptions {
        bool emit_o{false};
        bool emit_h{true};
        bool emit_cpp{false};
        bool emit_python_extension{false};
        bool emit_assembly{false};
        bool emit_bitcode{false};
        bool emit_stmt{false};
        bool emit_stmt_html{false};
        bool emit_static_library{true};
        bool emit_cpp_stub{false};
        bool emit_schedule{false};
        bool emit_registration{false};

        // This is an optional map used to replace the default extensions generated for
        // a file: if an key matches an output extension, emit those files with the
        // corresponding value instead (e.g., ".s" -> ".assembly_text"). This is
        // empty by default; it's mainly useful in build environments where the default
        // extensions are problematic, and avoids the need to rename output files
        // after the fact.
        std::map<std::string, std::string> substitutions;
    };

    virtual ~GeneratorBase();

    void set_generator_param_values(const GeneratorParamsMap &params);

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

    void emit_cpp_stub(const std::string &stub_file_path);

    // Call build() and produce a Module for the result.
    // If function_name is empty, generator_name() will be used for the function.
    Module build_module(const std::string &function_name = "",
                        const LinkageType linkage_type = LinkageType::ExternalPlusMetadata);

    /**
     * set_inputs is a variadic wrapper around set_inputs_vector, which makes usage much simpler
     * in many cases, as it constructs the relevant entries for the vector for you, which
     * is often a bit unintuitive at present. The arguments are passed in Input<>-declaration-order,
     * and the types must be compatible. Array inputs are passed as std::vector<> of the relevant type.
     *
     * Note: at present, scalar input types must match *exactly*, i.e., for Input<uint8_t>, you
     * must pass an argument that is actually uint8_t; an argument that is int-that-will-fit-in-uint8
     * will assert-fail at Halide compile time.
     */
    template <typename... Args>
    void set_inputs(const Args &...args) {
        // set_inputs_vector() checks this too, but checking it here allows build_inputs() to avoid out-of-range checks.
        GeneratorParamInfo &pi = this->param_info();
        user_assert(sizeof...(args) == pi.inputs().size())
                << "Expected exactly " << pi.inputs().size()
                << " inputs but got " << sizeof...(args) << "\n";
        set_inputs_vector(build_inputs(std::forward_as_tuple<const Args &...>(args...), make_index_sequence<sizeof...(Args)>{}));
    }

    Realization realize(std::vector<int32_t> sizes) {
        this->check_scheduled("realize");
        return get_pipeline().realize(sizes, get_target());
    }

    // Only enable if none of the args are Realization; otherwise we can incorrectly
    // select this method instead of the Realization-as-outparam variant
    template <typename... Args, typename std::enable_if<NoRealizations<Args...>::value>::type * = nullptr>
    Realization realize(Args&&... args) {
        this->check_scheduled("realize");
        return get_pipeline().realize(std::forward<Args>(args)..., get_target());
    }

    void realize(Realization r) {
        this->check_scheduled("realize");
        get_pipeline().realize(r, get_target());
    }

    // Return the Pipeline that has been built by the generate() method.
    // This method can only be used from a Generator that has a generate()
    // method (vs a build() method), and currently can only be called from
    // the schedule() method. (This may be relaxed in the future to allow
    // calling from generate() as long as all Outputs have been defined.)
    Pipeline get_pipeline();

    // Create Input<Buffer> or Input<Func> with dynamic type
    template<typename T,
             typename std::enable_if<!std::is_arithmetic<T>::value>::type * = nullptr>
    GeneratorInput<T> *add_input(const std::string &name, const Type &t, int dimensions) {
        check_exact_phase(GeneratorBase::ConfigureCalled);
        auto *p = new GeneratorInput<T>(name, t, dimensions);
        p->generator = this;
        param_info_ptr->owned_extras.push_back(std::unique_ptr<Internal::GIOBase>(p));
        param_info_ptr->filter_inputs.push_back(p);
        return p;
    }

    // Create a Input<Buffer> or Input<Func> with compile-time type
    template<typename T,
             typename std::enable_if<T::has_static_halide_type>::type * = nullptr>
    GeneratorInput<T> *add_input(const std::string &name, int dimensions) {
        check_exact_phase(GeneratorBase::ConfigureCalled);
        auto *p = new GeneratorInput<T>(name, dimensions);
        p->generator = this;
        param_info_ptr->owned_extras.push_back(std::unique_ptr<Internal::GIOBase>(p));
        param_info_ptr->filter_inputs.push_back(p);
        return p;
    }

    // Create Input<scalar>
    template<typename T,
             typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr>
    GeneratorInput<T> *add_input(const std::string &name) {
        check_exact_phase(GeneratorBase::ConfigureCalled);
        auto *p = new GeneratorInput<T>(name);
        p->generator = this;
        param_info_ptr->owned_extras.push_back(std::unique_ptr<Internal::GIOBase>(p));
        param_info_ptr->filter_inputs.push_back(p);
        return p;
    }

    // Create Output<Buffer> or Output<Func> with dynamic type
    template<typename T,
             typename std::enable_if<!std::is_arithmetic<T>::value>::type * = nullptr>
    GeneratorOutput<T> *add_output(const std::string &name, const Type &t, int dimensions) {
        check_exact_phase(GeneratorBase::ConfigureCalled);
        auto *p = new GeneratorOutput<T>(name, t, dimensions);
        p->generator = this;
        param_info_ptr->owned_extras.push_back(std::unique_ptr<Internal::GIOBase>(p));
        param_info_ptr->filter_outputs.push_back(p);
        return p;
    }

    // Create a Output<Buffer> or Output<Func> with compile-time type
    template<typename T,
             typename std::enable_if<T::has_static_halide_type>::type * = nullptr>
    GeneratorOutput<T> *add_output(const std::string &name, int dimensions) {
        check_exact_phase(GeneratorBase::ConfigureCalled);
        auto *p = new GeneratorOutput<T>(name, dimensions);
        p->generator = this;
        param_info_ptr->owned_extras.push_back(std::unique_ptr<Internal::GIOBase>(p));
        param_info_ptr->filter_outputs.push_back(p);
        return p;
    }

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE
    void add_requirement(Expr condition, Args&&... args) {
        get_pipeline().add_requirement(condition, std::forward<Args>(args)...);
    }

protected:
    GeneratorBase(size_t size, const void *introspection_helper);
    void set_generator_names(const std::string &registered_name, const std::string &stub_name);

    void init_from_context(const Halide::GeneratorContext &context) override;

    virtual Pipeline build_pipeline() = 0;
    virtual void call_configure() = 0;
    virtual void call_generate() = 0;
    virtual void call_schedule() = 0;

    void track_parameter_values(bool include_outputs);

    void pre_build();
    void post_build();
    void pre_configure();
    void post_configure();
    void pre_generate();
    void post_generate();
    void pre_schedule();
    void post_schedule();

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

        // Generator has had its configure() method called. (For Generators without
        // a configure() method, this phase will be skipped and will advance
        // directly to InputsSet.)
        ConfigureCalled,

        // All Input<>/Param<> fields have been set. (Applicable only in JIT mode;
        // in AOT mode, this can be skipped, going Created->GenerateCalled directly.)
        InputsSet,

        // Generator has had its generate() method called. (For Generators with
        // a build() method instead of generate(), this phase will be skipped
        // and will advance directly to ScheduleCalled.)
        GenerateCalled,

        // Generator has had its schedule() method (if any) called.
        ScheduleCalled,
    } phase{Created};

    void check_exact_phase(Phase expected_phase) const;
    void check_min_phase(Phase expected_phase) const;
    void advance_phase(Phase new_phase);

private:
    friend void ::Halide::Internal::generator_test();
    friend class GeneratorParamBase;
    friend class GIOBase;
    friend class GeneratorInputBase;
    friend class GeneratorOutputBase;
    friend class GeneratorParamInfo;
    friend class GeneratorStub;
    friend class SimpleGeneratorFactory;
    friend class StubOutputBufferBase;

    const size_t size;

    // Lazily-allocated-and-inited struct with info about our various Params.
    // Do not access directly: use the param_info() getter.
    std::unique_ptr<GeneratorParamInfo> param_info_ptr;

    mutable std::shared_ptr<ExternsMap> externs_map;

    bool inputs_set{false};
    std::string generator_registered_name, generator_stub_name;
    Pipeline pipeline;

    // Return our GeneratorParamInfo.
    GeneratorParamInfo &param_info();

    Internal::GeneratorOutputBase *find_output_by_name(const std::string &name);

    void check_scheduled(const char* m) const;

    void build_params(bool force = false);

    // Provide private, unimplemented, wrong-result-type methods here
    // so that Generators don't attempt to call the global methods
    // of the same name by accident: use the get_target() method instead.
    void get_host_target();
    void get_jit_target_from_environment();
    void get_target_from_environment();

    // Return the Output<Func> or Output<Buffer> with the given name,
    // which must be a singular (non-array) Func or Buffer output.
    // If no such name exists (or is non-array), assert; this method never returns an undefined Func.
    Func get_output(const std::string &n);

    // Return the Output<Func[]> with the given name, which must be an
    // array-of-Func output. If no such name exists (or is non-array), assert;
    // this method never returns undefined Funcs.
    std::vector<Func> get_array_output(const std::string &n);

    void set_inputs_vector(const std::vector<std::vector<StubInput>> &inputs);

    static void check_input_is_singular(Internal::GeneratorInputBase *in);
    static void check_input_is_array(Internal::GeneratorInputBase *in);
    static void check_input_kind(Internal::GeneratorInputBase *in, Internal::IOKind kind);

    // Allow Buffer<> if:
    // -- we are assigning it to an Input<Buffer<>> (with compatible type and dimensions),
    // causing the Input<Buffer<>> to become a precompiled buffer in the generated code.
    // -- we are assigningit to an Input<Func>, in which case we just Func-wrap the Buffer<>.
    template<typename T>
    std::vector<StubInput> build_input(size_t i, const Buffer<T> &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_is_singular(in);
        const auto k = in->kind();
        if (k == Internal::IOKind::Buffer) {
            Halide::Buffer<> b = arg;
            StubInputBuffer<> sib(b);
            StubInput si(sib);
            return {si};
        } else if (k == Internal::IOKind::Function) {
            Halide::Func f(arg.name() + "_im");
            f(Halide::_) = arg(Halide::_);
            StubInput si(f);
            return {si};
        } else {
            check_input_kind(in, Internal::IOKind::Buffer);  // just to trigger assertion
            return {};
        }
    }

    // Allow Input<Buffer<>> if:
    // -- we are assigning it to another Input<Buffer<>> (with compatible type and dimensions),
    // allowing us to simply pipe a parameter from an enclosing Generator to the Invoker.
    // -- we are assigningit to an Input<Func>, in which case we just Func-wrap the Input<Buffer<>>.
    template<typename T>
    std::vector<StubInput> build_input(size_t i, const GeneratorInput<Buffer<T>> &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_is_singular(in);
        const auto k = in->kind();
        if (k == Internal::IOKind::Buffer) {
            StubInputBuffer<> sib = arg;
            StubInput si(sib);
            return {si};
        } else if (k == Internal::IOKind::Function) {
            Halide::Func f = arg.funcs().at(0);
            StubInput si(f);
            return {si};
        } else {
            check_input_kind(in, Internal::IOKind::Buffer);  // just to trigger assertion
            return {};
        }
    }

    // Allow Func iff we are assigning it to an Input<Func> (with compatible type and dimensions).
    std::vector<StubInput> build_input(size_t i, const Func &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Function);
        check_input_is_singular(in);
        Halide::Func f = arg;
        StubInput si(f);
        return {si};
    }

    // Allow vector<Func> iff we are assigning it to an Input<Func[]> (with compatible type and dimensions).
    std::vector<StubInput> build_input(size_t i, const std::vector<Func> &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Function);
        check_input_is_array(in);
        // My kingdom for a list comprehension...
        std::vector<StubInput> siv;
        siv.reserve(arg.size());
        for (const auto &f : arg) {
            siv.emplace_back(f);
        }
        return siv;
    }

    // Expr must be Input<Scalar>.
    std::vector<StubInput> build_input(size_t i, const Expr &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Scalar);
        check_input_is_singular(in);
        StubInput si(arg);
        return {si};
    }

    // (Array form)
    std::vector<StubInput> build_input(size_t i, const std::vector<Expr> &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Scalar);
        check_input_is_array(in);
        std::vector<StubInput> siv;
        siv.reserve(arg.size());
        for (const auto &value : arg) {
            siv.emplace_back(value);
        }
        return siv;
    }

    // Any other type must be convertible to Expr and must be associated with an Input<Scalar>.
    // Use is_arithmetic since some Expr conversions are explicit.
    template<typename T,
             typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr>
    std::vector<StubInput> build_input(size_t i, const T &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Scalar);
        check_input_is_singular(in);
        // We must use an explicit Expr() ctor to preserve the type
        Expr e(arg);
        StubInput si(e);
        return {si};
    }

    // (Array form)
    template<typename T,
             typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr>
    std::vector<StubInput> build_input(size_t i, const std::vector<T> &arg) {
        auto *in = param_info().inputs().at(i);
        check_input_kind(in, Internal::IOKind::Scalar);
        check_input_is_array(in);
        std::vector<StubInput> siv;
        siv.reserve(arg.size());
        for (const auto &value : arg) {
            // We must use an explicit Expr() ctor to preserve the type;
            // otherwise, implicit conversions can downgrade (e.g.) float -> int
            Expr e(value);
            siv.emplace_back(e);
        }
        return siv;
    }

    template<typename... Args, size_t... Indices>
    std::vector<std::vector<StubInput>> build_inputs(const std::tuple<const Args &...>& t, index_sequence<Indices...>) {
        return {build_input(Indices, std::get<Indices>(t))...};
    }

    // No copy
    GeneratorBase(const GeneratorBase &) = delete;
    void operator=(const GeneratorBase &) = delete;
    // No move
    GeneratorBase(GeneratorBase&& that) = delete;
    void operator=(GeneratorBase&& that) = delete;
};

class GeneratorRegistry {
public:
    static void register_factory(const std::string &name, GeneratorFactory generator_factory);
    static void unregister_factory(const std::string &name);
    static std::vector<std::string> enumerate();
    // Note that this method will never return null:
    // if it cannot return a valid Generator, it should assert-fail.
    static std::unique_ptr<GeneratorBase> create(const std::string &name,
                                                 const Halide::GeneratorContext &context);

private:
    using GeneratorFactoryMap = std::map<const std::string, GeneratorFactory>;

    GeneratorFactoryMap factories;
    std::mutex mutex;

    static GeneratorRegistry &get_registry();

    GeneratorRegistry() = default;
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
    static std::unique_ptr<T> create(const Halide::GeneratorContext &context) {
        // We must have an object of type T (not merely GeneratorBase) to call a protected method,
        // because CRTP is a weird beast.
        auto g = std::unique_ptr<T>(new T());
        g->init_from_context(context);
        return g;
    }

    // This is public but intended only for use by the HALIDE_REGISTER_GENERATOR() macro.
    static std::unique_ptr<T> create(const Halide::GeneratorContext &context,
                                     const std::string &registered_name,
                                     const std::string &stub_name) {
        auto g = create(context);
        g->set_generator_names(registered_name, stub_name);
        return g;
    }

    using Internal::GeneratorBase::apply;
    using Internal::GeneratorBase::create;

    template <typename... Args>
    void apply(const Args &...args) {
#ifndef _MSC_VER
        // VS2015 apparently has some SFINAE issues, so this can inappropriately
        // trigger there. (We'll still fail when generate() is called, just
        // with a less-helpful error message.)
        static_assert(has_generate_method<T>::value, "apply() is not supported for old-style Generators.");
#endif
        call_configure();
        set_inputs(args...);
        call_generate();
        call_schedule();
    }

private:

    // std::is_member_function_pointer will fail if there is no member of that name,
    // so we use a little SFINAE to detect if there are method-shaped members.
    template<typename>
    struct type_sink { typedef void type; };

    template<typename T2, typename = void>
    struct has_configure_method : std::false_type {};

    template<typename T2>
    struct has_configure_method<T2, typename type_sink<decltype(std::declval<T2>().configure())>::type> : std::true_type {};

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

    // Implementations for build_pipeline_impl(), specialized on whether we
    // have build() or generate()/schedule() methods.

    // MSVC apparently has some weirdness with the usual sfinae tricks
    // for detecting method-shaped things, so we can't actually use
    // the helpers above outside of static_assert. Instead we make as
    // many overloads as we can exist, and then use C++'s preference
    // for treating a 0 as an int rather than a double to choose one
    // of them.
    Pipeline build_pipeline_impl(double) {
        static_assert(!has_configure_method<T2>::value, "The configure() method is ignored if you define a build() method; use generate() instead.");
        static_assert(!has_schedule_method<T2>::value, "The schedule() method is ignored if you define a build() method; use generate() instead.");
        pre_build();
        Pipeline p = ((T *)this)->build();
        post_build();
        return p;
    }

    template <typename T2 = T,
              typename = decltype(std::declval<T2>().generate())>
    Pipeline build_pipeline_impl(int) {
        // No: configure() must be called prior to this
        // (and in fact, prior to calling set_inputs).
        //
        // ((T *)this)->call_configure_impl(0, 0);

        ((T *)this)->call_generate_impl(0);
        ((T *)this)->call_schedule_impl(0, 0);
        return get_pipeline();
    }

    // Implementations for call_configure_impl(), specialized on whether we
    // have build() or configure()/generate()/schedule() methods.

    void call_configure_impl(double, double) {
        // Called as a side effect for build()-method Generators; quietly do nothing.
    }

    template<typename T2 = T,
             typename = decltype(std::declval<T2>().generate())>
    void call_configure_impl(double, int) {
        // Generator has a generate() method but no configure() method. This is ok. Just advance the phase.
        pre_configure();
        static_assert(!has_configure_method<T2>::value, "Did not expect a configure method here.");
        post_configure();
    }

    template<typename T2 = T,
             typename = decltype(std::declval<T2>().generate()),
             typename = decltype(std::declval<T2>().configure())>
    void call_configure_impl(int, int) {
        T *t = (T*)this;
        static_assert(std::is_void<decltype(t->configure())>::value, "configure() must return void");
        pre_configure();
        t->configure();
        post_configure();
    }

    // Implementations for call_generate_impl(), specialized on whether we
    // have build() or configure()/generate()/schedule() methods.

    void call_generate_impl(double) {
        user_error << "Unimplemented";
    }

    template <typename T2 = T,
              typename = decltype(std::declval<T2>().generate())>
    void call_generate_impl(int) {
        T *t = (T*)this;
        static_assert(std::is_void<decltype(t->generate())>::value, "generate() must return void");
        pre_generate();
        t->generate();
        post_generate();
    }

    // Implementations for call_schedule_impl(), specialized on whether we
    // have build() or configure()generate()/schedule() methods.

    void call_schedule_impl(double, double) {
        user_error << "Unimplemented";
    }

    template<typename T2 = T,
             typename = decltype(std::declval<T2>().generate())>
    void call_schedule_impl(double, int) {
        // Generator has a generate() method but no schedule() method. This is ok. Just advance the phase.
        pre_schedule();
        post_schedule();
    }

    template<typename T2 = T,
             typename = decltype(std::declval<T2>().generate()),
             typename = decltype(std::declval<T2>().schedule())>
    void call_schedule_impl(int, int) {
        T *t = (T*)this;
        static_assert(std::is_void<decltype(t->schedule())>::value, "schedule() must return void");
        pre_schedule();
        t->schedule();
        post_schedule();
    }

protected:
    Pipeline build_pipeline() override {
        return this->build_pipeline_impl(0);
    }

    void call_configure() override {
        this->call_configure_impl(0, 0);
    }

    void call_generate() override {
        this->call_generate_impl(0);
    }

    void call_schedule() override {
        this->call_schedule_impl(0, 0);
    }

private:
    friend void ::Halide::Internal::generator_test();
    friend class Internal::SimpleGeneratorFactory;
    friend void ::Halide::Internal::generator_test();
    friend class ::Halide::GeneratorContext;

    // No copy
    Generator(const Generator &) = delete;
    void operator=(const Generator &) = delete;
    // No move
    Generator(Generator&& that) = delete;
    void operator=(Generator&& that) = delete;
};

namespace Internal {

class RegisterGenerator {
public:
    RegisterGenerator(const char* registered_name, GeneratorFactory generator_factory) {
        Internal::GeneratorRegistry::register_factory(registered_name, generator_factory);
    }
};

class GeneratorStub : public NamesInterface {
public:
    GeneratorStub(const GeneratorContext &context,
                         GeneratorFactory generator_factory);

    GeneratorStub(const GeneratorContext &context,
                         GeneratorFactory generator_factory,
                         const GeneratorParamsMap &generator_params,
                         const std::vector<std::vector<Internal::StubInput>> &inputs);
    std::vector<std::vector<Func>> generate(const GeneratorParamsMap &generator_params,
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

    template<typename T2>
    std::vector<T2> get_array_output_buffer(const std::string &n) const {
        auto v =  generator->get_array_output(n);
        std::vector<T2> result;
        for (auto &o : v) {
            result.push_back(T2(o, generator));
        }
        return result;
    }

    std::vector<Func> get_array_output(const std::string &n) const {
        return generator->get_array_output(n);
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

    struct Names {
        std::vector<std::string> generator_params, inputs, outputs;
    };
    Names get_names() const;

    std::shared_ptr<GeneratorBase> generator;
};

}  // namespace Internal


}  // namespace Halide

// Define this namespace at global scope so that anonymous namespaces won't
// defeat our static_assert check; define a dummy type inside so we can
// check for type aliasing injected by anonymous namespace usage
namespace halide_register_generator {
    struct halide_global_ns;
};

#define _HALIDE_REGISTER_GENERATOR_IMPL(GEN_CLASS_NAME, GEN_REGISTRY_NAME, FULLY_QUALIFIED_STUB_NAME) \
    namespace halide_register_generator { \
        struct halide_global_ns; \
        namespace GEN_REGISTRY_NAME##_ns { \
            std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context); \
            std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context) { \
                return GEN_CLASS_NAME::create(context, #GEN_REGISTRY_NAME, #FULLY_QUALIFIED_STUB_NAME); \
            } \
        } \
        static auto reg_##GEN_REGISTRY_NAME = Halide::Internal::RegisterGenerator(#GEN_REGISTRY_NAME, GEN_REGISTRY_NAME##_ns::factory); \
    } \
    static_assert(std::is_same<::halide_register_generator::halide_global_ns, halide_register_generator::halide_global_ns>::value, \
                 "HALIDE_REGISTER_GENERATOR must be used at global scope");

#define _HALIDE_REGISTER_GENERATOR2(GEN_CLASS_NAME, GEN_REGISTRY_NAME) \
    _HALIDE_REGISTER_GENERATOR_IMPL(GEN_CLASS_NAME, GEN_REGISTRY_NAME, GEN_REGISTRY_NAME)

#define _HALIDE_REGISTER_GENERATOR3(GEN_CLASS_NAME, GEN_REGISTRY_NAME, FULLY_QUALIFIED_STUB_NAME) \
    _HALIDE_REGISTER_GENERATOR_IMPL(GEN_CLASS_NAME, GEN_REGISTRY_NAME, FULLY_QUALIFIED_STUB_NAME)

// MSVC has a broken implementation of variadic macros: it expands __VA_ARGS__
// as a single token in argument lists (rather than multiple tokens).
// Jump through some hoops to work around this.
#define __HALIDE_REGISTER_ARGCOUNT_IMPL(_1, _2, _3, COUNT, ...) \
   COUNT

#define _HALIDE_REGISTER_ARGCOUNT_IMPL(ARGS) \
   __HALIDE_REGISTER_ARGCOUNT_IMPL ARGS

#define _HALIDE_REGISTER_ARGCOUNT(...) \
   _HALIDE_REGISTER_ARGCOUNT_IMPL((__VA_ARGS__, 3, 2, 1, 0))

#define ___HALIDE_REGISTER_CHOOSER(COUNT) \
    _HALIDE_REGISTER_GENERATOR##COUNT

#define __HALIDE_REGISTER_CHOOSER(COUNT) \
    ___HALIDE_REGISTER_CHOOSER(COUNT)

#define _HALIDE_REGISTER_CHOOSER(COUNT) \
    __HALIDE_REGISTER_CHOOSER(COUNT)

#define _HALIDE_REGISTER_GENERATOR_PASTE(A, B) \
    A B

#define HALIDE_REGISTER_GENERATOR(...) \
    _HALIDE_REGISTER_GENERATOR_PASTE(_HALIDE_REGISTER_CHOOSER(_HALIDE_REGISTER_ARGCOUNT(__VA_ARGS__)), (__VA_ARGS__))

// HALIDE_REGISTER_GENERATOR_ALIAS() can be used to create an an alias-with-a-particular-set-of-param-values
// for a given Generator in the build system. Normally, you wouldn't want to do this;
// however, some existing Halide clients have build systems that make it challenging to
// specify GeneratorParams inside the build system, and this allows a somewhat simpler
// customization route for them. It's highly recommended you don't use this for new code.
//
// The final argument is really an initializer-list of GeneratorParams, in the form
// of an initializer-list for map<string, string>:
//
//    { { "gp-name", "gp-value"} [, { "gp2-name", "gp2-value" }] }
//
// It is specified as a variadic template argument to allow for the fact that the embedded commas
// would otherwise confuse the preprocessor; since (in this case) all we're going to do is
// pass it thru as-is, this is fine (and even MSVC's 'broken' __VA_ARGS__ should be OK here).
#define HALIDE_REGISTER_GENERATOR_ALIAS(GEN_REGISTRY_NAME, ORIGINAL_REGISTRY_NAME, ...) \
    namespace halide_register_generator { \
        struct halide_global_ns; \
        namespace ORIGINAL_REGISTRY_NAME##_ns { \
            std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context); \
        } \
        namespace GEN_REGISTRY_NAME##_ns { \
            std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context); \
            std::unique_ptr<Halide::Internal::GeneratorBase> factory(const Halide::GeneratorContext& context) { \
                auto g = ORIGINAL_REGISTRY_NAME##_ns::factory(context); \
                g->set_generator_param_values(__VA_ARGS__); \
                return g; \
            } \
        } \
        static auto reg_##GEN_REGISTRY_NAME = Halide::Internal::RegisterGenerator(#GEN_REGISTRY_NAME, GEN_REGISTRY_NAME##_ns::factory); \
    } \
    static_assert(std::is_same<::halide_register_generator::halide_global_ns, halide_register_generator::halide_global_ns>::value, \
                 "HALIDE_REGISTER_GENERATOR_ALIAS must be used at global scope");

#endif  // HALIDE_GENERATOR_H_
