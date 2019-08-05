#include "DebugArguments.h"
#include "IROperator.h"
#include "Module.h"

namespace Halide {
namespace Internal {

using std::vector;

void debug_arguments(LoweredFunc *func, const Target &t) {
    internal_assert(func);
    vector<Stmt> stmts;
    stmts.push_back(Evaluate::make(print("Entering Pipeline " + func->name)));
    stmts.push_back(Evaluate::make(print("Target: " + t.to_string())));
    for (LoweredArgument arg : func->args) {
        std::ostringstream name;
        Expr scalar_var = Variable::make(arg.type, arg.name);
        Expr buffer_var = Variable::make(type_of<halide_buffer_t *>(), arg.name + ".buffer");
        Expr value;
        switch (arg.kind) {
        case Argument::InputScalar:
            name << " Input " << arg.type << ' ' << arg.name << ':';
            value = scalar_var;
            break;
        case Argument::InputBuffer:
            name << " Input Buffer " << arg.name << ':';
            value = buffer_var;
            break;
        case Argument::OutputBuffer:
            name << " Output Buffer " << arg.name << ':';
            value = buffer_var;
            break;
        }
        stmts.push_back(Evaluate::make(print(name.str(), value)));
    }
    stmts.push_back(func->body);
    stmts.push_back(Evaluate::make(print("Exiting Pipeline " + func->name)));
    func->body = Block::make(stmts);
}

}  // namespace Internal
}  // namespace Halide
