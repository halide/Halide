#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

#include <type_traits>

#include "Argument.h"
#include "IR.h"

/** \file
 *
 * Classes for declaring scalar parameters to halide pipelines
 */

namespace Halide {

/** A scalar parameter to a halide pipeline. If you're jitting, this
 * should be bound to an actual value of type T using the set method
 * before you realize the function uses this. If you're statically
 * compiling, this param should appear in the argument list. */
template<typename T = void>
class Param {
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** True if T is of type void or const void */
    static const bool T_is_void = std::is_same<typename std::remove_const<T>::type, void>::value;

    // This is a deliberately non-existent type that allows us to compile Param<>
    // but provide less-confusing error messages if you attempt to call get<> or set<>
    // without explicit types.
    struct DynamicParamType;

    /** T unless T is (const) void, in which case (const)
     * uint8_t. Useful for providing return types for operator() */
    using not_void_T = typename std::conditional<T_is_void, DynamicParamType**, T>::type;


    void check_name() const {
        user_assert(param.name() != "__user_context") << "Param<void*>(\"__user_context\") "
            << "is no longer used to control whether Halide functions take explicit "
            << "user_context arguments. Use set_custom_user_context() when jitting, "
            << "or add Target::UserContext to the Target feature set when compiling ahead of time.";
    }

public:
    /** True if the Halide type is not void (or const void). */
    static constexpr bool has_static_type = !T_is_void;

    /** Get the Halide type of T. Callers should not use the result if
     * has_static_halide_type is false. */
    static Type static_type() {
        internal_assert(has_static_type);
        return type_of<T>();
    }

    /** Construct a scalar parameter of type T with a unique
     * auto-generated name */
    // @{
    Param() :
        param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
    }
    explicit Param(Type t) :
        param(t, false, 0, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        static_assert(!has_static_type, "Cannot use this ctor with an explicit type.");
    }
    // @}

    /** Construct a scalar parameter of type T with the given name. */
    // @{
    explicit Param(const std::string &n) :
        param(type_of<T>(), false, 0, n, /*is_explicit_name*/ true) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
    }
    explicit Param(const char *n) :
        param(type_of<T>(), false, 0, n, /*is_explicit_name*/ true) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
    }
    Param(Type t, const std::string &n) :
        param(t, false, 0, n, /*is_explicit_name*/ true) {
        static_assert(!has_static_type, "Cannot use this ctor with an explicit type.");
        check_name();
    }
    // @}

    /** Construct a scalar parameter of type T an initial value of
     * 'val'. Only triggers for non-pointer types. */
    template <typename T2 = T, typename std::enable_if<!std::is_pointer<T2>::value>::type * = nullptr>
    explicit Param(not_void_T val) :
        param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val'. */
    Param(const std::string &n, not_void_T val) :
        param(type_of<T>(), false, 0, n, /*is_explicit_name*/ true) {
        check_name();
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with an initial value of 'val'
    * and a given min and max. */
    Param(not_void_T val, Expr min, Expr max) :
        param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set_range(min, max);
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val' and a given min and max. */
    Param(const std::string &n, not_void_T val, Expr min, Expr max) :
        param(type_of<T>(), false, 0, n, /*is_explicit_name*/ true) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
        set_range(min, max);
        set<not_void_T>(val);
    }

    /** Get the name of this parameter */
    const std::string &name() const {
        return param.name();
    }

    /** Return true iff the name was explicitly specified in the ctor (vs autogenerated). */
    bool is_explicit_name() const {
        return param.is_explicit_name();
    }

    /** Get the current value of this parameter. Only meaningful when jitting.
        Asserts if type does not exactly match the Parameter's type. */
    template<typename T2 = not_void_T>
    NO_INLINE T2 get() const {
        return param.scalar<T2>();
    }

    /** Set the current value of this parameter. Only meaningful when jitting.
        Asserts if type does not exactly match the Parameter's type. */
    template<typename T2 = not_void_T>
    NO_INLINE void set(T2 val) {
        param.set_scalar<T2>(val);
    }

    /** Get the halide type of the Param */
    Type type() const {
        return param.type();
    }

    /** Get or set the possible range of this parameter. Use undefined
     * Exprs to mean unbounded. */
    // @{
    void set_range(Expr min, Expr max) {
        set_min_value(min);
        set_max_value(max);
    }

    void set_min_value(Expr min) {
        if (min.defined() && min.type() != param.type()) {
            min = Internal::Cast::make(param.type(), min);
        }
        param.set_min_value(min);
    }

    void set_max_value(Expr max) {
        if (max.defined() && max.type() != param.type()) {
            max = Internal::Cast::make(param.type(), max);
        }
        param.set_max_value(max);
    }

    Expr min_value() const {
        return param.min_value();
    }

    Expr max_value() const {
        return param.max_value();
    }
    // @}

    void set_estimate(const not_void_T &value) {
        param.set_estimate(Expr(value));
    }

    /** You can use this parameter as an expression in a halide
     * function definition */
    operator Expr() const {
        return Internal::Variable::make(param.type(), name(), param);
    }

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        return Expr(*this);
    }

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), Argument::InputScalar, type(), 0,
            param.scalar_expr(), param.min_value(), param.max_value());
    }
};

/** Returns an Expr corresponding to the user context passed to
 * the function (if any). It is rare that this function is necessary
 * (e.g. to pass the user context to an extern function written in C). */
inline Expr user_context_value() {
    return Internal::Variable::make(Handle(), "__user_context",
        Internal::Parameter(Handle(), false, 0, "__user_context", true));
}

}  // namespace Halide

#endif
