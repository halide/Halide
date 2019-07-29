#include "Halide.h"
#include "ASLog.h"

namespace Halide {
namespace Internal {

int aslog::aslog_level() {
    static int cached_aslog_level = ([]() -> int {
        // If HL_DEBUG_AUTOSCHEDULE is defined, use that value.
        std::string lvl = get_env_variable("HL_DEBUG_AUTOSCHEDULE");
        if (!lvl.empty()) {
            return atoi(lvl.c_str());
        }
        // Otherwise, use HL_DEBUG_CODEGEN.
        return debug::debug_level();
    })();
    return cached_aslog_level;
}

}  // namespace Internal
}  // namespace Halide
