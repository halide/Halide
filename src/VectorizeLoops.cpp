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

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class VectorizeLoops : public IRMutator {
    class VectorSubs : public IRMutator {
        string var;
        Expr replacement;
        string widening_suffix;
        Scope<Expr> scope;
        Scope<int> vectorized_allocations;

        bool scalarized;
        int scalar_lane;

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

            if (scalarized) {
                // When we're scalarized, we were supposed to hide all
                // the vector vars in scope.
                internal_assert(expr.type().is_scalar()) << op->name << " -> " << expr << "\n";
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

            // Internal allocations always get vectorized.
            if (vectorized_allocations.contains(op->name)) {
                int lanes = replacement.type().lanes();
                if (index.type().is_scalar()) {
                    Expr lanes_expr = make_const(index.type(), lanes);
                    if (scalarized) {
                        index = Add::make(Mul::make(index, lanes_expr), scalar_lane);
                    } else {
                        index = Ramp::make(Mul::make(index, lanes_expr), make_one(index.type()), lanes);
                    }
                } else {
                    internal_assert(!scalarized);
                    Type scalar_index_type = index.type().with_lanes(1);
                    Expr lanes_expr = make_const(scalar_index_type, lanes);
                    index = Mul::make(index, Broadcast::make(lanes_expr, lanes));
                    index = Add::make(index, Ramp::make(make_zero(scalar_index_type), make_one(scalar_index_type), lanes));
                }
            }

            if (index.same_as(op->index)) {
                expr = op;
            } else {
                int w = index.type().lanes();
                expr = Load::make(op->type.with_lanes(w), op->name, index, op->image, op->param);
            }
        }

        void visit(const Call *op) {

            // The shuffle_vector intrinsic has a different argument passing
            // convention than the rest of Halide. Instead of passing widened
            // expressions, individual scalar expressions for each lane are
            // passed as a variable number of arguments to the intrinisc.
            if (op->is_intrinsic(Call::shuffle_vector)) {

                int replacement_lanes = replacement.type().lanes();
                int shuffle_lanes = op->type.lanes();

                internal_assert(shuffle_lanes == (int)op->args.size() - 1);

                // To widen successfully, the intrinisic must either produce a
                // vector width result or a scalar result that we can broadcast.
                if (shuffle_lanes == 1) {

                    // Check to see if the shuffled expression contains the
                    // vectorized dimension variable. Since vectorization will
                    // change the vectorized dimension loop to a single value
                    // let, we need to eliminate any use of the vectorized
                    // dimension variable inside the shuffled expression
                    Expr shuffled_expr = op->args[0];

                    bool has_scalarized_expr = expr_uses_var(shuffled_expr,var);
                    if (has_scalarized_expr) {
                        shuffled_expr = scalarize(op);
                    }

                    // Otherwise, the shuffle produces a scalar result and has
                    // only one channel selector argument which must be scalar
                    internal_assert(op->args.size() == 2);
                    internal_assert(op->args[1].type().lanes() == 1);
                    Expr mutated_channel = mutate(op->args[1]);
                    int mutated_lanes = mutated_channel.type().lanes();

                    // Determine how to mutate the intrinsic. If the mutated
                    // channel expression matches the for-loop variable
                    // replacement exactly, and is the same lanes as the
                    // existing vector expression, then we can remove the
                    // shuffle_vector intrinisic and return the vector
                    // expression it contains directly.
                    if (equal(replacement,mutated_channel) &&
                        (shuffled_expr.type().lanes() == replacement_lanes)) {

                        // Note that we stop mutating at this expression. Any
                        // unvectorized variables inside this argument may
                        // continue to refer to scalar expressions in the
                        // enclosing lets whose names have not changed.
                        expr = shuffled_expr;

                    } else if (mutated_lanes == replacement_lanes) {
                        // Otherwise, if the mutated channel width matches the
                        // vectorized width but the expression is not a simple
                        // ramp across the whole width, then convert the channel
                        // expression into shuffle_vector intrinsic arguments.
                        vector<Expr> new_args(1+replacement_lanes);

                        // Append the vector expression as the first argument
                        new_args[0] = shuffled_expr;

                        // Extract each channel of the mutated channel
                        // expression, each is passed as a separate argument to
                        // shuffle_vector
                        for (int i = 0; i != replacement_lanes; ++i) {
                            new_args[1 + i] = extract_lane(mutated_channel, i);
                        }

                        expr = Call::make(op->type.with_lanes(replacement_lanes),
                                          op->name, new_args, op->call_type,
                                          op->func, op->value_index, op->image,
                                          op->param);

                    } else if (has_scalarized_expr) {
                        internal_assert(mutated_lanes == 1);
                        // Otherwise, the channel expression is independent of
                        // the dimension being vectorized, but the shuffled
                        // expression is not. Scalarize the whole node including
                        // the independent channel expression.
                        expr = scalarize(op);
                    } else {
                        internal_assert(mutated_lanes == 1);
                        // Otherwise, both the shuffled expression and the
                        // channel expressions of this shuffle_vector is
                        // independent of the dimension being vectorized
                        expr = op;
                    }

                } else {
                    // If the shuffle_vector result is not a scalar there are no
                    // rules for how to widen it.
                    internal_error << "Mismatched vector lanes in VectorSubs for shuffle_vector\n";
                }

            } else {
                // Otherwise, widen the call by changing the lanes of all of its
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
        }

        void visit(const Let *op) {

            // Vectorize the let value and check to see if it was vectorized by
            // this mutator. The type of the expression might already be vector
            // width.
            Expr mutated_value = mutate(op->value);
            bool was_vectorized = !op->value.type().is_vector() &&
                                   mutated_value.type().is_vector();

            // If the value was vectorized by this mutator, add a new name to
            // the scope for the vectorized value expression.
            std::string vectorized_name;
            if (was_vectorized) {
                vectorized_name = op->name + widening_suffix;
                scope.push(op->name, mutated_value);
            }

            Expr mutated_body = mutate(op->body);

            if (was_vectorized) {
                scope.pop(op->name);
            }

            // Check to see if the value and body were modified by this mutator
            if (mutated_value.same_as(op->value) &&
                mutated_body.same_as(op->body)) {
                expr = op;
            } else if (scalarized) {
                expr = Let::make(op->name, mutated_value, mutated_body);
            } else {
                // Otherwise create a new Let containing the original value
                // expression plus the widened expression
                expr = Let::make(op->name, op->value, mutated_body);

                if (was_vectorized) {
                    expr = Let::make(vectorized_name, mutated_value, expr);
                }
            }
        }

        void visit(const LetStmt *op) {
            Expr mutated_value = mutate(op->value);

            // Check if the value was vectorized by this mutator.
            bool was_vectorized = (!op->value.type().is_vector() &&
                                   mutated_value.type().is_vector());

            std::string vectorized_name;

            if (was_vectorized) {
                vectorized_name = op->name + widening_suffix;
                scope.push(op->name, mutated_value);
            }

            Stmt mutated_body = mutate(op->body);

            if (was_vectorized) {
                scope.pop(op->name);
            }

            if (mutated_value.same_as(op->value) &&
                mutated_body.same_as(op->body)) {
                stmt = op;
            } else if (scalarized) {
                internal_assert(!was_vectorized);
                stmt = LetStmt::make(op->name, mutated_value, mutated_body);
            } else {
                stmt = LetStmt::make(op->name, op->value, mutated_body);

                if (was_vectorized) {
                    stmt = LetStmt::make(vectorized_name, mutated_value, stmt);
                }
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
            // Internal allocations always get vectorized.
            if (vectorized_allocations.contains(op->name)) {
                int lanes = replacement.type().lanes();
                if (index.type().is_scalar()) {
                    Expr lanes_expr = make_const(index.type(), lanes);
                    if (scalarized) {
                        index = Add::make(Mul::make(index, lanes_expr), scalar_lane);
                    } else {
                        index = Ramp::make(Mul::make(index, lanes_expr), make_one(index.type()), lanes);
                    }
                } else {
                    internal_assert(!scalarized);
                    Type scalar_index_type = index.type().with_lanes(1);
                    Expr lanes_expr = make_const(scalar_index_type, lanes);
                    index = Mul::make(index, Broadcast::make(lanes_expr, lanes));
                    index = Add::make(index, Ramp::make(make_zero(scalar_index_type), make_one(scalar_index_type), lanes));
                }
            }

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
                // It's an if statement on a vector of
                // conditions. We'll have to scalarize and make
                // multiple copies of the if statement.
                debug(3) << "Scalarizing if then else\n";
                stmt = scalarize(op);
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
                std::cerr << "Warning: Encountered vector for loop over " << op->name
                          << " inside vector for loop over " << var << "."
                          << " Ignoring the vectorize directive for the inner for loop.\n";
                for_type = ForType::Serial;
            }

            Expr min = mutate(op->min);
            Expr extent = mutate(op->extent);
            user_assert(extent.type().is_scalar())
                << "Can't vectorize a for loop with an extent that varies per vector "
                << "lane. This is probably caused by scheduling something inside a "
                << "vectorized dimension.";

            if (min.type().is_vector()) {
                // Rebase the loop to zero and try again
                Expr var = Variable::make(Int(32), op->name);
                Stmt body = substitute(op->name, var + op->min, op->body);
                Stmt transformed = For::make(op->name, 0, op->extent, for_type, op->device_api, body);
                stmt = mutate(transformed);
                return;
            }

            Stmt body = mutate(op->body);

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

            // The new expanded dimension is innermost.
            if (!scalarized) {
                new_extents.push_back(Expr(replacement.type().lanes()));
            }

            for (size_t i = 0; i < op->extents.size(); i++) {
                new_extents.push_back(mutate(op->extents[i]));
                // Only support scalar sizes for now. For vector sizes, we
                // would need to take the horizontal max to convert to a
                // scalar size.
                user_assert(new_extents[i].type().is_scalar())
                    << "Cannot vectorize an allocation with a varying size per vector lane.\n";
            }

            if (op->new_expr.defined()) {
                new_expr = mutate(op->new_expr);
            }
            if (op->new_expr.defined()) {
                user_assert(new_expr.type().is_scalar())
                    << "Cannot vectorize an allocation with a varying new_expr per vector lane.\n";
            }

            if (!scalarized) {
                // Rewrite loads and stores to this allocation like so (this works for scalars and vectors):
                // foo[x] -> foo[x*lanes + ramp(0, 1, lanes)]
                vectorized_allocations.push(op->name, 0);
            }
            Stmt body = mutate(op->body);
            if (!scalarized) {
                vectorized_allocations.pop(op->name);
            }
            stmt = Allocate::make(op->name, op->type, new_extents, op->condition, body, new_expr, op->free_function);
        }

        Stmt scalarize(Stmt s) {
            Stmt result;
            int lanes = replacement.type().lanes();
            Expr old_replacement = replacement;
            internal_assert(!scalarized);
            scalarized = true;

            for (int i = 0; i < lanes; i++) {
                // Replace the var with a scalar version in the appropriate lane.
                replacement = extract_lane(old_replacement, i);
                scalar_lane = i;

                Stmt new_stmt = s;

                // Hide all the vectors in scope with a scalar version
                // in the appropriate lane.
                for (Scope<Expr>::iterator iter = scope.begin(); iter != scope.end(); ++iter) {
                    string name = iter.name() + ".lane." + std::to_string(i);
                    Expr lane = extract_lane(iter.value(), i);
                    new_stmt = substitute(iter.name(), Variable::make(lane.type(), name), new_stmt);
                    new_stmt = LetStmt::make(name, lane, new_stmt);
                }

                // Should only serve to rewrite access to internal allocations:
                // foo[x] -> foo[x * lanes + i]
                new_stmt = mutate(new_stmt);

                if (i == 0) {
                    result = new_stmt;
                } else {
                    result = Block::make(result, new_stmt);
                }
            }

            replacement = old_replacement;
            scalarized = false;

            return result;
        }

        Expr scalarize(Expr e) {
            // This method returns a select tree that produces a vector lanes
            // result expression

            // TODO: Add an intrinisic to create a vector from a list of scalars
            // instead of a select tree.

            Expr result;

            int lanes = replacement.type().lanes();
            Expr old_replacement = replacement;
            internal_assert(!scalarized);
            scalarized = true;

            for (int i = lanes - 1; i >= 0; --i) {
                // Extract a single lane from the vectorized variable expression
                // substituted in by this mutator
                replacement = extract_lane(old_replacement, i);
                scalar_lane = i;

                Expr new_expr = e;

                // Hide all the vector let values in scope with a scalar version
                // in the appropriate lane.
                for (Scope<Expr>::iterator iter = scope.begin(); iter != scope.end(); ++iter) {
                    string name = iter.name() + ".lane." + std::to_string(i);
                    Expr lane = extract_lane(iter.value(), i);
                    new_expr = substitute(iter.name(), Variable::make(lane.type(), name), new_expr);
                    new_expr = Let::make(name, lane, new_expr);
                }

                // Replace uses of the vectorized variable with the extracted
                // lane expression
                new_expr = substitute(var, scalar_lane, new_expr);

                if (i == lanes - 1) {
                    result = Broadcast::make(new_expr,lanes);
                } else {
                    Expr cond = (old_replacement == Broadcast::make(scalar_lane, lanes));
                    result = Select::make(cond, Broadcast::make(new_expr,lanes), result);
                }
            }

            replacement = old_replacement;
            scalarized = false;

            return result;
        }

    public:
        VectorSubs(string v, Expr r) : var(v), replacement(r),
                                       scalarized(false), scalar_lane(0) {

            std::ostringstream oss;
            widening_suffix = ".x" + std::to_string(replacement.type().lanes());
        }
    };

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

            // Replace the var with a ramp within the body
            Expr for_var = Variable::make(Int(32), for_loop->name);
            Expr replacement = Ramp::make(for_var, 1, extent->value);
            Stmt body = VectorSubs(for_loop->name, replacement).mutate(for_loop->body);

            // The for loop becomes a simple let statement
            stmt = LetStmt::make(for_loop->name, for_loop->min, body);

        } else {
            IRMutator::visit(for_loop);
        }
    }

};

Stmt vectorize_loops(Stmt s) {
    return VectorizeLoops().mutate(s);
}

}
}
