#include "Simplify_Internal.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

class CountVarUses : public IRVisitor {
    std::map<std::string, int> &var_uses;

    void visit(const Variable *var) override {
        var_uses[var->name]++;
    }

    void visit(const Load *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    using IRVisitor::visit;

public:
    CountVarUses(std::map<std::string, int> &var_uses)
        : var_uses(var_uses) {
    }
};

template<typename StmtOrExpr>
void count_var_uses(StmtOrExpr x, std::map<std::string, int> &var_uses) {
    CountVarUses counter(var_uses);
    x.accept(&counter);
}

}  // namespace

template<typename LetOrLetStmt, typename Body>
Body Simplify::simplify_let(const LetOrLetStmt *op, ExprInfo *bounds) {

    // Lets are often deeply nested. Get the intermediate state off
    // the call stack where it could overflow onto an explicit stack.
    struct Frame {
        const LetOrLetStmt *op;
        Expr value, new_value;
        string new_name;
        bool new_value_alignment_tracked = false, new_value_bounds_tracked = false;
        bool value_alignment_tracked = false, value_bounds_tracked = false;
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
        ExprInfo value_bounds;
        f.value = mutate(op->value, &value_bounds);

        // Iteratively peel off certain operations from the let value and push them inside.
        f.new_value = f.value;
        f.new_name = op->name + ".s";
        Expr new_var = Variable::make(f.new_value.type(), f.new_name);
        Expr replacement = new_var;

        debug(4) << "simplify let " << op->name << " = " << f.value << " in...\n";

        while (1) {
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
            const Call *call = f.new_value.template as<Call>();
            const Variable *var_b = nullptr;
            const Variable *var_a = nullptr;

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
            } else if (call && (call->is_intrinsic(Call::likely) || call->is_intrinsic(Call::likely_if_innermost))) {
                replacement = substitute(f.new_name, Call::make(call->type, call->name, {new_var}, Call::PureIntrinsic), replacement);
                f.new_value = call->args[0];
            } else {
                break;
            }
        }

        if (f.new_value.same_as(f.value)) {
            // Nothing to substitute
            f.new_value = Expr();
            replacement = Expr();
        } else {
            debug(4) << "new let " << f.new_name << " = " << f.new_value << " in ... " << replacement << " ...\n";
        }

        VarInfo info;
        info.old_uses = 0;
        info.new_uses = 0;
        info.replacement = replacement;

        var_info.push(op->name, info);

        // Before we enter the body, track the alignment info

        if (f.new_value.defined() && no_overflow_scalar_int(f.new_value.type())) {
            // Remutate new_value to get updated bounds
            ExprInfo new_value_bounds;
            f.new_value = mutate(f.new_value, &new_value_bounds);
            if (new_value_bounds.min_defined || new_value_bounds.max_defined || new_value_bounds.alignment.modulus != 1) {
                // There is some useful information
                bounds_and_alignment_info.push(f.new_name, new_value_bounds);
                f.new_value_bounds_tracked = true;
            }
        }

        if (no_overflow_scalar_int(f.value.type())) {
            if (value_bounds.min_defined || value_bounds.max_defined || value_bounds.alignment.modulus != 1) {
                bounds_and_alignment_info.push(op->name, value_bounds);
                f.value_bounds_tracked = true;
            }
        }

        result = op->body;
        op = result.template as<LetOrLetStmt>();
    }

    result = mutate_let_body(result, bounds);

    // TODO: var_info and vars_used are pretty redundant; however, at the time
    // of writing, both cover cases that the other does not:
    // - var_info prevents duplicate lets from being generated, even
    //   from different Frame objects.
    // - vars_used avoids dead lets being generated in cases where vars are
    //   seen as used by var_info, and then later removed.
    std::map<std::string, int> vars_used;
    count_var_uses(result, vars_used);

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
        if (it->value_bounds_tracked) {
            bounds_and_alignment_info.pop(it->op->name);
        }
        if (it->new_value_bounds_tracked) {
            bounds_and_alignment_info.pop(it->new_name);
        }

        VarInfo info = var_info.get(it->op->name);
        var_info.pop(it->op->name);

        if (it->new_value.defined() && (info.new_uses > 0 && vars_used.count(it->new_name) > 0)) {
            // The new name/value may be used
            result = LetOrLetStmt::make(it->new_name, it->new_value, result);
            count_var_uses(it->new_value, vars_used);
        }

        if (!remove_dead_lets || (info.old_uses > 0 && vars_used.count(it->op->name) > 0)) {
            // The old name is still in use. We'd better keep it as well.
            result = LetOrLetStmt::make(it->op->name, it->value, result);
            count_var_uses(it->value, vars_used);
        }

        const LetOrLetStmt *new_op = result.template as<LetOrLetStmt>();
        if (new_op &&
            new_op->name == it->op->name &&
            new_op->body.same_as(it->op->body) &&
            new_op->value.same_as(it->op->value)) {
            result = it->op;
        }
    }

    return result;
}

Expr Simplify::visit(const Let *op, ExprInfo *bounds) {
    return simplify_let<Let, Expr>(op, bounds);
}

Stmt Simplify::visit(const LetStmt *op) {
    return simplify_let<LetStmt, Stmt>(op, nullptr);
}

}  // namespace Internal
}  // namespace Halide
