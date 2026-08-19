#include "Qualify.h"
#include "Rename.h"

namespace Halide {
namespace Internal {

using std::string;

Expr qualify(const string &prefix, const Expr &value) {
    return rename_ir(value, [&](const string &name) { return prefix + name; });
}

}  // namespace Internal
}  // namespace Halide
