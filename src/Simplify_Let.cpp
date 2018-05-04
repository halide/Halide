#include "Simplify_Internal.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;

template<typename T, typename Body>
Body Simplify::simplify_let(const T *op, ConstBounds *bounds) {
    internal_assert(!var_info.contains(op->name))
        << "Simplify only works on code where every name is unique. Repeated name: " << op->name << "\n";

    // If the value is trivial, make a note of it in the scope so
    // we can subs it in later
    ConstBounds value_bounds;
    Expr value = mutate(op->value, &value_bounds);
    Body body = op->body;

    // Iteratively peel off certain operations from the let value and push them inside.
    Expr new_value = value;
    string new_name = op->name + ".s";
    Expr new_var = Variable::make(new_value.type(), new_name);
    Expr replacement = new_var;

    debug(4) << "simplify let " << op->name << " = " << value << " in ... " << op->name << " ...\n";

    while (1) {
        const Variable *var = new_value.as<Variable>();
        const Add *add = new_value.as<Add>();
        const Sub *sub = new_value.as<Sub>();
        const Mul *mul = new_value.as<Mul>();
        const Div *div = new_value.as<Div>();
        const Mod *mod = new_value.as<Mod>();
        const Min *min = new_value.as<Min>();
        const Max *max = new_value.as<Max>();
        const Ramp *ramp = new_value.as<Ramp>();
        const Cast *cast = new_value.as<Cast>();
        const Broadcast *broadcast = new_value.as<Broadcast>();
        const Shuffle *shuffle = new_value.as<Shuffle>();
        const Call *call = new_value.as<Call>();
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

        if (is_const(new_value)) {
            replacement = substitute(new_name, new_value, replacement);
            new_value = Expr();
            break;
        } else if (var) {
            replacement = substitute(new_name, var, replacement);
            new_value = Expr();
            break;
        } else if (add && (is_const(add->b) || var_b)) {
            replacement = substitute(new_name, Add::make(new_var, add->b), replacement);
            new_value = add->a;
        } else if (add && var_a) {
            replacement = substitute(new_name, Add::make(add->a, new_var), replacement);
            new_value = add->b;
        } else if (mul && (is_const(mul->b) || var_b)) {
            replacement = substitute(new_name, Mul::make(new_var, mul->b), replacement);
            new_value = mul->a;
        } else if (div && is_const(div->b)) {
            replacement = substitute(new_name, Div::make(new_var, div->b), replacement);
            new_value = div->a;
        } else if (sub && (is_const(sub->b) || var_b)) {
            replacement = substitute(new_name, Sub::make(new_var, sub->b), replacement);
            new_value = sub->a;
        } else if (mod && is_const(mod->b)) {
            replacement = substitute(new_name, Mod::make(new_var, mod->b), replacement);
            new_value = mod->a;
        } else if (min && is_const(min->b)) {
            replacement = substitute(new_name, Min::make(new_var, min->b), replacement);
            new_value = min->a;
        } else if (max && is_const(max->b)) {
            replacement = substitute(new_name, Max::make(new_var, max->b), replacement);
            new_value = max->a;
        } else if (ramp && is_const(ramp->stride)) {
            new_value = ramp->base;
            new_var = Variable::make(new_value.type(), new_name);
            replacement = substitute(new_name, Ramp::make(new_var, ramp->stride, ramp->lanes), replacement);
        } else if (broadcast) {
            new_value = broadcast->value;
            new_var = Variable::make(new_value.type(), new_name);
            replacement = substitute(new_name, Broadcast::make(new_var, broadcast->lanes), replacement);
        } else if (cast && cast->type.bits() > cast->value.type().bits()) {
            // Widening casts get pushed inwards, narrowing casts
            // stay outside. This keeps the temporaries small, and
            // helps with peephole optimizations in codegen that
            // skip the widening entirely.
            new_value = cast->value;
            new_var = Variable::make(new_value.type(), new_name);
            replacement = substitute(new_name, Cast::make(cast->type, new_var), replacement);
        } else if (shuffle && shuffle->is_slice()) {
            // Replacing new_value below might free the shuffle
            // indices vector, so save them now.
            std::vector<int> slice_indices = shuffle->indices;
            new_value = Shuffle::make_concat(shuffle->vectors);
            new_var = Variable::make(new_value.type(), new_name);
            replacement = substitute(new_name, Shuffle::make({new_var}, slice_indices), replacement);
        } else if (shuffle && shuffle->is_concat() &&
                   ((var_a && !var_b) || (!var_a && var_b))) {
            new_var = Variable::make(var_a ? shuffle->vectors[1].type() : shuffle->vectors[0].type(), new_name);
            Expr op_a = var_a ? shuffle->vectors[0] : new_var;
            Expr op_b = var_a ? new_var : shuffle->vectors[1];
            replacement = substitute(new_name, Shuffle::make_concat({op_a, op_b}), replacement);
            new_value = var_a ? shuffle->vectors[1] : shuffle->vectors[0];
        } else if (call && (call->is_intrinsic(Call::likely) || call->is_intrinsic(Call::likely_if_innermost))) {
            replacement = substitute(new_name, Call::make(call->type, call->name, {new_var}, Call::PureIntrinsic), replacement);
            new_value = call->args[0];
        } else {
            break;
        }
    }

    if (new_value.same_as(value)) {
        // Nothing to substitute
        new_value = Expr();
        replacement = Expr();
    } else {
        debug(4) << "new let " << new_name << " = " << new_value << " in ... " << replacement << " ...\n";
    }

    VarInfo info;
    info.old_uses = 0;
    info.new_uses = 0;
    info.replacement = replacement;

    var_info.push(op->name, info);

    // Before we enter the body, track the alignment info
    bool new_value_alignment_tracked = false, new_value_bounds_tracked = false;
    if (new_value.defined() && no_overflow_scalar_int(new_value.type())) {
        ModulusRemainder mod_rem = modulus_remainder(new_value, alignment_info);
        if (mod_rem.modulus > 1) {
            alignment_info.push(new_name, mod_rem);
            new_value_alignment_tracked = true;
        }
        ConstBounds new_value_bounds;
        new_value = mutate(new_value, &new_value_bounds);
        if (new_value_bounds.min_defined || new_value_bounds.max_defined) {
            bounds_info.push(new_name, new_value_bounds);
            new_value_bounds_tracked = true;
        }
    }
    bool value_alignment_tracked = false, value_bounds_tracked = false;;
    if (no_overflow_scalar_int(value.type())) {
        ModulusRemainder mod_rem = modulus_remainder(value, alignment_info);
        if (mod_rem.modulus > 1) {
            alignment_info.push(op->name, mod_rem);
            value_alignment_tracked = true;
        }
        if (value_bounds.min_defined || value_bounds.max_defined) {
            bounds_info.push(op->name, value_bounds);
            value_bounds_tracked = true;
        }
    }

    body = mutate_let_body(body, bounds);

    if (value_alignment_tracked) {
        alignment_info.pop(op->name);
    }
    if (value_bounds_tracked) {
        bounds_info.pop(op->name);
    }
    if (new_value_alignment_tracked) {
        alignment_info.pop(new_name);
    }
    if (new_value_bounds_tracked) {
        bounds_info.pop(new_name);
    }

    info = var_info.get(op->name);
    var_info.pop(op->name);

    Body result = body;

    if (new_value.defined() && info.new_uses > 0) {
        // The new name/value may be used
        result = T::make(new_name, new_value, result);
    }

    if (info.old_uses > 0 || !remove_dead_lets) {
        // The old name is still in use. We'd better keep it as well.
        result = T::make(op->name, value, result);
    }

    // Don't needlessly make a new Let/LetStmt node.  (Here's a
    // piece of template syntax I've never needed before).
    const T *new_op = result.template as<T>();
    if (new_op &&
        new_op->name == op->name &&
        new_op->body.same_as(op->body) &&
        new_op->value.same_as(op->value)) {
        return op;
    }

    return result;

}

Expr Simplify::visit(const Let *op, ConstBounds *bounds) {
    return simplify_let<Let, Expr>(op, bounds);
}

Stmt Simplify::visit(const LetStmt *op) {
    return simplify_let<LetStmt, Stmt>(op, nullptr);
}

}
}
