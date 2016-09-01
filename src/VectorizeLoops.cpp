#include <algorithm>

#include "VectorizeLoops.h"
#include "IRMutator.h"
#include "Scope.h"
#include "IRPrinter.h"
#include "Deinterleave.h"
#include "Substitute.h"
#include "IROperator.h"
#include "IREquality.h"
#include "ExprUsesVar.h"
#include "Solve.h"
#include "Simplify.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::pair;

namespace {

// For a given var, replace expressions like shuffle_vector(var, 4)
// with var.lane.4
class ReplaceShuffleVectors : public IRMutator {
    string var;

    using IRMutator::visit;

    void visit(const Call *op) {
        const Variable *v;
        const int64_t *i;
        if (op->is_intrinsic(Call::shuffle_vector) &&
            op->args.size() == 2 &&
            (v = op->args[0].as<Variable>()) &&
            v->name == var &&
            (i = as_const_int(op->args[1]))) {
            expr = Variable::make(op->type, var + ".lane." + std::to_string(*i));
        } else {
            IRMutator::visit(op);
        }
    }
public:
    ReplaceShuffleVectors(const string &v) : var(v) {}
};



/** Find the exact max and min lanes of a vector expression. Not
 * conservative like bounds_of_expr, but uses similar rules for some
 * common node types where it can be exact. Assumes any vector
 * variables defined externally also have .min_lane and .max_lane
 * versions in scope. */
Interval bounds_of_lanes(Expr e) {
    if (const Add *add = e.as<Add>()) {
        if (const Broadcast *b = add->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(add->a);
            return {ia.min + b->value, ia.max + b->value};
        } else if (const Broadcast *b = add->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(add->b);
            return {b->value + ia.min, b->value + ia.max};
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        if (const Broadcast *b = sub->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(sub->a);
            return {ia.min - b->value, ia.max - b->value};
        } else if (const Broadcast *b = sub->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(sub->b);
            return {b->value - ia.max, b->value - ia.max};
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        if (const Broadcast *b = mul->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->a);
                return {ia.min * b->value, ia.max * b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->a);
                return {ia.max * b->value, ia.min * b->value};
            }
        } else if (const Broadcast *b = mul->a.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->b);
                return {b->value * ia.min, b->value * ia.max};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->b);
                return {b->value * ia.max, b->value * ia.min};
            }
        }
    } else if (const Div *div = e.as<Div>()) {
        if (const Broadcast *b = div->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(div->a);
                return {ia.min / b->value, ia.max / b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(div->a);
                return {ia.max / b->value, ia.min / b->value};
            }
        }
    } else if (const And *and_ = e.as<And>()) {
        if (const Broadcast *b = and_->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(and_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = and_->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(and_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Or *or_ = e.as<Or>()) {
        if (const Broadcast *b = or_->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(or_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = or_->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(or_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Min *min = e.as<Min>()) {
        if (const Broadcast *b = min->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(min->a);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        } else if (const Broadcast *b = min->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(min->b);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        }
    } else if (const Max *max = e.as<Max>()) {
        if (const Broadcast *b = max->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(max->a);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        } else if (const Broadcast *b = max->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(max->b);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        }
    } else if (const Not *not_ = e.as<Not>()) {
        Interval ia = bounds_of_lanes(not_->a);
        return {!ia.max, !ia.min};
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr last_lane_idx = make_const(r->base.type(), r->lanes-1);
        if (is_positive_const(r->stride)) {
            return {r->base, r->base + last_lane_idx * r->stride};
        } else if (is_negative_const(r->stride)) {
            return {r->base + last_lane_idx * r->stride, r->base};
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return {b->value, b->value};
    } else if (const Variable *var = e.as<Variable>()) {
        return {Variable::make(var->type.element_of(), var->name + ".min_lane"),
                Variable::make(var->type.element_of(), var->name + ".max_lane")};
    } else if (const Let *let = e.as<Let>()) {
        Interval ia = bounds_of_lanes(let->value);
        Interval ib = bounds_of_lanes(let->body);
        if (expr_uses_var(ib.min, let->name + ".min_lane")) {
            ib.min = Let::make(let->name + ".min_lane", ia.min, ib.min);
        }
        if (expr_uses_var(ib.max, let->name + ".min_lane")) {
            ib.max = Let::make(let->name + ".min_lane", ia.min, ib.max);
        }
        if (expr_uses_var(ib.min, let->name + ".max_lane")) {
            ib.min = Let::make(let->name + ".max_lane", ia.max, ib.min);
        }
        if (expr_uses_var(ib.max, let->name + ".max_lane")) {
            ib.max = Let::make(let->name + ".max_lane", ia.max, ib.max);
        }
        return ib;
    }

    // Take the explicit min and max over the lanes
    Expr min_lane = extract_lane(e, 0);
    Expr max_lane = min_lane;
    for (int i = 1; i < e.type().lanes(); i++) {
        Expr next_lane = extract_lane(e, i);
        if (e.type().is_bool()) {
            min_lane = And::make(min_lane, next_lane);
            max_lane = Or::make(max_lane, next_lane);
        } else {
            min_lane = Min::make(min_lane, next_lane);
            max_lane = Max::make(max_lane, next_lane);
        }
    }
    return {min_lane, max_lane};
};

// Allocations inside vectorized loops grow an additional inner
// dimension to represent the separate copy of the allocation per
// vector lane. This means loads and stores to them need to be
// rewritten slightly.
class RewriteAccessToVectorAlloc : public IRMutator {
    Expr var;
    string alloc;
    int lanes;

    using IRMutator::visit;

    Expr mutate_index(string a, Expr index) {
        index = mutate(index);
        if (a == alloc) {
            return index * lanes + var;
        } else {
            return index;
        }
    }

    void visit(const Load *op) {
        expr = Load::make(op->type, op->name, mutate_index(op->name, op->index), op->image, op->param);
    }

    void visit(const Store *op) {
        stmt = Store::make(op->name, mutate(op->value), mutate_index(op->name, op->index), op->param);
    }

public:
    RewriteAccessToVectorAlloc(string v, string a, int l) :
        var(Variable::make(Int(32), v)), alloc(a), lanes(l) {}
};


// Substitutes a vector for a scalar var in a Stmt. Used on the
// body of every vectorized loop.
class VectorSubs : public IRMutator {
    // The var we're vectorizing
    string var;

    // What we're replacing it with. Usually a ramp.
    Expr replacement;

    // A suffix to attach to widened variables.
    string widening_suffix;

    // A scope containing lets and letstmts whose values became
    // vectors.
    Scope<Expr> scope;

    // A stack of all containing lets. We need to reinject the scalar
    // version of them if we scalarize inner code.
    vector<pair<string, Expr>> containing_lets;

    // Widen an expression to the given number of lanes.
    Expr widen(Expr e, int lanes) {
        if (e.type().lanes() == lanes) {
            return e;
        } else if (e.type().lanes() == 1) {
            return Broadcast::make(e, lanes);
        } else {
            internal_error << "Mismatched vector lanes in VectorSubs\n";
        }
        return Expr();
    }

    using IRMutator::visit;

    virtual void visit(const Cast *op) {
        Expr value = mutate(op->value);
        if (value.same_as(op->value)) {
            expr = op;
        } else {
            Type t = op->type.with_lanes(value.type().lanes());
            expr = Cast::make(t, value);
        }
    }

    virtual void visit(const Variable *op) {
        string widened_name = op->name + widening_suffix;
        if (op->name == var) {
            expr = replacement;
        } else if (scope.contains(op->name)) {
            // If the variable appears in scope then we previously widened
            // it and we use the new widened name for the variable.
            expr = Variable::make(scope.get(op->name).type(), widened_name);
        } else {
            expr = op;
        }
    }

    template<typename T>
    void mutate_binary_operator(const T *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            int w = std::max(a.type().lanes(), b.type().lanes());
            expr = T::make(widen(a, w), widen(b, w));
        }
    }

    void visit(const Add *op) {mutate_binary_operator(op);}
    void visit(const Sub *op) {mutate_binary_operator(op);}
    void visit(const Mul *op) {mutate_binary_operator(op);}
    void visit(const Div *op) {mutate_binary_operator(op);}
    void visit(const Mod *op) {mutate_binary_operator(op);}
    void visit(const Min *op) {mutate_binary_operator(op);}
    void visit(const Max *op) {mutate_binary_operator(op);}
    void visit(const EQ *op)  {mutate_binary_operator(op);}
    void visit(const NE *op)  {mutate_binary_operator(op);}
    void visit(const LT *op)  {mutate_binary_operator(op);}
    void visit(const LE *op)  {mutate_binary_operator(op);}
    void visit(const GT *op)  {mutate_binary_operator(op);}
    void visit(const GE *op)  {mutate_binary_operator(op);}
    void visit(const And *op) {mutate_binary_operator(op);}
    void visit(const Or *op)  {mutate_binary_operator(op);}

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (condition.same_as(op->condition) &&
            true_value.same_as(op->true_value) &&
            false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            int lanes = std::max(true_value.type().lanes(), false_value.type().lanes());
            lanes = std::max(lanes, condition.type().lanes());
            // Widen the true and false values, but we don't have to widen the condition
            true_value = widen(true_value, lanes);
            false_value = widen(false_value, lanes);
            expr = Select::make(condition, true_value, false_value);
        }
    }

    void visit(const Load *op) {
        Expr index = mutate(op->index);

        if (index.same_as(op->index)) {
            expr = op;
        } else {
            int w = index.type().lanes();
            expr = Load::make(op->type.with_lanes(w), op->name, index, op->image, op->param);
        }
    }

    void visit(const Call *op) {
        // Widen the call by changing the lanes of all of its
        // arguments and its return type
        vector<Expr> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        int max_lanes = 0;
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
            max_lanes = std::max(new_arg.type().lanes(), max_lanes);
        }

        if (!changed) {
            expr = op;
        } else {
            // Widen the args to have the same lanes as the max lanes found
            for (size_t i = 0; i < new_args.size(); i++) {
                new_args[i] = widen(new_args[i], max_lanes);
            }
            expr = Call::make(op->type.with_lanes(max_lanes), op->name, new_args,
                              op->call_type, op->func, op->value_index, op->image, op->param);
        }
    }

    void visit(const Let *op) {

        // Vectorize the let value and check to see if it was vectorized by
        // this mutator. The type of the expression might already be vector
        // width.
        Expr mutated_value = mutate(op->value);
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        // If the value was vectorized by this mutator, add a new name to
        // the scope for the vectorized value expression.
        std::string vectorized_name;
        if (was_vectorized) {
            vectorized_name = op->name + widening_suffix;
            scope.push(op->name, mutated_value);
        }

        Expr mutated_body = mutate(op->body);

        if (mutated_value.same_as(op->value) &&
            mutated_body.same_as(op->body)) {
            expr = op;
        } else if (was_vectorized) {
            scope.pop(op->name);
            expr = Let::make(vectorized_name, mutated_value, mutated_body);
        } else {
            expr = Let::make(op->name, mutated_value, mutated_body);
        }
    }

    void visit(const LetStmt *op) {
        Expr mutated_value = mutate(op->value);
        std::string mutated_name = op->name;

        // Check if the value was vectorized by this mutator.
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        if (was_vectorized) {
            mutated_name += widening_suffix;
            scope.push(op->name, mutated_value);
            // Also keep track of the original let, in case inner code scalarizes.
            containing_lets.push_back({op->name, op->value});
        }


        Stmt mutated_body = mutate(op->body);

        if (was_vectorized) {
            containing_lets.pop_back();
            scope.pop(op->name);

            // Inner code might have extracted my lanes using
            // extract_lane, which introduces a shuffle_vector. If
            // so we should define separate lets for the lanes and
            // get it to use those instead.
            mutated_body = ReplaceShuffleVectors(mutated_name).mutate(mutated_body);

            // Check if inner code wants my individual lanes.
            Type t = mutated_value.type();
            for (int i = 0; i < t.lanes(); i++) {
                string lane_name = mutated_name + ".lane." + std::to_string(i);
                if (stmt_uses_var(mutated_body, lane_name)) {
                    mutated_body =
                        LetStmt::make(lane_name, extract_lane(mutated_value, i), mutated_body);
                }
            }

            // Inner code may also have wanted my max or min lane
            bool uses_min_lane = stmt_uses_var(mutated_body, mutated_name + ".min_lane");
            bool uses_max_lane = stmt_uses_var(mutated_body, mutated_name + ".max_lane");

            if (uses_min_lane || uses_max_lane) {
                Interval i = bounds_of_lanes(mutated_value);

                if (uses_min_lane) {
                    mutated_body =
                        LetStmt::make(mutated_name + ".min_lane", i.min, mutated_body);
                }

                if (uses_max_lane) {
                    mutated_body =
                        LetStmt::make(mutated_name + ".max_lane", i.max, mutated_body);
                }
            }
        }

        if (mutated_value.same_as(op->value) &&
            mutated_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(mutated_name, mutated_value, mutated_body);
        }
    }

    void visit(const Provide *op) {
        vector<Expr> new_args(op->args.size());
        vector<Expr> new_values(op->values.size());
        bool changed = false;

        // Mutate the args
        int max_lanes = 0;
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
            max_lanes = std::max(new_arg.type().lanes(), max_lanes);
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_value = op->values[i];
            Expr new_value = mutate(old_value);
            if (!new_value.same_as(old_value)) changed = true;
            new_values[i] = new_value;
            max_lanes = std::max(new_value.type().lanes(), max_lanes);
        }

        if (!changed) {
            stmt = op;
        } else {
            // Widen the args to have the same lanes as the max lanes found
            for (size_t i = 0; i < new_args.size(); i++) {
                new_args[i] = widen(new_args[i], max_lanes);
            }
            for (size_t i = 0; i < new_values.size(); i++) {
                new_values[i] = widen(new_values[i], max_lanes);
            }
            stmt = Provide::make(op->name, new_values, new_args);
        }
    }

    void visit(const Store *op) {
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        if (value.same_as(op->value) && index.same_as(op->index)) {
            stmt = op;
        } else {
            int lanes = std::max(value.type().lanes(), index.type().lanes());
            stmt = Store::make(op->name, widen(value, lanes), widen(index, lanes), op->param);
        }
    }

    void visit(const AssertStmt *op) {
        if (op->condition.type().lanes() > 1) {
            stmt = scalarize(op);
        } else {
            stmt = op;
        }
    }

    void visit(const IfThenElse *op) {
        Expr cond = mutate(op->condition);
        int lanes = cond.type().lanes();
        debug(3) << "Vectorizing over " << var << "\n"
                 << "Old: " << op->condition << "\n"
                 << "New: " << cond << "\n";
        if (lanes > 1) {
            // We have an if statement with a vector condition,
            // which would mean control flow divergence within the
            // SIMD lanes.

            // First check if the condition is marked as likely.
            const Call *c = cond.as<Call>();
            if (c && (c->is_intrinsic(Call::likely) ||
                      c->is_intrinsic(Call::likely_if_innermost))) {

                // The meaning of the likely intrinsic is that
                // Halide should optimize for the case in which
                // *every* likely value is true. We can do that by
                // generating a scalar condition that checks if
                // the least-true lane is true.
                Expr all_true = bounds_of_lanes(c->args[0]).min;

                // Wrap it in the same flavor of likely
                all_true = Call::make(Bool(), c->name,
                                      {all_true}, Call::PureIntrinsic);

                // We should strip the likelies from the case
                // that's going to scalarize, because it's no
                // longer likely.
                Stmt without_likelies =
                    IfThenElse::make(op->condition.as<Call>()->args[0],
                                     op->then_case, op->else_case);

                stmt =
                    IfThenElse::make(all_true,
                                     mutate(op->then_case),
                                     scalarize(without_likelies));
            } else {
                // It's some arbitrary vector condition. Scalarize
                // it.
                stmt = scalarize(op);
            }
        } else {
            // It's an if statement on a scalar, we're ok to vectorize the innards.
            debug(3) << "Not scalarizing if then else\n";
            Stmt then_case = mutate(op->then_case);
            Stmt else_case = mutate(op->else_case);
            if (cond.same_as(op->condition) &&
                then_case.same_as(op->then_case) &&
                else_case.same_as(op->else_case)) {
                stmt = op;
            } else {
                stmt = IfThenElse::make(cond, then_case, else_case);
            }
        }
    }

    void visit(const For *op) {
        ForType for_type = op->for_type;
        if (for_type == ForType::Vectorized) {
            user_warning << "Warning: Encountered vector for loop over " << op->name
                         << " inside vector for loop over " << var << "."
                         << " Ignoring the vectorize directive for the inner for loop.\n";
            for_type = ForType::Serial;
        }

        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);

        Stmt body = op->body;

        if (min.type().is_vector()) {
            // Rebase the loop to zero and try again
            Expr var = Variable::make(Int(32), op->name);
            Stmt body = substitute(op->name, var + op->min, op->body);
            Stmt transformed = For::make(op->name, 0, op->extent, for_type, op->device_api, body);
            stmt = mutate(transformed);
            return;
        }

        if (extent.type().is_vector()) {
            // We'll iterate up to the max over the lanes, but
            // inject an if statement inside the loop that stops
            // each lane from going too far.

            extent = bounds_of_lanes(extent).max;
            Expr var = Variable::make(Int(32), op->name);
            body = IfThenElse::make(likely(var < op->min + op->extent), body);
        }

        body = mutate(body);

        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body) &&
            for_type == op->for_type) {
            stmt = op;
        } else {
            stmt = For::make(op->name, min, extent, for_type, op->device_api, body);
        }
    }

    void visit(const Allocate *op) {
        std::vector<Expr> new_extents;
        Expr new_expr;

        int lanes = replacement.type().lanes();

        // The new expanded dimension is innermost.
        new_extents.push_back(lanes);

        for (size_t i = 0; i < op->extents.size(); i++) {
            Expr extent = mutate(op->extents[i]);
            // For vector sizes, take the max over the lanes. Note
            // that we haven't changed the strides, which also may
            // vary per lane. This is a bit weird, but the way we
            // set up the vectorized memory means that lanes can't
            // clobber each others' memory, so it doesn't matter.
            if (extent.type().is_vector()) {
                extent = bounds_of_lanes(extent).max;
            }
            new_extents.push_back(extent);
        }

        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
            user_assert(new_expr.type().is_scalar())
                << "Cannot vectorize an allocation with a varying new_expr per vector lane.\n";
        }

        Stmt body = op->body;

        // Rewrite loads and stores to this allocation like so:
        // foo[x] -> foo[x*lanes + var]
        body = RewriteAccessToVectorAlloc(var, op->name, lanes).mutate(body);

        body = mutate(body);

        stmt = Allocate::make(op->name, op->type, new_extents, op->condition, body, new_expr, op->free_function);
    }

    Stmt scalarize(Stmt s) {
        // Wrap a serial loop around it. Maybe LLVM will have
        // better luck vectorizing it.

        // We'll need the original scalar versions of any containing lets.
        for (size_t i = containing_lets.size(); i > 0; i--) {
            const auto &l = containing_lets[i-1];
            s = LetStmt::make(l.first, l.second, s);
        }

        int lanes = replacement.type().lanes();
        s = For::make(var, 0, lanes, ForType::Serial, DeviceAPI::None, s);

        return s;
    }

    Expr scalarize(Expr e) {
        // This method returns a select tree that produces a vector lanes
        // result expression

        Expr result;
        int lanes = replacement.type().lanes();

        for (int i = lanes - 1; i >= 0; --i) {
            // Hide all the vector let values in scope with a scalar version
            // in the appropriate lane.
            for (Scope<Expr>::iterator iter = scope.begin(); iter != scope.end(); ++iter) {
                string name = iter.name() + ".lane." + std::to_string(i);
                Expr lane = extract_lane(iter.value(), i);
                e = substitute(iter.name(), Variable::make(lane.type(), name), e);
            }

            // Replace uses of the vectorized variable with the extracted
            // lane expression
            e = substitute(var, i, e);

            if (i == lanes - 1) {
                result = Broadcast::make(e, lanes);
            } else {
                Expr cond = (replacement == Broadcast::make(i, lanes));
                result = Select::make(cond, Broadcast::make(e, lanes), result);
            }
        }

        debug(0) << e << " -> " << result << "\n";

        return result;
    }

public:
    VectorSubs(string v, Expr r) : var(v), replacement(r) {
        widening_suffix = ".x" + std::to_string(replacement.type().lanes());
    }
};

// Vectorize all loops marked as such in a Stmt
class VectorizeLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *for_loop) {
        if (for_loop->for_type == ForType::Vectorized) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            if (!extent || extent->value <= 1) {
                user_error << "Loop over " << for_loop->name
                           << " has extent " << for_loop->extent
                           << ". Can only vectorize loops over a "
                           << "constant extent > 1\n";
            }

            Expr for_var = Variable::make(Int(32), for_loop->name);

            Stmt body = for_loop->body;
            if (!is_zero(for_loop->min)) {
                Expr adjusted = for_var + for_loop->min;
                body = substitute(for_loop->name, adjusted, for_loop->body);
            }

            // Replace the var with a ramp within the body
            Expr replacement = Ramp::make(0, 1, extent->value);
            stmt = VectorSubs(for_loop->name, replacement).mutate(body);
        } else {
            IRMutator::visit(for_loop);
        }
    }

};

} // Anonymous namespace

Stmt vectorize_loops(Stmt s) {
    return VectorizeLoops().mutate(s);
}

}
}
