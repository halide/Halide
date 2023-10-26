#include "Debug.h"
#include "Util.h"

namespace Halide {
namespace Internal {

int debug::debug_level() {
    static int cached_debug_level = ([]() -> int {
        return 1;
        // std::string lvl = get_env_variable("HL_DEBUG_CODEGEN");
        // return !lvl.empty() ? atoi(lvl.c_str()) : 1;
    })();
    return cached_debug_level;
}

}  // namespace Internal
}  // namespace Halide
