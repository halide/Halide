
#include "IR.h"

namespace Halide { 
namespace Internal {

class IREquals : public IRVisitor {
public:
    bool result;
    Expr expr;
    Stmt stmt;

    IREquals(Expr e) : result(true), expr(e) {
    }

    IREquals(Stmt s) : result(true), stmt(s) {
    }

    using IRVisitor::visit;

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
        const Variable *e = expr.as<Variable>();
        if (!e || e->name != op->name || e->type != op->type) {
            result = false;
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
        if (result && e && e->type == op->type && e->name == op->name) {
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

    void visit(const LetStmt *op) {
        const LetStmt *s = stmt.as<LetStmt>();
        if (result && s && s->name == op->name) {
            expr = s->value;
            op->value.accept(this);
            stmt = s->body;
            op->body.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const PrintStmt *op) {
        const PrintStmt *s = stmt.as<PrintStmt>();
        if (result && s && s->prefix == op->prefix) {
            for (size_t i = 0; result && (i < s->args.size()); i++) {
                expr = s->args[i];
                op->args[i].accept(this);
            }                
        } else {
            result = false;
        }
    }

    void visit(const AssertStmt *op) {
        const AssertStmt *s = stmt.as<AssertStmt>();
        if (result && s && s->message == op->message) {
            expr = s->condition;
            op->condition.accept(this);                
        } else {
            result = false;
        }
    }

    void visit(const Pipeline *op) {
        const Pipeline *s = stmt.as<Pipeline>();
        if (result && s && s->name == op->name && 
            (s->update.defined() == op->update.defined())) {
            stmt = s->produce;
            op->produce.accept(this);
            if (s->update.defined()) {
                stmt = s->update;
                op->update.accept(this);
            }
            stmt = s->consume;
            op->consume.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const For *op) {
        const For *s = stmt.as<For>();
        if (result && s && s->name == op->name && s->for_type == op->for_type) {
            expr = s->min;
            op->min.accept(this);
            expr = s->extent;
            op->extent.accept(this);
            stmt = s->body;
            op->body.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Store *op) {
        const Store *s = stmt.as<Store>();
        if (result && s && s->name == op->name) {
            expr = s->value;
            op->value.accept(this);
            expr = s->index;
            op->index.accept(this);
        } else {
            result = false;
        }
    }

    void visit(const Provide *op) {
        const Provide *s = stmt.as<Provide>();
        if (result && s && s->name == op->name) {
            expr = s->value;
            op->value.accept(this);
            for (size_t i = 0; result && (i < s->args.size()); i++) {
                expr = s->args[i];
                op->args[i].accept(this);
            }                      
        } else {
            result = false;
        }
    }

    void visit(const Allocate *op) {
        const Allocate *s = stmt.as<Allocate>();
        if (result && s && s->name == op->name && s->type == op->type) {
            expr = s->size;
            op->size.accept(this);
        } else {
            result = false;
        }

    }

    void visit(const Realize *op) {
        const Realize *s = stmt.as<Realize>();
        if (result && s && s->name == op->name && s->type == op->type) {
            for (size_t i = 0; result && (i < s->bounds.size()); i++) {
                expr = s->bounds[i].min;
                op->bounds[i].min.accept(this);
                expr = s->bounds[i].extent;
                op->bounds[i].extent.accept(this);
            }                                      
        } else {
            result = false;
        }
    }

    void visit(const Block *op) {
        const Block *s = stmt.as<Block>();
        if (result && s && (s->rest.defined() == op->rest.defined())) {
            stmt = s->first;
            op->first.accept(this);
            if (s->rest.defined()) {
                stmt = s->rest;
                op->rest.accept(this);
            }
        } else {
            result = false;
        }
    }
};

bool equal(Expr a, Expr b) {
    if (!a.defined() && !b.defined()) return true;
    if (!a.defined() || !b.defined()) return false;
    IREquals eq(a);
    b.accept(&eq);
    return eq.result;            
}

bool equal(Stmt a, Stmt b) {
    if (!a.defined() && !b.defined()) return true;
    if (!a.defined() || !b.defined()) return false;
    IREquals eq(a);
    b.accept(&eq);
    return eq.result;
}

}}
