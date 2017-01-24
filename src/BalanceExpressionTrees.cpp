#include "BalanceExpressionTrees.h"
#include "IRMutator.h"
#include "IREquality.h"
#include "Scope.h"
#include <algorithm>
namespace Halide {
namespace Internal {

using std::vector;

namespace {

typedef std::pair<Expr, int> RootWeightPair;
typedef std::map<Expr, int, IRDeepCompare> WeightedRoots;
class ExprHeights : public IRVisitor {
    using IRVisitor::visit;
    std::map<Expr, int, IRDeepCompare> m;
    Scope<int> var_heights;
public:
    // ExprHeights(Scope<int> var_heights) : var_heights(var_heights) {}
    void clear() { m.clear(); }
    void push(const std::string &name, int ht) {
        var_heights.push(name, ht);
    }
    void pop(const std::string &name) {
        var_heights.pop(name);
    }
    void push(Expr e) {
        internal_assert(e.type().is_vector()) << "We are interested in the heights of only vector types\n";
        auto it = m.find(e);
        internal_assert(it == m.end())
            << "Trying to push an expr that already exists in ExprHeights. Use the update method to update\n";

        e.accept(this);
        return;
    }
    void push(Expr e, int h) {
        internal_assert(e.type().is_vector()) << "We are interested in the heights of only vector types\n";
        m[e] = h;
        return;
    }
    void update_height(Expr e, int h) {
        push(e, h);
        return;
    }
    void erase(Expr e) {
        auto it = m.find(e);
        if (it != m.end()) {
            m.erase(it);
        }
    }
    int height(Expr e) {
        const Variable *var = e.as<Variable>();
        if (var) {
            internal_assert(var_heights.contains(var->name)) << "Height of variable " << var->name << " not found in scope\n";
            return var_heights.get(var->name);
        }
        auto it = m.find(e);
        if (it != m.end()) {
            return it->second;
        } else {
            e.accept(this);
            return m[e];
        }
    }
    vector<int> height(const vector<Expr> &exprs) {
        vector<int> heights;
        for (Expr e: exprs) {
            if (e.type().is_vector()) {
                heights.push_back(height(e));
            }
        }
        return heights;
    }
    void set_containing_scope(const Scope<int> *s) {
        var_heights.set_containing_scope(s);
    }
    Scope<int> *get_var_heights() {
        return &var_heights;
    }
    template<typename T>
    void visit_binary(const T *op) {
        if (op->type.is_vector()) {
            IRVisitor::visit(op);
            m[op] = std::max(height(op->a), height(op->b)) + 1;
        }
    }
    template<typename T>
    void visit_leaf(const T *op) {
        if (op->type.is_vector()) {
            m[op] = 0;
        }
    }
    void visit(const Add *op) { visit_binary<Add>(op); }
    void visit(const Sub *op) { visit_binary<Sub>(op); }
    void visit(const Mul *op) { visit_binary<Mul>(op); }
    void visit(const Div *op) { visit_binary<Div>(op); }
    void visit(const Mod *op) { visit_binary<Mod>(op); }
    void visit(const Min *op) { visit_binary<Min>(op); }
    void visit(const Max *op) { visit_binary<Max>(op); }
    void visit(const EQ *op) { visit_binary<EQ>(op); }
    void visit(const NE *op) { visit_binary<NE>(op); }
    void visit(const LT *op) { visit_binary<LT>(op); }
    void visit(const LE *op) { visit_binary<LE>(op); }
    void visit(const GT *op) { visit_binary<GT>(op); }
    void visit(const GE *op) { visit_binary<GE>(op); }
    void visit(const And *op) { visit_binary<And>(op); }
    void visit(const Or *op) { visit_binary<Or>(op); }

    void visit(const Load *op) { visit_leaf<Load>(op); }
    void visit(const IntImm *op) { visit_leaf<IntImm>(op); }
    void visit(const UIntImm *op) { visit_leaf<UIntImm>(op); }
    void visit(const FloatImm *op) { visit_leaf<FloatImm>(op); }
    void visit(const Ramp *op) { visit_leaf<Ramp>(op); }
    void visit(const Broadcast *op) { visit_leaf<Broadcast>(op); }

    template <typename T>
    void visit_let(const T *op) {
        if (op->value.type().is_vector()) {
            Expr value = op->value;
            // First calculate the height of value.
            value.accept(this);
            int ht = height(value);
            m[value] = ht;
            var_heights.push(op->name, ht);
            op->body.accept(this);
            var_heights.pop(op->name);
        }
    }

    void visit(const Let *op) { visit_let<Let>(op); }
    void visit(const LetStmt *op) { visit_let<LetStmt>(op); }

    void visit(const Cast *op) {
        if (op->type.is_vector()) {
            IRVisitor::visit(op);
            // A number of HVX operations fold widening and narrowing
            // into themselves. e.g. widening adds. So count the cast
            // as adding no height.
            m[op] = height(op->value);
        }
    }

    void visit(const Shuffle *op) {
        IRVisitor::visit(op);
        vector<int> heights = height(op->vectors);
        m[op] = *std::max_element(heights.begin(), heights.end());
    }

    void visit(const Call *op) {
        if (op->type.is_vector()) {
            IRVisitor::visit(op);
            vector<int> heights = height(op->args);
            m[op] = *std::max_element(heights.begin(), heights.end());
        }
    }

};

class FindRoots : public IRVisitor {
    using IRVisitor::visit;

    bool is_associative_or_cummutative(Expr a) {
        const Add *add = a.as<Add>();
        const Mul *mul = a.as<Mul>();
        const And *and_ = a.as<And>();
        const Or *or_ = a.as<Or>();
        const Min *min = a.as<Min>();
        const Max *max = a.as<Max>();
        if (add || mul || and_ || or_ || min || max) {
            return true;
        }

        const Sub *sub = a.as<Sub>();
        if (sub) {
            return true;
        }
        return false;
    }

    /** Each operand of op is a root, if it is a different
     * operation than op.
     *        +   <---- op
     *       /  \
     *      /    \
     *     *     * <--- root
     *    / \   / \
     *   4  v0 6   v1
     */
    template<typename T>
    void visit_binary(const T *op) {
        if (op->type.is_vector()) {
            const T *a = op->a.template as<T>();
            const T *b = op->b.template as<T>();

            if (!a && is_associative_or_cummutative(op->a)) {
                weighted_roots[op->a] = -1;
            }
            if (!b && is_associative_or_cummutative(op->b)) {
                weighted_roots[op->b] = -1;
            }
            if (is_associative_or_cummutative((Expr) op)) {
                IRVisitor::visit(op);
            }
        }
    }
    void visit(const Add *op) { visit_binary<Add>(op); }
    void visit(const Mul *op) { visit_binary<Mul>(op); }
public:
    WeightedRoots weighted_roots;
};

inline WeightedRoots find_roots(const Add *op) {
    if (op->type.is_vector()) {
        FindRoots f;
        op->accept(&f);
        f.weighted_roots[(Expr) op] = -1;
        return f.weighted_roots;
    } else {
        return {};
    }
}

void dump_roots(WeightedRoots &w) {
    if (!w.empty()) {
        debug(4) << "Roots are: \n";
        for (const RootWeightPair &r : w) {
            debug(4) << "Root:::->\n\t\t" << r.first << "\nWeight:::-> "<< r.second << "\n";
        }
    } else {
        debug(4) << "*** No Roots *** \n";
    }

}
struct WeightedLeaf {
    Expr e;
    int weight;
    WeightedLeaf(Expr e, int weight) : e(e), weight(weight) {}
    static bool Compare(const WeightedLeaf &lhs, const WeightedLeaf &rhs) {
        return lhs.weight > rhs.weight;
    }
};

class LeafPriorityQueue {
    vector<WeightedLeaf> q;
public:
    void push(Expr e, int wt) {
        if (!q.empty()) {
            q.push_back(WeightedLeaf(e, wt));
            std::push_heap(q.begin(), q.end(), WeightedLeaf::Compare);
        } else {
            q.push_back(WeightedLeaf(e, wt));
            std::make_heap(q.begin(), q.end(), WeightedLeaf::Compare);
        }
    }
    WeightedLeaf pop() {
        std::pop_heap(q.begin(), q.end(), WeightedLeaf::Compare);
        WeightedLeaf least_wt_leaf =  q.back();
        q.pop_back();
        return least_wt_leaf;
    }
    bool empty() {
        return q.empty();
    }
    size_t size() {
        return q.size();
    }
    void clear() {
        q.clear();
    }
};

struct GetTreeWeight : public IRVisitor {
    using IRVisitor::visit;
    int weight;

    bool is_simple_const(Expr e) {
        if (e.as<IntImm>()) return true;
        if (e.as<UIntImm>()) return true;
        return false;
    }

    template <typename T>
    void visit_leaf(const T *op) {
        if (op->type.is_vector()) {
            weight += 1;
        }
    }

    void visit(const Cast *op) {
        if (op->type.is_vector()) {
            // If the value to be cast is a simple
            // constant (immediate integer value) then
            // the cost is 0, else, the cost is 1 plus
            // the cost of the tree rooted at op->value
            if (!is_simple_const(op->value)) {
                IRVisitor::visit(op);
                weight += 1;
            }
        }
    }

    template<typename T>
    void visit_binary(const T *op) {
        if (op->type.is_vector()) {
            IRVisitor::visit(op);
            weight += 1;
        }
    }
    // Constants have 0 weight.
    // So, no visitors for IntImm, UIntImm, FloatImm, StringImm
    // Although, we shouldn't be seeing some of these.
    void visit(const Load *op) { visit_leaf<Load>(op); }
    void visit(const Add *op) { visit_binary<Add>(op); }
    void visit(const Sub *op) { visit_binary<Sub>(op); }
    void visit(const Mul *op) { visit_binary<Mul>(op); }
    void visit(const Div *op) { visit_binary<Div>(op); }
    void visit(const Mod *op) { visit_binary<Mod>(op); }
    void visit(const Min *op) { visit_binary<Min>(op); }
    void visit(const Max *op) { visit_binary<Max>(op); }
    void visit(const EQ *op) { visit_binary<EQ>(op); }
    void visit(const NE *op) { visit_binary<NE>(op); }
    void visit(const LT *op) { visit_binary<LT>(op); }
    void visit(const LE *op) { visit_binary<LE>(op); }
    void visit(const GT *op) { visit_binary<GT>(op); }
    void visit(const GE *op) { visit_binary<GE>(op); }
    void visit(const And *op) { visit_binary<And>(op); }
    void visit(const Or *op) { visit_binary<Or>(op); }

    void visit(const Broadcast *op) {
        if (op->type.is_vector()) {
            if (!is_simple_const(op->value)) {
                IRVisitor::visit(op);
                weight += 1;
            }
        }
    }

    GetTreeWeight() : weight(0) {}
};

// This class is used by the BalanceExpressionTrees to
// to balance subtrees.
class BalanceTree : public IRMutator {
    using IRMutator::visit;
    typedef std::vector<Expr> ExprWorkList;

    int get_weight(Expr e, bool is_root) {
        if (is_root) {
            auto it = weighted_roots.find(e);
            internal_assert(it != weighted_roots.end()) << "Root" << e << " not found in weighted_roots";
            if (it->second != -1) {
                debug(4) << "Found " << e << " in weights cache. Wt is " << it->second << "\n";
                return it->second;
            }
        }

        GetTreeWeight g;
        e.accept(&g);
        int wt = g.weight;

        if (is_root) {
            debug(4) << "Calculated wt for " << e << " : " << wt << "\n";
            weighted_roots[e] = wt;
        }

        return wt;
    }

    template<typename T>
    void visit_binary(const T *op) {

        debug(4) << "BalanceTree: << " << (Expr) op << "\n";

        auto it = weighted_roots.find((Expr) op);
        internal_assert(it != weighted_roots.end()) << "BalanceTree called on a non-root node\n";

        int a_ht = heights.height(op->a);
        int b_ht = heights.height(op->b);
        if (std::abs(a_ht - b_ht) <= 1) {
            // The sub-tree rooted at op is balanced.
            // Do nothing.
            debug(4) <<  ".. is balanced. Returning early from BalanceTree\n";
            expr = op;
            return;
        } else {
            debug(4) << ".. is imbalanced, left tree ht = " << a_ht << ", right tree ht = " << b_ht << "... balancing now\n";
        }

        worklist.push_back(op->a);
        worklist.push_back(op->b);

        while(!worklist.empty()) {
            Expr e = worklist.back();
            worklist.pop_back();

            debug(4) << "Removing from the worklist... " << e << "\n";

            it = weighted_roots.find(e);
            if (it != weighted_roots.end()) {
                debug(4) <<  ".. is a root..balancing\n";
                // Check if already visited before calling balance tree.
                Expr leaf = BalanceTree(weighted_roots, heights.get_var_heights()).mutate(e);
                debug(4) << ".. balanced to produce ->" << leaf << "\n";
                if (!leaf.same_as(e)) {
                    // This means that BalanceTree changed our root. Once
                    // a root always a root, except now it looks different.
                    // So make this change in weighted_roots
                    weighted_roots.erase(it);
                    weighted_roots[leaf] = -1;
                    heights.erase(e);
                    // The tree rooted at e changed into leaf. Pushing
                    // without an int value makes heights compute the
                    // height again.
                    heights.push(leaf);
                }
                leaves.push(leaf, get_weight(leaf, true /*is_root*/));
            } else {
                const T *o = e.as<T>();
                if (o) {
                    debug(4) << ".. is the same op, adding children\n";
                    worklist.push_back(o->a);
                    worklist.push_back(o->b);
                } else {
                    debug(4) << ".. is a leaf\n";
                    leaves.push(e, get_weight(e, false /*is_root*/));
                }
            }
        }

        while(leaves.size() > 1) {
            WeightedLeaf l1 = leaves.pop();
            WeightedLeaf l2 = leaves.pop();
            int combined_weight = l1.weight + l2.weight + 1;
            Expr e = T::make(l1.e, l2.e);
            leaves.push(e, combined_weight);
              // return balanced tree.
        }

        internal_assert(leaves.size() == 1)
            << "After balancing, a tree should have exactly one leaf, we have " << leaves.size() << "\n";
        expr = leaves.pop().e;
        leaves.clear();
    }

    void visit(const Add *op) { visit_binary<Add>(op); }
    void visit(const Mul *op) { visit_binary<Mul>(op); }

    ExprWorkList worklist;
    LeafPriorityQueue leaves;
    // Conv to reference?
    WeightedRoots weighted_roots;
    ExprHeights heights;
public:
    BalanceTree(WeightedRoots weighted_roots, const Scope<int> *var_heights) : weighted_roots(weighted_roots) { 
        heights.clear();
        heights.set_containing_scope(var_heights);
    }

};

class BalanceExpressionTrees : public IRMutator {
    using IRMutator::visit;

    void visit(const Add *op) {
        // We traverse the tree top to bottom and stop at the first vector add
        // and start looking for roots from there.
        if (op->type.is_vector()) {
            debug(4) << "Highest Add is << " << (Expr) op << "\n";

            // 1. Find Roots.
            weighted_roots = find_roots(op);
            if (weighted_roots.empty()) {
                expr = op;
                return;
            }

            debug(4) << "Found " << weighted_roots.size() << " roots\n";

            // 2. Balance the tree
            Expr e = BalanceTree(weighted_roots, h.get_var_heights()).mutate((Expr) op);

            if (e.same_as(op)) {
                expr = op;
            } else {
                debug(4) << "Balanced tree ->\n\t" << e << "\n";
                expr = e;
            }
        } else {
            expr = op;
        }
    }
    template<typename NodeType, typename LetType>
    void visit_let(NodeType &result, const LetType *op) {
        NodeType body = op->body;
        if (op->value.type().is_vector()) {
            op->value.accept(&h);
            int ht = h.height(op->value);
            h.push(op->name, ht);
            body = mutate(op->body);
            h.pop(op->name);
        }
        result = LetType::make(op->name, op->value, body);
    }
    void visit(const Let *op) { visit_let(expr, op); }
    void visit(const LetStmt *op) { visit_let(stmt, op); }
    WeightedRoots weighted_roots;
    // We need to calculate the heights
    // of any variables defined in the containing
    // scope of the tree that we'll balance.
    // So we need to compute that information.
    ExprHeights h;
};
}

Stmt balance_expression_trees(Stmt s) {
    s = BalanceExpressionTrees().mutate(s);
    return s;
}
} // namespace Internal
} // namespace Halide
