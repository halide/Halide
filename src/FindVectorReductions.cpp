#include "FindVectorReductions.h"
#include "Bounds.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

using std::pair;
using std::vector;

namespace Halide {
namespace Internal {

namespace {

// Don't try to find vector reductions bigger than this.
const int max_vector_reduction = 256;

// Don't try to search more than this many combinations
// of expressions.
const int max_combinations = 5;

// Rewrite x - y*z as x + y*(-z) where possible.
class RewriteMulSub : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Sub *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        Expr negative_b = lossless_negate(b);
        if (negative_b.defined()) {
            return Add::make(a, negative_b);
        } else if (!a.same_as(op->a)) {
            return Sub::make(a, b);
        } else {
            return op;
        }
    }
};

// Flatten a tree of BinOp into a list of ops.
template <typename BinOp>
void flatten(const BinOp *op, std::vector<Expr> &ops) {
    if (const BinOp *a = op->a.template as<BinOp>()) {
        flatten(a, ops);
    } else {
        ops.push_back(op->a);
    }
    if (const BinOp *b = op->b.template as<BinOp>()) {
        flatten(b, ops);
    } else {
        ops.push_back(op->b);
    }
}

// Find the sum of the constants added to e.
int find_constant_offset(const Expr &e) {
    const int64_t *offset = as_const_int(e);
    if (offset) {
        return *offset;
    }
    if (const Add *add = e.as<Add>()) {
        return find_constant_offset(add->a) + find_constant_offset(add->b);
    } else if (const Sub *sub = e.as<Sub>()) {
        return find_constant_offset(sub->a) - find_constant_offset(sub->b);
    }
    return 0;
}

// Find a common modulus and remainder for loads and shuffles.
class FindInterleavingPosition : public IRVisitor {
    using IRVisitor::visit;

    Scope<ModulusRemainder> positions;

    template <typename T>
    void visit_binop(const T *op) {
        op->a.accept(this);
        ModulusRemainder a = position;
        op->b.accept(this);
        ModulusRemainder b = position;

        position = ModulusRemainder::intersect(a, b);
    }

    void visit(const Add *op) { visit_binop(op); }
    void visit(const Sub *op) { visit_binop(op); }
    void visit(const Mul *op) { visit_binop(op); }
    void visit(const Div *op) { visit_binop(op); }
    void visit(const Mod *op) { visit_binop(op); }
    void visit(const Min *op) { visit_binop(op); }
    void visit(const Max *op) { visit_binop(op); }
    void visit(const And *op) { visit_binop(op); }
    void visit(const Or *op) { visit_binop(op); }
    void visit(const EQ *op) { visit_binop(op); }
    void visit(const NE *op) { visit_binop(op); }
    void visit(const LT *op) { visit_binop(op); }
    void visit(const LE *op) { visit_binop(op); }
    void visit(const GT *op) { visit_binop(op); }
    void visit(const GE *op) { visit_binop(op); }

    void visit(const Select *op) {
        op->condition.accept(this);
        ModulusRemainder a = position;
        op->true_value.accept(this);
        ModulusRemainder b = position;
        op->false_value.accept(this);
        ModulusRemainder c = position;

        position = ModulusRemainder::intersect(a, b);
        position = ModulusRemainder::intersect(position, c);
    }

    void visit(const Load *op) {
        if (const Ramp *ramp = op->index.as<Ramp>()) {
            const int64_t *stride = as_const_int(ramp->stride);
            if (stride) {
                int offset = find_constant_offset(ramp->base);
                position = ModulusRemainder(*stride, offset);
                return;
            }
        }
        position = ModulusRemainder(1, 0);
    }

    void visit(const Variable *op) {
        if (positions.contains(op->name)) {
            position = positions.get(op->name);
        } else {
            position = ModulusRemainder(1, 0);
        }
    }

    template <typename LetT>
    void visit_let(const LetT *op) {
        op->value.accept(this);
        positions.push(op->name, position);
        op->body.accept(this);
        positions.pop(op->name);
    }

    void visit(const Let *op) {
        visit_let(op);
    }

    void visit(const LetStmt *op) {
        visit_let(op);
    }

    void visit(const Ramp *op) {
        op->base.accept(this);
        const int64_t *stride = as_const_int(op->stride);
        if (stride) {
            position = position * *stride;
        } else {
            position = ModulusRemainder(1, 0);
        }
    }

    void visit(const Shuffle *op) {
        IRVisitor::visit(op);
        if (op->is_slice()) {
            position = position * op->slice_stride();
            position = position + op->slice_begin();
        } else {
            position = ModulusRemainder(1, 0);
        }
    }

public:
    ModulusRemainder position;
};

ModulusRemainder find_interleaving_position(const Expr &x) {
    FindInterleavingPosition f;
    x.accept(&f);
    return f.position;
}

bool is_interleave(const Expr &e) {
    if (const Shuffle *s = e.as<Shuffle>()) {
        if (s->is_interleave()) {
            return true;
        }
    }
    return false;
}

// Try to interleave a sequence of binary operators.
template <typename BinOp>
Expr try_interleave(const std::vector<Expr> &ops) {
    std::vector<Expr> a, b;
    for (const Expr &i : ops) {
        if (const BinOp *m = i.as<BinOp>()) {
            a.push_back(m->a);
            b.push_back(m->b);
        } else {
            return Expr();
        }
    }

    Expr interleaved_a = Shuffle::make_interleave(a);
    Expr interleaved_b = Shuffle::make_interleave(b);
    interleaved_a = simplify(interleaved_a);
    interleaved_b = simplify(interleaved_b);
    if (!is_interleave(interleaved_a) || !is_interleave(interleaved_b)) {
        return BinOp::make(interleaved_a, interleaved_b);
    } else {
        return Expr();
    }
}

// Try to interleave a sequence of expressions. Returns an interleaved expression, or
// an undefined expression if not successful.
Expr try_interleave(const std::vector<Expr> &ops) {
    Expr interleaved = Shuffle::make_interleave(ops);
    interleaved = simplify(interleaved);
    if (!is_interleave(interleaved)) {
        return interleaved;
    }

    // TODO: Maybe the simplifier should try to do this? But it might be expensive, it is quite speculative.
    Expr result = try_interleave<Mul>(ops);
    if (result.defined()) {
        return result;
    }

    // I can't think of a reason to check for other operators here.

    return Expr();
}

// Try to find a vector reduction of ops[i][indices[i]].
Expr find_vector_reduction(const std::vector<int> &indices, std::vector<std::vector<Expr>> &ops, VectorReduce::Operator reduce_op) {
    if (indices.size() == ops.size()) {
        std::vector<Expr> interleave_ops;
        for (int i = 0; i < (int)indices.size(); ++i) {
            interleave_ops.push_back(ops[i][indices[i]]);
        }

        Expr interleaved = try_interleave(interleave_ops);
        if (interleaved.defined()) {
            for (int i = 0; i < (int)indices.size(); ++i) {
                ops[i].erase(ops[i].begin() + indices[i]);
            }
            return VectorReduce::make(reduce_op, interleaved, interleaved.type().lanes() / ops.size());
        }
    } else {
        int search_end = (int)ops[indices.size()].size();
        // This algorithm has dangerous complexity. To reduce the risk of catastrophically slow
        // compilation, limit the search to max_combinations.
        search_end = std::min(search_end, max_combinations);

        std::vector<int> next_indices = indices;
        next_indices.push_back(-1);
        for (int j = 0; j < search_end; ++j) {
            next_indices.back() = j;
            Expr result = find_vector_reduction(next_indices, ops, reduce_op);
            if (result.defined()) {
                return result;
            }
        }
    }
    return Expr();
}

Expr find_vector_reduction(std::vector<std::vector<Expr>> &ops, VectorReduce::Operator reduce_op) {
    return find_vector_reduction({}, ops, reduce_op);
}

bool any_empty(const std::vector<std::vector<Expr>>& v_of_v) {
    for (const auto &i : v_of_v) {
        if (i.empty()) {
            return true;
        }
    }
    return false;
}

class FindVectorReductions : public IRMutator {
    using IRMutator::visit;

    template <typename BinOp>
    Expr visit_binop(const BinOp *op, VectorReduce::Operator reduce_op) {
        // Flatten the tree of ops into a list of operands.
        std::vector<Expr> flattened;
        flatten(op, flattened);

        // Mutate the operands.
        bool changed = false;
        for (Expr &i : flattened) {
            Expr new_i = mutate(i);
            if (!new_i.same_as(i)) {
                changed = true;
            }
        }

        Expr result;

        // Helper to add an expression to the result.
        auto add_to_result = [&](Expr &&x) {
            if (result.defined()) {
                result = BinOp::make(result, std::move(x));
            } else {
                result = std::move(x);
            }
        };

        // Group terms first by the possible reduction factor, and then
        // by the interleaving position.
        std::map<int, std::vector<std::vector<Expr>>> factors;
        for (Expr &i : flattened) {
            ModulusRemainder pos = find_interleaving_position(i);
            if (pos.modulus > 0 && pos.modulus <= max_vector_reduction) {
                std::vector<std::vector<Expr>> &remainders = factors[pos.modulus];
                if (0 <= pos.remainder && pos.remainder < pos.modulus) {
                    remainders.resize(pos.modulus);
                    remainders[pos.remainder].emplace_back(std::move(i));
                } else {
                    add_to_result(std::move(i));
                }
            }
        }

        // Try to find vector reductions.
        std::vector<Expr> vector_reductions;
        for (auto &i : factors) {
            int factor = i.first;
            std::vector<std::vector<Expr>> &ops = i.second;
            if (factor > 1 && !ops.empty()) {
                while (!any_empty(ops)) {
                    Expr vector_reduction = find_vector_reduction(ops, reduce_op);
                    if (vector_reduction.defined()) {
                        vector_reductions.emplace_back(std::move(vector_reduction));
                    } else {
                        // We didn't find a vector reduction, so break out of the loop.
                        break;
                    }
                }
            }

            // Put remaining ops in the result.
            while (!ops.empty()) {
                while (!ops.back().empty()) {
                    add_to_result(std::move(ops.back().back()));
                    ops.back().pop_back();
                }
                ops.pop_back();
            }
        }

        if (vector_reductions.empty() && !changed) {
            return op;
        }

        // Add the vector reductions to the result.
        for (Expr &i : vector_reductions) {
            add_to_result(std::move(i));
        }

        return result;
    }

    Expr visit(const Add *op) {
        return visit_binop<Add>(op, VectorReduce::Add);
    }

    Expr visit(const Mul *op) {
        return visit_binop<Mul>(op, VectorReduce::Mul);
    }

    Expr visit(const Min *op) {
        return visit_binop<Min>(op, VectorReduce::Min);
    }

    Expr visit(const Max *op) {
        return visit_binop<Max>(op, VectorReduce::Max);
    }

    // TODO: Bitwise and, or?
};

}  // namespace

Stmt find_vector_reductions(Stmt s) {
    s = RewriteMulSub().mutate(s);
    return FindVectorReductions().mutate(s);
}

Expr find_vector_reductions(Expr e) {
    e = RewriteMulSub().mutate(e);
    return FindVectorReductions().mutate(e);
}

}  // namespace Internal
}  // namespace Halide
