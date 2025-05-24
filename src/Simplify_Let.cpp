#include "Simplify_Internal.h"
#include "Substitute.h"

#include <unordered_set>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

class FindVarUses : public IRVisitor {
    std::unordered_set<std::string> &unused_vars;

    void visit(const Variable *var) override {
        unused_vars.erase(var->name);
    }

    void visit(const Load *op) override {
        if (!unused_vars.empty()) {
            unused_vars.erase(op->name);
            IRVisitor::visit(op);
        }
    }

    void visit(const Store *op) override {
        if (!unused_vars.empty()) {
            unused_vars.erase(op->name);
            IRVisitor::visit(op);
        }
    }

    void visit(const Block *op) override {
        // Early out at Block nodes if we've already seen every name we're
        // interested in. In principle we could early-out at every node, but
        // blocks, loads, and stores seem to be enough.
        if (!unused_vars.empty()) {
            op->first.accept(this);
            if (!unused_vars.empty()) {
                op->rest.accept(this);
            }
        }
    }

    using IRVisitor::visit;

public:
    FindVarUses(std::unordered_set<std::string> &unused_vars)
        : unused_vars(unused_vars) {
    }
};

template<typename StmtOrExpr>
void find_var_uses(const StmtOrExpr &x, std::unordered_set<std::string> &unused_vars) {
    FindVarUses counter(unused_vars);
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

        debug(4) << "simplify let " << op->name << " = (" << f.value.type() << ") " << f.value << " in...\n";

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
            } else if (shuffle && shuffle->is_concat() && is_pure(shuffle)) {
                // Substitute in all concatenates as they will likely simplify
                // with other shuffles.
                // As the structure of this while loop makes it hard to peel off
                // pure operations from _all_ arguments to the Shuffle, we will
                // instead subsitute all of the vars that go in the shuffle, and
                // instead guard against side effects by checking with `is_pure()`.
                replacement = substitute(f.new_name, shuffle, replacement);
                f.new_value = Expr();
                break;
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
            } else if ((tag = Call::as_tag(f.new_value)) != nullptr && !tag->is_intrinsic(Call::strict_float)) {
                // Most tags should be stripped here, but not strict_float(); removing it will change the semantics
                // of the let-expr we are producing.
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

    // TODO: var_info and unused_vars are pretty redundant; however, at the time
    // of writing, both cover cases that the other does not:
    // - var_info prevents duplicate lets from being generated, even
    //   from different Frame objects.
    // - unused_vars avoids dead lets being generated in cases where vars are
    //   seen as used by var_info, and then later removed.

    std::unordered_set<std::string> unused_vars(frames.size());
    // Insert everything we think *might* be used, and then visit the body,
    // removing things from the set as we find uses of them.
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
            unused_vars.insert(f.op->name);
        } else if (f.info.new_uses && f.new_value.defined()) {
            unused_vars.insert(f.new_name);
        }
    }
    find_var_uses(result, unused_vars);

    for (const auto &frame : reverse_view(frames)) {
        if (frame.value_bounds_tracked) {
            bounds_and_alignment_info.pop(frame.op->name);
        }
        if (frame.new_value_bounds_tracked) {
            bounds_and_alignment_info.pop(frame.new_name);
        }

        if (frame.new_value.defined() && (frame.info.new_uses > 0 && !unused_vars.count(frame.new_name))) {
            // The new name/value may be used
            result = LetOrLetStmt::make(frame.new_name, frame.new_value, result);
            find_var_uses(frame.new_value, unused_vars);
        }

        if ((!remove_dead_code && std::is_same<LetOrLetStmt, LetStmt>::value) ||
            (frame.info.old_uses > 0 && !unused_vars.count(frame.op->name))) {
            // The old name is still in use. We'd better keep it as well.
            result = LetOrLetStmt::make(frame.op->name, frame.value, result);
            find_var_uses(frame.value, unused_vars);
        }

        const LetOrLetStmt *new_op = result.template as<LetOrLetStmt>();
        if (new_op &&
            new_op->name == frame.op->name &&
            new_op->body.same_as(frame.op->body) &&
            new_op->value.same_as(frame.op->value)) {
            result = frame.op;
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
