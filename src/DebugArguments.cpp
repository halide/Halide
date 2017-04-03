#include "DebugArguments.h"
#include "Module.h"

namespace Halide {
namespace Internal {

using std::vector;

void debug_arguments(LoweredFunc *func) {
    internal_assert(func);
    vector<Stmt> stmts;
    Expr header = Call::make(Int(32), "halide_print", {Expr("Entering Pipeline " + func->name + "\n")}, Call::Extern);
    stmts.push_back(Evaluate::make(header));
    for (LoweredArgument arg : func->args) {
        vector<Expr> print_args;
        switch (arg.kind) {
        case Argument::InputScalar: {
            std::ostringstream type;
            type << arg.type;
            print_args.push_back(Expr(" Input " + type.str() + " " + arg.name + ": "));
            print_args.push_back(Variable::make(arg.type, arg.name));
            print_args.push_back(Expr("\n"));
            break;
        }
        case Argument::InputBuffer: {
            print_args.push_back(Expr(" Input Buffer " + arg.name + ": "));
            print_args.push_back(Variable::make(type_of<halide_buffer_t *>(), arg.name + ".buffer"));
            print_args.push_back(Expr("\n"));
            break;
        }
        case Argument::OutputBuffer: {
            print_args.push_back(Expr(" Output Buffer " + arg.name + ": "));
            print_args.push_back(Variable::make(type_of<halide_buffer_t *>(), arg.name + ".buffer"));
            print_args.push_back(Expr("\n"));
            break;
        }                                                                  }
        Expr str = Call::make(type_of<char *>(), Call::stringify, print_args, Call::Intrinsic);
        Expr p = Call::make(Int(32), "halide_print", {str}, Call::Extern);
        stmts.push_back(Evaluate::make(p));
    }    

    stmts.push_back(func->body);

    Expr footer = Call::make(Int(32), "halide_print", {Expr("Exiting Pipeline " + func->name + "\n")}, Call::Extern);   
    stmts.push_back(Evaluate::make(footer));

    func->body = Block::make(stmts);
}

}
}
