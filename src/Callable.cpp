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
    std::vector<Callable::QuickCallCheckInfo> call_check_info;
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

    contents->call_check_info.reserve(contents->jit_cache.arguments.size());
    for (const Argument &a : contents->jit_cache.arguments) {
        const auto cci = (a.name == "__user_context") ?
                             Callable::make_ucon_cci() :
                             (a.is_scalar() ? Callable::make_scalar_cci(a.type) : Callable::make_buffer_cci());
        contents->call_check_info.push_back(cci);
    }
}

const std::vector<Argument> &Callable::arguments() const {
    return contents->jit_cache.arguments;
}

void Callable::check_arg_count_and_types(size_t argc, const QuickCallCheckInfo *actual_cci, const char *verb) const {
    const size_t required_arg_count = contents->jit_cache.arguments.size();

    // TODO: this assumes that the caller uses the no-explicit-JITUserContext call;
    // the errors will be misleading otherwise.
    constexpr int hidden_args = 1;

    user_assert(argc == required_arg_count) << "Error " << verb << " '" << contents->name << "':\n"
                                            << "Expected exactly " << (required_arg_count - hidden_args) << " arguments, "
                                            << "but saw " << (argc - hidden_args) << ".";

    const QuickCallCheckInfo *expected_cci = contents->call_check_info.data();
    for (size_t i = 0; i < argc; i++) {
        if (actual_cci[i] != expected_cci[i]) {
            const Argument &a = contents->jit_cache.arguments.at(i);
            const char *kind = a.is_scalar() ? "scalar" : "buffer";
            // Note that we don't report the "actual type" here, just the expected type...
            // saving the actual type leads to more code bloat than we can justify
            // for this. (Consider adding as a debug-only enhancement?)
            user_error << "Error " << verb << " '" << contents->name << "':\n"
                       << "Argument " << (i - hidden_args + 1)
                       << " of " << (required_arg_count - hidden_args) << " ('" << a.name << "') was expected to be a "
                       << kind << " of type '" << a.type << "'.\n";
        }
    }
}

// Entry point used from the std::function<> variant; we can skip the check_arg_count_and_types() stuff
// since we verified the signature when we created the std::function, so incorrect types or counts
// should be impossible.
/*static*/ int Callable::call_argv_fast(size_t argc, const void *const *argv) const {
    // Callable should enforce these, so we can use assert() instead of internal_assert()
    assert(contents->jit_cache.jit_target.has_feature(Target::UserContext));
    assert(contents->jit_cache.arguments[0].name == "__user_context");

    JITUserContext *context = *(JITUserContext **)const_cast<void *>(argv[0]);
    assert(context != nullptr);

    JITFuncCallContext jit_call_context(context, contents->saved_jit_handlers);

    // debug(2) << "Calling jitted function\n";
    int exit_status = contents->jit_cache.call_jit_code(contents->jit_cache.jit_target, argv);
    // debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    // If we're profiling, report runtimes and reset profiler stats.
    contents->jit_cache.finish_profiling(context);

    // Don't call jit_call_context.finalize(): if no error handler was installed,
    // it will call halide_error_handler for us, which is fine for realize()
    // and friends because there is no other way to report an error. For this code
    // path, though, we just return the error code (if any); if a custom error
    // handler was installed, it presumably will get the first shot at handling it,
    // and if not, the caller must handle it.
    //
    // No: jit_call_context.finalize(exit_status);
    jit_call_context.finalize(exit_status);

    return exit_status;
}

int Callable::call_argv_checked(size_t argc, const void *const *argv, const QuickCallCheckInfo *actual_cci) const {
    // It's *essential* we call this for safety.
    check_arg_count_and_types(argc, actual_cci, "calling");
    return call_argv_fast(argc, argv);
}

}  // namespace Halide
