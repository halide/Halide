#ifndef HALIDE_CALLABLE_H
#define HALIDE_CALLABLE_H

/** \file
 *
 * Defines the front-end class representing a jitted, callable Halide pipeline.
 */

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
    // use uint64_t and still be ok.
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

    // We use the lower 31 bits as a bitmask; if the bit is set, a value of
    // the wrong type was specified for that arg. We use the sign bit for
    // "wrong number of arguments", in which case the rest of the bits are
    // ignored.
    using BadArgMask = int32_t;

    template<int Size>
    struct ArgvStorage {
        const void *argv[Size];
        // We need a place to store the scalar inputs, since we need a pointer
        // to them and it's better to avoid relying on stack spill of arguments.
        // Note that this will usually have unused slots, but it's cheap and easy
        // compile-time allocation on the stack.
        uintptr_t argv_scalar_store[Size];

        static constexpr BadArgMask make_bad_arg_mask(bool is_bad, size_t idx) {
            return ((BadArgMask)is_bad) << idx;
        }

        template<typename T, int Dims>
        HALIDE_ALWAYS_INLINE void fill_slot(const CallCheckInfo &cci, BadArgMask &bad_arg_mask, size_t idx, const ::Halide::Buffer<T, Dims> &value) {
            bad_arg_mask |= make_bad_arg_mask(cci != make_buffer_cci(), idx);
            // Don't bother checking type-and-dimensions here (the callee will do that)
            // TODO: would it be worthwhile to check *static* type/dim of buffer to get compile-time failures?
            //
            // Don't call ::Halide::Buffer::raw_buffer(): it includes "user_assert(defined())"
            // as part of the wrapper code, and we want this lean-and-mean
            argv[idx] = value.get()->raw_buffer();
        }

        template<typename T, int Dims>
        HALIDE_ALWAYS_INLINE void fill_slot(const CallCheckInfo &cci, BadArgMask &bad_arg_mask, size_t idx, const ::Halide::Runtime::Buffer<T, Dims> &value) {
            bad_arg_mask |= make_bad_arg_mask(cci != make_buffer_cci(), idx);
            // Don't bother checking type-and-dimensions here (the callee will do that)
            // TODO: would it be worthwhile to check *static* type/dim of buffer to get compile-time failures?
            argv[idx] = value.raw_buffer();
        }

        HALIDE_ALWAYS_INLINE
        void fill_slot(const CallCheckInfo &cci, BadArgMask &bad_arg_mask, size_t idx, halide_buffer_t *value) {
            bad_arg_mask |= make_bad_arg_mask(cci != make_buffer_cci(), idx);
            // Don't bother checking type-and-dimensions here (the callee will do that)
            argv[idx] = value;
        }

        HALIDE_ALWAYS_INLINE
        void fill_slot(const CallCheckInfo &cci, BadArgMask &bad_arg_mask, size_t idx, JITUserContext *value) {
            bad_arg_mask |= make_bad_arg_mask(cci != make_ucon_cci(), idx);
            auto *dest = &argv_scalar_store[idx];
            *dest = (uintptr_t)value;
            argv[idx] = dest;
        }

        template<typename T>
        HALIDE_ALWAYS_INLINE void fill_slot(const CallCheckInfo &cci, BadArgMask &bad_arg_mask, size_t idx, const T &value) {
            // TODO: this will fail to compile if halide_type_of<>() is not defined for the type;
            // normally that's the right behavior for halide_type_of, but in this case we'd like to
            // degrade and just have it report a bad type, but the use of static_assert() in the failure
            // case means SFINAE won't help us.
            bad_arg_mask |= make_bad_arg_mask(cci != make_scalar_cci(halide_type_of<T>()), idx);
            auto *dest = &argv_scalar_store[idx];
            *(T *)dest = value;
            argv[idx] = dest;
        }

        template<typename T>
        HALIDE_ALWAYS_INLINE void fill_slots(const CallCheckInfo *call_check_info, BadArgMask &bad_arg_mask, size_t idx, const T &value) {
            fill_slot(call_check_info[idx], bad_arg_mask, idx, value);
        }

        template<typename First, typename Second, typename... Rest>
        HALIDE_ALWAYS_INLINE void fill_slots(const CallCheckInfo *call_check_info, BadArgMask &bad_arg_mask, int idx, First &&first, Second &&second, Rest &&...rest) {
            fill_slots<First>(call_check_info, bad_arg_mask, idx, std::forward<First>(first));
            fill_slots<Second, Rest...>(call_check_info, bad_arg_mask, idx + 1, std::forward<Second>(second), std::forward<Rest>(rest)...);
        }
    };

    Callable();
    Callable(const std::string &name,
             const JITHandlers &jit_handlers,
             const std::map<std::string, JITExtern> &jit_externs,
             Internal::JITCache &&jit_cache,
             std::vector<CallCheckInfo> &&call_check_info);

    size_t arg_count() const;
    const CallCheckInfo *call_check_info() const;

    void do_fail_bad_call(BadArgMask bad_arg_mask, size_t hidden_args) const;

    template<int hidden_args>
    HALIDE_NEVER_INLINE void fail_bad_call(BadArgMask bad_arg_mask) const {
        do_fail_bad_call(bad_arg_mask, hidden_args);
    }

    // Note that the first entry in argv must always be a JITUserContext*.
    //
    // Currently private because it does no type checking and is "dangerous",
    // but we could make public if there is a demand for it.
    int call_argv(size_t argc, const void **argv) const;

    template<size_t hidden_args, typename... Args>
    int call(JITUserContext *context, Args &&...args) {
        constexpr size_t count = sizeof...(args) + 1;
        static_assert(count <= 31, "A maximum of 31 arguments are supported by Callable.");
        BadArgMask bad_arg_mask = -1;
        ArgvStorage<count> argv;
        if (count == arg_count()) {
            // Must not call fill_slots() if we have a bad number of args (otherwise
            // we could overread the call_check_info array).
            bad_arg_mask = 0;
            argv.fill_slots(call_check_info(), bad_arg_mask, 0, context, std::forward<Args>(args)...);
        }
        if (bad_arg_mask) {
            fail_bad_call<hidden_args>(bad_arg_mask);
        }
        return call_argv(count, &argv.argv[0]);
    }

public:
    /** Return the expected Arguments for this Callable, in the order they must be specified, including all outputs.
     * Note that the first entry will *always* specify a JITUserContext. */
    const std::vector<Argument> &arguments() const;

    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(JITUserContext *context, Args &&...args) {
        return call<0, Args...>(context, std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_MUST_USE_RESULT int
    operator()(Args &&...args) {
        JITUserContext empty_jit_user_context;
        return call<1, Args...>(&empty_jit_user_context, std::forward<Args>(args)...);
    }
};

}  // namespace Halide

#endif
