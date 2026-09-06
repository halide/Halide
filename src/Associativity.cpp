#include "Associativity.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "Util.h"

#include <algorithm>
#include <iterator>

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

template<typename T>
vector<T> get_subvector(const vector<T> &v, const set<int> &indices) {
    vector<T> sub;
    for (const auto &index : indices) {
        internal_assert(index < (int)v.size());
        sub.push_back(v[index]);
    }
    return sub;
}

// Replace self-references to 'func' with arguments 'args' at
// 'value_index' in the Expr/Stmt with some Var
class ConvertSelfRef : public IRGraphMutator {
    using IRGraphMutator::visit;

    const string &func;
    const vector<Expr> &args;
    // If that function has multiple values, which value does this
    // call node refer to?
    const int value_index;
    const vector<string> &op_x_names;

    Expr visit(const Call *op) override {
        if (!is_solvable) {
            return op;
        }
        Expr expr = IRGraphMutator::visit(op);
        op = expr.as<Call>();
        internal_assert(op);

        if ((op->call_type == Call::Halide) && (func == op->name)) {
            internal_assert(args.size() == op->args.size())
                << "Self-reference should have the same number of args as the original\n";
            for (size_t i = 0; i < op->args.size(); i++) {
                if (!graph_equal(op->args[i], args[i])) {
                    debug(5) << "Self-reference of " << op->name
                             << " with different args from the LHS. Operation is not associative\n";
                    is_solvable = false;
                    return expr;
                }
            }
            // Substitute the call
            internal_assert(op->value_index < (int)op_x_names.size());
            debug(5) << "   Substituting Call " << op->name << " at value index "
                     << op->value_index << " with " << op_x_names[op->value_index] << "\n";
            expr = Variable::make(op->type, op_x_names[op->value_index]);

            if (op->value_index == value_index) {
                x_part = op;
            } else {
                x_dependencies.insert(op->value_index);
            }
        }
        return expr;
    }

public:
    ConvertSelfRef(const string &f, const vector<Expr> &args, int idx,
                   const vector<string> &x_names)
        : func(f), args(args), value_index(idx), op_x_names(x_names) {
    }

    bool is_solvable = true;
    set<int> x_dependencies;  // Contains dependencies on self-reference at different tuple indices
    Expr x_part;              // Undefined if there is no self-reference at value_index
};

bool associative_op_pattern_match(const Expr &e,
                                  const Expr &op,
                                  const vector<string> &x_names,
                                  const vector<string> &y_names,
                                  const Scope<> &x_scope,
                                  map<string, Expr> &match) {

    internal_assert(e.type() == op.type())
        << "Expr has type " << e.type() << ", while pattern has type " << op.type() << "\n";
    map<string, Expr> result;
    if (expr_match(op, e, result)) {
        debug(5) << "Found associative ops for " << e << " -> " << op << ":\n"
                 << [&] {
                        std::stringstream ss;
                        for (const auto &[var, val] : result) {
                            ss << "  " << var << " -> " << val << "\n";
                        }
                        return ss.str();
                    }();

        for (size_t i = 0; i < x_names.size(); ++i) {
            const auto &iter = result.find("x" + std::to_string(i));
            if (iter != result.end()) {
                const Variable *xvar = iter->second.as<Variable>();
                if ((xvar == nullptr) || (xvar->name != x_names[i])) {
                    debug(5) << "...Skipping match since the x_part is different than expected. "
                             << "Expect: " << x_names[i] << "; get: " << iter->second << "\n";
                    return false;
                }
            }
        }
        for (size_t i = 0; i < y_names.size(); ++i) {
            const auto &iter = result.find("y" + std::to_string(i));
            if (iter != result.end()) {
                // Make sure that y_part should not depend on x vars
                if (expr_uses_vars(iter->second, x_scope)) {
                    debug(5) << "...Skipping match since the y_part depends on x vars\n";
                    return false;
                }
            }
        }
        for (size_t i = 0; i < x_names.size(); ++i) {
            const auto &iter = result.find("k" + std::to_string(i));
            if (iter != result.end()) {
                // Make sure that k_part is constant
                if (!is_const(iter->second)) {
                    debug(5) << "...Skipping match since the k_part is not constant\n";
                    return false;
                }
            }
        }

        // Make sure that the new matches are in agreement with any previous matches
        for (const auto &iter : result) {
            const auto &match_iter = match.find(iter.first);
            if (match_iter == match.end()) {
                debug(5) << "Adding result: " << iter.first << " -> " << iter.second << "\n";
                match.emplace(iter.first, iter.second);
            } else {
                if (iter.first != match_iter->first || !equal(iter.second, match_iter->second)) {
                    debug(5) << "Failed to match: (" << iter.first << ", " << iter.second << ") != ("
                             << match_iter->first << ", " << match_iter->second << ")\n";
                    return false;
                }
            }
        }
        return true;
    }
    debug(5) << "expr_match(" << op << ", " << e << ") == false\n";
    return false;
}

// Return true if we are able to find a match in the table (i.e. the op can be
// proven associative) and update 'assoc_op'.
bool find_match(const vector<AssociativePattern> &table, const vector<string> &op_x_names,
                const vector<string> &op_y_names, const vector<Expr> &x_parts,
                const vector<Expr> &exprs, AssociativeOp &assoc_op) {
    internal_assert(op_x_names.size() == op_y_names.size());
    internal_assert(op_x_names.size() == x_parts.size());
    internal_assert(op_x_names.size() == exprs.size());
    internal_assert(op_x_names.size() == assoc_op.size());

    Scope<> x_scope;
    for (const auto &x : op_x_names) {
        x_scope.push(x);
    }

    for (const AssociativePattern &pattern : table) {
        internal_assert(pattern.size() == op_x_names.size());
        map<string, Expr> pattern_match;
        bool matched = true;
        // If any of element in 'pattern' does not match, try the next thing in
        // the table.
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (!associative_op_pattern_match(exprs[i], pattern.ops[i], op_x_names,
                                              op_y_names, x_scope, pattern_match)) {
                matched = false;
                break;
            }
        }
        if (!matched) {
            continue;
        }

        for (size_t index = 0; index < op_y_names.size(); ++index) {
            const auto &y_iter = pattern_match.find("y" + std::to_string(index));
            if (y_iter == pattern_match.end()) {
                // Didn't find y{index} during pattern matching. Try next pattern.
                matched = false;
                break;
            }
            Expr y_part = y_iter->second;
            debug(5) << "Pattern at index " << index << ":\n  " << op_x_names[index]
                     << " -> " << x_parts[index] << "\n  " << op_y_names[index]
                     << " -> " << y_part << "\n";

            assoc_op.xs[index] = {op_x_names[index], x_parts[index]};
            assoc_op.ys[index] = {op_y_names[index], y_part};
        }
        if (!matched) {
            continue;
        }
        // Build the concrete ops by renaming the pattern's abstract
        // wildcard variables (x0, y0, k0, ...) to the actual variable
        // names used in the expressions.
        map<string, Expr> replacement;
        for (size_t index = 0; index < op_x_names.size(); ++index) {
            replacement["x" + std::to_string(index)] = Variable::make(exprs[index].type(), op_x_names[index]);
            replacement["y" + std::to_string(index)] = Variable::make(exprs[index].type(), op_y_names[index]);
        }
        for (const auto &[wildcard, identity] : pattern_match) {
            if (wildcard[0] == 'k') {
                replacement[wildcard] = identity;
            }
        }
        for (size_t index = 0; index < pattern.ops.size(); ++index) {
            assoc_op.pattern.ops[index] = substitute(replacement, pattern.ops[index]);
            assoc_op.pattern.identities[index] = pattern.identities[index];
        }
        assoc_op.pattern.is_commutative = pattern.is_commutative;
        return true;
    }
    return false;
}

// Return a pair of booleans indicating if an operator is associative.
// 'assoc_op' contains the equivalent associative binary/unary operator
// for that operator. If the operator is non-associative, 'assoc_op' is not valid.
bool extract_associative_op(const vector<Expr> &exprs, const vector<string> &op_x_names,
                            const vector<string> &op_y_names, const vector<Expr> &x_parts,
                            AssociativeOp &assoc_op) {
    if (exprs.size() == 1) {
        Type t = exprs[0].type();
        if (!x_parts[0].defined()) {
            // An update that just assigns some value is not associative,
            // because there's no good identity. An identity is necessary
            // because things like rfactor will combine the identity with
            // partially computed values and expect it to do nothing. For an
            // example, see https://github.com/halide/Halide/issues/7893
            return false;
        } else if (equal(exprs[0], Variable::make(t, op_x_names[0]))) {
            // Self assignment, f(x) = f(x), is both associative
            // and commutative. The identity can be anything since it's
            // going to be replaced by itself.
            debug(5) << "Self assignment: " << x_parts[0] << " = " << x_parts[0] << "\n";
            assoc_op.pattern.ops[0] = Variable::make(t, op_x_names[0]);
            assoc_op.pattern.identities[0] = make_const(t, 0);
            assoc_op.pattern.is_commutative = true;
            assoc_op.xs[0] = {op_x_names[0], x_parts[0]};
            assoc_op.ys[0] = {"", Expr()};
            return true;
        }
    }
    return find_match(get_ops_table(exprs), op_x_names, op_y_names,
                      x_parts, exprs, assoc_op);
}

bool is_subset_of(const std::set<int> &a, const std::set<int> &b) {
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

// Compute the dependency subgraphs for a tuple reduction. First closes the
// dependency relation transitively, then returns only the earliest (by index)
// maximal dependency sets, clearing any set contained in a dominating one.
vector<set<int>> compute_subgraphs(vector<set<int>> dependencies) {
    // Compute the transitive closure using Warshall's algorithm.
    for (size_t k = 0; k < dependencies.size(); ++k) {
        for (size_t i = 0; i < dependencies.size(); ++i) {
            if (dependencies[i].count(k)) {
                for (int j : dependencies[k]) {
                    dependencies[i].insert(j);
                }
            }
        }
    }

    // Keep only maximal dependency sets. A set is removed if another
    // set strictly contains it or is identical but has a lower index.
    vector<set<int>> subgraphs(dependencies.size());
    for (size_t i = 0; i < dependencies.size(); ++i) {
        if (dependencies[i].empty()) {
            continue;
        }
        bool is_maximal = true;
        for (size_t j = 0; j < dependencies.size(); ++j) {
            const bool can_dominate =
                (dependencies[j].size() > dependencies[i].size()) ||
                (dependencies[j].size() == dependencies[i].size() && j < i);
            if (can_dominate && is_subset_of(dependencies[i], dependencies[j])) {
                is_maximal = false;
                break;
            }
        }
        if (is_maximal) {
            subgraphs[i] = dependencies[i];
        }
    }
    return subgraphs;
}

}  // anonymous namespace

AssociativeOp prove_associativity(const string &f, vector<Expr> args, vector<Expr> exprs) {
    AssociativeOp assoc_op(exprs.size());

    for (Expr &arg : args) {
        // Undo the existing CSE pass done at function definition time
        // to ensure things like += are in the expected form. Make no
        // further transformations so that the LHS and RHS don't
        // diverge.
        arg = substitute_in_all_lets(arg);
    }

    vector<string> op_x_names(exprs.size()), op_y_names(exprs.size());
    for (size_t idx = 0; idx < exprs.size(); ++idx) {
        op_x_names[idx] = unique_name("_x_" + std::to_string(idx));
        op_y_names[idx] = unique_name("_y_" + std::to_string(idx));
    }

    vector<set<int>> dependencies(exprs.size());
    vector<Expr> x_parts(exprs.size());
    bool all_independent = true;

    // For a Tuple of exprs to be associative, each element of the Tuple
    // has to be associative.
    for (int idx = exprs.size() - 1; idx >= 0; --idx) {
        // Undo the existing CSE pass done at function definition time.
        exprs[idx] = substitute_in_all_lets(exprs[idx]);

        // Replace any self-reference to Func 'f' with a Var
        ConvertSelfRef csr(f, args, idx, op_x_names);
        exprs[idx] = csr(exprs[idx]);
        if (!csr.is_solvable) {
            return AssociativeOp();
        }
        if (!csr.x_dependencies.empty()) {
            all_independent = false;
        }
        x_parts[idx] = csr.x_part;
        dependencies[idx] = csr.x_dependencies;
        // Add a dependency on itself (regardless of whether it actually
        // depends on its previous values) to compute the subgraph
        dependencies[idx].insert(idx);

        exprs[idx] = common_subexpression_elimination(exprs[idx]);
        exprs[idx] = simplify(exprs[idx]);
        exprs[idx] = solve_expression(exprs[idx], op_x_names[idx]).result;  // Move 'x' to the left as possible
        exprs[idx] = substitute_in_all_lets(exprs[idx]);
    }
    internal_assert((exprs.size() != 1) || all_independent) << "1D tuple should be all independent\n";

    vector<set<int>> subgraphs;
    if (!all_independent) {
        debug(5) << "There are cross-dependencies. Need to prove associativity in bulk.\n";
        // Decompose the tuple into subgraphs and solve for each separately
        subgraphs = compute_subgraphs(dependencies);
    } else {
        debug(5) << "All tuple elements are independent. Try proving associativity of "
                 << "each element separately.\n";
        // If all elements are independent, the subgraph is equal to the dependencies graph
        subgraphs = dependencies;
    }
    internal_assert(subgraphs.size() == exprs.size());

    for (size_t i = 0; i < subgraphs.size(); ++i) {
        if (subgraphs[i].empty()) {
            debug(5) << "Empty subgraph " << i << "\n";
            continue;
        }
        if (subgraphs[i].size() > 2) {
            // TODO(psuriana): Currently only support max of 2 tuple elements
            debug(5) << "Subgraph " << i << " size is " << subgraphs[i].size() << " which is bigger than 2\n";
            return AssociativeOp();
        }

        vector<Expr> sub_exprs = get_subvector(exprs, subgraphs[i]);
        vector<string> sub_op_x_names = get_subvector(op_x_names, subgraphs[i]);
        vector<string> sub_op_y_names = get_subvector(op_y_names, subgraphs[i]);
        vector<Expr> sub_x_parts = get_subvector(x_parts, subgraphs[i]);
        AssociativeOp sub_assoc_op(sub_exprs.size());

        // TODO(psuriana): In general, if we fail to find a match for the
        // set of initial subgraphs, we need to consider other possible
        // grouping of those initial subgraphs. Since only the 'x' is
        // apparent from the Halide update definition, the compute_subgraphs
        // method over-partitions the graph (e.g. 2x2 matrix multiplication
        // written as a four-dimensional reduction).

        if (!extract_associative_op(sub_exprs, sub_op_x_names, sub_op_y_names,
                                    sub_x_parts, sub_assoc_op)) {
            debug(5) << "Cannot find matching associative ops\n";
            return AssociativeOp();
        }

        debug(5) << "...Proving associativity of subgraph " << i << "\n";
        const set<int> &indices = subgraphs[i];
        for (auto iter = indices.begin(); iter != indices.end(); ++iter) {
            int index = *iter;
            int j = std::distance(indices.begin(), iter);

            // If the ops/x/y have been extracted previously, we have to make sure
            // they are consistent with the new extracted values.
            if (assoc_op.pattern.ops[index].defined()) {
                if (!equal(assoc_op.pattern.ops[index], sub_assoc_op.pattern.ops[j]) ||
                    !equal(assoc_op.pattern.identities[index], sub_assoc_op.pattern.identities[j])) {
                    debug(5) << "Conflicting associative ops/identities from different subgraphs\n";
                    return AssociativeOp();
                }
            }
            if (assoc_op.xs[index].expr.defined()) {
                if (assoc_op.xs[index] != sub_assoc_op.xs[j]) {
                    debug(5) << "Conflicting associative x-replacements from different subgraphs\n";
                    return AssociativeOp();
                }
            }
            if (assoc_op.ys[index].expr.defined()) {
                if (assoc_op.ys[index] != sub_assoc_op.ys[j]) {
                    debug(5) << "Conflicting associative y-replacements from different subgraphs\n";
                    return AssociativeOp();
                }
            }

            assoc_op.pattern.ops[index] = sub_assoc_op.pattern.ops[j];
            assoc_op.pattern.identities[index] = sub_assoc_op.pattern.identities[j];
            assoc_op.pattern.is_commutative = sub_assoc_op.pattern.is_commutative;
            assoc_op.xs[index] = sub_assoc_op.xs[j];
            assoc_op.ys[index] = sub_assoc_op.ys[j];
        }
    }

    assoc_op.is_associative = true;
    debug(5) << "Found associative ops:\n"
             << assoc_op << "\n";
    return assoc_op;
}

}  // namespace Internal
}  // namespace Halide
