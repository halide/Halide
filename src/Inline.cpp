#include <set>

#include "Inline.h"
#include "CSE.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Qualify.h"

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;

class Inliner : public IRMutator {
    using IRMutator::visit;

    Function func;

    // Sanity check that this is a reasonable function to inline
    void check(Function f) {

        assert(!f.has_reduction_definition());

        const Schedule &s = f.schedule();

        if (!s.store_level.is_inline()) {
            std::cerr << "Function " << f.name() << " is scheduled to be computed inline, "
                      << "but is not scheduled to be stored inline. A storage schedule "
                      << "is meaningless for functions computed inline.\n";
            assert(false);
        }

        for (size_t i = 0; i < s.dims.size(); i++) {
            Schedule::Dim d = s.dims[i];
            if (d.for_type == For::Parallel) {
                std::cerr << "Cannot parallelize dimension "
                          << d.var << " of function "
                          << f.name() << " because the function is scheduled inline.\n";
                assert(false);
            } else if (d.for_type == For::Unrolled) {
                std::cerr << "Cannot unroll dimension "
                          << d.var << " of function "
                          << f.name() << " because the function is scheduled inline.\n";
                assert(false);
            } else if (d.for_type == For::Vectorized) {
                std::cerr << "Cannot vectorize dimension "
                          << d.var << " of function "
                          << f.name() << " because the function is scheduled inline.\n";
                assert(false);
            }
        }

        for (size_t i = 0; i < s.splits.size(); i++) {
            if (s.splits[i].is_rename()) {
                std::cerr << "Warning: It is meaningless to rename variable "
                          << s.splits[i].old_var << " of function "
                          << f.name() << " to " << s.splits[i].outer
                          << " because " << f.name() << " is scheduled inline.\n";
            } else if (s.splits[i].is_fuse()) {
                std::cerr << "Warning: It is meaningless to fuse variables "
                          << s.splits[i].inner << " and " << s.splits[i].outer
                          << " because " << f.name() << " is scheduled inline.\n";
            } else {
                std::cerr << "Warning: It is meaningless to split variable "
                          << s.splits[i].old_var << " of function "
                          << f.name() << " into "
                          << s.splits[i].outer << " * "
                          << s.splits[i].factor << " + "
                          << s.splits[i].inner << " because "
                          << f.name() << " is scheduled inline.\n";
            }
        }

        for (size_t i = 0; i < s.bounds.size(); i++) {
            std::cerr << "Warning: It is meaningless to bound dimension "
                      << s.bounds[i].var << " of function "
                      << f.name() << " to be within ["
                      << s.bounds[i].min << ", "
                      << s.bounds[i].extent << "] because the function is scheduled inline.\n";
        }

    }

    void visit(const Call *op) {
        if (op->name == func.name()) {

            // Mutate the args
            vector<Expr> args(op->args.size());
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(op->args[i]);
            }
            // Grab the body
            Expr body = qualify(func.name() + ".", func.values()[op->value_index]);

            // Bind the args using Let nodes
            assert(args.size() == func.args().size());

            for (size_t i = 0; i < args.size(); i++) {
                body = Let::make(func.name() + "." + func.args()[i], args[i], body);
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
        check(func);
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

}
}
