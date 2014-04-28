#include "Error.h"

namespace Halide {

bool exceptions_enabled() {
    #ifdef WITH_EXCEPTIONS
    return true;
    #else
    return false;
    #endif
}

Error::Error(const std::string &msg) : std::runtime_error(msg) {
}

CompileError::CompileError(const std::string &msg) : Error(msg) {
}

RuntimeError::RuntimeError(const std::string &msg) : Error(msg) {
}

InternalError::InternalError(const std::string &msg) : Error(msg) {
}


namespace Internal {

// Force the classes to exist, even if exceptions are off
namespace {
CompileError _compile_error("");
RuntimeError _runtime_error("");
InternalError _internal_error("");
}

void ErrorReport::explode() {
    // TODO: Add an option to error out on warnings too
    if (warning) {
        std::cerr << msg->str();
        delete msg;
        return;
    }

#ifdef WITH_EXCEPTIONS
    if (std::uncaught_exception()) {
        // This should never happen - evaluating one of the arguments
        // to the error message would have to throw an
        // exception. Nonetheless, in case it does, preserve the
        // exception already in flight and suppress this one.
        delete msg;
        return;
    } else if (runtime) {
        RuntimeError err(msg->str());
        delete msg;
        throw err;
    } else if (user) {
        CompileError err(msg->str());
        delete msg;
        throw err;
    } else {
        InternalError err(msg->str());
        delete msg;
        throw err;
    }
#else
    std::cerr << msg->str();
    delete msg;
    abort();
#endif
}
}

}
