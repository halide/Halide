#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::string;

class IREquals : public IRVisitor {
public:
    int result;
    Expr expr;
    Stmt stmt;

    IREquals(Expr e) : result(0), expr(e) {
    }

    IREquals(Stmt s) : result(0), stmt(s) {
    }

    void visit(const IntImm *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;
        const IntImm *e = expr.as<IntImm>();
        if (e->value < op->value) {
            result = -1;
        } else if (e->value > op->value) {
            result = 1;
        }
    }

    void visit(const FloatImm *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;
        const FloatImm *e = expr.as<FloatImm>();
        if (e->value < op->value) {
            result = -1;
        } else if (e->value > op->value) {
            result = 1;
        }
    }

    void visit(const StringImm *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;
        const StringImm *e = expr.as<StringImm>();
        if (e->value < op->value) {
            result = -1;
        } else if (e->value > op->value) {
            result = 1;
        }
    }

    int compare_types(Type a, Type b) {
        if (a.code < b.code) {
            result = -1;
        } else if (a.code > b.code) {
            result = 1;
        } else if (a.bits < b.bits) {
            result = -1;
        } else if (a.bits > b.bits) {
            result = 1;
        } else if (a.width < b.width) {
            result = -1;
        } else if (a.width > b.width) {
            result = 1;
        }
        return result;
    }

    int compare_node_types(Expr a, Expr b) {
        const void *ta = a.ptr->type_info();
        const void *tb = b.ptr->type_info();
        if (ta < tb) {
            result = -1;
        } else if (ta > tb) {
            result = 1;
        }
        return compare_types(a.type(), b.type());
    }

    int compare_node_types(Stmt a, Stmt b) {
        const void *ta = a.ptr->type_info();
        const void *tb = b.ptr->type_info();
        if (ta < tb) {
            result = -1;
        } else if (ta > tb) {
            result = 1;
        }
        return result;
    }

    int compare_names(const string &a, const string &b) {
        result = a.compare(b);
        if (result < 0) result = -1;
        if (result > 0) result = 1;
        return result;
    }

    void visit(const Cast *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        expr = expr.as<Cast>()->value;
        op->value.accept(this);
    }

    void visit(const Variable *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Variable *e = expr.as<Variable>();

        compare_names(e->name, op->name);
    }

    template<typename T>
    void visit_binary_operator(const T *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const T *e = expr.as<T>();

        expr = e->a;
        op->a.accept(this);

        expr = e->b;
        op->b.accept(this);
    }

    void visit(const Add *op) {visit_binary_operator(op);}
    void visit(const Sub *op) {visit_binary_operator(op);}
    void visit(const Mul *op) {visit_binary_operator(op);}
    void visit(const Div *op) {visit_binary_operator(op);}
    void visit(const Mod *op) {visit_binary_operator(op);}
    void visit(const Min *op) {visit_binary_operator(op);}
    void visit(const Max *op) {visit_binary_operator(op);}
    void visit(const EQ *op) {visit_binary_operator(op);}
    void visit(const NE *op) {visit_binary_operator(op);}
    void visit(const LT *op) {visit_binary_operator(op);}
    void visit(const LE *op) {visit_binary_operator(op);}
    void visit(const GT *op) {visit_binary_operator(op);}
    void visit(const GE *op) {visit_binary_operator(op);}
    void visit(const And *op) {visit_binary_operator(op);}
    void visit(const Or *op) {visit_binary_operator(op);}

    void visit(const Not *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Not *e = expr.as<Not>();

        expr = e->a;
        op->a.accept(this);
    }

    void visit(const Select *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Select *e = expr.as<Select>();

        expr = e->condition;
        op->condition.accept(this);

        expr = e->true_value;
        op->true_value.accept(this);

        expr = e->false_value;
        op->false_value.accept(this);
    }

    void visit(const Load *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Load *e = expr.as<Load>();

        if (compare_names(op->name, e->name)) return;

        expr = e->index;
        op->index.accept(this);
    }

    void visit(const Ramp *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Ramp *e = expr.as<Ramp>();

        // No need to compare width because we already compared types

        expr = e->base;
        op->base.accept(this);

        expr = e->stride;
        op->stride.accept(this);
    }

    void visit(const Broadcast *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Broadcast *e = expr.as<Broadcast>();

        expr = e->value;
        op->value.accept(this);
    }

    void visit(const Call *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Call *e = expr.as<Call>();

        if (compare_names(e->name, op->name)) return;

        if (e->call_type < op->call_type) {
            result = -1;
        } else if (e->call_type > op->call_type) {
            result = 1;
        } else if (e->value_index < op->value_index) {
            result = -1;
        } else if (e->value_index > op->value_index) {
            result = 1;
        } else if (e->args.size() < op->args.size()) {
            result = -1;
        } else if (e->args.size() > op->args.size()) {
            result = 1;
        } else {
            for (size_t i = 0; (result == 0) && (i < e->args.size()); i++) {
                expr = e->args[i];
                op->args[i].accept(this);
            }
        }
    }

    void visit(const Let *op) {
        if (result || expr.same_as(op) || compare_node_types(expr, op)) return;

        const Let *e = expr.as<Let>();

        if (compare_names(e->name, op->name)) return;

        expr = e->value;
        op->value.accept(this);

        expr = e->body;
        op->body.accept(this);
    }

    void visit(const LetStmt *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const LetStmt *s = stmt.as<LetStmt>();

        if (compare_names(s->name, op->name)) return;

        expr = s->value;
        op->value.accept(this);

        stmt = s->body;
        op->body.accept(this);
    }

    void visit(const AssertStmt *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const AssertStmt *s = stmt.as<AssertStmt>();

        expr = s->condition;
        op->condition.accept(this);

        expr = s->message;
        op->message.accept(this);
    }

    void visit(const Pipeline *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Pipeline *s = stmt.as<Pipeline>();

        if (compare_names(s->name, op->name)) return;

        if (s->update.defined() && !op->update.defined()) {
            result = -1;
        } else if (!s->update.defined() && op->update.defined()) {
            result = 1;
        } else {
            stmt = s->produce;
            op->produce.accept(this);

            if (s->update.defined()) {
                stmt = s->update;
                op->update.accept(this);
            }

            stmt = s->consume;
            op->consume.accept(this);
        }
    }

    void visit(const For *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const For *s = stmt.as<For>();

        if (compare_names(s->name, op->name)) return;

        if (s->for_type < op->for_type) {
            result = -1;
        } else if (s->for_type > op->for_type) {
            result = 1;
        } else {
            expr = s->min;
            op->min.accept(this);

            expr = s->extent;
            op->extent.accept(this);

            stmt = s->body;
            op->body.accept(this);
        }
    }

    void visit(const Store *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Store *s = stmt.as<Store>();

        if (compare_names(s->name, op->name)) return;

        expr = s->value;
        op->value.accept(this);
        if (result) return;

        expr = s->index;
        op->index.accept(this);
    }

    void visit(const Provide *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Provide *s = stmt.as<Provide>();

        if (compare_names(s->name, op->name)) return;

        if (s->args.size() < op->args.size()) {
            result = -1;
        } else if (s->args.size() > op->args.size()) {
            result = 1;
        } else if (s->values.size() < op->values.size()) {
            result = -1;
        } else if (s->values.size() > op->values.size()) {
            result = 1;
        } else {
            for (size_t i = 0; (result == 0) && (i < s->values.size()); i++) {
                expr = s->values[i];
                op->values[i].accept(this);
            }
            for (size_t i = 0; (result == 0) && (i < s->args.size()); i++) {
                expr = s->args[i];
                op->args[i].accept(this);
            }
        }
    }

    void visit(const Allocate *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Allocate *s = stmt.as<Allocate>();

        if (compare_names(s->name, op->name)) return;

        if (s->extents.size() < op->extents.size()) {
            result = -1;
        } else if (s->extents.size() > op->extents.size()) {
            result = 1;
        } else {
            for (size_t i = 0; i < s->extents.size(); i++) {
                expr = s->extents[i];
                op->extents[i].accept(this);
            }
        }

        stmt = s->body;
        op->body.accept(this);

        expr = s->condition;
        op->condition.accept(this);
    }

    void visit(const Realize *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Realize *s = stmt.as<Realize>();

        if (compare_names(s->name, op->name)) return;

        if (s->types.size() < op->types.size()) {
            result = -1;
        } else if (s->types.size() > op->types.size()) {
            result = 1;
        } else if (s->bounds.size() < op->bounds.size()) {
            result = -1;
        } else if (s->bounds.size() > op->bounds.size()) {
            result = 1;
        } else {
            for (size_t i = 0; (result == 0) && (i < s->types.size()); i++) {
                compare_types(s->types[i], op->types[i]);
            }
            for (size_t i = 0; (result == 0) && (i < s->bounds.size()); i++) {
                expr = s->bounds[i].min;
                op->bounds[i].min.accept(this);

                expr = s->bounds[i].extent;
                op->bounds[i].extent.accept(this);
            }

            stmt = s->body;
            op->body.accept(this);

            expr = s->condition;
            op->condition.accept(this);
        }
    }

    void visit(const Block *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Block *s = stmt.as<Block>();

        if (!s->rest.defined() && op->rest.defined()) {
            result = -1;
        } else if (s->rest.defined() && !op->rest.defined()) {
            result = 1;
        } else {
            stmt = s->first;
            op->first.accept(this);

            if (result == 0 && s->rest.defined()) {
                stmt = s->rest;
                op->rest.accept(this);
            }
        }
    }

    void visit(const Free *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Free *s = stmt.as<Free>();

        compare_names(s->name, op->name);
    }

    void visit(const IfThenElse *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const IfThenElse *s = stmt.as<IfThenElse>();

        if (!s->else_case.defined() && op->else_case.defined()) {
            result = -1;
        } else if (s->else_case.defined() && !op->else_case.defined()) {
            result = 1;
        } else {

            expr = s->condition;
            op->condition.accept(this);

            stmt = s->then_case;
            op->then_case.accept(this);

            if (result == 0 && s->else_case.defined()) {
                stmt = s->else_case;
                op->else_case.accept(this);
            }
        }
    }

    void visit(const Evaluate *op) {
        if (result || stmt.same_as(op) || compare_node_types(stmt, op)) return;

        const Evaluate *s = stmt.as<Evaluate>();

        expr = s->value;
        op->value.accept(this);
    }
};

int deep_compare(Expr a, Expr b) {
    // Undefined exprs come first
    // debug(0) << "deep comparison of " << a << " and " << b << "\n";
    if (!a.defined() && b.defined()) return -1;
    if (a.defined() && !b.defined()) return 1;
    if (!a.defined() && !b.defined()) return 0;
    IREquals eq(a);
    b.accept(&eq);
    return eq.result;
}

int deep_compare(Stmt a, Stmt b) {
    // Undefined stmts come first
    if (!a.defined() && b.defined()) return -1;
    if (a.defined() && !b.defined()) return 1;
    if (!a.defined() && !b.defined()) return 0;
    IREquals eq(a);
    b.accept(&eq);
    return eq.result;
}

EXPORT bool equal(Expr a, Expr b) {
    return deep_compare(a, b) == 0;
}

bool equal(Stmt a, Stmt b) {
    return deep_compare(a, b) == 0;
}

}}
