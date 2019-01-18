#include "RemoveUndef.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::vector;

class RemoveUndef : public IRMutator2 {
public:
    Expr predicate;
private:
    using IRMutator2::visit;

    Scope<> dead_vars;

    Expr visit(const Variable *op) override {
        if (dead_vars.contains(op->name)) {
            return Expr();
        } else {
            return op;
        }
    }

    template<typename T>
    Expr mutate_binary_operator(const T *op) {
        Expr a = mutate(op->a);
        if (!a.defined()) return Expr();
        Expr b = mutate(op->b);
        if (!b.defined()) return Expr();
        if (a.same_as(op->a) &&
            b.same_as(op->b)) {
            return op;
        } else {
            return T::make(std::move(a), std::move(b));
        }
    }

    Expr visit(const Cast *op) override {
        Expr value = mutate(op->value);
        if (!value.defined()) return Expr();
        if (value.same_as(op->value)) {
            return op;
        } else {
            return Cast::make(op->type, std::move(value));
        }
    }

    Expr visit(const Add *op) override {return mutate_binary_operator(op);}
    Expr visit(const Sub *op) override {return mutate_binary_operator(op);}
    Expr visit(const Mul *op) override {return mutate_binary_operator(op);}
    Expr visit(const Div *op) override {return mutate_binary_operator(op);}
    Expr visit(const Mod *op) override {return mutate_binary_operator(op);}
    Expr visit(const Min *op) override {return mutate_binary_operator(op);}
    Expr visit(const Max *op) override {return mutate_binary_operator(op);}
    Expr visit(const EQ *op)  override {return mutate_binary_operator(op);}
    Expr visit(const NE *op)  override {return mutate_binary_operator(op);}
    Expr visit(const LT *op)  override {return mutate_binary_operator(op);}
    Expr visit(const LE *op)  override {return mutate_binary_operator(op);}
    Expr visit(const GT *op)  override {return mutate_binary_operator(op);}
    Expr visit(const GE *op)  override {return mutate_binary_operator(op);}
    Expr visit(const And *op) override {return mutate_binary_operator(op);}
    Expr visit(const Or *op)  override {return mutate_binary_operator(op);}

    Expr visit(const Not *op) override {
        Expr a = mutate(op->a);
        if (!a.defined()) return Expr();
        if (a.same_as(op->a)) {
            return op;
        } else {
            return Not::make(a);
        }
    }

    Expr visit(const Select *op) override {
        Expr cond = mutate(op->condition);
        Expr t = mutate(op->true_value);
        Expr f = mutate(op->false_value);

        if (!cond.defined()) {
            return Expr();
        }

        if (!t.defined() && !f.defined()) {
            return Expr();
        }

        if (!t.defined()) {
            // Swap the cases so that we only need to deal with the
            // case when false is not defined below.
            cond = Not::make(cond);
            t = f;
            f = Expr();
        }

        if (!f.defined()) {
            // We need to convert this to an if-then-else
            if (predicate.defined()) {
                predicate = predicate && cond;
            } else {
                predicate = cond;
            }
            return t;
        } else if (cond.same_as(op->condition) &&
            t.same_as(op->true_value) &&
            f.same_as(op->false_value)) {
            return op;
        } else {
            return Select::make(cond, t, f);
        }
    }

    Expr visit(const Load *op) override {
        Expr pred = mutate(op->predicate);
        if (!pred.defined()) return Expr();
        Expr index = mutate(op->index);
        if (!index.defined()) return Expr();
        if (pred.same_as(op->predicate) && index.same_as(op->index)) {
            return op;
        } else {
            return Load::make(op->type, op->name, index, op->image, op->param, pred, op->alignment);
        }
    }

    Expr visit(const Ramp *op) override {
        Expr base = mutate(op->base);
        if (!base.defined()) return Expr();
        Expr stride = mutate(op->stride);
        if (!stride.defined()) return Expr();
        if (base.same_as(op->base) &&
            stride.same_as(op->stride)) {
            return op;
        } else {
            return Ramp::make(base, stride, op->lanes);
        }
    }

    Expr visit(const Broadcast *op) override {
        Expr value = mutate(op->value);
        if (!value.defined()) return Expr();
        if (value.same_as(op->value)) {
            return op;
        } else {
            return Broadcast::make(value, op->lanes);
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::undef)) {
            return Expr();
        }

        vector<Expr> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.defined()) return Expr();
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        if (!changed) {
            return op;
        } else {
            return Call::make(op->type, op->name, new_args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        if (!value.defined()) {
            dead_vars.push(op->name);
        }
        Expr body = mutate(op->body);
        if (!value.defined()) {
            dead_vars.pop(op->name);
        }
        if (!body.defined()) return Expr();
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            return op;
        } else if (!value.defined()) {
            return body;
        } else {
            Expr expr = Let::make(op->name, value, body);
            predicate = substitute(op->name, value, predicate);
            return expr;
        }
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        if (!value.defined()) {
            dead_vars.push(op->name);
        }
        Stmt body = mutate(op->body);
        if (!value.defined()) {
            dead_vars.pop(op->name);
        }
        if (!body.defined()) return Stmt();
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            return op;
        } else if (!value.defined()) {
            return body;
        } else {
            return LetStmt::make(op->name, value, body);
        }
    }

    Stmt visit(const AssertStmt *op) override {
        Expr condition = mutate(op->condition);
        if (!condition.defined()) {
            return Stmt();
        }

        Expr message = mutate(op->message);
        if (!message.defined()) {
            return Stmt();
        }

        if (condition.same_as(op->condition) && message.same_as(op->message)) {
            return op;
        } else {
            return AssertStmt::make(condition, message);
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);
        if (!body.defined()) return Stmt();
        if (body.same_as(op->body)) {
            return op;
        } else {
            return ProducerConsumer::make(op->name, op->is_producer, body);
        }
    }

    Stmt visit(const For *op) override {
        Expr min = mutate(op->min);
        if (!min.defined()) {
            return Stmt();
        }
        Expr extent = mutate(op->extent);
        if (!extent.defined()) {
            return Stmt();
        }
        Stmt body = mutate(op->body);
        if (!body.defined()) return Stmt();
        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            return op;
        } else {
            return For::make(op->name, min, extent, op->for_type, op->device_api, body);
        }
    }

    Stmt visit(const Store *op) override {
        predicate = Expr();

        Expr pred = mutate(op->predicate);
        Expr value = mutate(op->value);
        if (!value.defined()) {
            return Stmt();
        }

        Expr index = mutate(op->index);
        if (!index.defined()) {
            return Stmt();
        }

        if (predicate.defined()) {
            // This becomes a conditional store
            Stmt stmt = IfThenElse::make(predicate, Store::make(op->name, value, index, op->param, pred, op->alignment));
            predicate = Expr();
            return stmt;
        } else if (pred.same_as(op->predicate) &&
                   value.same_as(op->value) &&
                   index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, pred, op->alignment);
        }
    }

    Stmt visit(const Provide *op) override {
        predicate = Expr();

        vector<Expr> new_args(op->args.size());
        vector<Expr> new_values(op->values.size());
        vector<Expr> args_predicates;
        vector<Expr> values_predicates;
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            predicate = Expr();
            Expr new_arg = mutate(old_arg);
            if (!new_arg.defined()) {
                return Stmt();
            }
            args_predicates.push_back(predicate);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        for (size_t i = 1; i < args_predicates.size(); i++) {
            user_assert(equal(args_predicates[i-1], args_predicates[i]))
                << "Conditionally-undef args in a Tuple should have the same conditions\n"
                << "  Condition " << i-1 << ": " << args_predicates[i-1] << "\n"
                << "  Condition " << i << ": " << args_predicates[i] << "\n";
        }

        bool all_values_undefined = true;
        for (size_t i = 0; i < op->values.size(); i++) {
            Expr old_value = op->values[i];
            predicate = Expr();
            Expr new_value = mutate(old_value);
            if (!new_value.defined()) {
                new_value = undef(old_value.type());
            } else {
                all_values_undefined = false;
                values_predicates.push_back(predicate);
            }
            if (!new_value.same_as(old_value)) changed = true;
            new_values[i] = new_value;
        }

        if (all_values_undefined) {
            return Stmt();
        }

        for (size_t i = 1; i < values_predicates.size(); i++) {
            user_assert(equal(values_predicates[i-1], values_predicates[i]))
                << "Conditionally-undef values in a Tuple should have the same conditions\n"
                << "  Condition " << i-1 << ": " << values_predicates[i-1] << "\n"
                << "  Condition " << i << ": " << values_predicates[i] << "\n";
        }

        if (predicate.defined()) {
            Stmt stmt = IfThenElse::make(predicate, Provide::make(op->name, new_values, new_args));
            predicate = Expr();
            return stmt;
        } else if (!changed) {
            return op;
        } else {
            return Provide::make(op->name, new_values, new_args);
        }
    }

    Stmt visit(const Allocate *op) override {
        std::vector<Expr> new_extents;
        bool all_extents_unmodified = true;
        for (size_t i = 0; i < op->extents.size(); i++) {
            new_extents.push_back(mutate(op->extents[i]));
            if (!new_extents.back().defined()) {
                return Stmt();
            }
            all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
        }
        Stmt body = mutate(op->body);
        if (!body.defined()) return Stmt();

        Expr condition = mutate(op->condition);
        if (!condition.defined()) return Stmt();

        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }

        if (all_extents_unmodified &&
            body.same_as(op->body) &&
            condition.same_as(op->condition) &&
            new_expr.same_as(op->new_expr)) {
            return op;
        } else {
            return Allocate::make(op->name, op->type, op->memory_type,
                                  new_extents, condition, body, new_expr, op->free_function);
        }
    }

    Stmt visit(const Free *op) override {
        return op;
    }

    Stmt visit(const Realize *op) override {
        Region new_bounds(op->bounds.size());
        bool bounds_changed = false;

        // Mutate the bounds
        for (size_t i = 0; i < op->bounds.size(); i++) {
            Expr old_min    = op->bounds[i].min;
            Expr old_extent = op->bounds[i].extent;
            Expr new_min    = mutate(old_min);
            if (!new_min.defined()) {
                return Stmt();
            }
            Expr new_extent = mutate(old_extent);
            if (!new_extent.defined()) {
                return Stmt();
            }
            if (!new_min.same_as(old_min))       bounds_changed = true;
            if (!new_extent.same_as(old_extent)) bounds_changed = true;
            new_bounds[i] = Range(new_min, new_extent);
        }

        Stmt body = mutate(op->body);
        if (!body.defined()) return Stmt();

        Expr condition = mutate(op->condition);
        if (!condition.defined()) return Stmt();

        if (!bounds_changed &&
            body.same_as(op->body) &&
            condition.same_as(op->condition)) {
            return op;
        } else {
            return Realize::make(op->name, op->types, op->memory_type, new_bounds, condition, body);
        }
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (!first.defined()) {
            return rest;
        } else if (!rest.defined()) {
            return first;
        } else if (first.same_as(op->first) &&
                   rest.same_as(op->rest)) {
            return op;
        } else {
            return Block::make(first, rest);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);
        if (!condition.defined()) {
            return Stmt();
        }
        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (!then_case.defined() && !else_case.defined()) {
            return Stmt();
        }

        if (!then_case.defined()) {
            condition = Not::make(condition);
            then_case = else_case;
            else_case = Stmt();
        }

        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(condition, then_case, else_case);
        }
    }

    Stmt visit(const Evaluate *op) override {
        Expr v = mutate(op->value);
        if (!v.defined()) {
            return Stmt();
        } else if (v.same_as(op->value)) {
            return op;
        } else {
            return Evaluate::make(v);
        }
    }
};

Stmt remove_undef(Stmt s) {
    RemoveUndef r;
    s = r.mutate(s);
    internal_assert(!r.predicate.defined())
        << "Undefined expression leaked outside of a Store node: "
        << r.predicate << "\n";
    return s;
}

}  // namespace Internal
}  // namespace Halide
