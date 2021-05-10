#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

Expr find_constant_upper_bound_expensive(Expr e, const Scope<Interval> &scope) {
    class PropagateSelectConditions : public IRGraphMutator {
        using IRMutator::visit;
        Expr visit(const Select *op) override {
            if (op->type != Int(32)) {
                return op;
            }
            const LT *lt = op->condition.as<LT>();
            const LE *le = op->condition.as<LE>();
            if (lt && is_const(lt->a)) {
                // c0 < x
                Expr true_value = graph_substitute(lt->b, max(lt->b, lt->a + 1), op->true_value);
                Expr false_value = graph_substitute(lt->b, min(lt->b, lt->a), op->false_value);
                return select(op->condition, mutate(true_value), mutate(false_value));
            } else if (le && is_const(le->a)) {
                // c0 <= x
                Expr true_value = graph_substitute(le->b, max(le->b, le->a), op->true_value);
                Expr false_value = graph_substitute(le->b, min(le->b, le->a - 1), op->false_value);
                return select(op->condition, mutate(true_value), mutate(false_value));
            } else if (lt && is_const(lt->b)) {
                // x < c0
                Expr true_value = graph_substitute(lt->a, min(lt->a, lt->b - 1), op->true_value);
                Expr false_value = graph_substitute(lt->a, max(lt->a, lt->b), op->false_value);
                return select(op->condition, mutate(true_value), mutate(false_value));
            } else if (le && is_const(le->b)) {
                // x <= c0
                Expr true_value = graph_substitute(lt->a, min(lt->a, lt->b), op->true_value);
                Expr false_value = graph_substitute(lt->a, max(lt->a, lt->b + 1), op->false_value);
                return select(op->condition, mutate(true_value), mutate(false_value));
            } else {
                return IRMutator::visit(op);
            }
        }
    };
    class PullMinMaxOutermost : public IRMutator {
        IRMatcher::Wild<0> x;
        IRMatcher::Wild<1> y;
        IRMatcher::Wild<2> z;
        IRMatcher::Wild<3> w;
        IRMatcher::WildConst<0> c0;
        IRMatcher::WildConst<1> c1;
        using IRMutator::visit;
        Expr visit(const Add *op) override {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
            }
            return a + b;
        }
        Expr visit(const Min *op) override {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
            }
            return min(a, b);
        }
        Expr visit(const Max *op) override {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
            }
            return max(a, b);
        }
        int64_t mutation_count = 0;
    public:
        using IRMutator::mutate;
        Expr mutate(const Expr &e) override {
            mutation_count++;
            // We can perform about one mutation per 65 nanoseconds
            // (measured). This algorithm is exponential time, and we'd
            // like to set a time limit of roughly a second.
            const int64_t nanoseconds_per_mutation = 65;
            const int64_t mutations_per_second = 1000000000 / nanoseconds_per_mutation;
            const int64_t max_seconds = 1;
            const int64_t max_mutations = max_seconds * mutations_per_second;
            if (mutation_count > max_mutations) {
                return e;
            }
            Expr new_e = IRMutator::mutate(e);
            if (e.type() != Int(32)) {
                return new_e;
            }
            auto rewrite = IRMatcher::rewriter(new_e, e.type());
            if (
                // Fold
                rewrite(c0 + c1, fold(c0 + c1)) ||
                rewrite(c0 - c1, fold(c0 - c1)) ||
                rewrite(max(c0, c1), fold(max(c0, c1))) ||
                rewrite(min(c0, c1), fold(min(c0, c1))) ||
                rewrite((x + c0) + c1, x + fold(c0 + c1)) ||
                rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||
                rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||
                rewrite(min(x + c0, x + c1), x + fold(min(c0, c1))) ||
                rewrite(max(x + c0, x + c1), x + fold(max(c0, c1))) ||
                rewrite(min(min(x + c1, y), x + c0), min(x + fold(min(c0, c1)), y)) ||
                rewrite(max(max(x + c1, y), x + c0), max(x + fold(max(c0, c1)), y)) ||
                rewrite(min(min(y, x + c1), x + c0), min(y, x + fold(min(c0, c1)))) ||
                rewrite(max(max(y, x + c1), x + c0), max(y, x + fold(max(c0, c1)))) ||
                // Canonicalize
                rewrite(max(c0, x), max(x, c0)) ||
                rewrite(min(c0, x), min(x, c0)) ||
                rewrite(c0 + x, x + c0) ||
                rewrite(x - c0, x + fold(-c0)) ||
                rewrite(x - (y + c0), (x - y) + fold(-c0)) ||
                // Simplify
                rewrite(max(x, x), x) ||
                rewrite(min(x, x), x) ||
                rewrite(max(x, max(x, y)), max(x, y)) ||
                rewrite(min(x, min(x, y)), min(x, y)) ||
                rewrite(max(max(x, y), x), max(x, y)) ||
                rewrite(min(min(x, y), x), min(x, y)) ||
                rewrite(max(y, max(x, y)), max(x, y)) ||
                rewrite(min(y, min(x, y)), min(x, y)) ||
                rewrite(max(max(x, y), y), max(x, y)) ||
                rewrite(min(min(x, y), y), min(x, y)) ||
                rewrite(x - x, 0) ||
                rewrite((x + y) - x, y) ||
                rewrite(x + (y - x), x) ||
                rewrite((x + y) - (x + z), y - z) ||
                rewrite((y + x) - (x + z), y - z) ||
                rewrite((x + y) - (z + x), y - z) ||
                rewrite((y + x) - (z + x), y - z) ||
                // Distribute to move select/min/max outermost
                rewrite(x - select(y, z, w), select(y, x - z, x - w)) ||
                rewrite(x + select(y, z, w), select(y, x + z, x + w)) ||
                rewrite(select(y, z, w) - x, select(y, z - x, w - x)) ||
                rewrite(x + min(y, z), min(x + y, x + z)) ||
                rewrite(min(x, y) + z, min(x + z, y + z)) ||
                rewrite(x + max(y, z), max(x + y, x + z)) ||
                rewrite(max(x, y) + z, max(x + z, y + z)) ||
                rewrite(x - min(y, z), max(x - y, x - z)) ||
                rewrite(min(x, y) - z, min(x - z, y - z)) ||
                rewrite(x - max(y, z), min(x - y, x - z)) ||
                rewrite(max(x, y) - z, max(x - z, y - z)) ||
                rewrite(max(x, y) * c0, max(x * c0, y * c0), c0 > 0) ||
                rewrite(max(x, y) * c0, min(x * c0, y * c0), c0 < 0) ||
                rewrite(min(x, y) * c0, min(x * c0, y * c0), c0 > 0) ||
                rewrite(min(x, y) * c0, max(x * c0, y * c0), c0 < 0) ||
                /*
                rewrite(select(x, min(y, z), w), min(select(x, y, w), select(x, z, w))) ||
                rewrite(select(x, max(y, z), w), max(select(x, y, w), select(x, z, w))) ||
                rewrite(select(x, w, min(y, z)), min(select(x, w, y), select(x, w, z))) ||
                rewrite(select(x, w, max(y, z)), max(select(x, w, y), select(x, w, z))) ||
                */
                /*
                // max outermost
                rewrite(min(max(x, y), z), max(min(x, z), min(y, z))) ||
                rewrite(min(x, max(y, z)), max(min(x, y), min(x, z))) ||
                */
                // min outside max
                rewrite(max(min(x, y), z), min(max(x, z), max(y, z))) ||
                rewrite(max(x, min(y, z)), min(max(x, y), max(x, z))) ||
                // select outside min
                rewrite(min(select(x, y, z), w), select(x, min(y, w), min(z, w))) ||
                rewrite(min(select(x, y, z), w), select(x, min(y, w), min(z, w))) ||
                rewrite(min(w, select(x, y, z)), select(x, min(w, y), min(w, z))) ||
                rewrite(min(w, select(x, y, z)), select(x, min(w, y), min(w, z))) ||
                false) {
                return mutate(rewrite.result);
            } else {
                return new_e;
            }
        }
    };
    e = remove_likelies(e);
    Expr ub = find_constant_bound(e, Direction::Upper);
    if (ub.defined()) {
        return ub;
    }
    // Rewrite the expression into a min over a potentially very large
    // number of terms. Hopefully one of those terms is a constant.
    e = substitute_in_all_lets(e);
    debug(0) << "\n1)\n"
             << e << "\n";
    e = PropagateSelectConditions().mutate(e);
    debug(0) << "\n2)\n"
             << e << "\n";
    e = PullMinMaxOutermost().mutate(e);
    debug(0) << "\n3)\n"
             << e << "\n";
    // Take a max across all branches of any outermost select
    vector<Expr> cases;
    vector<Expr> pending{e};
    while (!pending.empty()) {
        Expr next = pending.back();
        pending.pop_back();
        if (const Select *s = next.as<Select>()) {
            pending.push_back(s->true_value);
            pending.push_back(s->false_value);
        } else {
            cases.push_back(next);
        }
    }
    for (const Expr &c : cases) {
        debug(0) << "Case:\n";
        // Then unroll inner mins and look for a constant
        Expr case_ub;
        vector<Expr> clauses{c};
        while (!clauses.empty()) {
            Expr next = clauses.back();
            clauses.pop_back();
            if (const Min *m = next.as<Min>()) {
                clauses.push_back(m->a);
                clauses.push_back(m->b);
                continue;
            }
            next = simplify(next);
            debug(0) << next << "\n";
            if (is_const(next)) {
                if (case_ub.defined()) {
                    case_ub = min(ub, next);
                } else {
                    case_ub = next;
                }
            }
        }
        if (!case_ub.defined()) {
            return case_ub;
        } else if (ub.defined()) {
            ub = max(ub, case_ub);
        } else {
            ub = case_ub;
        }
    }
    if (ub.defined()) {
        return simplify(ub);
    } else {
        return ub;
    }
}