#include "WrapExternStages.h"
#include "Argument.h"
#include "IRMutator.h"
#include "IROperator.h"

#include <set>

namespace Halide {
namespace Internal {

using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

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

class WrapExternStages : public IRMutator2 {
    using IRMutator2::visit;

    string make_wrapper(const Call *op) {
        string wrapper_name = replace_all(prefix + op->name, ":", "_");
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
        for (Argument a : args) {
            if (a.kind == Argument::InputBuffer ||
                a.kind == Argument::OutputBuffer) {
                Expr new_buffer_var = Variable::make(type_of<struct halide_buffer_t *>(), a.name + ".buffer");

                // Allocate some stack space for the old buffer
                string old_buffer_name = a.name + ".old_buffer_t";
                Expr old_buffer_var = Variable::make(type_of<struct buffer_t *>(), old_buffer_name);
                Expr old_buffer = Call::make(type_of<struct buffer_t *>(), Call::alloca,
                                             {(int)sizeof(buffer_t)}, Call::Intrinsic);
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
                const int bounds_query_only = a.kind == Argument::InputBuffer ? 1 : 0;
                Expr upgrade_call = Call::make(Int(32), "halide_upgrade_buffer_t",
                                               {a.name, old_buffer_var, new_buffer_var, bounds_query_only},
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
            body = LetStmt::make(p.first, p.second, body);
            old_buffers.pop_back();
        }

        // Add the wrapper to the module
        debug(2) << "Wrapped extern call to " << op->name << ":\n" << body << "\n\n";
        LoweredFunc wrapper(wrapper_name, args, body, LinkageType::Internal, NameMangling::C);
        module.append(wrapper);

        // Return the name
        return wrapper_name;
    }

    Expr visit(const Call *op) override {
        if (op->is_extern() && op->func.defined()) {
            Function f(op->func);
            internal_assert(f.has_extern_definition());
            if (f.extern_definition_uses_old_buffer_t()) {
                user_assert(module.target().has_feature(Target::LegacyBufferWrappers))
                    << "You must specify the legacy_buffer_wrappers feature in the Target "
                    << "when passing uses_old_buffer_t = true to define_extern().";
                vector<Expr> new_args;
                for (Expr e : op->args) {
                    new_args.push_back(mutate(e));
                }
                return Call::make(op->type, make_wrapper(op), new_args, Call::Extern, op->func);
            } else {
                return IRMutator2::visit(op);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }

    set<string> done;
    Module module;
public:
    WrapExternStages(Module m) : module(m) {}
    string prefix;
};
}

void wrap_legacy_extern_stages(Module m) {
    WrapExternStages wrap(m);
    // We'll be appending new functions to the module as we traverse
    // its functions, so we have to iterate with some care.
    size_t num_functions = m.functions().size();
    for (size_t i = 0; i < num_functions; i++) {
        wrap.prefix = "_halide_wrapper_" + m.functions()[i].name + "_";
        Stmt old_body = m.functions()[i].body;
        Stmt new_body = wrap.mutate(old_body);
        m.functions()[i].body = new_body;
        debug(2) << "Body after wrapping extern calls:\n" << new_body << "\n\n";
    }
}

void add_legacy_wrapper(Module module, const LoweredFunc &fn) {
    if (!module.target().has_feature(Target::LegacyBufferWrappers)) {
        return;
    }

    // Build the arguments to the wrapper function
    vector<LoweredArgument> args;
    vector<Stmt> upgrades, downgrades;
    vector<Expr> call_args;
    vector<pair<string, Expr>> new_buffers;
    for (LoweredArgument arg : fn.args) {
        if (arg.kind == Argument::InputScalar) {
            args.push_back(arg);
            call_args.push_back(Variable::make(arg.type, arg.name));
        } else {
            // Buffer arguments become opaque pointers
            args.emplace_back(arg.name, Argument::InputScalar, type_of<buffer_t *>(), 0);

            string new_buffer_name = arg.name + ".upgraded";
            Expr new_buffer_var = Variable::make(type_of<struct halide_buffer_t *>(), new_buffer_name);

            Expr old_buffer_var = Variable::make(type_of<struct buffer_t *>(), arg.name);

            // We can't get these fields from the old buffer
            BufferBuilder builder;
            builder.type = arg.type;
            builder.dimensions = arg.dimensions;
            Expr new_buffer = builder.build();

            new_buffers.emplace_back(new_buffer_name, new_buffer);

            // Make the call that downgrades the new buffer into the
            // old buffer struct.  We'll only do the full downgrade in
            // bounds query mode
            Expr downgrade_call = Call::make(Int(32), "halide_downgrade_buffer_t",
                                             {arg.name, new_buffer_var, old_buffer_var},
                                             Call::Extern);
            Stmt downgrade = make_checked_call(downgrade_call);

            // Otherwise we'll just copy over the device state flags
            Expr downgrade_device_call = Call::make(Int(32), "halide_downgrade_buffer_t_device_fields",
                                                    {arg.name, new_buffer_var, old_buffer_var},
                                                    Call::Extern);
            Stmt downgrade_device = make_checked_call(downgrade_device_call);

            Expr bounds_query = Call::make(Bool(), Call::buffer_is_bounds_query,
                                           {new_buffer_var}, Call::Extern);
            downgrade = IfThenElse::make(bounds_query, downgrade, downgrade_device);
            downgrades.push_back(downgrade);

            // Make the call to upgrade old buffer
            // fields into the original new
            // buffer. Important for bounds queries.
            const int bounds_query_only = 0;
            Expr upgrade_call = Call::make(Int(32), "halide_upgrade_buffer_t",
                                           {arg.name, old_buffer_var, new_buffer_var, bounds_query_only},
                                           Call::Extern);
            upgrades.push_back(make_checked_call(upgrade_call));

            call_args.push_back(new_buffer_var);
        }
    }

    Call::CallType call_type = Call::Extern;
    if (fn.name_mangling == NameMangling::CPlusPlus ||
        (fn.name_mangling == NameMangling::Default &&
         module.target().has_feature(Target::CPlusPlusMangling))) {
        call_type = Call::ExternCPlusPlus;
    }
    Expr inner_call = Call::make(Int(32), fn.name, call_args, call_type);
    Stmt body = make_checked_call(inner_call);
    body = Block::make({Block::make(upgrades), body, Block::make(downgrades)});

    while (!new_buffers.empty()) {
        auto p = new_buffers.back();
        body = LetStmt::make(p.first, p.second, body);
        new_buffers.pop_back();
    }

    string name = fn.name;
    if (!module.target().has_feature(Target::CPlusPlusMangling)) {
        // We can't overload the same name, so add a suffix.
        name += "_old_buffer_t";
    }

    // Add the wrapper to the module.
    debug(2) << "Added legacy wrapper for " << fn.name << ":\n" << body << "\n\n";
    LoweredFunc wrapper(name, args, body, LinkageType::External, NameMangling::Default);
    module.append(wrapper);
}

}  // namespace Internal
}  // namespace Halide
