#include "IRMatch.h"

#include "IREquality.h"
#include "IROperator.h"
#include <iostream>

namespace Halide { 
namespace Internal {

void expr_match_test() {
    map<string, Expr> env;
    Expr x = new Variable(Int(32), "x");
    Expr y = new Variable(Int(32), "y");
    Expr fx = new Variable(Float(32), "fx");
    Expr fy = new Variable(Float(32), "fy");

    assert(expr_match(x, 3, env) && 
           equal(env["x"], 3));

    assert(expr_match(x + 3, (y*2) + 3, env) &&
           equal(env["x"], y*2));

    assert(expr_match(fx * 17 + cast<float>(y + cast<int>(fy)), 
                      (81.0f * fy) * 17 + cast<float>(x/2 + cast<int>(4.5f)), env) &&
           equal(env["fx"], 81.0f * fy) &&
           equal(env["y"], x/2) &&
           equal(env["fy"], 4.5f));

    assert(!expr_match(fx + 17, fx + 18, env) &&
           env.empty());
    assert(!expr_match((fx*2) + 17, fx + 17, env) &&
           env.empty());
    assert(!expr_match(x * 3, 3 * x, env) &&
           env.empty());

    std::cout << "expr_match test passed" << std::endl;
}

class IRMatch : public IRVisitor {
public:
    bool result;
    map<string, Expr> env;
    Expr expr;

    IRMatch(Expr e) : result(true), expr(e) {
    }

    void visit(const IntImm *op) {
        const IntImm *e = expr.as<IntImm>();
        if (!e || e->value != op->value) {
            result = false;
        }
    }

    void visit(const FloatImm *op) {
        const FloatImm *e = expr.as<FloatImm>();
        if (!e || e->value != op->value) {
            result = false;
        }
    }

    void visit(const Cast *op) {
        const Cast *e = expr.as<Cast>();
        if (result && e && e->type == op->type) {
            expr = e->value;
            op->value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Variable *op) {
        if (op->type != expr.type()) {
            result = false;
        } else {
            env[op->name] = expr;
        }
    }

    template<typename T>
    void visit_binary_operator(const T *op) {
        const T *e = expr.as<T>();
        if (result && e) {
            expr = e->a;
            op->a.accept(this);
            expr = e->b;
            op->b.accept(this);
        } else {
            result = false;
        }
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
        const Not *e = expr.as<Not>();
        if (result && e) {
            expr = e->a;
            op->a.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Select *op) {
        const Select *e = expr.as<Select>();
        if (result && e) {
            expr = e->condition;
            op->condition.accept(this);
            expr = e->true_value;
            op->true_value.accept(this);
            expr = e->false_value;
            op->false_value.accept(this);
        } else {
            result = false;
        }            
    }

    void visit(const Load *op) {
        const Load *e = expr.as<Load>();
        if (result && e && e->type == op->type && e->buffer == op->buffer) {
            expr = e->index;
            op->index.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Ramp *op) {
        const Ramp *e = expr.as<Ramp>();
        if (result && e && e->width == op->width) {
            expr = e->base;
            op->base.accept(this);
            expr = e->stride;
            op->stride.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Broadcast *op) {
        const Broadcast *e = expr.as<Broadcast>();
        if (result && e && e->width == op->width) {
            expr = e->value;
            op->value.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Call *op) {
        const Call *e = expr.as<Call>();
        if (result && e && 
            e->type == op->type && 
            e->name == op->name && 
            e->call_type == op->call_type &&
            e->args.size() == op->args.size()) {
            for (size_t i = 0; result && (i < e->args.size()); i++) {
                expr = e->args[i];
                op->args[i].accept(this);
            }
        } else {
            result = false;
        }
    }

    void visit(const Let *op) {
        const Let *e = expr.as<Let>();
        if (result && e && e->name == op->name) {
            expr = e->value;
            op->value.accept(this);
            expr = e->body;
            op->body.accept(this);
        } else {
            result = false;
        }
    }
};

bool expr_match(Expr pattern, Expr expr, map<string, Expr> &env) {
    if (!pattern.defined() && !pattern.defined()) return true;
    if (!pattern.defined() || !pattern.defined()) return false;

    IRMatch eq(expr);
    pattern.accept(&eq);
    if (eq.result) {
        env.swap(eq.env);
        return true;
    } else {
        env.clear();
        return false;
    }
}

}}
