#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

IRDeepCompare::CmpResult IRDeepCompare::compare_expr(const Expr &a, const Expr &b) {
    if (result != Equal) {
        return result;
    }
    if (a.same_as(b)) {
        result = Equal;
        return result;
    }

    if (!a.defined() && !b.defined()) {
        result = Equal;
        return result;
    }

    if (!a.defined()) {
        result = LessThan;
        return result;
    }

    if (!b.defined()) {
        result = GreaterThan;
        return result;
    }

    // If in the future we have hashes for Exprs, this is a good place
    // to compare the hashes:
    /*
    uint64_t ha = a.hash(), hb = b.hash();
    if (ha < hb) {
        result = LessThan;
        return;
    }
    if (ha > hb) {
        result = GreaterThan;
        return;
    }
    */

    const void *ta = a.ptr->type_info();
    const void *tb = b.ptr->type_info();
    if (ta < tb) {
        result = LessThan;
        return result;
    } else if (ta > tb) {
        result = GreaterThan;
        return result;
    }

    if (compare_types(a.type(), b.type()) != Equal) {
        return result;
    }

    // Check the cache - perhaps these exprs have already been compared and found equal.
    if (cache.enabled() && cache.contains(a, b)) {
        result = Equal;
        return result;
    }

    expr = a;
    b.accept(this);

    if (cache.enabled() && result == Equal) {
        cache.insert(a, b);
    }

    return result;
}

IRDeepCompare::CmpResult IRDeepCompare::compare_stmt(const Stmt &a, const Stmt &b) {
    if (result != Equal) {
        return result;
    }

    if (a.same_as(b)) {
        result = Equal;
        return result;
    }

    if (!a.defined() && !b.defined()) {
        result = Equal;
        return result;
    }

    if (!a.defined()) {
        result = LessThan;
        return result;
    }

    if (!b.defined()) {
        result = GreaterThan;
        return result;
    }

    const void *ta = a.ptr->type_info();
    const void *tb = b.ptr->type_info();
    if (ta < tb) {
        result = LessThan;
        return result;
    } else if (ta > tb) {
        result = GreaterThan;
        return result;
    }

    stmt = a;
    b.accept(this);

    return result;
}

bool IRDeepCompare::operator()(const Expr &a, const Expr &b) {
    result = Equal;
    compare_expr(a, b);
    return result == LessThan;
}

bool IRDeepCompare::operator()(const Stmt &a, const Stmt &b) {
    result = Equal;
    compare_stmt(a, b);
    return result == LessThan;
}

void IRDeepCompare::visit(const IntImm *op) {

    const IntImm *e = expr.as<IntImm>();
    if (e->value < op->value) {
        result = LessThan;
    } else if (e->value > op->value) {
        result = GreaterThan;
    }
}

void IRDeepCompare::visit(const FloatImm *op) {
    const FloatImm *e = expr.as<FloatImm>();
    if (e->value < op->value) {
        result = LessThan;
    } else if (e->value > op->value) {
        result = GreaterThan;
    }
}

void IRDeepCompare::visit(const StringImm *op) {
    const StringImm *e = expr.as<StringImm>();
    compare_names(e->value, op->value);
}

IRDeepCompare::CmpResult IRDeepCompare::compare_types(Type a, Type b) {
    if (a.code < b.code) {
        result = LessThan;
    } else if (a.code > b.code) {
        result = GreaterThan;
    } else if (a.bits < b.bits) {
        result = LessThan;
    } else if (a.bits > b.bits) {
        result = GreaterThan;
    } else if (a.width < b.width) {
        result = LessThan;
    } else if (a.width > b.width) {
        result = GreaterThan;
    }
    return result;
}

IRDeepCompare::CmpResult IRDeepCompare::compare_names(const string &a, const string &b) {
    if (result != Equal) return result;
    int string_cmp = a.compare(b);
    if (string_cmp < 0) result = LessThan;
    if (string_cmp > 0) result = GreaterThan;
    return result;
}

void IRDeepCompare::visit(const Cast *op) {
    compare_expr(expr.as<Cast>()->value, op->value);
}

void IRDeepCompare::visit(const Variable *op) {
    const Variable *e = expr.as<Variable>();
    compare_names(e->name, op->name);
}

namespace {
template<typename T>
void visit_binary_operator(IRDeepCompare *cmp, const T *op, Expr expr) {
    const T *e = expr.as<T>();
    cmp->compare_expr(e->a, op->a);
    cmp->compare_expr(e->b, op->b);
}
}

void IRDeepCompare::visit(const Add *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Sub *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Mul *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Div *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Mod *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Min *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Max *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const EQ *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const NE *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const LT *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const LE *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const GT *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const GE *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const And *op) {visit_binary_operator(this, op, expr);}
void IRDeepCompare::visit(const Or *op) {visit_binary_operator(this, op, expr);}

void IRDeepCompare::visit(const Not *op) {
    const Not *e = expr.as<Not>();
    compare_expr(e->a, op->a);
}

void IRDeepCompare::visit(const Select *op) {
    const Select *e = expr.as<Select>();
    compare_expr(e->condition, op->condition);
    compare_expr(e->true_value, op->true_value);
    compare_expr(e->false_value, op->false_value);

}

void IRDeepCompare::visit(const Load *op) {
    const Load *e = expr.as<Load>();
    compare_names(op->name, e->name);
    compare_expr(e->index, op->index);
}

void IRDeepCompare::visit(const Ramp *op) {
    const Ramp *e = expr.as<Ramp>();
    // No need to compare width because we already compared types
    compare_expr(e->base, op->base);
    compare_expr(e->stride, op->stride);
}

void IRDeepCompare::visit(const Broadcast *op) {
    const Broadcast *e = expr.as<Broadcast>();
    compare_expr(e->value, op->value);
}

void IRDeepCompare::visit(const Call *op) {
    const Call *e = expr.as<Call>();
    if (compare_names(e->name, op->name) != Equal) return;

    if (e->call_type < op->call_type) {
        result = LessThan;
    } else if (e->call_type > op->call_type) {
        result = GreaterThan;
    } else if (e->value_index < op->value_index) {
        result = LessThan;
    } else if (e->value_index > op->value_index) {
        result = GreaterThan;
    } else if (e->args.size() < op->args.size()) {
        result = LessThan;
    } else if (e->args.size() > op->args.size()) {
        result = GreaterThan;
    } else {
        for (size_t i = 0; (result == Equal) && (i < e->args.size()); i++) {
            compare_expr(e->args[i], op->args[i]);
        }
    }
}

void IRDeepCompare::visit(const Let *op) {
    const Let *e = expr.as<Let>();

    compare_names(e->name, op->name);
    compare_expr(e->value, op->value);
    compare_expr(e->body, op->body);
}

void IRDeepCompare::visit(const LetStmt *op) {
    const LetStmt *s = stmt.as<LetStmt>();

    compare_names(s->name, op->name);
    compare_expr(s->value, op->value);
    compare_stmt(s->body, op->body);
}

void IRDeepCompare::visit(const AssertStmt *op) {
    const AssertStmt *s = stmt.as<AssertStmt>();

    compare_expr(s->condition, op->condition);
    compare_expr(s->message, op->message);
}

void IRDeepCompare::visit(const Pipeline *op) {
    const Pipeline *s = stmt.as<Pipeline>();

    compare_names(s->name, op->name);
    compare_stmt(s->produce, op->produce);
    compare_stmt(s->update, op->update);
    compare_stmt(s->consume, op->consume);
}

void IRDeepCompare::visit(const For *op) {
    const For *s = stmt.as<For>();

    if (compare_names(s->name, op->name) != Equal) return;

    if (s->for_type < op->for_type) {
        result = LessThan;
    } else if (s->for_type > op->for_type) {
        result = GreaterThan;
    } else {
        compare_expr(s->min, op->min);
        compare_expr(s->extent, op->extent);
        compare_stmt(s->body, op->body);
    }
}

void IRDeepCompare::visit(const Store *op) {
    const Store *s = stmt.as<Store>();

    compare_names(s->name, op->name);

    compare_expr(s->value, op->value);
    compare_expr(s->index, op->index);
}

void IRDeepCompare::visit(const Provide *op) {
    const Provide *s = stmt.as<Provide>();

    if (compare_names(s->name, op->name) != Equal) return;

    if (s->args.size() < op->args.size()) {
        result = LessThan;
    } else if (s->args.size() > op->args.size()) {
        result = GreaterThan;
    } else if (s->values.size() < op->values.size()) {
        result = LessThan;
    } else if (s->values.size() > op->values.size()) {
        result = GreaterThan;
    } else {
        for (size_t i = 0; (result == Equal) && (i < s->values.size()); i++) {
            compare_expr(s->values[i], op->values[i]);
        }
        for (size_t i = 0; (result == Equal) && (i < s->args.size()); i++) {
            compare_expr(s->args[i], op->args[i]);
        }
    }
}

void IRDeepCompare::visit(const Allocate *op) {
    const Allocate *s = stmt.as<Allocate>();

    if (compare_names(s->name, op->name) != Equal) return;

    if (s->extents.size() < op->extents.size()) {
        result = LessThan;
    } else if (s->extents.size() > op->extents.size()) {
        result = GreaterThan;
    } else {
        for (size_t i = 0; i < s->extents.size(); i++) {
            compare_expr(s->extents[i], op->extents[i]);
        }
    }

    compare_stmt(s->body, op->body);
    compare_expr(s->condition, op->condition);
}

void IRDeepCompare::visit(const Realize *op) {
    const Realize *s = stmt.as<Realize>();

    if (compare_names(s->name, op->name) != Equal) return;

    if (s->types.size() < op->types.size()) {
        result = LessThan;
    } else if (s->types.size() > op->types.size()) {
        result = GreaterThan;
    } else if (s->bounds.size() < op->bounds.size()) {
        result = LessThan;
    } else if (s->bounds.size() > op->bounds.size()) {
        result = GreaterThan;
    } else {
        for (size_t i = 0; (result == Equal) && (i < s->types.size()); i++) {
            compare_types(s->types[i], op->types[i]);
        }
        for (size_t i = 0; (result == Equal) && (i < s->bounds.size()); i++) {
            compare_expr(s->bounds[i].min, op->bounds[i].min);
            compare_expr(s->bounds[i].extent, op->bounds[i].extent);
        }

        compare_stmt(s->body, op->body);
        compare_expr(s->condition, op->condition);
    }
}

void IRDeepCompare::visit(const Block *op) {
    const Block *s = stmt.as<Block>();

    compare_stmt(s->first, op->first);
    compare_stmt(s->rest, op->rest);
}

void IRDeepCompare::visit(const Free *op) {
    const Free *s = stmt.as<Free>();

    compare_names(s->name, op->name);
}

void IRDeepCompare::visit(const IfThenElse *op) {
    const IfThenElse *s = stmt.as<IfThenElse>();

    compare_expr(s->condition, op->condition);
    compare_stmt(s->then_case, op->then_case);
    compare_stmt(s->else_case, op->else_case);
}

void IRDeepCompare::visit(const Evaluate *op) {
    const Evaluate *s = stmt.as<Evaluate>();

    compare_expr(s->value, op->value);
}

bool equal(Expr a, Expr b) {
    return IRDeepCompare().compare_expr(a, b) == IRDeepCompare::Equal;
}

bool equal(Stmt a, Stmt b) {
    return IRDeepCompare().compare_stmt(a, b) == IRDeepCompare::Equal;
}


// Testing code
namespace {

IRDeepCompare::CmpResult flip_result(IRDeepCompare::CmpResult r) {
    switch(r) {
    case IRDeepCompare::LessThan: return IRDeepCompare::GreaterThan;
    case IRDeepCompare::Equal: return IRDeepCompare::Equal;
    case IRDeepCompare::GreaterThan: return IRDeepCompare::LessThan;
    case IRDeepCompare::Unknown: return IRDeepCompare::Unknown;
    }
    return IRDeepCompare::Unknown;
}

void check_equal(Expr a, Expr b) {
    IRDeepCompare::CmpResult r = IRDeepCompare(5).compare_expr(a, b);
    internal_assert(r == IRDeepCompare::Equal)
        << "Error in ir_equality_test: " << r
        << " instead of " << IRDeepCompare::Equal
        << " when comparing:\n" << a
        << "\nand\n" << b << "\n";
}

void check_not_equal(Expr a, Expr b) {
    IRDeepCompare::CmpResult r1 = IRDeepCompare(5).compare_expr(a, b);
    IRDeepCompare::CmpResult r2 = IRDeepCompare(5).compare_expr(b, a);
    internal_assert(r1 != IRDeepCompare::Equal &&
                    r1 != IRDeepCompare::Unknown &&
                    flip_result(r1) == r2)
        << "Error in ir_equality_test: " << r1
        << " is not the opposite of " << r2
        << " when comparing:\n" << a
        << "\nand\n" << b << "\n";
}

} // namespace

void ir_equality_test() {
    Expr x = Variable::make(Int(32), "x");
    check_equal(Ramp::make(x, 4, 3), Ramp::make(x, 4, 3));
    check_not_equal(Ramp::make(x, 2, 3), Ramp::make(x, 4, 3));

    check_equal(x, Variable::make(Int(32), "x"));
    check_not_equal(x, Variable::make(Int(32), "y"));

    // Something that will hang if IREquality has poor computational
    // complexity.
    Expr e1 = x, e2 = x;
    for (int i = 0; i < 100; i++) {
        e1 = e1*e1 + e1;
        e2 = e2*e2 + e2;
    }
    check_equal(e1, e2);
    // These are only discovered to be not equal way down the tree:
    e2 = e2*e2 + e2;
    check_not_equal(e1, e2);

    debug(0) << "ir_equality_test passed\n";
}

}}
