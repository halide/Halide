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

    // Encoded values for efficient runtime type checking;
    // identical to jit_cache.arguments in length.
    std::vector<Callable::CallCheckInfo> call_check_info;

    // Save the jit_handlers and jit_externs as they were at the time this
    // Callable was created, in case the Pipeline's version is mutated in
    // between creation and call -- we want the Callable to remain immutable
    // after creation, regardless of what you do to the Func.
    JITHandlers saved_jit_handlers;
    std::map<std::string, JITExtern> saved_jit_externs;
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
                   JITCache &&jit_cache,
                   std::vector<Callable::CallCheckInfo> &&call_check_info)
    : contents(new CallableContents) {
    contents->name = name;
    contents->jit_cache = std::move(jit_cache);
    contents->call_check_info = std::move(call_check_info);
    contents->saved_jit_handlers = jit_handlers;
    contents->saved_jit_externs = jit_externs;
}

const Callable::CallCheckInfo *Callable::call_check_info() const {
    return contents->call_check_info.data();
}

size_t Callable::arg_count() const {
    return contents->jit_cache.arguments.size();
}

const std::vector<Argument> &Callable::arguments() const {
    return contents->jit_cache.arguments;
}

void Callable::do_fail_bad_call(BadArgMask bad_arg_mask, size_t hidden_args) const {
    const size_t required_arg_count = arg_count();
    const size_t required_user_visible_args = required_arg_count - hidden_args;
    std::ostringstream errors;
    if (bad_arg_mask < 0) {
        // Note that we don't report the "actual count" here, just the expected count, in the
        // name of keeping the generated code size small.
        errors << "- Expected exactly " << required_user_visible_args << " arguments.";
    } else {
        uint64_t mask = 1;
        for (size_t i = 0; i < required_arg_count; i++, mask <<= 1) {
            if (!(bad_arg_mask & mask)) {
                continue;
            }
            const Argument &a = contents->jit_cache.arguments.at(i);
            const char *kind = a.is_scalar() ? "scalar" : "buffer";
            // Note that we don't report the "actual type" here, just the expected type...
            // saving the actual type leads to more code bloat than we can justify
            // for this. (Consider adding as a debug-only enhancement?)
            errors
                << "- Argument " << (i - hidden_args + 1)
                << " of " << required_user_visible_args << " ('" << a.name << "') was expected to be a "
                << kind << " of type '" << a.type << "'.\n";
        }
    }
    user_error << "Error calling '" << contents->name << "':\n"
               << errors.str();
}

int Callable::call_argv(size_t argc, const void **argv) const {
    assert(contents->jit_cache.jit_target.has_feature(Target::UserContext));

    const size_t args_size = contents->jit_cache.arguments.size();
    user_assert(argc == args_size)
        << "Expected " << args_size
        << " arguments (including user_context), but got " << argc << "\n";

    assert(contents->jit_cache.arguments[0].name == "__user_context");
    JITUserContext *context = *(JITUserContext **)const_cast<void *>(argv[0]);
    JITFuncCallContext jit_call_context(context, contents->saved_jit_handlers);

    debug(2) << "Calling jitted function\n";
    int exit_status = contents->jit_cache.call_jit_code(contents->jit_cache.jit_target, argv);
    debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

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

}  // namespace Halide
