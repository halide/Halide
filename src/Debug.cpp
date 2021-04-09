#include "Debug.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {

int cached_debug_level = ([]() -> int {
    std::string lvl = get_env_variable("HL_DEBUG_CODEGEN");
    return !lvl.empty() ? atoi(lvl.c_str()) : 0;
})();

}  // namespace

int debug::debug_level() {
    return cached_debug_level;
}

int debug::set_debug_level(int d) {
    int old_level = cached_debug_level;
    cached_debug_level = d;
    return old_level;
}

}  // namespace Internal
}  // namespace Halide
