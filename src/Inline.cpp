#include <set>
#include <utility>

#include "CSE.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Inline.h"
#include "Qualify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

// Sanity check that this is a reasonable function to inline
void validate_schedule_inlined_function(Function f) {
    const FuncSchedule &func_s = f.schedule();
    const StageSchedule &stage_s = f.definition().schedule();

    if (!func_s.store_level().is_inlined()) {
        user_error << "Function " << f.name() << " is scheduled to be computed inline, "
                   << "but is not scheduled to be stored inline. A storage schedule "
                   << "is meaningless for functions computed inline.\n";
    }

    // Inlining is allowed only if there is no specialization.
    user_assert(f.definition().specializations().empty())
        << "Function " << f.name() << " is scheduled inline, so it"
        << " must not have any specializations. Specialize on the"
        << " scheduled function instead.\n";

    if (func_s.memoized()) {
        user_error << "Cannot memoize function "
                   << f.name() << " because the function is scheduled inline.\n";
    }

    for (const auto &d : stage_s.dims()) {
        if (d.is_unordered_parallel()) {
            user_error << "Cannot parallelize dimension "
                       << d.var << " of function "
                       << f.name() << " because the function is scheduled inline.\n";
        } else if (d.for_type == ForType::Unrolled) {
            user_error << "Cannot unroll dimension "
                       << d.var << " of function "
                       << f.name() << " because the function is scheduled inline.\n";
        } else if (d.for_type == ForType::Vectorized) {
            user_error << "Cannot vectorize dimension "
                       << d.var << " of function "
                       << f.name() << " because the function is scheduled inline.\n";
        }
    }

    for (const auto &i : stage_s.splits()) {
        if (i.is_rename()) {
            user_warning << "It is meaningless to rename variable "
                         << i.old_var << " of function "
                         << f.name() << " to " << i.outer
                         << " because " << f.name() << " is scheduled inline.\n";
        } else if (i.is_fuse()) {
            user_warning << "It is meaningless to fuse variables "
                         << i.inner << " and " << i.outer
                         << " because " << f.name() << " is scheduled inline.\n";
        } else {
            user_warning << "It is meaningless to split variable "
                         << i.old_var << " of function "
                         << f.name() << " into "
                         << i.outer << " * "
                         << i.factor << " + "
                         << i.inner << " because "
                         << f.name() << " is scheduled inline.\n";
        }
    }

    for (const auto &i : func_s.bounds()) {
        if (i.min.defined()) {
            user_warning << "It is meaningless to bound dimension "
                         << i.var << " of function "
                         << f.name() << " to be within ["
                         << i.min << ", "
                         << i.extent << "] because the function is scheduled inline.\n";
        } else if (i.modulus.defined()) {
            user_warning << "It is meaningless to align the bounds of dimension "
                         << i.var << " of function "
                         << f.name() << " to have modulus/remainder ["
                         << i.modulus << ", "
                         << i.remainder << "] because the function is scheduled inline.\n";
        }
    }
}

class Inliner : public IRMutator {
    using IRMutator::visit;

    Function func;

    Expr visit(const Call *op) override {
        if (op->name == func.name()) {

            // Mutate the args
            vector<Expr> args(op->args.size());
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(op->args[i]);
            }
            // Grab the body
            Expr body = qualify(func.name() + ".", func.values()[op->value_index]);

            const vector<string> func_args = func.args();

            // Bind the args using Let nodes
            internal_assert(args.size() == func_args.size());

            for (size_t i = 0; i < args.size(); i++) {
                if (is_const(args[i]) || args[i].as<Variable>()) {
                    body = substitute(func.name() + "." + func_args[i], args[i], body);
                } else {
                    body = Let::make(func.name() + "." + func_args[i], args[i], body);
                }
            }

            found++;

            return body;

        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Variable *op) override {
        if (op->name == func.name() + ".buffer") {
            const Call *call = func.is_wrapper();
            internal_assert(call);
            // Do a whole-image inline. Substitute the .buffer symbol
            // for the wrapped object's .buffer symbol.
            string buf_name;
            if (call->call_type == Call::Halide) {
                buf_name = call->name;
                if (Function(call->func).outputs() > 1) {
                    buf_name += "." + std::to_string(call->value_index);
                }
                buf_name += ".buffer";
                return Variable::make(type_of<halide_buffer_t *>(), buf_name);
            } else if (call->param.defined()) {
                return Variable::make(type_of<halide_buffer_t *>(), call->name + ".buffer", call->param);
            } else {
                internal_assert(call->image.defined());
                return Variable::make(type_of<halide_buffer_t *>(), call->name + ".buffer", call->image);
            }
        } else {
            return op;
        }
    }

    Stmt visit(const Provide *op) override {
        ScopedValue<int> old_found(found, 0);
        Stmt stmt = IRMutator::visit(op);

        // TODO: making this > 1 should be desirable,
        // but explodes compiletimes in some situations.
        if (found > 0) {
            stmt = common_subexpression_elimination(stmt);
        }

        return stmt;
    }

public:
    int found = 0;

    Inliner(const Function &f)
        : func(f) {
        internal_assert(f.can_be_inlined()) << "Illegal to inline " << f.name() << "\n";
        validate_schedule_inlined_function(f);
    }
};

Stmt inline_function(Stmt s, const Function &f) {
    Inliner i(f);
    s = i.mutate(s);
    return s;
}

Expr inline_function(Expr e, const Function &f) {
    Inliner i(f);
    e = i.mutate(e);
    // TODO: making this > 1 should be desirable,
    // but explodes compiletimes in some situations.
    if (i.found > 0) {
        e = common_subexpression_elimination(e);
    }
    return e;
}

// Inline all calls to 'f' inside 'caller'
void inline_function(Function caller, const Function &f) {
    Inliner i(f);
    caller.mutate(&i);
    if (caller.has_extern_definition()) {
        for (ExternFuncArgument &arg : caller.extern_arguments()) {
            if (arg.is_func() && arg.func.same_as(f.get_contents())) {
                const Call *call = f.is_wrapper();
                internal_assert(call);
                arg.func = call->func;
            }
        }
    }
}

}  // namespace Internal
}  // namespace Halide
