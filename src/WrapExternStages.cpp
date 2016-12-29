#include "WrapExternStages.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Argument.h"

#include <set>

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;
using std::pair;

namespace {
class WrapExternStages : public IRMutator {
    using IRMutator::visit;

    // Make a call and return the result upwards immediately if it's
    // non-zero. Assumes that inner or outer code will throw an
    // appropriate error.
    Stmt make_checked_call(Expr call) {
        internal_assert(call.type() == Int(32));
        string result_var_name = unique_name('t');
        Expr result_var = Variable::make(Int(32), result_var_name);
        Stmt s = AssertStmt::make(result_var == 0, result_var);
        s = LetStmt::make(result_var_name, call, s);
        return s;
    }

    string make_wrapper(const Call *op) {
        string wrapper_name = "halide_private_wrapper_" + op->name;
        if (done.count(op->name)) {
            return wrapper_name;
        }
        done.insert(op->name);

        Function f(op->func);

        // Build the arguments to the wrapper function
        vector<Argument> args;
        for (ExternFuncArgument arg : f.extern_arguments()) {
            if (arg.arg_type == ExternFuncArgument::FuncArg) {
                Function f_arg(arg.func);
                for (auto b : f_arg.output_buffers()) {
                    args.emplace_back(b.name(), Argument::InputBuffer,
                                      b.type(), b.dimensions());
                }
            } else if (arg.arg_type == ExternFuncArgument::BufferArg) {
                args.emplace_back(arg.buffer.name(), Argument::InputBuffer,
                                  arg.buffer.type(), arg.buffer.dimensions());
            } else if (arg.arg_type == ExternFuncArgument::ExprArg) {
                args.emplace_back(unique_name('a'), Argument::InputScalar,
                                  arg.expr.type(), 0);
            } else if (arg.arg_type == ExternFuncArgument::ImageParamArg) {
                args.emplace_back(arg.image_param.name(), Argument::InputBuffer,
                                  arg.image_param.type(), arg.image_param.dimensions());
            }
        }
        for (auto b : f.output_buffers()) {
            args.emplace_back(b.name(), Argument::OutputBuffer, b.type(), b.dimensions());
        }

        // Build the body of the wrapper.
        vector<Stmt> upgrades, downgrades;
        vector<pair<string, Expr>> old_buffers;
        vector<Expr> call_args;
        vector<Expr> old_buffer_struct_elems(sizeof(buffer_t) / sizeof(uint64_t),
                                             make_zero(UInt(64)));
        for (Argument a : args) {
            if (a.kind == Argument::InputBuffer ||
                a.kind == Argument::OutputBuffer) {
                Expr new_buffer_var = Variable::make(a.type, a.name);

                // Allocate some stack space for the old buffer
                string old_buffer_name = a.name + ".old_buffer_t";
                Expr old_buffer_var = Variable::make(type_of<struct buffer_t *>(), old_buffer_name);
                Expr old_buffer = Call::make(type_of<uint64_t *>(), Call::make_struct,
                                             old_buffer_struct_elems, Call::Intrinsic);
                old_buffer = reinterpret<struct buffer_t *>(old_buffer);
                old_buffers.emplace_back(old_buffer_name, old_buffer);

                // Make the call that downgrades the new
                // buffer into the old buffer struct.
                Expr downgrade_call = Call::make(Int(32), "halide_downgrade_buffer_t",
                                                 {a.name, new_buffer_var, old_buffer_var},
                                                 Call::Extern);
                downgrades.push_back(make_checked_call(downgrade_call));

                // Make the call to upgrade old buffer
                // fields into the original new
                // buffer. Important for bounds queries.
                Expr upgrade_call = Call::make(Int(32), "halide_upgrade_buffer_t",
                                               {a.name, old_buffer_var, new_buffer_var},
                                               Call::Extern);
                upgrades.push_back(make_checked_call(upgrade_call));
                call_args.push_back(old_buffer_var);
            } else {
                call_args.push_back(Variable::make(a.type, a.name));
            }
        }

        Expr inner_call = Call::make(op->type, op->name, call_args, op->call_type);
        Stmt body = make_checked_call(inner_call);
        body = Block::make({Block::make(downgrades), body, Block::make(upgrades)});

        while (!old_buffers.empty()) {
            auto p = old_buffers.back();
            old_buffers.pop_back();
            body = LetStmt::make(p.first, p.second, body);
        }

        // Add the wrapper to the module
        debug(2) << "Wrapped extern call to " << op->name << ":\n" << body << "\n\n";
        LoweredFunc wrapper(wrapper_name, args, body, LoweredFunc::Internal);
        module.append(wrapper);

        // Return the name
        return wrapper_name;
    }

    void visit(const Call *op) {
        if ((op->call_type == Call::Extern ||
             op->call_type == Call::ExternCPlusPlus) &&
            op->func.defined()) {
            Function f(op->func);
            internal_assert(f.has_extern_definition());
            if (f.extern_definition_uses_old_buffer_t()) {
                vector<Expr> new_args;
                for (Expr e : op->args) {
                    new_args.push_back(mutate(e));
                }
                expr = Call::make(op->type, make_wrapper(op), new_args, op->call_type, op->func);
            } else {
                IRMutator::visit(op);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    set<string> done;
    Module module;
public:
    WrapExternStages(Module m) : module(m) {}
};
}

void wrap_extern_stages(Module m) {
    WrapExternStages wrap(m);
    // We'll be appending new functions to the module as we traverse
    // its functions, so we have to iterate with some care.
    size_t num_functions = m.functions().size();
    for (size_t i = 0; i < num_functions; i++) {
        m.functions()[i].body = wrap.mutate(m.functions()[i].body);
        debug(2) << "Body after wrapping extern calls:\n" << m.functions()[i].body << "\n\n";
    }
}

}
}
