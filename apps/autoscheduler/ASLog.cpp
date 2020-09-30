#include "ASLog.h"

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
    if ((getenv_s(&length, NULL, 0, env_var_name) != 0) || (length == 0)) {
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
    if (lvl) return std::string(lvl);
#endif

    return "";
}

}  // namespace

int aslog::aslog_level() {
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

std::string conform_name(const std::string &name, const std::string &prefix="_") {
    auto invalid_contents = [] (const char& c) { 
        return std::ispunct(c) || std::isspace(c);
    };

    auto invalid_prefix = [] (const char& c) { 
        return (c != '_') && !(std::isalpha(c));
    };

    std::string result(name);
    std::replace_if(result.begin(), result.end(), invalid_contents, '_');
    if(invalid_prefix(result.front())) { result = std::string(prefix) + result; }
    return result;
}

}  // namespace Internal
}  // namespace Halide
