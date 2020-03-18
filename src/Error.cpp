#include "Error.h"

#include <signal.h>

#ifdef _MSC_VER
#include <io.h>
inline int isatty(int fd) {
    return _isatty(fd);
}
#else
#include <unistd.h>
#endif

namespace Halide {

namespace {

CompileTimeErrorReporter *custom_error_reporter = nullptr;

void error_abort() {
#ifdef _MSC_VER
    const std::string s = Internal::get_env_variable("HL_DISABLE_WINDOWS_ABORT_DIALOG");
    const int disable = !s.empty() ? atoi(s.c_str()) : 0;
    if (disable) {
        // Debug variants of the MSVC runtime will present an "Abort, Retry, Ignore"
        // dialog in response to a call to abort(); we want to be able to disable this
        // for (e.g.) buildbots, where we never want that behavior. This is a close approximation
        // that will kill the process in a similar way.
        // (Note that 3 is the exit code for the "abort" button.)
        raise(SIGABRT);
        exit(1);
    }
#endif

    abort();
}

}  // namespace

void set_custom_compile_time_error_reporter(CompileTimeErrorReporter *error_reporter) {
    custom_error_reporter = error_reporter;
}

bool exceptions_enabled() {
#ifdef WITH_EXCEPTIONS
    return true;
#else
    return false;
#endif
}

Error::Error(const std::string &msg)
    : std::runtime_error(msg) {
}

CompileError::CompileError(const std::string &msg)
    : Error(msg) {
}

RuntimeError::RuntimeError(const std::string &msg)
    : Error(msg) {
}

InternalError::InternalError(const std::string &msg)
    : Error(msg) {
}

namespace Internal {

// Force the classes to exist, even if exceptions are off
namespace {
CompileError _compile_error("");
RuntimeError _runtime_error("");
InternalError _internal_error("");
}  // namespace

ErrorReport::ErrorReport(const char *file, int line, const char *condition_string, int flags)
    : flags(flags) {
// Note that we deliberately try to put the entire message into a single line
// (aside from newlines inserted by user code) to make it easy to filter
// specific warnings or messages via (e.g.) grep.... unless we are likely to be
// outputting to a proper terminal, in which case newlines are used to improve readability.
#if defined(WITH_EXCEPTIONS)
    const bool use_newlines = false;
#else
    const bool use_newlines = (custom_error_reporter == nullptr) && isatty(2);
#endif
    const char sep = use_newlines ? '\n' : ' ';

    const std::string &source_loc = Introspection::get_source_location();
    const char *what = (flags & Warning) ? "Warning" : "Error";
    if (flags & User) {
        // Only mention where inside of libHalide the error tripped if we have debug level > 0
        debug(1) << "User error triggered at " << file << ":" << line << "\n";
        if (condition_string) {
            debug(1) << "Condition failed: " << condition_string << "\n";
        }
        msg << what << ":";
        if (!source_loc.empty()) {
            msg << " (at " << source_loc << ")";
        }
        msg << sep;
    } else {
        msg << "Internal " << what << " at " << file << ":" << line;
        if (source_loc.empty()) {
            msg << " triggered by user code at " << source_loc << ":";
        }
        msg << sep;
        if (condition_string) {
            msg << "Condition failed: " << condition_string << ":" << sep;
        }
    }
}

ErrorReport::~ErrorReport()
#if __cplusplus >= 201100 || _MSC_VER >= 1900
    noexcept(false)
#endif
{
    if (!msg.str().empty() && msg.str().back() != '\n') {
        msg << '\n';
    }

    if (custom_error_reporter != nullptr) {
        if (flags & Warning) {
            custom_error_reporter->warning(msg.str().c_str());
            return;
        } else {
            custom_error_reporter->error(msg.str().c_str());
            // error() should not have returned to us, but just in case
            // it does, make sure we don't continue.
            error_abort();
        }
    }

    // TODO: Add an option to error out on warnings too
    if (flags & Warning) {
        std::cerr << msg.str();
        return;
    }

#ifdef WITH_EXCEPTIONS
    if (std::uncaught_exception()) {
        // This should never happen - evaluating one of the arguments
        // to the error message would have to throw an
        // exception. Nonetheless, in case it does, preserve the
        // exception already in flight and suppress this one.
        return;
    } else if (flags & Runtime) {
        RuntimeError err(msg.str());
        throw err;
    } else if (flags & User) {
        CompileError err(msg.str());
        throw err;
    } else {
        InternalError err(msg.str());
        throw err;
    }
#else
    std::cerr << msg.str();
    error_abort();
#endif
}
}  // namespace Internal

}  // namespace Halide
