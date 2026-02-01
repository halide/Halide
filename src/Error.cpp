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
            // Halide Errors are presumed to be nicely formatted as-is
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

template<typename T>
[[noreturn]] void throw_error_common(const T &e) {
    if (custom_error_reporter) {
        custom_error_reporter->error(e.what());
        // error() should not have returned to us, but just in case
        // it does, make sure we don't continue.
        abort();
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    throw e;
#else
    std::cerr << e.what() << std::flush;
    abort();
#endif
}

}  // namespace

void throw_error(const RuntimeError &e) {
    throw_error_common(e);
}

void throw_error(const CompileError &e) {
    throw_error_common(e);
}

void throw_error(const InternalError &e) {
    throw_error_common(e);
}

void issue_warning(const char *warning) {
    if (custom_error_reporter) {
        custom_error_reporter->warning(warning);
    } else {
        std::cerr << warning;
    }
}

}  // namespace Internal

}  // namespace Halide
