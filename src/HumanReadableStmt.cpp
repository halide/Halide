#include "HumanReadableStmt.h"

#include "IR.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Lower.h"

#include <map>
#include <string>

namespace Halide {
namespace Internal {

Stmt human_readable_stmt(Function f, Stmt s, Buffer buf,
                         std::map<std::string, Expr> replacements) {
    for (size_t i = 0; i < f.output_buffers().size(); i++) {
        std::string name = f.output_buffers()[i].name();
        replacements[name + ".min.0"] = buf.min(0);
        replacements[name + ".min.1"] = buf.min(1);
        replacements[name + ".min.2"] = buf.min(2);
        replacements[name + ".min.3"] = buf.min(3);

        replacements[name + ".stride.0"] = buf.stride(0);
        replacements[name + ".stride.1"] = buf.stride(1);
        replacements[name + ".stride.2"] = buf.stride(2);
        replacements[name + ".stride.3"] = buf.stride(3);

        replacements[name + ".extent.0"] = buf.extent(0);
        replacements[name + ".extent.1"] = buf.extent(1);
        replacements[name + ".extent.2"] = buf.extent(2);
        replacements[name + ".extent.3"] = buf.extent(3);

        replacements[name + ".elem_size"] = f.output_buffers()[i].type().bytes();
    }

    return simplify(substitute(replacements, s));
}

}
}
