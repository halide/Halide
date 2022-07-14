#include "ASLog.h"

#include <array>

namespace Halide {
namespace Internal {

namespace {

std::string get_env_variable(char const *env_var_name) {
    if (!env_var_name) {
        return "";
    }

#ifdef _MSC_VER
    // call getenv_s without a buffer to determine the correct string length:
    size_t length = 0;
    if ((getenv_s(&length, nullptr, 0, env_var_name) != 0) || (length == 0)) {
        return "";
    }
    // call it again to retrieve the value of the environment variable;
    // note that 'length' already accounts for the null-terminator
    std::string lvl(length - 1, '@');
    size_t read = 0;
    if ((getenv_s(&read, &lvl[0], length, env_var_name) != 0) || (read != length)) {
        return "";
    }
    return lvl;
#else
    char *lvl = getenv(env_var_name);
    if (lvl) {
        return std::string(lvl);
    }
#endif

    return "";
}

constexpr int kMaxLevel = 3;
std::array<aslog_stream, kMaxLevel + 1> aslog_streams = {
    aslog_stream(0),
    aslog_stream(1),
    aslog_stream(2),
    aslog_stream(3),
};

}  // namespace

aslog_stream &aslog(int verbosity) {
    assert(verbosity <= kMaxLevel);
    return aslog_streams[verbosity];
}

int aslog_level() {
    static int cached_aslog_level = ([]() -> int {
        // If HL_DEBUG_AUTOSCHEDULE is defined, use that value.
        std::string lvl = get_env_variable("HL_DEBUG_AUTOSCHEDULE");
        if (!lvl.empty()) {
            return atoi(lvl.c_str());
        }
        // Otherwise, use HL_DEBUG_CODEGEN.
        lvl = get_env_variable("HL_DEBUG_CODEGEN");
        return !lvl.empty() ? atoi(lvl.c_str()) : 0;
    })();
    return cached_aslog_level;
}

}  // namespace Internal
}  // namespace Halide
