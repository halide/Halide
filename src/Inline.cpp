#include <set>

#include "Inline.h"
#include "CSE.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Qualify.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;

// Sanity check that this is a reasonable function to inline
void validate_schedule_inlined_function(Function f) {
    const FuncSchedule &func_s = f.schedule();
    const StageSchedule &stage_s = f.definition().schedule();

    if (!func_s.store_level().is_inline()) {
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

    for (size_t i = 0; i < stage_s.dims().size(); i++) {
        Dim d = stage_s.dims()[i];
        if (d.is_parallel()) {
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

    for (size_t i = 0; i < stage_s.splits().size(); i++) {
        if (stage_s.splits()[i].is_rename()) {
            user_warning << "It is meaningless to rename variable "
                         << stage_s.splits()[i].old_var << " of function "
                         << f.name() << " to " << stage_s.splits()[i].outer
                         << " because " << f.name() << " is scheduled inline.\n";
        } else if (stage_s.splits()[i].is_fuse()) {
            user_warning << "It is meaningless to fuse variables "
                         << stage_s.splits()[i].inner << " and " << stage_s.splits()[i].outer
                         << " because " << f.name() << " is scheduled inline.\n";
        } else {
            user_warning << "It is meaningless to split variable "
                         << stage_s.splits()[i].old_var << " of function "
                         << f.name() << " into "
                         << stage_s.splits()[i].outer << " * "
                         << stage_s.splits()[i].factor << " + "
                         << stage_s.splits()[i].inner << " because "
                         << f.name() << " is scheduled inline.\n";
        }
    }

    for (size_t i = 0; i < func_s.bounds().size(); i++) {
        if (func_s.bounds()[i].min.defined()) {
            user_warning << "It is meaningless to bound dimension "
                         << func_s.bounds()[i].var << " of function "
                         << f.name() << " to be within ["
                         << func_s.bounds()[i].min << ", "
                         << func_s.bounds()[i].extent << "] because the function is scheduled inline.\n";
        } else if (func_s.bounds()[i].modulus.defined()) {
            user_warning << "It is meaningless to align the bounds of dimension "
                         << func_s.bounds()[i].var << " of function "
                         << f.name() << " to have modulus/remainder ["
                         << func_s.bounds()[i].modulus << ", "
                         << func_s.bounds()[i].remainder << "] because the function is scheduled inline.\n";
        }
    }
}

class Inliner : public IRMutator {
    using IRMutator::visit;

    Function func;

    void visit(const Call *op) {
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
                body = Let::make(func.name() + "." + func_args[i], args[i], body);
            }

            expr = body;

            found = true;

        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Provide *op) {
        bool old_found = found;

        found = false;
        IRMutator::visit(op);

        if (found) {
            stmt = common_subexpression_elimination(stmt);
        }

        found = old_found;
    }

public:
    bool found;

    Inliner(Function f) : func(f), found(false) {
        internal_assert(f.can_be_inlined()) << "Illegal to inline " << f.name() << "\n";
        validate_schedule_inlined_function(f);
    }

};

Stmt inline_function(Stmt s, Function f) {
    Inliner i(f);
    s = i.mutate(s);
    return s;
}

Expr inline_function(Expr e, Function f) {
    Inliner i(f);
    e = i.mutate(e);
    if (i.found) {
        e = common_subexpression_elimination(e);
    }
    return e;
}

// Inline all calls to 'f' inside 'caller'
void inline_function(Function caller, Function f) {
    Inliner i(f);
    caller.mutate(&i);
}

}
}
