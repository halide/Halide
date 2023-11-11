#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

#include <type_traits>

#include "Argument.h"
#include "ExternFuncArgument.h"
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
    Parameter param;

    // This is a deliberately non-existent type that allows us to compile Param<>
    // but provide less-confusing error messages if you attempt to call get<> or set<>
    // without explicit types.
    struct DynamicParamType;

    /** T unless T is (const) void, in which case pointer-to-useless-type.` */
    using not_void_T = typename std::conditional<std::is_void<T>::value, DynamicParamType *, T>::type;

    void check_name() const {
        user_assert(param.name() != "__user_context")
            << "Param<void*>(\"__user_context\") "
            << "is no longer used to control whether Halide functions take explicit "
            << "user_context arguments. Use set_custom_user_context() when jitting, "
            << "or add Target::UserContext to the Target feature set when compiling ahead of time.";
    }

    // Allow all Param<> variants friend access to each other
    template<typename OTHER_TYPE>
    friend class Param;

public:
    /** True if the Halide type is not void (or const void). */
    static constexpr bool has_static_type = !std::is_void<T>::value;

    /** Get the Halide type of T. Callers should not use the result if
     * has_static_halide_type is false. */
    static Type static_type() {
        internal_assert(has_static_type);
        return type_of<T>();
    }

    /** Construct a scalar parameter of type T with a unique
     * auto-generated name */
    // @{
    Param()
        : param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide:.*:Param<.*>", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
    }
    explicit Param(Type t)
        : param(t, false, 0, Internal::make_entity_name(this, "Halide:.*:Param<.*>", 'p')) {
        static_assert(!has_static_type, "Cannot use this ctor with an explicit type.");
    }
    // @}

    /** Construct a scalar parameter of type T with the given name. */
    // @{
    explicit Param(const std::string &n)
        : param(type_of<T>(), false, 0, n) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
    }
    explicit Param(const char *n)
        : param(type_of<T>(), false, 0, n) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
    }
    Param(Type t, const std::string &n)
        : param(t, false, 0, n) {
        static_assert(!has_static_type, "Cannot use this ctor with an explicit type.");
        check_name();
    }
    // @}

    /** Construct a scalar parameter of type T an initial value of
     * 'val'. Only triggers for non-pointer types. */
    template<typename T2 = T, typename std::enable_if<!std::is_pointer<T2>::value>::type * = nullptr>
    explicit Param(not_void_T val)
        : param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide:.*:Param<.*>", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val'. */
    Param(const std::string &n, not_void_T val)
        : param(type_of<T>(), false, 0, n) {
        check_name();
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with an initial value of 'val'
     * and a given min and max. */
    Param(not_void_T val, const Expr &min, const Expr &max)
        : param(type_of<T>(), false, 0, Internal::make_entity_name(this, "Halide:.*:Param<.*>", 'p')) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        set_range(min, max);
        set<not_void_T>(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val' and a given min and max. */
    Param(const std::string &n, not_void_T val, const Expr &min, const Expr &max)
        : param(type_of<T>(), false, 0, n) {
        static_assert(has_static_type, "Cannot use this ctor without an explicit type.");
        check_name();
        set_range(min, max);
        set<not_void_T>(val);
    }

    /** Construct a Param<void> from any other Param. */
    template<typename OTHER_TYPE, typename T2 = T, typename std::enable_if<std::is_void<T2>::value>::type * = nullptr>
    Param(const Param<OTHER_TYPE> &other)
        : param(other.param) {
        // empty
    }

    /** Construct a Param<non-void> from a Param with matching type.
     * (Do the check at runtime so that we can assign from Param<void> if the types are compatible.) */
    template<typename OTHER_TYPE, typename T2 = T, typename std::enable_if<!std::is_void<T2>::value>::type * = nullptr>
    Param(const Param<OTHER_TYPE> &other)
        : param(other.param) {
        user_assert(other.type() == type_of<T>())
            << "Param<" << type_of<T>() << "> cannot be constructed from a Param with type " << other.type();
    }

    /** Copy a Param<void> from any other Param. */
    template<typename OTHER_TYPE, typename T2 = T, typename std::enable_if<std::is_void<T2>::value>::type * = nullptr>
    Param<T> &operator=(const Param<OTHER_TYPE> &other) {
        param = other.param;
        return *this;
    }

    /** Copy a Param<non-void> from a Param with matching type.
     * (Do the check at runtime so that we can assign from Param<void> if the types are compatible.) */
    template<typename OTHER_TYPE, typename T2 = T, typename std::enable_if<!std::is_void<T2>::value>::type * = nullptr>
    Param<T> &operator=(const Param<OTHER_TYPE> &other) {
        user_assert(other.type() == type_of<T>())
            << "Param<" << type_of<T>() << "> cannot be copied from a Param with type " << other.type();
        param = other.param;
        return *this;
    }

    /** Get the name of this parameter */
    const std::string &name() const {
        return param.name();
    }

    /** Get the current value of this parameter. Only meaningful when jitting.
        Asserts if type does not exactly match the Parameter's type. */
    template<typename T2 = not_void_T>
    HALIDE_NO_USER_CODE_INLINE T2 get() const {
        return param.scalar<T2>();
    }

    /** Set the current value of this parameter. Only meaningful when jitting.
        Asserts if type is not losslessly-convertible to Parameter's type. */
    template<typename SOME_TYPE>
    HALIDE_NO_USER_CODE_INLINE void set(const SOME_TYPE &val) {
        if constexpr (!std::is_void<T>::value) {
            user_assert(Internal::IsRoundtrippable<T>::value(val))
                << "The value " << val << " cannot be losslessly converted to type " << type();
            param.set_scalar<T>(val);
        } else {
            // clang-format off

            // Specialized version for when T = void (thus the type is only known at runtime,
            // not compiletime). Note that this actually works fine for all Params; we specialize
            // it just to reduce code size for the common case of T != void.

            #define HALIDE_HANDLE_TYPE_DISPATCH(CODE, BITS, TYPE)                                     \
                case halide_type_t(CODE, BITS).as_u32():                                              \
                    user_assert(Internal::IsRoundtrippable<TYPE>::value(val))                         \
                        << "The value " << val << " cannot be losslessly converted to type " << type; \
                    param.set_scalar<TYPE>(Internal::StaticCast<TYPE>::value(val));                   \
                    break;

            const Type type = param.type();
            switch (((halide_type_t)type).element_of().as_u32()) {
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 32, float)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 64, double)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 8, int8_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 16, int16_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 32, int32_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 64, int64_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 1, bool)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 8, uint8_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 16, uint16_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 32, uint32_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 64, uint64_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_handle, 64, uint64_t)  // Handle types are always set via set_scalar<uint64_t>, not set_scalar<void*>
            default:
                internal_error << "Unsupported type in Param::set<" << type << ">\n";
            }

            #undef HALIDE_HANDLE_TYPE_DISPATCH

            // clang-format on
        }
    }

    /** Get the halide type of the Param */
    Type type() const {
        return param.type();
    }

    /** Get or set the possible range of this parameter. Use undefined
     * Exprs to mean unbounded. */
    // @{
    void set_range(const Expr &min, const Expr &max) {
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

    template<typename SOME_TYPE>
    HALIDE_NO_USER_CODE_INLINE void set_estimate(const SOME_TYPE &val) {
        if constexpr (!std::is_void<T>::value) {
            user_assert(Internal::IsRoundtrippable<T>::value(val))
                << "The value " << val << " cannot be losslessly converted to type " << type();
            param.set_estimate(Expr(val));
        } else {
            // clang-format off

            // Specialized version for when T = void (thus the type is only known at runtime,
            // not compiletime). Note that this actually works fine for all Params; we specialize
            // it just to reduce code size for the common case of T != void.

            #define HALIDE_HANDLE_TYPE_DISPATCH(CODE, BITS, TYPE)                                     \
                case halide_type_t(CODE, BITS).as_u32():                                              \
                    user_assert(Internal::IsRoundtrippable<TYPE>::value(val))                         \
                        << "The value " << val << " cannot be losslessly converted to type " << type; \
                    param.set_estimate(Expr(Internal::StaticCast<TYPE>::value(val)));                   \
                    break;

            const Type type = param.type();
            switch (((halide_type_t)type).element_of().as_u32()) {
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 32, float)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_float, 64, double)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 8, int8_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 16, int16_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 32, int32_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_int, 64, int64_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 1, bool)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 8, uint8_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 16, uint16_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 32, uint32_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_uint, 64, uint64_t)
                HALIDE_HANDLE_TYPE_DISPATCH(halide_type_handle, 64, uint64_t)  // Handle types are always set via set_scalar<uint64_t>, not set_scalar<void*>
            default:
                internal_error << "Unsupported type in Param::set<" << type << ">\n";
            }

            #undef HALIDE_HANDLE_TYPE_DISPATCH

            // clang-format on
        }
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
                        param.get_argument_estimates());
    }

    const Parameter &parameter() const {
        return param;
    }

    Parameter &parameter() {
        return param;
    }
};

/** Returns an Expr corresponding to the user context passed to
 * the function (if any). It is rare that this function is necessary
 * (e.g. to pass the user context to an extern function written in C). */
inline Expr user_context_value() {
    return Internal::Variable::make(Handle(), "__user_context",
                                    Parameter(Handle(), false, 0, "__user_context"));
}

}  // namespace Halide

#endif
