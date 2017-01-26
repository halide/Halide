#include "Debug.h"

namespace Halide {
namespace Internal {

int debug::debug_level() {
    static int cached_debug_level = ([]() -> int {
        std::string lvl = get_env_variable("HL_DEBUG_CODEGEN");
        return !lvl.empty() ? atoi(lvl.c_str()) : 0;
    })();
    return cached_debug_level;
}

}
}
