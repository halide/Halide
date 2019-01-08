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
            args.emplace_back(arg.name, Argument::InputScalar, type_of<buffer_t *>(), 0, ArgumentEstimates{});

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
    DEBUG(2) << "Added legacy wrapper for " << fn.name << ":\n" << body << "\n\n";
    LoweredFunc wrapper(name, args, body, LinkageType::External, NameMangling::Default);
    module.append(wrapper);
}

}  // namespace Internal
}  // namespace Halide
