#include "CSE.h"
#include "Debug.h"
#include "ExternFuncArgument.h"
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

    for (const auto &split : stage_s.splits()) {
        switch (split.split_type) {
        case Split::RenameVar:
            user_warning << "It is meaningless to rename variable "
                         << split.old_var << " of function "
                         << f.name() << " to " << split.outer
                         << " because " << f.name() << " is scheduled inline.\n";
            break;
        case Split::FuseVars:
            user_warning << "It is meaningless to fuse variables "
                         << split.inner << " and " << split.outer
                         << " because " << f.name() << " is scheduled inline.\n";
            break;
        case Split::SplitVar:
            user_warning << "It is meaningless to split variable "
                         << split.old_var << " of function "
                         << f.name() << " into "
                         << split.outer << " * "
                         << split.factor << " + "
                         << split.inner << " because "
                         << f.name() << " is scheduled inline.\n";

            break;
        }
    }

    for (const auto &b : func_s.bounds()) {
        if (b.min.defined()) {
            user_warning << "It is meaningless to bound dimension "
                         << b.var << " of function "
                         << f.name() << " to be within ["
                         << b.min << ", "
                         << b.extent << "] because the function is scheduled inline.\n";
        } else if (b.modulus.defined()) {
            user_warning << "It is meaningless to align the bounds of dimension "
                         << b.var << " of function "
                         << f.name() << " to have modulus/remainder ["
                         << b.modulus << ", "
                         << b.remainder << "] because the function is scheduled inline.\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Inliner: design notes
//
// Picking how many functions to inline per CSE invocation is a balance
// between two penalties:
//
// - Doing all N functions in one pass is bad. CSE's RemoveLets, while
//   substituting Let bindings away, can re-walk shared subtrees
//   exponentially in their nesting depth, and the materialized body at a
//   call site after N levels of inlining is a DAG of exactly that shape.
//   Per-CSE-invocation cost grows roughly exponentially in the batch size.
//
// - Doing one function per pass is also bad. Each pass walks the entire
//   current IR, which grows as bodies get materialized; N passes ×
//   O(|s|) is quadratic in N.
//
// The exponential bites much harder than the quadratic, so we use a small
// constant batch_size (8). Empirically the optimum drifts roughly like
// log(N) (K≈8 at N=100, K≈12 at N=300), so a constant works well across
// the range we measured.
//
// Implementation: each add() call assigns the entry an order_id (its
// position in the add() sequence). operator() processes the set by
// iterative deepening through that sequence -- a series of passes that
// raise an active_limit by batch_size each time, with visit(Call) only
// inlining entries whose order_id is below the current limit. The CSE
// that runs between passes (per-Provide in the Stmt mutator, top-level
// in the Expr form) flattens shared subtrees into named Let references,
// so the next pass's RemoveLets input has bounded shared-Let nesting.
//
// Correct for any add() order: a Call that survives a pass (because it
// got wrapped inside a body materialized by an earlier limit) is picked
// up by a later pass once its order_id falls under the limit. But add()
// in consumer-first (reverse-topological) order is best for performance:
// each pass's substitutions then expose the next layer of producer Calls
// for the following pass. With the wrong order, the work piles up in the
// final pass once the limit hits the entries the call sites actually
// reference, defeating the bounded-per-pass cost.
// ---------------------------------------------------------------------------

Inliner::Inliner(const Function &f) {
    internal_assert(f.can_be_inlined()) << "Illegal to inline " << f.name() << "\n";
    validate_schedule_inlined_function(f);
    add(f);
}

void Inliner::add(const Function &f) {
    for (int i = 0; i < f.outputs(); i++) {
        Key k{f.name(), i};
        auto [it, inserted] = to_inline.insert({k, Entry{}});
        if (inserted) {
            it->second.func = f;
            // order_id is the entry's position in the add() sequence, used
            // by operator()'s iterative-deepening loop to decide when this
            // entry first becomes eligible to inline.
            it->second.order_id = to_inline.size() - 1;
        }
    }
}

Expr Inliner::operator()(const Expr &e) {
    // Single-pass path: either we're already inside an outer deepening
    // pass (recursive call from get_qualified_body), or the set is small
    // enough that the deepening loop wouldn't do anything useful.
    if (active_limit != SIZE_MAX || to_inline.size() <= batch_size) {
        return common_subexpression_elimination(mutate(e));
    }
    Expr result = e;
    size_t limit = batch_size;
    while (true) {
        active_limit = limit;
        min_skipped_order_id = SIZE_MAX;
        Expr mutated = mutate(result);
        active_limit = SIZE_MAX;
        // Only run CSE if mutate actually changed anything; a pass that
        // only discovered above-the-limit Calls produces an unchanged
        // result and there's nothing for CSE to do.
        if (!mutated.same_as(result)) {
            result = common_subexpression_elimination(mutated);
        }
        // Re-processing of cached bodies in visit(Call) bubbles their
        // remaining un-inlined Call into min_skipped_order_id, so this
        // truly means "nothing inlinable is left anywhere in the result."
        if (min_skipped_order_id == SIZE_MAX) {
            break;
        }
        // Jump directly to the next un-inlined entry instead of stepping
        // by batch_size through regions of order-space the input doesn't
        // reference. (No need to check limit against to_inline.size():
        // if every entry were below the limit, none could have been
        // skipped, so min_skipped_order_id would be SIZE_MAX already.)
        limit = min_skipped_order_id + batch_size;
    }
    return result;
}

Stmt Inliner::operator()(const Stmt &s) {
    if (active_limit != SIZE_MAX || to_inline.size() <= batch_size) {
        return mutate(s);
    }
    // Same deepening loop as the Expr version. No top-of-loop CSE here
    // because the CSE that bounds per-pass work happens in two places
    // already: the per-Provide CSE inside visit(Provide) flattens each
    // Provide after this pass's substitutions, and any recursive
    // operator()(Expr) from get_qualified_body sees active_limit set and
    // CSEs the qualified body it produces.
    Stmt result = s;
    size_t limit = batch_size;
    while (true) {
        active_limit = limit;
        min_skipped_order_id = SIZE_MAX;
        result = mutate(result);
        active_limit = SIZE_MAX;
        if (min_skipped_order_id == SIZE_MAX) {
            break;
        }
        limit = min_skipped_order_id + batch_size;
    }
    return result;
}

Expr Inliner::visit(const Call *op) {
    auto it = to_inline.find({op->name, op->value_index});
    if (it != to_inline.end()) {
        // If this entry's order_id is past the current limit, leave the
        // Call alone; a later pass with a higher limit will pick it up.
        // Remember the smallest such order_id so the outer loop can jump
        // the limit directly to it instead of stepping by batch_size.
        if (it->second.order_id >= active_limit) {
            min_skipped_order_id = std::min(min_skipped_order_id, it->second.order_id);
            return IRMutator::visit(op);
        }
        // Below the limit: actually substitute.
        Entry &entry = it->second;
        const Function &func = entry.func;
        // Mutate the args
        auto args = mutate(op->args);

        // Compute (or re-compute) the cached qualified body. The cache is
        // (re-)processed whenever the current active_limit has expanded
        // past the lowest pending order_id inside the cached body, so the
        // newly-active functions get pulled in here instead of leaving
        // the outer mutate walks to pick them up at every call site.
        if (!entry.qualified_body.defined()) {
            entry.qualified_body = qualify(func.name() + ".", func.values()[op->value_index]);
            // Fall through to the recursive operator() call below.
            entry.lowest_pending_order_id = 0;
        }
        if (active_limit > entry.lowest_pending_order_id) {
            // Save/restore the outer pass's tracking around the recursive
            // call so we can record per-entry lowest pending while still
            // bubbling the outer's seen-skip up.
            size_t saved_min = min_skipped_order_id;
            min_skipped_order_id = SIZE_MAX;
            entry.qualified_body = (*this)(entry.qualified_body);
            entry.lowest_pending_order_id = min_skipped_order_id;
            min_skipped_order_id = std::min(saved_min, entry.lowest_pending_order_id);
        }
        Expr body = entry.qualified_body;

        const vector<string> func_args = func.args();

        // Bind the args using Let nodes
        internal_assert(args.size() == func_args.size());

        for (size_t i = 0; i < args.size(); i++) {
            body = Let::make(func.name() + "." + func_args[i], args[i], body);
        }

        return body;
    }
    return IRMutator::visit(op);
}

Expr Inliner::visit(const Variable *op) {
    // Whole-image inline for wrappers: if op is "<f>.buffer" for some inlined
    // wrapper f, rewrite it to the wrapped buffer's Variable. Extract the
    // "<f>" prefix and look up directly rather than scanning to_inline.
    const string suffix = ".buffer";
    if (op->name.size() <= suffix.size() ||
        op->name.compare(op->name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return op;
    }
    // Wrappers always have a single output, so look up by (name, 0).
    auto it = to_inline.find({op->name.substr(0, op->name.size() - suffix.size()), 0});
    if (it == to_inline.end()) {
        return op;
    }
    const Function &func = it->second.func;
    const Call *call = func.is_wrapper();
    internal_assert(call);
    if (call->call_type == Call::Halide) {
        string buf_name = call->name;
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
}

Stmt Inliner::visit(const Provide *op) {
    Stmt stmt = IRMutator::visit(op);
    // CSE on the Provide if it changed -- IRMutator returns op unchanged
    // if no child Expr was rewritten, so this skips CSE on Provides where
    // no inlining (or wrapper .buffer rewrite) touched anything inside.
    // Running CSE on the whole Stmt rather than each value/index separately
    // catches shared subexpressions between them.
    if (!stmt.same_as(op)) {
        stmt = common_subexpression_elimination(stmt);
    }
    return stmt;
}

Stmt inline_function(const Stmt &s, const Function &f) {
    return Inliner(f)(s);
}

Expr inline_function(Expr e, const Function &f) {
    return Inliner(f)(e);
}

Stmt inline_functions(const Stmt &s, const vector<Function> &fs) {
    if (fs.empty()) {
        return s;
    }
    Inliner i;
    for (const Function &f : fs) {
        internal_assert(f.can_be_inlined()) << "Illegal to inline " << f.name() << "\n";
        validate_schedule_inlined_function(f);
        i.add(f);
    }
    return i(s);
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
