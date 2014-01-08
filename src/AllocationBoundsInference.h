#include "IR.h"
#include <map>

namespace Halide {
namespace Internal {

Stmt allocation_bounds_inference(Stmt s, const std::map<std::string, Function> &env);

}
}
