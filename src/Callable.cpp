#include <map>

#include "Argument.h"
#include "Callable.h"
#include "JITModule.h"
#include "Pipeline.h"

using namespace Halide::Internal;

namespace Halide {

struct CallableContents {
    mutable RefCount ref_count;

    // Name of the jitted function, here solely for error reporting
    std::string name;

    // The cached code
    JITCache jit_cache;

    // Save the jit_handlers and jit_externs as they were at the time this
    // Callable was created, in case the Pipeline's version is mutated in
    // between creation and call -- we want the Callable to remain immutable
    // after creation, regardless of what you do to the Func.
    JITHandlers saved_jit_handlers;
    std::map<std::string, JITExtern> saved_jit_externs;

    // Encoded values for efficient runtime type checking;
    // identical to jit_cache.arguments in length.
    std::vector<Callable::QuickCallCheckInfo> quick_call_check_info;

    // Encoded values for complete runtime type checking, used
    // only for make_std_function. Lazily created.
    std::vector<Callable::FullCallCheckInfo> full_call_check_info;
};

namespace Internal {
template<>
RefCount &ref_count<CallableContents>(const CallableContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<CallableContents>(const CallableContents *p) {
    delete p;
}
}  // namespace Internal

Callable::Callable()
    : contents(nullptr) {
}

bool Callable::defined() const {
    return contents.defined();
}

Callable::Callable(const std::string &name,
                   const JITHandlers &jit_handlers,
                   const std::map<std::string, JITExtern> &jit_externs,
                   JITCache &&jit_cache)
    : contents(new CallableContents) {
    contents->name = name;
    contents->jit_cache = std::move(jit_cache);
    contents->saved_jit_handlers = jit_handlers;
    contents->saved_jit_externs = jit_externs;

    contents->quick_call_check_info.reserve(contents->jit_cache.arguments.size());
    for (const Argument &a : contents->jit_cache.arguments) {
        const auto qcci = (a.name == "__user_context") ?
                              Callable::make_ucon_qcci() :
                              (a.is_scalar() ? Callable::make_scalar_qcci(a.type) : Callable::make_buffer_qcci());
        contents->quick_call_check_info.push_back(qcci);
    }

    // Don't create full_call_check_info yet.
}

const std::vector<Argument> &Callable::arguments() const {
    return contents->jit_cache.arguments;
}

Callable::FailureFn Callable::do_check_fail(int bad_idx, size_t argc, const char *verb) const {
    const size_t required_arg_count = contents->jit_cache.arguments.size();

    // TODO: this assumes that the caller uses the no-explicit-JITUserContext call;
    // the errors will be misleading otherwise.
    constexpr int hidden_args = 1;

    std::ostringstream o;
    if (bad_idx < 0) {
        o << "Error " << verb << " '" << contents->name << "': "
          << "Expected exactly " << (required_arg_count - hidden_args) << " arguments, "
          << "but saw " << (argc - hidden_args) << ".";
    } else {
        // Capture *this to ensure that the CallableContents stay valid as long as the std::function does
        const Argument &a = contents->jit_cache.arguments.at(bad_idx);
        const char *kind = a.is_scalar() ? "scalar" : "buffer";
        // Note that we don't report the "actual type" here, just the expected type...
        // saving the actual type leads to more code bloat than we can justify
        // for this. (Consider adding as a debug-only enhancement?)
        o << "Error " << verb << " '" << contents->name << "': "
          << "Argument " << (bad_idx - hidden_args + 1)
          << " of " << (required_arg_count - hidden_args) << " ('" << a.name << "') was expected to be a "
          << kind << " of type '" << a.type << "' and dimension " << (int)a.dimensions << ".\n";
    }
    std::string msg = o.str();

    return [*this, msg](JITUserContext *context) -> int {
        constexpr int exit_status = halide_error_code_internal_error;  // TODO: perhaps return a more useful error code?;

        if (context && context->handlers.custom_error) {
            context->handlers.custom_error(context, msg.c_str());
        } else if (contents->saved_jit_handlers.custom_error) {
            contents->saved_jit_handlers.custom_error(context, msg.c_str());
        } else {
            if (msg.empty()) {
                halide_runtime_error << "The pipeline returned exit status " << exit_status << " but halide_error was never called.\n";
            } else {
                halide_runtime_error << msg;
            }
        }
        return exit_status;
    };
}

Callable::FailureFn Callable::check_qcci(size_t argc, const QuickCallCheckInfo *actual_qcci) const {
    const size_t required_arg_count = contents->quick_call_check_info.size();
    if (argc == required_arg_count) {
        const QuickCallCheckInfo *expected_qcci = contents->quick_call_check_info.data();
        for (size_t i = 0; i < argc; i++) {
            if (actual_qcci[i] != expected_qcci[i]) {
                return do_check_fail(i, argc, "calling");
            }
        }
    } else {
        return do_check_fail(-1, argc, "calling");
    }

    return nullptr;
}

Callable::FailureFn Callable::check_fcci(size_t argc, const FullCallCheckInfo *actual_fcci) const {
    user_assert(defined()) << "Cannot call() a default-constructed Callable.";

    // Lazily create full_call_check_info upon the first call to make_std_function().
    if (contents->full_call_check_info.empty()) {
        contents->full_call_check_info.reserve(contents->jit_cache.arguments.size());
        for (const Argument &a : contents->jit_cache.arguments) {
            const auto fcci = a.is_scalar() ? Callable::make_scalar_fcci(a.type) : Callable::make_buffer_fcci(a.type, a.dimensions);
            contents->full_call_check_info.push_back(fcci);
        }
    }

    FailureFn failure_fn = nullptr;
    const size_t required_arg_count = contents->full_call_check_info.size();
    if (argc == required_arg_count) {
        const FullCallCheckInfo *expected_fcci = contents->full_call_check_info.data();
        for (size_t i = 0; i < argc; i++) {
            if (!Callable::is_compatible_fcci(actual_fcci[i], expected_fcci[i])) {
                failure_fn = do_check_fail(i, argc, "defining");
                break;
            }
        }
    } else {
        failure_fn = do_check_fail(-1, argc, "defining");
    }

    if (failure_fn) {
        // Go ahead and call it now, since we know that every possible call will fail.
        // (We'll also return it as a sentinel so the caller knows that this is the case;
        // if the Callable has hooked the error handler to do nothing, we don't want want
        // to try to continue executing this path in the caller.)
        JITUserContext empty;
        (void)failure_fn(&empty);
    }

    return failure_fn;
}

// Entry point used from the std::function<> variant; we can skip the check_qcci() stuff
// since we verified the signature when we created the std::function, so incorrect types or counts
// should be impossible.
/*static*/ int Callable::call_argv_fast(size_t argc, const void *const *argv) const {
    // Callable should enforce these, so we can use assert() instead of internal_assert() --
    // this is effectively just documentation that these invariants are expected to have
    // been enforced prior to this call.
    assert(contents->jit_cache.jit_target.has_feature(Target::UserContext));
    assert(contents->jit_cache.arguments[0].name == "__user_context");

    JITUserContext *context = *(JITUserContext **)const_cast<void *>(argv[0]);
    assert(context != nullptr);

    JITFuncCallContext jit_call_context(context, contents->saved_jit_handlers);

    int exit_status = contents->jit_cache.call_jit_code(contents->jit_cache.jit_target, argv);

    // If we're profiling, report runtimes and reset profiler stats.
    contents->jit_cache.finish_profiling(context);

    jit_call_context.finalize(exit_status);

    return exit_status;
}

int Callable::call_argv_checked(size_t argc, const void *const *argv, const QuickCallCheckInfo *actual_qcci) const {
    user_assert(defined()) << "Cannot call() a default-constructed Callable.";

    // It's *essential* we call this for safety.
    const auto failure_fn = check_qcci(argc, actual_qcci);
    if (failure_fn) {
        JITUserContext *context = *(JITUserContext **)const_cast<void *>(argv[0]);
        return failure_fn(context);
    }
    return call_argv_fast(argc, argv);
}

}  // namespace Halide
