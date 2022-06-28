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
    : contents(new CallableContents) {
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

void Callable::do_check_fail(int bad_idx, size_t argc, const char *verb) const {
    const size_t required_arg_count = contents->jit_cache.arguments.size();

    // TODO: this assumes that the caller uses the no-explicit-JITUserContext call;
    // the errors will be misleading otherwise.
    constexpr int hidden_args = 1;

    if (bad_idx < 0) {
        user_error << "Error " << verb << " '" << contents->name << "':\n"
                   << "Expected exactly " << (required_arg_count - hidden_args) << " arguments, "
                   << "but saw " << (argc - hidden_args) << ".";
    } else {
        const Argument &a = contents->jit_cache.arguments.at(bad_idx);
        const char *kind = a.is_scalar() ? "scalar" : "buffer";
        // Note that we don't report the "actual type" here, just the expected type...
        // saving the actual type leads to more code bloat than we can justify
        // for this. (Consider adding as a debug-only enhancement?)
        user_error << "Error " << verb << " '" << contents->name << "':\n"
                   << "Argument " << (bad_idx - hidden_args + 1)
                   << " of " << (required_arg_count - hidden_args) << " ('" << a.name << "') was expected to be a "
                   << kind << " of type '" << a.type << "' and dimension " << (int)a.dimensions << ".\n";
    }
}

void Callable::check_qcci(size_t argc, const QuickCallCheckInfo *actual_qcci) const {
    const size_t required_arg_count = contents->quick_call_check_info.size();
    if (argc == required_arg_count) {
        const QuickCallCheckInfo *expected_qcci = contents->quick_call_check_info.data();
        for (size_t i = 0; i < argc; i++) {
            if (actual_qcci[i] != expected_qcci[i]) {
                do_check_fail(i, argc, "calling");
            }
        }
    } else {
        do_check_fail(-1, argc, "calling");
    }
}

void Callable::check_fcci(size_t argc, const FullCallCheckInfo *actual_fcci) const {
    // Lazily create full_call_check_info upon the first call to make_std_function().
    if (contents->full_call_check_info.empty()) {
        contents->full_call_check_info.reserve(contents->jit_cache.arguments.size());
        for (const Argument &a : contents->jit_cache.arguments) {
            const auto fcci = a.is_scalar() ? Callable::make_scalar_fcci(a.type) : Callable::make_buffer_fcci(a.type, a.dimensions);
            contents->full_call_check_info.push_back(fcci);
        }
    }

    const size_t required_arg_count = contents->full_call_check_info.size();
    if (argc == required_arg_count) {
        const FullCallCheckInfo *expected_fcci = contents->full_call_check_info.data();
        for (size_t i = 0; i < argc; i++) {
            if (!Callable::is_compatible_fcci(actual_fcci[i], expected_fcci[i])) {
                do_check_fail(i, argc, "defining");
            }
        }
    } else {
        do_check_fail(-1, argc, "defining");
    }
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

    // Don't call jit_call_context.finalize(): if no error handler was installed,
    // it will call halide_error_handler for us, which is fine for realize()
    // and friends because there is no other way to report an error. For this code
    // path, though, we just return the error code (if any); if a custom error
    // handler was installed, it presumably will get the first shot at handling it,
    // and if not, the caller must handle it by checking that the result code
    // is zero, in the same way that callers to an AOT-compiled Halide function must.

    return exit_status;
}

int Callable::call_argv_checked(size_t argc, const void *const *argv, const QuickCallCheckInfo *actual_qcci) const {
    // It's *essential* we call this for safety.
    check_qcci(argc, actual_qcci);
    return call_argv_fast(argc, argv);
}

}  // namespace Halide
