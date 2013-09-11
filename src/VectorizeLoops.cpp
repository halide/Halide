#include "VectorizeLoops.h"
#include "IRMutator.h"
#include "Scope.h"
#include "IRPrinter.h"
#include "Deinterleave.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class VectorizeLoops : public IRMutator {
    class VectorSubs : public IRMutator {
        string var;
        Expr replacement;
        Scope<Expr> scope;

        Expr widen(Expr e, int width) {
            if (e.type().width == width) return e;
            else if (e.type().width == 1) return Broadcast::make(e, width);
            else assert(false && "Mismatched vector widths in VectorSubs");
            return Expr();
        }

        using IRMutator::visit;

        virtual void visit(const Cast *op) {
            Expr value = mutate(op->value);
            if (value.same_as(op->value)) {
                expr = op;
            } else {
                Type t = op->type.vector_of(value.type().width);
                expr = Cast::make(t, value);
            }
        }

        virtual void visit(const Variable *op) {
            if (op->name == var) {
                expr = replacement;
            } else if (scope.contains(op->name)) {
                // The type of a var may have changed. E.g. if
                // we're vectorizing across x we need to know the
                // type of y has changed in the following example:
                // let y = x + 1 in y*3
                expr = Variable::make(scope.get(op->name).type(), op->name);
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
                int w = std::max(a.type().width, b.type().width);
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
                int width = std::max(true_value.type().width, false_value.type().width);
                width = std::max(width, condition.type().width);
                // Widen the true and false values, but we don't have to widen the condition
                true_value = widen(true_value, width);
                false_value = widen(false_value, width);
                expr = Select::make(condition, true_value, false_value);
            }
        }

        void visit(const Load *op) {
            Expr index = mutate(op->index);
            if (index.same_as(op->index)) {
                expr = op;
            } else {
                int w = index.type().width;
                expr = Load::make(op->type.vector_of(w), op->name, index, op->image, op->param);
            }
        }

        void visit(const Call *op) {
            vector<Expr> new_args(op->args.size());
            bool changed = false;

            // Mutate the args
            int max_width = 0;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr old_arg = op->args[i];
                Expr new_arg = mutate(old_arg);
                if (!new_arg.same_as(old_arg)) changed = true;
                new_args[i] = new_arg;
                max_width = std::max(new_arg.type().width, max_width);
            }

            if (!changed) {
                expr = op;
            } else {
                // Widen the args to have the same width as the max width found
                for (size_t i = 0; i < new_args.size(); i++) {
                    new_args[i] = widen(new_args[i], max_width);
                }
                expr = Call::make(op->type.vector_of(max_width), op->name, new_args,
                                  op->call_type, op->func, op->value_index, op->image, op->param);
            }
        }

        void visit(const Let *op) {
            Expr value = mutate(op->value);
            if (value.type().is_vector()) {
                scope.push(op->name, value);
            }

            Expr body = mutate(op->body);

            if (value.type().is_vector()) {
                scope.pop(op->name);
            }

            if (value.same_as(op->value) && body.same_as(op->body)) {
                expr = op;
            } else {
                expr = Let::make(op->name, value, body);
            }
        }

        void visit(const LetStmt *op) {
            Expr value = mutate(op->value);
            if (value.type().is_vector()) {
                scope.push(op->name, value);
            }

            Stmt body = mutate(op->body);

            if (value.type().is_vector()) {
                scope.pop(op->name);
            }

            if (value.same_as(op->value) && body.same_as(op->body)) {
                stmt = op;
            } else {
                stmt = LetStmt::make(op->name, value, body);
            }
        }

        void visit(const Provide *op) {
            vector<Expr> new_args(op->args.size());
            vector<Expr> new_values(op->values.size());
            bool changed = false;

            // Mutate the args
            int max_width = 0;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr old_arg = op->args[i];
                Expr new_arg = mutate(old_arg);
                if (!new_arg.same_as(old_arg)) changed = true;
                new_args[i] = new_arg;
                max_width = std::max(new_arg.type().width, max_width);
            }

            for (size_t i = 0; i < op->args.size(); i++) {
                Expr old_value = op->values[i];
                Expr new_value = mutate(old_value);
                if (!new_value.same_as(old_value)) changed = true;
                new_values[i] = new_value;
                max_width = std::max(new_value.type().width, max_width);
            }

            if (!changed) {
                stmt = op;
            } else {
                // Widen the args to have the same width as the max width found
                for (size_t i = 0; i < new_args.size(); i++) {
                    new_args[i] = widen(new_args[i], max_width);
                }
                for (size_t i = 0; i < new_values.size(); i++) {
                    new_values[i] = widen(new_values[i], max_width);
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
                int width = std::max(value.type().width, index.type().width);
                stmt = Store::make(op->name, widen(value, width), widen(index, width));
            }
        }

        void visit(const IfThenElse *op) {
            Expr cond = mutate(op->condition);
            int width = cond.type().width;
            debug(3) << "Vectorizing over " << var << "\n"
                     << "Old: " << op->condition << "\n"
                     << "New: " << cond << "\n";
            if (width > 1) {
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

        /*
        void visit(const AssertStmt *op) {
            Expr cond = mutate(op->condition);
            int width = cond.type().width;
            if (width > 1) {
                // Assert that all of the return values are true
                Expr check_all = Call::make(Bool(), Call::horizontal_and, vec(cond), Call::Intrinsic);
                stmt = AssertStmt::make(check_all, op->message);
            }
        }
        */

        Stmt scalarize(Stmt s) {
            Stmt result;
            int width = replacement.type().width;
            for (int i = 0; i < width; i++) {
                // Replace the var with a scalar version in the appropriate lane.
                Stmt new_stmt = substitute(var, extract_lane(replacement, i), s);

                // Hide all the vectors in scope with a scalar version
                // in the appropriate lane.
                for (Scope<Expr>::iterator iter = scope.begin(); iter != scope.end(); ++iter) {
                    string name = iter.name() + ".lane." + int_to_string(i);
                    Expr lane = extract_lane(iter.value(), i);
                    new_stmt = substitute(iter.name(), Variable::make(lane.type(), name), new_stmt);
                    new_stmt = LetStmt::make(name, lane, new_stmt);
                }

                if (i == 0) {
                    result = new_stmt;
                } else {
                    result = Block::make(result, new_stmt);
                }
            }
            return result;
        }

    public:
        VectorSubs(string v, Expr r) : var(v), replacement(r) {
        }
    };

    using IRMutator::visit;

    void visit(const For *for_loop) {
        if (for_loop->for_type == For::Vectorized) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            if (!extent || extent->value <= 1) {
                std::cerr << "Loop over " << for_loop->name
                          << " has extent " << for_loop->extent
                          << ". Can only vectorize loops over a "
                          << "constant extent > 1\n";
                assert(false);
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
