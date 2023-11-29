#include "IREquality.h"
#include "IROperator.h"
#include "IRVisitor.h"

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
    enum CmpResult { Unknown,
                     Equal,
                     LessThan,
                     GreaterThan };

    /** The result of the comparison. Should be Equal, LessThan, or GreaterThan. */
    CmpResult result = Equal;

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
    IRComparer(IRCompareCache *c = nullptr)
        : cache(c) {
    }

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

    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Variable *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Acquire *) override;
    void visit(const Store *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const Realize *) override;
    void visit(const Block *) override;
    void visit(const Fork *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
    void visit(const Shuffle *) override;
    void visit(const Prefetch *) override;
    void visit(const Atomic *) override;
    void visit(const VectorReduce *) override;
    void visit(const HoistedStorage *) override;
};

template<typename T>
IRComparer::CmpResult IRComparer::compare_scalar(T a, T b) {
    if (result != Equal) {
        return result;
    }

    if constexpr (std::is_floating_point_v<T>) {
        // NaNs are equal to each other and less than non-nans
        if (std::isnan(a) && std::isnan(b)) {
            result = Equal;
            return result;
        }
        if (std::isnan(a)) {
            result = LessThan;
            return result;
        }
        if (std::isnan(b)) {
            result = GreaterThan;
            return result;
        }
    }

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

    // Undefined values are equal to each other and less than defined values
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

    if (compare_scalar(a->node_type, b->node_type) != Equal) {
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

    if (compare_scalar(a->node_type, b->node_type) != Equal) {
        return result;
    }

    stmt = a;
    b.accept(this);

    return result;
}

IRComparer::CmpResult IRComparer::compare_types(Type a, Type b) {
    if (result != Equal) {
        return result;
    }

    compare_scalar(a.code(), b.code());
    compare_scalar(a.bits(), b.bits());
    compare_scalar(a.lanes(), b.lanes());

    if (result != Equal) {
        return result;
    }

    const halide_handle_cplusplus_type *ha = a.handle_type;
    const halide_handle_cplusplus_type *hb = b.handle_type;

    if (ha == hb) {
        // Same handle type, or both not handles, or both void *
        return result;
    }

    if (ha == nullptr) {
        // void* < T*
        result = LessThan;
        return result;
    }

    if (hb == nullptr) {
        // T* > void*
        result = GreaterThan;
        return result;
    }

    // They're both non-void handle types with distinct type info
    // structs. We now need to distinguish between different C++
    // pointer types (e.g. char * vs const float *). If would be nice
    // if the structs were unique per C++ type. Then comparing the
    // pointers above would be sufficient.  Unfortunately, different
    // shared libraries in the same process each create a distinct
    // struct for the same type. We therefore have to do a deep
    // comparison of the type info fields.

    compare_scalar(ha->reference_type, hb->reference_type);
    compare_names(ha->inner_name.name, hb->inner_name.name);
    compare_scalar(ha->inner_name.cpp_type_type, hb->inner_name.cpp_type_type);
    compare_scalar(ha->namespaces.size(), hb->namespaces.size());
    compare_scalar(ha->enclosing_types.size(), hb->enclosing_types.size());
    compare_scalar(ha->cpp_type_modifiers.size(), hb->cpp_type_modifiers.size());

    if (result != Equal) {
        return result;
    }

    for (size_t i = 0; i < ha->namespaces.size(); i++) {
        compare_names(ha->namespaces[i], hb->namespaces[i]);
    }

    if (result != Equal) {
        return result;
    }

    for (size_t i = 0; i < ha->enclosing_types.size(); i++) {
        compare_scalar(ha->enclosing_types[i].cpp_type_type,
                       hb->enclosing_types[i].cpp_type_type);
        compare_names(ha->enclosing_types[i].name,
                      hb->enclosing_types[i].name);
    }

    if (result != Equal) {
        return result;
    }

    for (size_t i = 0; i < ha->cpp_type_modifiers.size(); i++) {
        compare_scalar(ha->cpp_type_modifiers[i],
                       hb->cpp_type_modifiers[i]);
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare_names(const string &a, const string &b) {
    if (result != Equal) {
        return result;
    }

    int string_cmp = a.compare(b);
    if (string_cmp < 0) {
        result = LessThan;
    } else if (string_cmp > 0) {
        result = GreaterThan;
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare_expr_vector(const vector<Expr> &a, const vector<Expr> &b) {
    if (result != Equal) {
        return result;
    }

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

void IRComparer::visit(const Reinterpret *op) {
    compare_expr(expr.as<Reinterpret>()->value, op->value);
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
}  // namespace

void IRComparer::visit(const Add *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Sub *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Mul *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Div *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Mod *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Min *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Max *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const EQ *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const NE *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const LT *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const LE *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const GT *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const GE *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const And *op) {
    visit_binary_operator(this, op, expr);
}
void IRComparer::visit(const Or *op) {
    visit_binary_operator(this, op, expr);
}

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
    compare_scalar(e->alignment.modulus, op->alignment.modulus);
    compare_scalar(e->alignment.remainder, op->alignment.remainder);
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

void IRComparer::visit(const Acquire *op) {
    const Acquire *s = stmt.as<Acquire>();

    compare_expr(s->semaphore, op->semaphore);
    compare_expr(s->count, op->count);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const Store *op) {
    const Store *s = stmt.as<Store>();

    compare_names(s->name, op->name);

    compare_expr(s->predicate, op->predicate);
    compare_expr(s->value, op->value);
    compare_expr(s->index, op->index);
    compare_scalar(s->alignment.modulus, op->alignment.modulus);
    compare_scalar(s->alignment.remainder, op->alignment.remainder);
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
    compare_types(s->type, op->type);
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

void IRComparer::visit(const Fork *op) {
    const Fork *s = stmt.as<Fork>();

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
    const Prefetch *s = stmt.as<Prefetch>();

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
    compare_expr(s->condition, op->condition);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const Atomic *op) {
    const Atomic *s = stmt.as<Atomic>();

    compare_names(s->producer_name, op->producer_name);
    compare_names(s->mutex_name, op->mutex_name);
    compare_stmt(s->body, op->body);
}

void IRComparer::visit(const VectorReduce *op) {
    const VectorReduce *e = expr.as<VectorReduce>();

    compare_scalar(op->op, e->op);
    // We've already compared types, so it's enough to compare the value
    compare_expr(op->value, e->value);
}

void IRComparer::visit(const HoistedStorage *op) {
    const HoistedStorage *s = stmt.as<HoistedStorage>();

    compare_names(s->name, op->name);
    compare_stmt(s->body, op->body);
}

}  // namespace

// Now the methods exposed in the header.
bool equal(const Expr &a, const Expr &b) {
    return IRComparer().compare_expr(a, b) == IRComparer::Equal;
}

bool graph_equal(const Expr &a, const Expr &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_expr(a, b) == IRComparer::Equal;
}

bool graph_less_than(const Expr &a, const Expr &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_expr(a, b) == IRComparer::LessThan;
}

bool equal(const Stmt &a, const Stmt &b) {
    return IRComparer().compare_stmt(a, b) == IRComparer::Equal;
}

bool graph_equal(const Stmt &a, const Stmt &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_stmt(a, b) == IRComparer::Equal;
}

bool graph_less_than(const Stmt &a, const Stmt &b) {
    IRCompareCache cache(8);
    return IRComparer(&cache).compare_stmt(a, b) == IRComparer::LessThan;
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
    switch (r) {
    case IRComparer::LessThan:
        return IRComparer::GreaterThan;
    case IRComparer::Equal:
        return IRComparer::Equal;
    case IRComparer::GreaterThan:
        return IRComparer::LessThan;
    case IRComparer::Unknown:
        return IRComparer::Unknown;
    }
    return IRComparer::Unknown;
}

void check_equal(const Expr &a, const Expr &b) {
    IRCompareCache cache(5);
    IRComparer::CmpResult r = IRComparer(&cache).compare_expr(a, b);
    internal_assert(r == IRComparer::Equal)
        << "Error in ir_equality_test: " << r
        << " instead of " << IRComparer::Equal
        << " when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
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
        << " when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
}

}  // namespace

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
        e1 = e1 * e1 + e1;
        e2 = e2 * e2 + e2;
    }
    check_equal(e1, e2);
    // These are only discovered to be not equal way down the tree:
    e2 = e2 * e2 + e2;
    check_not_equal(e1, e2);

    debug(0) << "ir_equality_test passed\n";
}

}  // namespace Internal
}  // namespace Halide
