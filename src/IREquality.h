#ifndef HALIDE_IR_EQUALITY_H
#define HALIDE_IR_EQUALITY_H

/** \file
 * Methods to test Exprs and Stmts for equality of value
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** A compare struct suitable for use in std::map and std::set that
 * computes a lexical ordering on IR nodes. Returns LessThan if the
 * first expression is before the second, Equal if they're equal, and
 * GreaterThan if the first expression is after the second. */
class IRDeepCompare : public IRVisitor {
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

    /** Return whether an Expr or Stmt is less than another. */
    // @{
    bool operator()(const Expr &a, const Expr &b);
    bool operator()(const Stmt &a, const Stmt &b);
    // @{

    /** If the expressions you're comparing may contain many repeated
     * subexpressions, it's worth setting the cache_bits to something
     * greater than zero. Currently this is only done in
     * common-subexpression elimination. */
    IRDeepCompare(int cache_bits = 0) : result(Equal), cache(cache_bits) {}

private:
    Expr expr;
    Stmt stmt;

    /** Lossily track known equal exprs with a cache. On collision,
     * the old value is evicted. Provides minor compile-time
     * improvements, but more importantly, it guards against certain
     * types of pathological behavior (comparing graph-structured
     * Exprs to each other). We only track equal exprs, because
     * comparing non-equal exprs tends to early-out very quickly, so
     * it's not worth the insertion and lookup cost. */
    struct Cache {
        int bits;

        struct Entry {
            Expr a, b;
        };

        uint32_t hash(const Expr &a, const Expr &b) const {
            // Note this hash is symmetric in a and b, so that a
            // comparison in a and b hashes to the same bucket as
            // a comparison on b and a.
            uint64_t pa = (uint64_t)(a.ptr);
            uint64_t pb = (uint64_t)(b.ptr);
            pa ^= pb;
            pa ^= pa >> bits;
            pa ^= pa >> (bits*2);
            return pa & ((1 << bits) - 1);
        }

        void insert(const Expr &a, const Expr &b) {
            uint32_t h = hash(a, b);
            entries[h].a = a;
            entries[h].b = b;
        }

        bool contains(const Expr &a, const Expr &b) const {
            uint32_t h = hash(a, b);
            const Entry &e = entries[h];
            return ((a.same_as(e.a) && b.same_as(e.b)) ||
                    (a.same_as(e.b) && b.same_as(e.a)));
        }

        bool enabled() const {
            return bits != 0;
        }

        std::vector<Entry> entries;

        Cache(int b) : bits(b), entries(b == 0 ? 0 : (1 << b)) {}

    } cache;

    CmpResult compare_names(const std::string &a, const std::string &b);
    CmpResult compare_types(Type a, Type b);

    void visit(const IntImm *);
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
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const Block *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);

};

/** A deep IR compare class with the cache size in the type, for
 *  easier use with STL. */
template<int cache_bits>
class IRCachingDeepCompare : public IRDeepCompare {
public:
    IRCachingDeepCompare() : IRDeepCompare(cache_bits) {}
};

/** Compare IR nodes for equality of value. Traverses entire IR
 * tree. For equality of reference, use Expr::same_as */
// @{
EXPORT bool equal(Expr a, Expr b);
EXPORT bool equal(Stmt a, Stmt b);
// @}

EXPORT void ir_equality_test();

}
}

#endif
