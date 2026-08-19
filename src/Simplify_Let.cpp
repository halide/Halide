#include "Simplify_Internal.h"
#include "Substitute.h"

#include <unordered_map>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

class FindVarUses : public IRVisitor {
    // Tracks how often each name of interest is used. An entry that's still
    // false has not been used at all. An entry that's true has been used
    // exactly once. A name that's been erased is used more than once.
    std::unordered_map<std::string, bool> &var_uses;

    void note(const std::string &name) {
        auto it = var_uses.find(name);
        if (it == var_uses.end()) {
            return;
        }
        if (!it->second) {
            it->second = true;
        } else {
            var_uses.erase(it);
        }
    }

    void visit(const Variable *var) override {
        note(var->name);
    }

    // Loads and stores name a buffer, not a Variable, so there's no node to
    // substitute a value into. Erase such names entirely - they're used, and
    // they can't be inlined.
    void visit(const Load *op) override {
        if (!var_uses.empty()) {
            var_uses.erase(op->name);
            IRVisitor::visit(op);
        }
    }

    void visit(const Store *op) override {
        if (!var_uses.empty()) {
            var_uses.erase(op->name);
            IRVisitor::visit(op);
        }
    }

    void visit(const Block *op) override {
        // Early out at Block nodes if there's nothing left to learn. In
        // principle we could early-out at every node, but blocks, loads, and
        // stores seem to be enough.
        if (!var_uses.empty()) {
            op->first.accept(this);
            if (!var_uses.empty()) {
                op->rest.accept(this);
            }
        }
    }

    using IRVisitor::visit;

public:
    FindVarUses(std::unordered_map<std::string, bool> &var_uses)
        : var_uses(var_uses) {
    }
};

template<typename StmtOrExpr>
void find_var_uses(const StmtOrExpr &x, std::unordered_map<std::string, bool> &var_uses) {
    FindVarUses counter(var_uses);
    x.accept(&counter);
}

}  // namespace

template<typename LetOrLetStmt, typename Body>
Body Simplify::simplify_let(const LetOrLetStmt *op, ExprInfo *info) {

    // Lets are often deeply nested. Get the intermediate state off
    // the call stack where it could overflow onto an explicit stack.
    struct Frame {
        const LetOrLetStmt *op;
        Expr value, new_value, new_var;
        string new_name;
        bool new_value_alignment_tracked = false, new_value_bounds_tracked = false;
        bool value_alignment_tracked = false, value_bounds_tracked = false;
        VarInfo info;
        Frame(const LetOrLetStmt *op)
            : op(op) {
        }
    };

    vector<Frame> frames;
    Body result;

    while (op) {
        frames.emplace_back(op);
        Frame &f = frames.back();

        internal_assert(!var_info.contains(op->name))
            << "Simplify only works on code where every name is unique. Repeated name: " << op->name << "\n";

        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        ExprInfo value_info;
        f.value = mutate(op->value, &value_info);

        // Iteratively peel off certain operations from the let value and push them inside.
        f.new_value = f.value;
        f.new_name = op->name + ".s";
        Expr new_var = Variable::make(f.new_value.type(), f.new_name);
        Expr replacement = new_var;

        debug(4) << "simplify let " << op->name << " = " << f.value << " in...\n";

        while (true) {
            const Variable *var = f.new_value.template as<Variable>();
            const Add *add = f.new_value.template as<Add>();
            const Sub *sub = f.new_value.template as<Sub>();
            const Mul *mul = f.new_value.template as<Mul>();
            const Div *div = f.new_value.template as<Div>();
            const Mod *mod = f.new_value.template as<Mod>();
            const Min *min = f.new_value.template as<Min>();
            const Max *max = f.new_value.template as<Max>();
            const Ramp *ramp = f.new_value.template as<Ramp>();
            const Cast *cast = f.new_value.template as<Cast>();
            const Broadcast *broadcast = f.new_value.template as<Broadcast>();
            const Shuffle *shuffle = f.new_value.template as<Shuffle>();
            const Variable *var_b = nullptr;
            const Variable *var_a = nullptr;
            const Call *tag = nullptr;

            if (add) {
                var_a = add->a.as<Variable>();
                var_b = add->b.as<Variable>();
            } else if (sub) {
                var_b = sub->b.as<Variable>();
            } else if (mul) {
                var_b = mul->b.as<Variable>();
            } else if (shuffle && shuffle->is_concat() && shuffle->vectors.size() == 2) {
                var_a = shuffle->vectors[0].as<Variable>();
                var_b = shuffle->vectors[1].as<Variable>();
            }

            if (is_const(f.new_value)) {
                replacement = substitute(f.new_name, f.new_value, replacement);
                f.new_value = Expr();
                break;
            } else if (var) {
                replacement = substitute(f.new_name, var, replacement);
                f.new_value = Expr();
                break;
            } else if (add && (is_const(add->b) || var_b)) {
                replacement = substitute(f.new_name, Add::make(new_var, add->b), replacement);
                f.new_value = add->a;
            } else if (add && var_a) {
                replacement = substitute(f.new_name, Add::make(add->a, new_var), replacement);
                f.new_value = add->b;
            } else if (mul && (is_const(mul->b) || var_b)) {
                replacement = substitute(f.new_name, Mul::make(new_var, mul->b), replacement);
                f.new_value = mul->a;
            } else if (div && is_const(div->b)) {
                replacement = substitute(f.new_name, Div::make(new_var, div->b), replacement);
                f.new_value = div->a;
            } else if (sub && (is_const(sub->b) || var_b)) {
                replacement = substitute(f.new_name, Sub::make(new_var, sub->b), replacement);
                f.new_value = sub->a;
            } else if (sub && is_const(sub->a)) {
                replacement = substitute(f.new_name, Sub::make(sub->a, new_var), replacement);
                f.new_value = sub->b;
            } else if (mod && is_const(mod->b)) {
                replacement = substitute(f.new_name, Mod::make(new_var, mod->b), replacement);
                f.new_value = mod->a;
            } else if (min && is_const(min->b)) {
                replacement = substitute(f.new_name, Min::make(new_var, min->b), replacement);
                f.new_value = min->a;
            } else if (max && is_const(max->b)) {
                replacement = substitute(f.new_name, Max::make(new_var, max->b), replacement);
                f.new_value = max->a;
            } else if (ramp && is_const(ramp->stride)) {
                f.new_value = ramp->base;
                new_var = Variable::make(f.new_value.type(), f.new_name);
                replacement = substitute(f.new_name, Ramp::make(new_var, ramp->stride, ramp->lanes), replacement);
            } else if (broadcast) {
                f.new_value = broadcast->value;
                new_var = Variable::make(f.new_value.type(), f.new_name);
                replacement = substitute(f.new_name, Broadcast::make(new_var, broadcast->lanes), replacement);
            } else if (cast && cast->type.bits() > cast->value.type().bits()) {
                // Widening casts get pushed inwards, narrowing casts
                // stay outside. This keeps the temporaries small, and
                // helps with peephole optimizations in codegen that
                // skip the widening entirely.
                f.new_value = cast->value;
                new_var = Variable::make(f.new_value.type(), f.new_name);
                replacement = substitute(f.new_name, Cast::make(cast->type, new_var), replacement);
            } else if (shuffle && shuffle->is_slice()) {
                // Replacing new_value below might free the shuffle
                // indices vector, so save them now.
                std::vector<int> slice_indices = shuffle->indices;
                f.new_value = Shuffle::make_concat(shuffle->vectors);
                new_var = Variable::make(f.new_value.type(), f.new_name);
                replacement = substitute(f.new_name, Shuffle::make({new_var}, slice_indices), replacement);
            } else if (shuffle && shuffle->is_concat() &&
                       ((var_a && !var_b) || (!var_a && var_b))) {
                new_var = Variable::make(var_a ? shuffle->vectors[1].type() : shuffle->vectors[0].type(), f.new_name);
                Expr op_a = var_a ? shuffle->vectors[0] : new_var;
                Expr op_b = var_a ? new_var : shuffle->vectors[1];
                replacement = substitute(f.new_name, Shuffle::make_concat({op_a, op_b}), replacement);
                f.new_value = var_a ? shuffle->vectors[1] : shuffle->vectors[0];
            } else if ((tag = Call::as_tag(f.new_value))) {
                // tags should be stripped here.
                replacement = substitute(f.new_name, Call::make(tag->type, tag->name, {new_var}, Call::PureIntrinsic), replacement);
                f.new_value = tag->args[0];
            } else {
                break;
            }
        }

        if (f.new_value.same_as(f.value)) {
            // Nothing to substitute
            f.new_value = Expr();
            replacement = Expr();
            new_var = Expr();
        } else {
            debug(4) << "new let " << f.new_name << " = " << f.new_value << " in ... " << replacement << " ...\n";
        }

        VarInfo info;
        info.old_uses = 0;
        info.new_uses = 0;
        info.replacement = replacement;
        f.new_var = new_var;

        var_info.push(op->name, info);

        // Before we enter the body, track the alignment info
        if (f.new_value.defined() && no_overflow_scalar_int(f.new_value.type())) {
            // Remutate new_value to get updated bounds
            ExprInfo new_value_info;
            f.new_value = mutate(f.new_value, &new_value_info);
            if (new_value_info.bounds.min_defined ||
                new_value_info.bounds.max_defined ||
                new_value_info.alignment.modulus != 1) {
                // There is some useful information
                bounds_and_alignment_info.push(f.new_name, new_value_info);
                f.new_value_bounds_tracked = true;
            }
        }

        if (no_overflow_scalar_int(f.value.type())) {
            if (value_info.bounds.min_defined ||
                value_info.bounds.max_defined ||
                value_info.alignment.modulus != 1) {
                bounds_and_alignment_info.push(op->name, value_info);
                f.value_bounds_tracked = true;
            }
        }

        result = op->body;
        op = result.template as<LetOrLetStmt>();
    }

    result = mutate_let_body(result, info);

    // TODO: var_info and var_uses are pretty redundant; however, at the time
    // of writing, both cover cases that the other does not:
    // - var_info prevents duplicate lets from being generated, even
    //   from different Frame objects.
    // - var_uses avoids dead lets being generated in cases where vars are
    //   seen as used by var_info, and then later removed.

    std::unordered_map<std::string, bool> var_uses(frames.size());
    // Insert everything we think *might* be used, and then visit the body.
    for (auto &f : frames) {
        f.info = var_info.get(f.op->name);
        // Drop any reference to new_var held by the replacement expression so
        // that the only references are either f.new_var, or ones in the body or
        // new_values of other lets.
        f.info.replacement = Expr();
        if (f.new_var.is_sole_reference()) {
            // Any new_uses must have been eliminated by later mutations.
            f.info.new_uses = 0;
        }
        var_info.pop(f.op->name);
        if (f.info.old_uses) {
            internal_assert(f.info.new_uses == 0);
            var_uses.emplace(f.op->name, false);
        } else if (f.info.new_uses && f.new_value.defined()) {
            var_uses.emplace(f.new_name, false);
        }
    }
    // Substituting into a Stmt would move the value's evaluation later, past
    // whatever happens in between, so only Exprs get substituted.
    constexpr bool substitute_single_uses = std::is_same_v<Body, Expr>;

    find_var_uses(result, var_uses);

    // Bindings for the lets we dropped in favour of inlining the value. They
    // stay in scope until this function returns.
    vector<ScopedBinding<VarInfo>> substituted;

    // Inner frames have already been dealt with, and a let value can only refer
    // to names bound outside it, so by the time a frame is reached its counts
    // are final.
    //
    // An unused name loses its let. A name used exactly once loses it too, and
    // gets inlined at that single use instead.
    auto handle_let = [&](const std::string &name, const Expr &value) {
        auto it = var_uses.find(name);
        if (it != var_uses.end() && !it->second) {
            // Unused. The value doesn't survive, so the names it uses don't
            // count as used.
            return;
        }
        if (substitute_single_uses && it != var_uses.end()) {
            VarInfo replacement_info;
            replacement_info.old_uses = 0;
            replacement_info.new_uses = 0;
            replacement_info.replacement = value;
            substituted.emplace_back(var_info, name, std::move(replacement_info));
        } else {
            result = LetOrLetStmt::make(name, value, result);
        }
        // The value survives, so the names it uses are used.
        find_var_uses(value, var_uses);
    };

    for (auto &frame : reverse_view(frames)) {
        if (frame.value_bounds_tracked) {
            bounds_and_alignment_info.pop(frame.op->name);
        }
        if (frame.new_value_bounds_tracked) {
            bounds_and_alignment_info.pop(frame.new_name);
        }

        if (frame.new_value.defined() && frame.info.new_uses > 0) {
            handle_let(frame.new_name, frame.new_value);
        }

        if (frame.info.old_uses > 0) {
            handle_let(frame.op->name, frame.value);
        }

        const LetOrLetStmt *new_op = result.template as<LetOrLetStmt>();
        if (new_op &&
            new_op->name == frame.op->name &&
            new_op->body.same_as(frame.op->body) &&
            new_op->value.same_as(frame.op->value)) {
            result = frame.op;
        }
    }

    if constexpr (substitute_single_uses) {
        if (!substituted.empty()) {
            // One re-mutation resolves every replacement, chains included,
            // because visit(Variable) re-mutates each replacement as it
            // injects it. This is the result we return, so it fills in info.
            result = mutate(result, info);
        }
    }

    return result;
}

Expr Simplify::visit(const Let *op, ExprInfo *info) {
    return simplify_let<Let, Expr>(op, info);
}

Stmt Simplify::visit(const LetStmt *op) {
    return simplify_let<LetStmt, Stmt>(op, nullptr);
}

}  // namespace Internal
}  // namespace Halide
