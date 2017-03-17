#include "IREquality.h"
#include "IRVisitor.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

/** The class that does the work of comparing two IR nodes. */
class IRComparer : public IRVisitor {
public:

    /** Different possible results of a comparison. Unknown should
     * only occur internally due to a cache miss. */
    enum CmpResult {Unknown, Equal, LessThan, GreaterThan};

    /** The result of the comparison. Should be Equal, LessThan, or GreaterThan. */
    CmpResult result;

    /** Compare two expressions or statements and return the
     * result. Returns the result immediately if it is already
     * non-zero. */
    // @{
    CmpResult compare_expr(const Expr &a, const Expr &b);
    CmpResult compare_stmt(const Stmt &a, const Stmt &b);
    // @}

    /** If the expressions you're comparing may contain many repeated
     * subexpressions, it's worth passing in a cache to use.
     * Currently this is only done in common-subexpression
     * elimination. */
    IRComparer(IRCompareCache *c = nullptr) : result(Equal), cache(c) {}

private:
    Expr expr;
    Stmt stmt;
    IRCompareCache *cache;

    CmpResult compare_names(const std::string &a, const std::string &b);
    CmpResult compare_types(Type a, Type b);
    CmpResult compare_expr_vector(const std::vector<Expr> &a, const std::vector<Expr> &b);

    // Compare two things that already have a well-defined operator<
    template<typename T>
    CmpResult compare_scalar(T a, T b);

    void visit(const IntImm *);
    void visit(const UIntImm *);
    void visit(const FloatImm *);
    void visit(const StringImm *);
    void visit(const Cast *);
    void visit(const Variable *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Call *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const ProducerConsumer *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const Block *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);
    void visit(const Shuffle *);
    void visit(const Prefetch *);
};

template<typename T>
IRComparer::CmpResult IRComparer::compare_scalar(T a, T b) {
    if (result != Equal) return result;

    if (a < b) {
        result = LessThan;
    } else if (a > b) {
        result = GreaterThan;
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare_expr(const Expr &a, const Expr &b) {
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
    // if (compare_scalar(a.hash(), b.hash()) != Equal) {
    //   return result;
    // }

    if (compare_scalar(a->type_info(), b->type_info()) != Equal) {
        return result;
    }

    if (compare_types(a.type(), b.type()) != Equal) {
        return result;
    }

    // Check the cache - perhaps these exprs have already been compared and found equal.
    if (cache && cache->contains(a, b)) {
        result = Equal;
        return result;
    }

    expr = a;
    b.accept(this);

    if (cache && result == Equal) {
        cache->insert(a, b);
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare_stmt(const Stmt &a, const Stmt &b) {
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

    if (compare_scalar(a->type_info(), b->type_info()) != Equal) {
        return result;
    }

    stmt = a;
    b.accept(this);

    return result;
}

IRComparer::CmpResult IRComparer::compare_types(Type a, Type b) {
    if (result != Equal) return result;

    compare_scalar(a.code(), b.code());
    compare_scalar(a.bits(), b.bits());
    compare_scalar(a.lanes(), b.lanes());
    compare_scalar((uintptr_t)a.handle_type, (uintptr_t)b.handle_type);

    return result;
}

IRComparer::CmpResult IRComparer::compare_names(const string &a, const string &b) {
    if (result != Equal) return result;

    int string_cmp = a.compare(b);
    if (string_cmp < 0) {
        result = LessThan;
    } else if (string_cmp > 0) {
        result = GreaterThan;
    }

    return result;
}


IRComparer::CmpResult IRComparer::compare_expr_vector(const vector<Expr> &a, const vector<Expr> &b) {
    if (result != Equal) return result;

    compare_scalar(a.size(), b.size());
    for (size_t i = 0; (i < a.size()) && result == Equal; i++) {
        compare_expr(a[i], b[i]);
    }

    return result;
}

void IRComparer::visit(const IntImm *op) {
    const IntImm *e = expr.as<IntImm>();
    compare_scalar(e->value, op->value);
}

void IRComparer::visit(const UIntImm *op) {
    const UIntImm *e = expr.as<UIntImm>();
    compare_scalar(e->value, op->value);
}

void IRComparer::visit(const FloatImm *op) {
    const FloatImm *e = expr.as<FloatImm>();
    compare_scalar(e->value, op->value);
}

void IRComparer::visit(const StringImm *op) {
    const StringImm *e = expr.as<StringImm>();
    compare_names(e->value, op->value);
}

void IRComparer::visit(const Cast *op) {
    compare_expr(expr.as<Cast>()->value, op->value);
}

void IRComparer::visit(const Variable *op) {
    const Variable *e = expr.as<Variable>();
    compare_names(e->name, op->name);
}

namespace {
template<typename T>
void visit_binary_operator(IRComparer *cmp, const T *op, Expr expr) {
    const T *e = expr.as<T>();
    cmp->compare_expr(e->a, op->a);
    cmp->compare_expr(e->b, op->b);
}
}

void IRComparer::visit(const Add *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Sub *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Mul *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Div *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Mod *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Min *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Max *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const EQ *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const NE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const LT *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const LE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const GT *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const GE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const And *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Or *op) {visit_binary_operator(this, op, expr);}

void IRComparer::visit(const Not *op) {
    const Not *e = expr.as<Not>();
    compare_expr(e->a, op->a);
}

void IRComparer::visit(const Select *op) {
    const Select *e = expr.as<Select>();
    compare_expr(e->condition, op->condition);
    compare_expr(e->true_value, op->true_value);
    compare_expr(e->false_value, op->false_value);

}

void IRComparer::visit(const Load *op) {
    const Load *e = expr.as<Load>();
    compare_names(op->name, e->name);
    compare_expr(e->predicate, op->predicate);
    compare_expr(e->index, op->index);
}

void IRComparer::visit(const Ramp *op) {
    const Ramp *e = expr.as<Ramp>();
    // No need to compare width because we already compared types
    compare_expr(e->base, op->base);
    compare_expr(e->stride, op->stride);
}

void IRComparer::visit(const Broadcast *op) {
    const Broadcast *e = expr.as<Broadcast>();
    compare_expr(e->value, op->value);
}

void IRComparer::visit(const Call *op) {
    const Call *e = expr.as<Call>();

    compare_names(e->name, op->name);
    compare_scalar(e->call_type, op->call_type);
    compare_scalar(e->value_index, op->value_index);
    compare_expr_vector(e->args, op->args);
}

void IRComparer::visit(const Let *op) {
    const Let *e = expr.as<Let>();

    compare_names(e->name, op->name);
    compare_expr(e->value, op->value);
    compare_expr(e->body, op->body);
}

void IRComparer::visit(const LetStmt *op) {
    const LetStmt *s = stmt.as<LetStmt>();

    compare_names(s->name, op->name);
    compare_expr(s->value, op->value);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const AssertStmt *op) {
    const AssertStmt *s = stmt.as<AssertStmt>();

    compare_expr(s->condition, op->condition);
    compare_expr(s->message, op->message);
}

void IRComparer::visit(const ProducerConsumer *op) {
    const ProducerConsumer *s = stmt.as<ProducerConsumer>();

    compare_names(s->name, op->name);
    compare_scalar(s->is_producer, op->is_producer);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const For *op) {
    const For *s = stmt.as<For>();

    compare_names(s->name, op->name);
    compare_scalar(s->for_type, op->for_type);
    compare_expr(s->min, op->min);
    compare_expr(s->extent, op->extent);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const Store *op) {
    const Store *s = stmt.as<Store>();

    compare_names(s->name, op->name);

    compare_expr(s->predicate, op->predicate);
    compare_expr(s->value, op->value);
    compare_expr(s->index, op->index);
}

void IRComparer::visit(const Provide *op) {
    const Provide *s = stmt.as<Provide>();

    compare_names(s->name, op->name);
    compare_expr_vector(s->args, op->args);
    compare_expr_vector(s->values, op->values);
}

void IRComparer::visit(const Allocate *op) {
    const Allocate *s = stmt.as<Allocate>();

    compare_names(s->name, op->name);
    compare_expr_vector(s->extents, op->extents);
    compare_stmt(s->body, op->body);
    compare_expr(s->condition, op->condition);
    compare_expr(s->new_expr, op->new_expr);
    compare_names(s->free_function, op->free_function);
}

void IRComparer::visit(const Realize *op) {
    const Realize *s = stmt.as<Realize>();

    compare_names(s->name, op->name);
    compare_scalar(s->types.size(), op->types.size());
    compare_scalar(s->bounds.size(), op->bounds.size());
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

void IRComparer::visit(const Block *op) {
    const Block *s = stmt.as<Block>();

    compare_stmt(s->first, op->first);
    compare_stmt(s->rest, op->rest);
}

void IRComparer::visit(const Free *op) {
    const Free *s = stmt.as<Free>();

    compare_names(s->name, op->name);
}

void IRComparer::visit(const IfThenElse *op) {
    const IfThenElse *s = stmt.as<IfThenElse>();

    compare_expr(s->condition, op->condition);
    compare_stmt(s->then_case, op->then_case);
    compare_stmt(s->else_case, op->else_case);
}

void IRComparer::visit(const Evaluate *op) {
    const Evaluate *s = stmt.as<Evaluate>();

    compare_expr(s->value, op->value);
}

void IRComparer::visit(const Shuffle *op) {
    const Shuffle *e = expr.as<Shuffle>();

    compare_expr_vector(e->vectors, op->vectors);

    compare_scalar(e->indices.size(), op->indices.size());
    for (size_t i = 0; (i < e->indices.size()) && result == Equal; i++) {
        compare_scalar(e->indices[i], op->indices[i]);
    }
}

void IRComparer::visit(const Prefetch *op) {
    const Prefetch *s = expr.as<Prefetch>();

    compare_names(s->name, op->name);
    compare_scalar(s->bounds.size(), op->bounds.size());
    for (size_t i = 0; (result == Equal) && (i < s->bounds.size()); i++) {
        compare_expr(s->bounds[i].min, op->bounds[i].min);
        compare_expr(s->bounds[i].extent, op->bounds[i].extent);
    }
}

} // namespace


// Now the methods exposed in the header.
bool equal(const Expr &a, const Expr &b) {
    return IRComparer().compare_expr(a, b) == IRComparer::Equal;
}

bool graph_equal(const Expr &a, const Expr &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_expr(a, b) == IRComparer::Equal;
}

bool equal(const Stmt &a, const Stmt &b) {
    return IRComparer().compare_stmt(a, b) == IRComparer::Equal;
}

bool graph_equal(const Stmt &a, const Stmt &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_stmt(a, b) == IRComparer::Equal;
}

bool IRDeepCompare::operator()(const Expr &a, const Expr &b) const {
    IRComparer cmp;
    cmp.compare_expr(a, b);
    return cmp.result == IRComparer::LessThan;
}

bool IRDeepCompare::operator()(const Stmt &a, const Stmt &b) const {
    IRComparer cmp;
    cmp.compare_stmt(a, b);
    return cmp.result == IRComparer::LessThan;
}

bool ExprWithCompareCache::operator<(const ExprWithCompareCache &other) const {
    IRComparer cmp(cache);
    cmp.compare_expr(expr, other.expr);
    return cmp.result == IRComparer::LessThan;
}

// Testing code
namespace {

IRComparer::CmpResult flip_result(IRComparer::CmpResult r) {
    switch(r) {
    case IRComparer::LessThan: return IRComparer::GreaterThan;
    case IRComparer::Equal: return IRComparer::Equal;
    case IRComparer::GreaterThan: return IRComparer::LessThan;
    case IRComparer::Unknown: return IRComparer::Unknown;
    }
    return IRComparer::Unknown;
}

void check_equal(const Expr &a, const Expr &b) {
    IRCompareCache cache(5);
    IRComparer::CmpResult r = IRComparer(&cache).compare_expr(a, b);
    internal_assert(r == IRComparer::Equal)
        << "Error in ir_equality_test: " << r
        << " instead of " << IRComparer::Equal
        << " when comparing:\n" << a
        << "\nand\n" << b << "\n";
}

void check_not_equal(const Expr &a, const Expr &b) {
    IRCompareCache cache(5);
    IRComparer::CmpResult r1 = IRComparer(&cache).compare_expr(a, b);
    IRComparer::CmpResult r2 = IRComparer(&cache).compare_expr(b, a);
    internal_assert(r1 != IRComparer::Equal &&
                    r1 != IRComparer::Unknown &&
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
