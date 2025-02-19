#include "Error.h"
#include "Util.h"  // for get_env_variable

#include <csignal>
#include <exception>
#include <mutex>

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

}  // namespace

void set_custom_compile_time_error_reporter(CompileTimeErrorReporter *error_reporter) {
    custom_error_reporter = error_reporter;
}

bool exceptions_enabled() {
#ifdef HALIDE_WITH_EXCEPTIONS
    return true;
#else
    return false;
#endif
}

Error::Error(const char *msg)
    : what_(new char[strlen(msg) + 1]) {
    strcpy(what_, msg);
}

Error::Error(const std::string &msg)
    : Error(msg.c_str()) {
}

Error::Error(const Error &that)
    : Error(that.what_) {
}

Error &Error::operator=(const Error &that) {
    if (this != &that) {
        delete[] this->what_;
        this->what_ = new char[strlen(that.what_) + 1];
        strcpy(this->what_, that.what_);
    }
    return *this;
}

Error::Error(Error &&that) noexcept {
    this->what_ = that.what_;
    that.what_ = nullptr;
}

Error &Error::operator=(Error &&that) noexcept {
    if (this != &that) {
        delete[] this->what_;
        this->what_ = that.what_;
        that.what_ = nullptr;
    }
    return *this;
}

Error::~Error() {
    delete[] what_;
}

const char *Error::what() const noexcept {
    return what_;
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

CompileError::CompileError(const char *msg)
    : Error(msg) {
}

RuntimeError::RuntimeError(const char *msg)
    : Error(msg) {
}

InternalError::InternalError(const char *msg)
    : Error(msg) {
}

namespace Internal {

void unhandled_exception_handler() {
    // Note that we use __cpp_exceptions (rather than HALIDE_WITH_EXCEPTIONS)
    // to maximize the chance of dealing with uncaught exceptions in weird
    // build situations (i.e., exceptions enabled via C++ but HALIDE_WITH_EXCEPTIONS
    // is somehow not set).
#ifdef __cpp_exceptions
    // This is a trick: rethrow the pending (unhandled) exception
    // so that we can see what it is and log `what` before dying.
    if (auto ce = std::current_exception()) {
        try {
            std::rethrow_exception(ce);
        } catch (Error &e) {
            // Halide Errors are presume to be nicely formatted as-is
            std::cerr << e.what() << std::flush;
        } catch (std::exception &e) {
            // Arbitrary C++ exceptions... who knows?
            std::cerr << "Uncaught exception: " << e.what() << "\n"
                      << std::flush;
        } catch (...) {
            std::cerr << "Uncaught exception: <unknown>\n"
                      << std::flush;
        }
    }
#else
    std::cerr << "unhandled_exception_handler() called but Halide was compiled without exceptions enabled; this should not happen.\n"
              << std::flush;
#endif
    abort();
}

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
#if defined(HALIDE_WITH_EXCEPTIONS)
    const bool use_newlines = false;
#else
    const bool use_newlines = (custom_error_reporter == nullptr) && isatty(2);
#endif
    const char sep = use_newlines ? '\n' : ' ';

    const char *what = (flags & Warning) ? "Warning" : "Error";
    if (flags & User) {
        // Only mention where inside of libHalide the error tripped if we have debug level > 0
        debug(1) << "User error triggered at " << file << ":" << line << "\n";
        if (condition_string) {
            debug(1) << "Condition failed: " << condition_string << "\n";
        }
        msg << what << ":";
        msg << sep;
    } else {
        msg << "Internal " << what << " at " << file << ":" << line;
        msg << sep;
        if (condition_string) {
            msg << "Condition failed: " << condition_string << ":" << sep;
        }
    }
}

ErrorReport::~ErrorReport() noexcept(false) {
    if (!msg.str().empty() && msg.str().back() != '\n') {
        msg << "\n";
    }

    if (custom_error_reporter != nullptr) {
        if (flags & Warning) {
            custom_error_reporter->warning(msg.str().c_str());
            return;
        } else {
            custom_error_reporter->error(msg.str().c_str());
            // error() should not have returned to us, but just in case
            // it does, make sure we don't continue.
            abort();
        }
    }

    // TODO: Add an option to error out on warnings too
    if (flags & Warning) {
        std::cerr << msg.str();
        return;
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    if (std::uncaught_exceptions() > 0) {
        // This should never happen - evaluating one of the arguments
        // to the error message would have to throw an
        // exception. Nonetheless, in case it does, preserve the
        // exception already in flight and suppress this one.
        return;
    } else if (flags & Runtime) {
        throw RuntimeError(msg.str());
    } else if (flags & User) {
        throw CompileError(msg.str());
    } else {
        throw InternalError(msg.str());
    }
#else
    std::cerr << msg.str() << std::flush;
    abort();
#endif
}
}  // namespace Internal

}  // namespace Halide
