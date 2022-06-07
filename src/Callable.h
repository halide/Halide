#ifndef HALIDE_CALLABLE_H
#define HALIDE_CALLABLE_H

/** \file
 *
 * Defines the front-end class representing a jitted, callable Halide pipeline.
 */

#include <array>
#include <map>

#include "Buffer.h"
#include "IntrusivePtr.h"
#include "JITModule.h"

namespace Halide {

struct Argument;
struct CallableContents;

namespace PythonBindings {
class PyCallable;
}

class Callable {
private:
    friend class Pipeline;
    friend struct CallableContents;
    friend class PythonBindings::PyCallable;

    Internal::IntrusivePtr<CallableContents> contents;

    enum class CallCheckType {
        // Unused = 1,
        Scalar = 2,
        Buffer = 3,
        UserContext = 4
    };

    // This value is constructed so we can do the necessary runtime check
    // with a single 32-bit compare. The value here is:
    //
    //     halide_type_t().with_lanes(CallCheckType).as_u32()
    //
    // This relies on the fact that the halide_type_t we use here
    // is never a vector, thus the 'lanes' value should always be 1.
    // Is this a little bit evil? Yeah, maybe. We could probably
    // use uint64_t and still be ok. Alternately, we could probably
    // pack all this info into a single byte if it helped...
    using CallCheckInfo = uint32_t;

    static constexpr CallCheckInfo make_scalar_cci(halide_type_t t) {
        return t.with_lanes((uint16_t)CallCheckType::Scalar).as_u32();
    }

    static constexpr CallCheckInfo make_buffer_cci() {
        return halide_type_t(halide_type_handle, 64, (uint16_t)CallCheckType::Buffer).as_u32();
    }

    static constexpr CallCheckInfo make_ucon_cci() {
        return halide_type_t(halide_type_handle, 64, (uint16_t)CallCheckType::UserContext).as_u32();
    }

    template<typename>
    struct is_halide_buffer : std::false_type {};

    template<typename T, int Dims>
    struct is_halide_buffer<::Halide::Buffer<T, Dims>> : std::true_type {};

    template<typename T, int Dims>
    struct is_halide_buffer<::Halide::Runtime::Buffer<T, Dims>> : std::true_type {};

    template<>
    struct is_halide_buffer<halide_buffer_t *> : std::true_type {};

    template<typename T>
    static constexpr CallCheckInfo build_cci() {
        using T0 = typename std::remove_const<typename std::remove_reference<T>::type>::type;
        if constexpr (std::is_same<T0, JITUserContext *>::value) {
            return make_ucon_cci();
        } else if constexpr (is_halide_buffer<T0>::value) {
            // Don't bother checking type-and-dimensions here (the callee will do that)
            // TODO: would it be worthwhile to check *static* type/dim of buffer to get compile-time failures?
            return make_buffer_cci();
        } else if constexpr (std::is_arithmetic<T0>::value || std::is_pointer<T0>::value) {
            return make_scalar_cci(halide_type_of<T0>());
        } else {
            // static_assert(false) will fail all the time, even inside constexpr,
            // but gating on sizeof(T) is a nice trick that ensures we will always
            // fail here (since no T is ever size 0).
            static_assert(!sizeof(T), "Illegal type passed to Callable.");
        }
    }

    template<int Size>
    struct ArgvStorage {
        const void *argv[Size];
        // We need a place to store the scalar inputs, since we need a pointer
        // to them and it's better to avoid relying on stack spill of arguments.
        // Note that this will usually have unused slots, but it's cheap and easy
        // compile-time allocation on the stack.
        uintptr_t argv_scalar_store[Size];

        template<typename T, int Dims>
        HALIDE_ALWAYS_INLINE void fill_slot(size_t idx, const ::Halide::Buffer<T, Dims> &value) {
            // Don't call ::Halide::Buffer::raw_buffer(): it includes "user_assert(defined())"
            // as part of the wrapper code, and we want this lean-and-mean
            argv[idx] = value.get()->raw_buffer();
        }

        template<typename T, int Dims>
        HALIDE_ALWAYS_INLINE void fill_slot(size_t idx, const ::Halide::Runtime::Buffer<T, Dims> &value) {
            argv[idx] = value.raw_buffer();
        }

        HALIDE_ALWAYS_INLINE
        void fill_slot(size_t idx, halide_buffer_t *value) {
            argv[idx] = value;
        }

        HALIDE_ALWAYS_INLINE
        void fill_slot(size_t idx, JITUserContext *value) {
            auto *dest = &argv_scalar_store[idx];
            *dest = (uintptr_t)value;
            argv[idx] = dest;
        }

        template<typename T>
        HALIDE_ALWAYS_INLINE void fill_slot(size_t idx, const T &value) {
            auto *dest = &argv_scalar_store[idx];
            *(T *)dest = value;
            argv[idx] = dest;
        }

        template<typename T>
        HALIDE_ALWAYS_INLINE void fill_slots(size_t idx, const T &value) {
            fill_slot(idx, value);
        }

        template<typename First, typename Second, typename... Rest>
        HALIDE_ALWAYS_INLINE void fill_slots(int idx, First &&first, Second &&second, Rest &&...rest) {
            fill_slots<First>(idx, std::forward<First>(first));
            fill_slots<Second, Rest...>(idx + 1, std::forward<Second>(second), std::forward<Rest>(rest)...);
        }
    };

    Callable();
    Callable(const std::string &name,
             const JITHandlers &jit_handlers,
             const std::map<std::string, JITExtern> &jit_externs,
             Internal::JITCache &&jit_cache,
             std::vector<CallCheckInfo> &&call_check_info);

    static std::vector<CallCheckInfo> build_expected_call_check_info(const std::vector<Argument> &args, const std::string &ucon_arg_name);

    // Note that the first entry in argv must always be a JITUserContext*.
    //
    // Currently private because it does limited type checking and is "dangerous",
    // but we could make public if there is a demand for it.
    int call_argv(size_t argc, const void **argv, const CallCheckInfo *actual_cci) const;

    template<typename... Args>
    int call(JITUserContext *context, Args &&...args) {
        // This is built at compile time!
        static constexpr auto actual_arg_types = std::array<CallCheckInfo, 1 + sizeof...(Args)>{
            build_cci<JITUserContext *>(),
            build_cci<Args>()...,
        };

        constexpr size_t count = sizeof...(args) + 1;
        ArgvStorage<count> argv;
        argv.fill_slots(0, context, std::forward<Args>(args)...);
        return call_argv(count, &argv.argv[0], actual_arg_types.data());
    }

    /** Return the expected Arguments for this Callable, in the order they must be specified, including all outputs.
     * Note that the first entry will *always* specify a JITUserContext. */
    const std::vector<Argument> &arguments() const;

public:
    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(JITUserContext *context, Args &&...args) {
        return call(context, std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(Args &&...args) {
        JITUserContext empty;
        return call(&empty, std::forward<Args>(args)...);
    }
};

}  // namespace Halide

#endif
