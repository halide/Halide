#ifndef HALIDE_SCHEDULE_PARAM_H
#define HALIDE_SCHEDULE_PARAM_H

#include <type_traits>

#include "IR.h"
#include "ObjectInstanceRegistry.h"

/** \file
 *
 * Classes for declaring scalar parameters to halide pipelines
 */

namespace Halide {

namespace Internal {

class ScheduleParamBase {
public:
    const std::string &name() const {
        return sp_name;
    }

    bool is_looplevel_param() const {
        return type == Handle();
    }

    const Type &scalar_type() const {
        internal_assert(!is_looplevel_param());
        return type;
    }

    operator Expr() const { 
        user_assert(!is_looplevel_param()) << "Only scalar ScheduleParams can be converted to Expr.";
        return scalar_expr;
    }

    operator LoopLevel() const { 
        user_assert(is_looplevel_param()) << "Only ScheduleParam<LoopLevel> can be converted to LoopLevel.";
        return loop_level;
    }

    // This is provided only for StubEmitter; other code should not need to use it.
    const Internal::Parameter &parameter() const { 
        return scalar;
    }

    // This is provided only for StubEmitter; other code should not need to use it.
    virtual void set_from_string(const std::string &new_value_string) = 0;

    // overload the set() function to call the right virtual method based on type.
    // This allows us to attempt to set a ScheduleParam via a
    // plain C++ type, even if we don't know the specific templated
    // subclass. Attempting to set the wrong type will assert.
    //
    // It's always a bit iffy to use macros for this, but IMHO it clarifies the situation here.
#define HALIDE_SCHEDULE_PARAM_TYPED_SETTER(TYPE) \
    virtual void set(const TYPE &new_value) = 0;

    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(bool)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int8_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int16_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int32_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int64_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint8_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint16_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint32_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint64_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(float)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(double)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(LoopLevel)

#undef HALIDE_SCHEDULE_PARAM_TYPED_SETTER

protected:
    const std::string sp_name;
    const Type type;

    Internal::Parameter scalar;
    Expr scalar_expr;

    // We must use a not-undefined LoopLevel, so that if we later mutate it,
    // references that use it will see the same content pointer.
    // We choose to use an "invalid" possibility here so that if we neglect
    // to set it, a scheduling error results; alternately, we could default
    // to LoopLevel::root() or LoopLevel::inlined().
    LoopLevel loop_level{"__invalid_func_name", "__invalid_var_name", false};

    NO_INLINE ScheduleParamBase(const Type &t)
        : sp_name(""), type(t),
          scalar(t, /*is_buffer*/ false, 0, "", /*is_explicit_name*/ false, 
            /*register_instance*/ false, /*is_removed_before_lowering*/ true),
          scalar_expr(Internal::Variable::make(t, scalar.name() + ".schedule_param_var", scalar)) {
        Internal::ObjectInstanceRegistry::register_instance(this, 0, Internal::ObjectInstanceRegistry::ScheduleParam, this, nullptr);
    }

    NO_INLINE explicit ScheduleParamBase(const Type &t, const std::string &name)
        : sp_name(name), type(t),
          scalar(t, /*is_buffer*/ false, 0, name + ".schedule_param_param", 
            /*is_explicit_name*/ true, /*register_instance*/ false, /*is_removed_before_lowering*/ true),
          scalar_expr(Internal::Variable::make(t, scalar.name() + ".schedule_param_var", scalar)) {
        Internal::ObjectInstanceRegistry::register_instance(this, 0, Internal::ObjectInstanceRegistry::ScheduleParam, this, nullptr);
    }

    NO_INLINE ~ScheduleParamBase() {
        Internal::ObjectInstanceRegistry::unregister_instance(this);
    }

    // No copy
    ScheduleParamBase(const ScheduleParamBase &) = delete;
    void operator=(const ScheduleParamBase &) = delete;
    // No move
    ScheduleParamBase(ScheduleParamBase&& that) = delete;
    void operator=(ScheduleParamBase&& that) = delete;
};

}  // namespace Internal

/** A ScheduleParam is a "Param" that can contain a scalar Expr or a LoopLevel;
 * unlike Param<>, its value cannot be set at runtime. All ScheduleParam values
 * are finalized just before lowering, and must translate into a constant scalar
 * value (or a well-defined LoopLevel) at that point. The value of 
 * should be bound to an actual value of type T using the set method
 * before you realize the function uses this. If you're statically
 * compiling, this param should appear in the argument list.
 */
template <typename T>
class ScheduleParam : public Internal::ScheduleParamBase {
    template <typename T2 = T,
              typename std::enable_if<std::is_arithmetic<T2>::value>::type * = nullptr>
    static Type get_param_type() { 
        return type_of<T>();
    }

    template <typename T2 = T,
              typename std::enable_if<!std::is_arithmetic<T2>::value>::type * = nullptr>
    static Type get_param_type() {
        return Handle();
    }

    template <typename T2, typename std::enable_if<std::is_arithmetic<T>::value && 
                                                   std::is_convertible<T2, T>::value>::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const T2 &value, const char *type) {
        // Arithmetic types must roundtrip losslessly.
        if (!std::is_same<T, T2>::value &&
            std::is_arithmetic<T>::value &&
            std::is_arithmetic<T2>::value) {
            const T t = value;
            const T2 t2 = t;
            if (t2 != value) {
                user_error << "The ScheduleParam " << name() << " cannot be set with a value of type " << type << ".\n";
            }
        }
        scalar.set_scalar<T>(value);
    }

    template <typename T2, typename std::enable_if<std::is_same<T2, LoopLevel>::value>::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const LoopLevel &value, const char *msg) {
        user_assert(is_looplevel_param()) << "Only ScheduleParam<LoopLevel> can be set withLoopLevel.";
        loop_level.copy_from(value);
    }

    template <typename T2, typename std::enable_if<!std::is_convertible<T2, T>::value>::type * = nullptr>
    HALIDE_ALWAYS_INLINE void typed_setter_impl(const T2 &value, const char *type) {
        user_error << "The ScheduleParam " << name() << " cannot be set with a value of type " << type << ".\n";
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_same<T2, LoopLevel>::value>::type * = nullptr>
    NO_INLINE void set_from_string_impl(const std::string &new_value_string) {
        if (new_value_string == "root") {
            set(LoopLevel::root());
        } else if (new_value_string == "inline") {
            set(LoopLevel::inlined());
        } else {
            user_error << "Unable to parse " << name() << ": " << new_value_string;
        }
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_same<T2, bool>::value>::type * = nullptr>
    NO_INLINE void set_from_string_impl(const std::string &new_value_string) {
        if (new_value_string == "true") {
            set(true);
        } else if (new_value_string == "false") {
            set(false);
        } else {
            user_error << "Unable to parse " << name() << ": " << new_value_string;
        }
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_arithmetic<T2>::value && !std::is_same<T2, bool>::value>::type * = nullptr>
    NO_INLINE void set_from_string_impl(const std::string &new_value_string) {
        std::istringstream iss(new_value_string);
        T t;
        iss >> t;
        user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse " << name() << ": " << new_value_string;
        set(t);
    }

    void set_from_string(const std::string &new_value_string) override {
        set_from_string_impl(new_value_string);
    }

public:
    using type = T;

    ScheduleParam() : Internal::ScheduleParamBase(get_param_type()) {}

    explicit ScheduleParam(const std::string &name) : Internal::ScheduleParamBase(get_param_type(), name) {}

    ScheduleParam(const std::string &name, const T &value) : Internal::ScheduleParamBase(get_param_type(), name) {
        set(value);
    }

    ScheduleParam(const std::string &name, const std::string &value) : Internal::ScheduleParamBase(get_param_type(), name) {
        set_from_string(value);
    }

#define HALIDE_SCHEDULE_PARAM_TYPED_SETTER(TYPE) \
    void set(const TYPE &new_value) override { typed_setter_impl<TYPE>(new_value, #TYPE); }

    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(bool)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int8_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int16_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int32_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(int64_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint8_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint16_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint32_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(uint64_t)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(float)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(double)
    HALIDE_SCHEDULE_PARAM_TYPED_SETTER(LoopLevel)

#undef HALIDE_SCHEDULE_PARAM_TYPED_SETTER

    // Note that we deliberately do not provide a way to retrieve the non-Expr value
    // of ScheduleParam: this is because the value is probably inaccurate at the point
    // you'd be tempted to examine it, since it won't be finalized until the start of lowering.
    // Here's the code that we'd use to do so, if we find we need to:

    // template <typename T2 = T, 
    //           typename std::enable_if<std::is_arithmetic<T2>::value>::type * = nullptr>
    // operator T() const {
    //     return scalar.get_scalar<T>();
    // }

    // template <typename T2 = T, 
    //           typename std::enable_if<std::is_same<T2, LoopLevel>::value>::type * = nullptr>
    // operator T() const {
    //     return loop_level;
    // }
};

}  // namespace Halide

#endif
