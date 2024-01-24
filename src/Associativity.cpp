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
using std::pair;
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
        debug(5) << "Found associative ops for " << e << " -> " << op
                 << ", y_part: " << result["y0"] << "\n";

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
                if (!equal(iter.first, match_iter->first) || !equal(iter.second, match_iter->second)) {
                    return false;
                }
            }
        }
        return true;
    }
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

        vector<pair<Expr, Expr>> replacement;  // find -> replacement
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
            replacement.emplace_back(y_part, Variable::make(y_part.type(), op_y_names[index]));
        }
        if (!matched) {
            continue;
        }
        for (size_t index = 0; index < exprs.size(); ++index) {
            Expr e = exprs[index];
            // Order of substitution matters, e.g. in the argmin case, _y_0 -> g(rx)[0]
            // and _y_1 -> rx. If we substitute the 2nd element rx first, substitution
            // of g(rx)[0] will fail.
            for (const auto &iter : replacement) {
                e = substitute(iter.first, iter.second, e);
            }
            assoc_op.pattern.ops[index] = e;
            assoc_op.pattern.identities[index] = pattern.identities[index];
        }
        assoc_op.pattern.is_commutative = pattern.is_commutative;
        return true;
    }
    return false;
}

// Return a pair of booleans indicating if an operator is associative.
// 'assoc_op' contains the the equivalent associative binary/unary operator
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
            // partially-computed values and expect it to do nothing. For an
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

void add_transitive_dependencies(vector<set<int>> &dependencies) {
    // TODO(psuriana): there might be a better way to find all the transitive dependencies
    bool change = true;
    while (change) {
        change = false;
        for (size_t i = 0; i < dependencies.size(); ++i) {
            for (size_t j = 0; j < dependencies.size(); ++j) {
                if (i == j) {
                    continue;
                }
                if (dependencies[i].count(j)) {
                    for (const auto &idx : dependencies[j]) {
                        if (dependencies[i].count(idx) == 0) {
                            dependencies[i].insert(idx);
                            change = true;
                        }
                    }
                }
            }
        }
    }
}

// Given dependencies of each tuple element, compute the set of subgraphs:
// all vertices that are reachable from a given vertex. If a subgraph is fully
// contained in another subgraph, remove it from the final output.
vector<set<int>> compute_subgraphs(vector<set<int>> dependencies) {
    vector<set<int>> subgraphs(dependencies.size());
    for (size_t i = 0; i < dependencies.size(); ++i) {
        // Check if the current subgraph is a subset of another
        const auto &current = dependencies[i];
        if (current.empty()) {
            continue;
        }
        bool should_remove = false;
        for (size_t j = 0; j < dependencies.size(); ++j) {
            const auto &other = dependencies[j];
            if ((i == j) || (current.size() > other.size()) || (j < i && subgraphs[i].empty())) {
                continue;
            }
            vector<int> diff;
            // Compute the vertices in the current set that are not contained in the other
            std::set_difference(current.begin(), current.end(), other.begin(), other.end(),
                                std::inserter(diff, diff.begin()));
            if (diff.empty()) {
                // 'current' is fully contained in 'other'
                should_remove = true;
                break;
            }
        }
        if (!should_remove) {
            subgraphs[i] = current;
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
        exprs[idx] = csr.mutate(exprs[idx]);
        if (!csr.is_solvable) {
            return AssociativeOp();
        }
        if (!csr.x_dependencies.empty()) {
            all_independent = false;
        }
        x_parts[idx] = csr.x_part;
        dependencies[idx] = csr.x_dependencies;
        // Add dependency on itself (regardless whether it actually depends on
        // its previous values) for the purpose of computing the subgraph
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
        // Find all transitive dependencies and add them to the graph
        add_transitive_dependencies(dependencies);
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

namespace {

std::string print_args(const string &f, const vector<Expr> &args, const vector<Expr> &exprs) {
    std::ostringstream stream;
    stream << f << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        stream << args[i];
        if (i != args.size() - 1) {
            stream << ", ";
        }
    }
    stream << ") = ";

    if (exprs.size() == 1) {
        stream << exprs[0];
    } else if (exprs.size() > 1) {
        stream << "Tuple(";
        for (size_t i = 0; i < exprs.size(); ++i) {
            stream << exprs[i];
            if (i != exprs.size() - 1) {
                stream << ", ";
            }
        }
        stream << ")";
    }
    return stream.str();
}

void check_associativity(const string &f, const vector<Expr> &args, const vector<Expr> &exprs,
                         const AssociativeOp &assoc_op) {
    auto result = prove_associativity(f, args, exprs);
    internal_assert(result.associative() == assoc_op.associative())
        << "Checking associativity: " << print_args(f, args, exprs) << "\n"
        << "  Expect is associative: " << assoc_op.associative() << "\n"
        << "  instead of " << result.associative() << "\n";
    if (assoc_op.associative()) {
        map<string, Expr> replacement;
        for (size_t i = 0; i < assoc_op.size(); ++i) {
            internal_assert(equal(result.pattern.identities[i], assoc_op.pattern.identities[i]))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect identity: " << assoc_op.pattern.identities[i] << "\n"
                << "  instead of " << result.pattern.identities[i] << "\n";
            internal_assert(equal(result.xs[i].expr, assoc_op.xs[i].expr))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect x: " << assoc_op.xs[i].expr << "\n"
                << "  instead of " << result.xs[i].expr << "\n";
            internal_assert(equal(result.ys[i].expr, assoc_op.ys[i].expr))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect y: " << assoc_op.ys[i].expr << "\n"
                << "  instead of " << result.ys[i].expr << "\n";

            if (result.xs[i].expr.defined()) {
                replacement.emplace(assoc_op.xs[i].var, Variable::make(result.xs[i].expr.type(), result.xs[i].var));
            }
            if (result.ys[i].expr.defined()) {
                replacement.emplace(assoc_op.ys[i].var, Variable::make(result.ys[i].expr.type(), result.ys[i].var));
            }
        }
        for (size_t i = 0; i < assoc_op.size(); ++i) {
            Expr expected_op = substitute(replacement, assoc_op.pattern.ops[i]);

            internal_assert(equal(result.pattern.ops[i], expected_op))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect bin op: " << expected_op << "\n"
                << "  instead of " << result.pattern.ops[i] << "\n";

            debug(5) << "\nExpected op: " << expected_op << "\n";
            debug(5) << "Operator: " << result.pattern.ops[i] << "\n";
            debug(5) << "   identity: " << result.pattern.identities[i] << "\n";
            debug(5) << "   x: " << result.xs[i].var << " -> " << result.xs[i].expr << "\n";
            debug(5) << "   y: " << result.ys[i].var << " -> " << result.ys[i].expr << "\n";
        }
    }
}

}  // anonymous namespace

void associativity_test() {
    typedef AssociativeOp::Replacement Replacement;

    {
        // Tests for saturating addition
        Type t = UInt(8);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr x_idx = Variable::make(Int(32), "x_idx");
        Expr f_call_0 = Call::make(t, "f", {x_idx}, Call::CallType::Halide, FunctionPtr(), 0);

        // f(x) = uint8(uint16(x + y), 255)
        check_associativity("f", {x_idx}, {Cast::make(UInt(8), min(Cast::make(UInt(16), y + f_call_0), make_const(t, 255)))},
                            AssociativeOp(
                                AssociativePattern(Cast::make(UInt(8), min(Cast::make(UInt(16), x + y), make_const(t, 255))), make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = uint8(uint16(x + y), uint16(255))
        check_associativity("f", {x_idx}, {Cast::make(UInt(8), min(Cast::make(UInt(16), y + f_call_0), Cast::make(UInt(16), make_const(t, 255))))},
                            AssociativeOp(
                                AssociativePattern(Cast::make(UInt(8), min(Cast::make(UInt(16), x + y), make_const(t, 255))), make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = select(x > 255 - y, 255, y)
        check_associativity("f", {x_idx}, {select(f_call_0 > make_const(t, 255) - y, make_const(t, 255), y)},
                            AssociativeOp(
                                AssociativePattern(select(x > make_const(t, 255) - y, make_const(t, 255), y), make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = select(x >= -y, 255, y)
        check_associativity("f", {x_idx}, {select(f_call_0 >= -y, make_const(t, 255), y)},
                            AssociativeOp(
                                AssociativePattern(select(x < -y, y, make_const(t, 255)), make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));
    }

    {
        // Tests for logical And/Or
        Type t = UInt(1);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr x_idx = Variable::make(Int(32), "x_idx");
        Expr f_call_0 = Call::make(t, "f", {x_idx}, Call::CallType::Halide, FunctionPtr(), 0);

        // f(x) = y && f(x)
        check_associativity("f", {x_idx}, {And::make(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(And::make(x, y), const_true(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = y || f(x)
        check_associativity("f", {x_idx}, {Or::make(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(Or::make(x, y), const_false(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));
    }

    {
        // Tests for 1D reduction
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr rx = Variable::make(t, "rx");
        Expr f_call_0 = Call::make(t, "f", {x}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr g_call_0 = Call::make(t, "g", {rx}, Call::CallType::Halide, FunctionPtr(), 0);

        // f(x) = f(x)
        check_associativity("f", {x}, {f_call_0},
                            AssociativeOp(
                                AssociativePattern(x, make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("", Expr())},
                                true));

        // f(x) = min(f(x), y + int16(z))
        check_associativity("f", {x}, {min(f_call_0, y + Cast::make(Int(16), z))},
                            AssociativeOp(
                                AssociativePattern(min(x, y), t.max(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y + Cast::make(Int(16), z))},
                                true));

        // f(x) = f(x) + g(rx) + y + z
        check_associativity("f", {x}, {y + z + f_call_0},
                            AssociativeOp(
                                AssociativePattern(x + y, make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y + z)},
                                true));

        // f(x) = max(y, f(x))
        check_associativity("f", {x}, {max(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = max(f(x) + g(rx), g(rx)) -> not associative
        check_associativity("f", {x}, {max(f_call_0 + g_call_0, g_call_0)}, AssociativeOp());

        // f(x) = max(f(x) + g(rx), f(x) - 3) -> f(x) + max(g(rx) - 3)
        check_associativity("f", {x}, {max(f_call_0 + g_call_0, f_call_0 - 3)},
                            AssociativeOp(
                                AssociativePattern(x + y, 0, true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", max(g_call_0, -3))},
                                true));

        // f(x) = max(max(min(f(x), g(rx) + 2), f(x)), g(rx) + 2) -> can be simplified into max(f(x), g(rx) + 2)
        check_associativity("f", {x}, {max(max(min(f_call_0, g_call_0 + 2), f_call_0), g_call_0 + 2)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", g_call_0 + 2)},
                                true));

        // f(x) = max(x0, f(x)) -> x0 may conflict with the wildcard associative op pattern
        Expr x0 = Variable::make(t, "x0");
        check_associativity("f", {x}, {max(x0, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", x0)},
                                true));
    }

    {
        // Tests for multi-dimensional reduction (with mixed types)
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr rx = Variable::make(t, "rx");

        vector<Type> ts = {Int(32), Int(32), Float(32)};
        vector<Expr> xs(3), ys(3), zs(3);
        for (size_t i = 0; i < xs.size(); ++i) {
            xs[i] = Variable::make(ts[i], "x" + std::to_string(i));
            ys[i] = Variable::make(ts[i], "y" + std::to_string(i));
            zs[i] = Variable::make(ts[i], "z" + std::to_string(i));
        }

        Expr f_call_0 = Call::make(ts[0], "f", {x}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr f_call_1 = Call::make(ts[1], "f", {x}, Call::CallType::Halide, FunctionPtr(), 1);
        Expr f_call_2 = Call::make(ts[2], "f", {x}, Call::CallType::Halide, FunctionPtr(), 2);
        Expr g_call_0 = Call::make(ts[0], "g", {rx}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr g_call_1 = Call::make(ts[1], "g", {rx}, Call::CallType::Halide, FunctionPtr(), 1);

        // f(x) = Tuple(f(x)[0], f(x)[2] + z)
        check_associativity("f", {x}, {f_call_0, f_call_1 + cast(ts[1], z)},
                            AssociativeOp(
                                AssociativePattern({xs[0], xs[1] + ys[1]},
                                                   {make_const(ts[0], 0), make_const(ts[1], 0)},
                                                   true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("", Expr()), Replacement("y1", cast(ts[1], z))},
                                true));

        // f(x) = Tuple(min(f(x)[0], g(rx)), f(x)[1]*g(x)*2, f(x)[2] + z)
        check_associativity("f", {x}, {min(f_call_0, g_call_0), f_call_1 * g_call_0 * 2, f_call_2 + cast(ts[2], z)},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), xs[1] * ys[1], xs[2] + ys[2]},
                                    {ts[0].max(), make_const(ts[1], 1), make_const(ts[2], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1), Replacement("x2", f_call_2)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_0 * 2), Replacement("y2", cast(ts[2], z))},
                                true));

        // Complex multiplication: f(x) = Tuple(f(x)[0]*g(r.x)[0] - f(x)[1]*g(r.x)[1], f(x)[0]*g(r.x)[1] + f(x)[1]*g(r.x)[0])
        check_associativity("f", {x}, {f_call_0 * g_call_0 - f_call_1 * g_call_1, f_call_0 * g_call_1 + f_call_1 * g_call_0},
                            AssociativeOp(
                                AssociativePattern(
                                    {xs[0] * ys[0] - ys[1] * xs[1], xs[1] * ys[0] + ys[1] * xs[0]},
                                    {make_const(ts[0], 1), make_const(ts[1], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_1)},
                                true));

        // 1D argmin: f(x) = Tuple(min(f(x)[0], g(r.x)[0]), select(f(x)[0] < g(r.x)[0], f(x)[1], g(r.x)[1])
        check_associativity("f", {x}, {min(f_call_0, g_call_0), select(f_call_0 < g_call_0, f_call_1, g_call_1)},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), select(xs[0] < ys[0], xs[1], ys[1])},
                                    {ts[0].max(), make_const(ts[1], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_1)},
                                true));
    }

    {
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr rx = Variable::make(t, "rx");
        Expr ry = Variable::make(t, "ry");

        vector<Type> ts = {UInt(8), Int(32), Int(16), Float(32)};
        vector<Expr> xs(4), ys(4), zs(4);
        for (size_t i = 0; i < xs.size(); ++i) {
            xs[i] = Variable::make(ts[i], "x" + std::to_string(i));
            ys[i] = Variable::make(ts[i], "y" + std::to_string(i));
            zs[i] = Variable::make(ts[i], "z" + std::to_string(i));
        }

        Expr f_xy_call_0 = Call::make(ts[0], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr f_xy_call_1 = Call::make(ts[1], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 1);
        Expr f_xy_call_2 = Call::make(ts[2], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 2);
        Expr f_xy_call_3 = Call::make(ts[3], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 3);
        Expr g_xy_call_0 = Call::make(ts[0], "g", {rx, ry}, Call::CallType::Halide, FunctionPtr(), 0);

        // 2D argmin + sum
        // f(x, y) = Tuple(min(f(x, y)[0], g(r.x, r.y)[0]),
        //                 f(x, y)[1] + r.x,
        //                 select(f(x, y)[0] < g(r.x, r.y)[0], f(x)[2], r.x),
        //                 select(f(x, y)[0] < g(r.x, r.y)[0], f(x)[3], r.y))
        check_associativity("f", {x, y},
                            {min(f_xy_call_0, g_xy_call_0),
                             f_xy_call_1 + rx,
                             select(f_xy_call_0 < g_xy_call_0, f_xy_call_2, cast(Int(16), rx)),
                             select(f_xy_call_0 < g_xy_call_0, f_xy_call_3, cast(Float(32), ry))},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), xs[1] + ys[1], select(xs[0] < ys[0], xs[2], ys[2]), select(xs[0] < ys[0], xs[3], ys[3])},
                                    {ts[0].max(), make_const(ts[1], 0), make_const(ts[2], 0), make_const(ts[3], 0)},
                                    true),
                                {Replacement("x0", f_xy_call_0), Replacement("x1", f_xy_call_1),
                                 Replacement("x2", f_xy_call_2), Replacement("x3", f_xy_call_3)},
                                {Replacement("y0", g_xy_call_0), Replacement("y1", rx),
                                 Replacement("y2", cast(Int(16), rx)), Replacement("y3", cast(Float(32), ry))},
                                true));
    }

    std::cout << "Associativity test passed\n";
}

}  // namespace Internal
}  // namespace Halide
