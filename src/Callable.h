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
struct IsHalideBuffer : std::false_type {};

template<typename T, int Dims>
struct IsHalideBuffer<::Halide::Buffer<T, Dims>> : std::true_type {};

template<typename T, int Dims>
struct IsHalideBuffer<::Halide::Runtime::Buffer<T, Dims>> : std::true_type {};

template<>
struct IsHalideBuffer<halide_buffer_t *> : std::true_type {};

template<>
struct IsHalideBuffer<const halide_buffer_t *> : std::true_type {};

template<typename>
struct HalideBufferStaticTypeAndDims {
    static constexpr halide_type_t type() {
        return halide_type_t();
    }
    static constexpr int dims() {
        return -1;
    }
};

template<typename T, int Dims>
struct HalideBufferStaticTypeAndDims<::Halide::Buffer<T, Dims>> {
    static constexpr halide_type_t type() {
        if constexpr (std::is_void_v<T>) {
            return halide_type_t();
        } else {
            return halide_type_of<T>();
        }
    }
    static constexpr int dims() {
        return Dims;
    }
};

template<typename T, int Dims>
struct HalideBufferStaticTypeAndDims<::Halide::Runtime::Buffer<T, Dims>> {
    static constexpr halide_type_t type() {
        if constexpr (std::is_void_v<T>) {
            return halide_type_t();
        } else {
            return halide_type_of<T>();
        }
    }
    static constexpr int dims() {
        return Dims;
    }
};

}  // namespace Internal

class Callable {
private:
    friend class Pipeline;
    friend struct CallableContents;
    friend class PythonBindings::PyCallable;

    Internal::IntrusivePtr<CallableContents> contents;

    // ---------------------------------

    // This value is constructed so we can do the necessary runtime check
    // with a single 16-bit compare. It's designed to to the minimal checking
    // necessary to ensure that the arguments are well-formed, but not necessarily
    // "correct"; in particular, it deliberately skips checking type-and-dim
    // of Buffer arguments, since the generated code has assertions to check
    // for that anyway.
    using QuickCallCheckInfo = uint16_t;

    static constexpr QuickCallCheckInfo _make_qcci(uint8_t code, uint8_t bits) {
        return (((uint16_t)code) << 8) | (uint16_t)bits;
    }

    static constexpr QuickCallCheckInfo make_scalar_qcci(halide_type_t t) {
        return _make_qcci(t.code, t.bits);
    }

    static constexpr QuickCallCheckInfo make_buffer_qcci() {
        constexpr uint8_t fake_bits_buffer_cci = 3;
        return _make_qcci(halide_type_handle, fake_bits_buffer_cci);
    }

    static constexpr QuickCallCheckInfo make_ucon_qcci() {
        constexpr uint8_t fake_bits_ucon_cci = 5;
        return _make_qcci(halide_type_handle, fake_bits_ucon_cci);
    }

    template<typename T>
    static constexpr QuickCallCheckInfo make_qcci() {
        using T0 = typename std::remove_const<typename std::remove_reference<T>::type>::type;
        if constexpr (std::is_same<T0, JITUserContext *>::value) {
            return make_ucon_qcci();
        } else if constexpr (Internal::IsHalideBuffer<T0>::value) {
            // Don't bother checking type-and-dimensions here (the callee will do that)
            return make_buffer_qcci();
        } else if constexpr (std::is_arithmetic<T0>::value || std::is_pointer<T0>::value) {
            return make_scalar_qcci(halide_type_of<T0>());
        } else {
            // static_assert(false) will fail all the time, even inside constexpr,
            // but gating on sizeof(T) is a nice trick that ensures we will always
            // fail here (since no T is ever size 0).
            static_assert(!sizeof(T), "Illegal type passed to Callable.");
        }
    }

    template<typename... Args>
    static constexpr std::array<QuickCallCheckInfo, sizeof...(Args)> make_qcci_array() {
        return std::array<QuickCallCheckInfo, sizeof...(Args)>{make_qcci<Args>()...};
    }

    // ---------------------------------

    // This value is constructed so we can do a complete type-and-dim check
    // of Buffers, and is used for the make_std_function() method, to ensure
    // that if we specify static type-and-dims for Buffers, the ones we specify
    // actually match the underlying code. We take horrible liberties with halide_type_t
    // to make this happen -- specifically, encoding dimensionality and buffer-vs-scalar
    // into the 'lanes' field -- but that's ok since this never escapes into other usage.
    using FullCallCheckInfo = halide_type_t;

    static constexpr FullCallCheckInfo _make_fcci(halide_type_t type, int dims, bool is_buffer) {
        return type.with_lanes(((uint16_t)dims << 1) | (uint16_t)(is_buffer ? 1 : 0));
    }

    static constexpr FullCallCheckInfo make_scalar_fcci(halide_type_t t) {
        return _make_fcci(t, 0, false);
    }

    static constexpr FullCallCheckInfo make_buffer_fcci(halide_type_t t, int dims) {
        return _make_fcci(t, dims, true);
    }

    static bool is_compatible_fcci(FullCallCheckInfo actual, FullCallCheckInfo expected) {
        if (actual == expected) {
            return true;  // my, that was easy
        }

        // Might still be compatible
        const bool a_is_buffer = (actual.lanes & 1) != 0;
        const int a_dims = (((int16_t)actual.lanes) >> 1);
        const halide_type_t a_type = actual.with_lanes(0);

        const bool e_is_buffer = (expected.lanes & 1) != 0;
        const int e_dims = (((int16_t)expected.lanes) >> 1);
        const halide_type_t e_type = expected.with_lanes(0);

        const bool types_match = (a_type == halide_type_t()) ||
                                 (e_type == halide_type_t()) ||
                                 (a_type == e_type);

        const bool dims_match = a_dims < 0 ||
                                e_dims < 0 ||
                                a_dims == e_dims;

        return a_is_buffer == e_is_buffer && types_match && dims_match;
    }

    template<typename T>
    static constexpr FullCallCheckInfo make_fcci() {
        using T0 = typename std::remove_const<typename std::remove_reference<T>::type>::type;
        if constexpr (Internal::IsHalideBuffer<T0>::value) {
            using TypeAndDims = Internal::HalideBufferStaticTypeAndDims<T0>;
            return make_buffer_fcci(TypeAndDims::type(), TypeAndDims::dims());
        } else if constexpr (std::is_arithmetic<T0>::value || std::is_pointer<T0>::value) {
            return make_scalar_fcci(halide_type_of<T0>());
        } else {
            // static_assert(false) will fail all the time, even inside constexpr,
            // but gating on sizeof(T) is a nice trick that ensures we will always
            // fail here (since no T is ever size 0).
            static_assert(!sizeof(T), "Illegal type passed to Callable.");
        }
    }

    template<typename... Args>
    static constexpr std::array<FullCallCheckInfo, sizeof...(Args)> make_fcci_array() {
        return std::array<FullCallCheckInfo, sizeof...(Args)>{make_fcci<Args>()...};
    }

    // ---------------------------------

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
            // as part of the wrapper code, and we want this lean-and-mean. Instead, stick in a null
            // value for undefined buffers, and let the Halide pipeline fail with the usual null-ptr
            // check. (Note that H::R::B::get() *never* returns null; you must check defined() first.)
            argv[idx] = value.defined() ? value.get()->raw_buffer() : nullptr;
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
        void fill_slot(size_t idx, const halide_buffer_t *value) {
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

    Callable(const std::string &name,
             const JITHandlers &jit_handlers,
             const std::map<std::string, JITExtern> &jit_externs,
             Internal::JITCache &&jit_cache);

    // Note that the first entry in argv must always be a JITUserContext*.
    int call_argv_checked(size_t argc, const void *const *argv, const QuickCallCheckInfo *actual_cci) const;

    using FailureFn = std::function<int(JITUserContext *)>;

    FailureFn do_check_fail(int bad_idx, size_t argc, const char *verb) const;
    FailureFn check_qcci(size_t argc, const QuickCallCheckInfo *actual_cci) const;
    FailureFn check_fcci(size_t argc, const FullCallCheckInfo *actual_cci) const;

    template<typename... Args>
    int call(JITUserContext *context, Args &&...args) const {
        // This is built at compile time!
        static constexpr auto actual_arg_types = make_qcci_array<JITUserContext *, Args...>();

        constexpr size_t count = sizeof...(args) + 1;
        ArgvStorage<count> argv(context, std::forward<Args>(args)...);
        return call_argv_checked(count, &argv.argv[0], actual_arg_types.data());
    }

    /** Return the expected Arguments for this Callable, in the order they must be specified, including all outputs.
     * Note that the first entry will *always* specify a JITUserContext. */
    const std::vector<Argument> &arguments() const;

public:
    /** Construct a default Callable. This is not usable (trying to call it will fail).
     * The defined() method will return false. */
    Callable();

    /** Return true if the Callable is well-defined and usable, false if it is a default-constructed empty Callable. */
    bool defined() const;

    template<typename... Args>
    HALIDE_FUNCTION_ATTRS int
    operator()(JITUserContext *context, Args &&...args) const {
        return call(context, std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_FUNCTION_ATTRS int
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
     * TODO: is it possible to annotate the result of a std::function<> with HALIDE_FUNCTION_ATTRS?
     */
    template<typename First, typename... Rest>
    std::function<int(First, Rest...)>
    make_std_function() const {
        if constexpr (std::is_same_v<First, JITUserContext *>) {
            constexpr auto actual_arg_types = make_fcci_array<First, Rest...>();
            const auto failure_fn = check_fcci(actual_arg_types.size(), actual_arg_types.data());
            if (failure_fn) {
                // Return a wrapper for the failure_fn in case the error handler is a no-op,
                // so that subsequent calls won't attempt to use possibly-wrong argv packing.
                return [*this, failure_fn](auto &&first, auto &&...rest) -> int {
                    return failure_fn(std::forward<First>(first));
                };
            }

            // Capture *this to ensure that the CallableContents stay valid as long as the std::function does
            return [*this](auto &&first, auto &&...rest) -> int {
                constexpr size_t count = 1 + sizeof...(rest);
                ArgvStorage<count> argv(std::forward<First>(first), std::forward<Rest>(rest)...);
                return call_argv_fast(count, &argv.argv[0]);
            };
        } else {
            // Explicitly prepend JITUserContext* as first actual-arg-type.
            constexpr auto actual_arg_types = make_fcci_array<JITUserContext *, First, Rest...>();
            const auto failure_fn = check_fcci(actual_arg_types.size(), actual_arg_types.data());
            if (failure_fn) {
                // Return a wrapper for the failure_fn in case the error handler is a no-op,
                // so that subsequent calls won't attempt to use possibly-wrong argv packing.
                return [*this, failure_fn](auto &&first, auto &&...rest) -> int {
                    JITUserContext empty;
                    return failure_fn(&empty);
                };
            }

            // Capture *this to ensure that the CallableContents stay valid as long as the std::function does
            return [*this](auto &&first, auto &&...rest) -> int {
                // Explicitly prepend an (empty) JITUserContext to the args.
                JITUserContext empty;
                constexpr size_t count = 1 + 1 + sizeof...(rest);
                ArgvStorage<count> argv(&empty, std::forward<First>(first), std::forward<Rest>(rest)...);
                return call_argv_fast(count, &argv.argv[0]);
            };
        }
    }

    /** Unsafe low-overhead way of invoking the Callable.
     *
     * This function relies on the same calling convention as the argv-based
     * functions generated for ahead-of-time compiled Halide pilelines.
     *
     * Very rough specifications of the calling convention (but check the source
     * code to be sure):
     *
     *   * Arguments are passed in the same order as they appear in the C
     *     function argument list.
     *   * The first entry in argv must always be a JITUserContext*. Please,
     *     note that this means that argv[0] actually contains JITUserContext**.
     *   * All scalar arguments are passed by pointer, not by value, regardless of size.
     *   * All buffer arguments (input or output) are passed as halide_buffer_t*.
     *
     */
    int call_argv_fast(size_t argc, const void *const *argv) const;
};

}  // namespace Halide

#endif
