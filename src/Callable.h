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

namespace Internal {

template<typename>
struct is_halide_buffer : std::false_type {};

template<typename T, int Dims>
struct is_halide_buffer<::Halide::Buffer<T, Dims>> : std::true_type {};

template<typename T, int Dims>
struct is_halide_buffer<::Halide::Runtime::Buffer<T, Dims>> : std::true_type {};

template<>
struct is_halide_buffer<halide_buffer_t *> : std::true_type {};

}  // namespace Internal

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

    template<typename T>
    static constexpr CallCheckInfo build_cci() {
        using T0 = typename std::remove_const<typename std::remove_reference<T>::type>::type;
        if constexpr (std::is_same<T0, JITUserContext *>::value) {
            return make_ucon_cci();
        } else if constexpr (Internal::is_halide_buffer<T0>::value) {
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

        template<typename... Args>
        explicit ArgvStorage(Args &&...args) {
            fill_slots(0, std::forward<Args>(args)...);
        }

    private:
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
    int call_argv_checked(size_t argc, const void *const *argv, const CallCheckInfo *actual_cci) const;
    int call_argv_fast(size_t argc, const void *const *argv) const;

    void check_arg_count_and_types(size_t argc, const CallCheckInfo *actual_cci, const char *verb) const;

    template<typename... Args>
    int call(JITUserContext *context, Args &&...args) const {
        // This is built at compile time!
        static constexpr auto actual_arg_types = std::array<CallCheckInfo, 1 + sizeof...(Args)>{
            build_cci<JITUserContext *>(),
            build_cci<Args>()...,
        };

        constexpr size_t count = sizeof...(args) + 1;
        ArgvStorage<count> argv(context, std::forward<Args>(args)...);
        return call_argv_checked(count, &argv.argv[0], actual_arg_types.data());
    }

    /** Return the expected Arguments for this Callable, in the order they must be specified, including all outputs.
     * Note that the first entry will *always* specify a JITUserContext. */
    const std::vector<Argument> &arguments() const;

public:
    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(JITUserContext *context, Args &&...args) const {
        return call(context, std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(Args &&...args) const {
        JITUserContext empty;
        return call(&empty, std::forward<Args>(args)...);
    }

    /** This allows us to construct a std::function<> that wraps the Callable.
     * This is nice in that it is, well, just a std::function, but also in that
     * since the argument-count-and-type checking are baked into the language,
     * we can do the relevant checking only once -- when we first create the std::function --
     * and skip it on all actual *calls* to the function, making it slightly more efficient.
     * It's also more type-forgiving, in that the usual C++ numeric coercion rules apply here.
     *
     * The downside is that there isn't (currently) any way to automatically infer
     * the static types reliably, since we may be using (e.g.) a Param<void>, where the
     * type in question isn't available to the C++ compiler. This means that the coder
     * must supply the correct type signature when calling this function -- but the good news
     * is that if you get it wrong, this function will fail when you call it. (In other words:
     * it can't choose the right thing for you, but it can tell you when you do the wrong thing.)
     *
     * TODO: it's possible that we could infer the correct signatures in some cases,
     * and only fail for the ambiguous cases, but that would require a lot more template-fu
     * here and elsewhere. I think this is good enough for now.
     *
     * TODO: for plain-old-Callable, we don't bother checking the static type-and-dims of Buffer<>
     * values passed in (we defer to the runtime to handle that), but for this we probably want to
     * do that check -- currently we allow any sort of Buffer<> as an argument for a buffer slot,
     * so you can specify something that is guaranteed to fail at runtime. Ouch.
     */
    template<typename First, typename... Rest>
    std::function<int(First, Rest...)>
    make_std_function() const {
        if constexpr (std::is_same_v<First, JITUserContext *>) {
            constexpr auto actual_arg_types = std::array<CallCheckInfo, 1 + sizeof...(Rest)>{
                build_cci<First>(),
                build_cci<Rest>()...,
            };
            check_arg_count_and_types(actual_arg_types.size(), actual_arg_types.data(), "defining");

            // Capture *this to ensure that the CallableContents stay valid as long as the std::function does
            return [*this](auto &&first, auto &&...rest) -> int {
                constexpr size_t count = 1 + sizeof...(rest);
                ArgvStorage<count> argv(std::forward<First>(first), std::forward<Rest>(rest)...);
                return call_argv_fast(count, &argv.argv[0]);
            };
        } else {
            // Explicitly prepend JITUserContext* as first actual-arg-type.
            constexpr auto actual_arg_types = std::array<CallCheckInfo, 1 + 1 + sizeof...(Rest)>{
                build_cci<JITUserContext *>(),
                build_cci<First>(),
                build_cci<Rest>()...,
            };
            check_arg_count_and_types(actual_arg_types.size(), actual_arg_types.data(), "defining");

            // Capture *this to ensure that the CallableContents stay valid as long as the std::function does
            return [*this](auto &&first, auto &&...rest) -> int {
                // Explicitly prepend an (empty) JITUserContext to the args.
                JITUserContext empty;
                constexpr size_t count = 1 + 1 + sizeof...(rest) + 1;
                ArgvStorage<count> argv(&empty, std::forward<First>(first), std::forward<Rest>(rest)...);
                return call_argv_fast(count, &argv.argv[0]);
            };
        }
    }
};

}  // namespace Halide

#endif
