#include "ExprToJuliaLP.h"
#include <map>
#include <set>
#include <utility>      // std::pair
#include "generate_bounds_cegis.h"

using namespace Halide;
using namespace Halide::Internal;

class ExprToJuliaLP : public IRVisitor {
    using IRVisitor::visit;

    std::ostringstream current;
    // std::string variable_str;
    // std::string constraint_str;

    std::string clear_current() {
        std::string str = current.str();
        current.str(std::string());
        return str;
    }

    int counter = 0;
    std::string get_new_var_name() {
        return "_t" + std::to_string(counter++);
    }
    typedef std::pair<std::string, std::string> string_pair;

    std::set<std::string> present_exprs;
    std::map<std::string, string_pair> names_to_maxs;
    std::map<std::string, string_pair> names_to_mins;
    std::map<std::string, std::string> names_to_conditionals;

    // Indicator Name -> ((Var name, cond), (a, b))
    std::map<std::string, std::pair<string_pair, string_pair>> names_of_indicators;

    std::map<std::string, Expr> possibly_correlated_expressions;

    void visit(const IntImm *op) override {
        current << op->value;
    }

    void visit(const UIntImm *op) override {
        if (op->type.is_bool()) {
            if (op->value) {
                current << " true ";
            } else {
                current <<  " false ";
            }
        } else {
            current << op->value;
        }
    }

    void fail(std::string thing) {
        std::string str = "AJ didn't implement " + thing + " yet, but I guess he should";
        assert(false && str.c_str());
    }

    void visit(const FloatImm *imm) override {
        fail("FloatImm");
    }

    void visit(const StringImm *imm) override {
        fail("StringImm");
    }

    template <typename Op>
    void regular_binary_op(Op op, std::string oper) {
        current << "(";
        op->a.accept(this);
        current << " " << oper << " ";
        op->b.accept(this);
        current << ")";
    }

    template <typename Op>
    void flipped_binary_op(Op op, std::string oper) {
        current << "(";
        op->b.accept(this);
        current << " " << oper << " ";
        op->a.accept(this);
        current << ")";
    }


    void visit(const Add *op) override {
        regular_binary_op(op, "+");
    }

    void visit(const Sub *op) override {
        regular_binary_op(op, "-");
    }

    void visit(const Mul *op) override {
        regular_binary_op(op, "*");
    }

    void visit(const Div *op) override {
        fail("Div");
    }

    void visit(const Mod *op) override {
        fail("Mod");
    }

    void visit(const EQ *op) override {
        regular_binary_op(op, "==");
    }

    void visit(const NE *op) override {
        fail("NE (!=)");
    }

    void visit(const LT *op) override {
        // TODO: FIX THIS
        // flipped_binary_op(op, ">=");
        regular_binary_op(op, "<");
    }

    void visit(const LE *op) override {
        regular_binary_op(op, "<=");
    }

    void visit(const GT *op) override {
        // TODO: FIX THIS
        // flipped_binary_op(op, "<=");
        regular_binary_op(op, ">");
    }

    void visit(const GE *op) override {
        regular_binary_op(op, ">=");
    }

    void visit(const And *op) override {
        fail("And");
    }

    void visit(const Or *op) override {
        fail("Or");
    }

    void visit(const Not *op) override {
        fail("Not");
    }

    void visit(const Cast *op) override {
        fail("Cast");
    }

    void visit(const Call *op) override {
        fail("Call");
    }

    void visit(const Ramp *op) override {
        fail("Ramp");
    }

    void visit(const Let *op) override {
        fail("Let");
    }

    void visit(const Broadcast *op) override {
        fail("Broadcast");
    }


    // here's where things get messy!
    void visit(const Max *op) override {
        const std::string name = get_new_var_name();
        const std::string cond_name = get_new_var_name();
        possibly_correlated_expressions[name] = Expr(op);
        current << name;
        // Save this for later
        std::string keeper = clear_current();

        // get each value
        op->a.accept(this);
        std::string a_str = clear_current();
        op->b.accept(this);
        std::string b_str = clear_current();

        names_to_maxs[name] = std::make_pair(std::move(a_str), std::move(b_str));
        // For big M method
        names_to_conditionals[name] = cond_name;

        // Re-add what the expression is.
        current << keeper;
    }

    void visit(const Min *op) override {
        const std::string name = get_new_var_name();
        const std::string cond_name = get_new_var_name();
        possibly_correlated_expressions[name] = Expr(op);
        current << name;
        // Save this for later
        std::string keeper = clear_current();

        // get each value
        op->a.accept(this);
        std::string a_str = clear_current();
        op->b.accept(this);
        std::string b_str = clear_current();

        names_to_mins[name] = std::make_pair(std::move(a_str), std::move(b_str));
        // For big M method
        names_to_conditionals[name] = cond_name;

        // Re-add what the expression is.
        current << keeper;
    }

    void visit(const Select *op) override {
        std::string indicator_name = get_new_var_name();
        std::string var_name = get_new_var_name();
        
        current << var_name;
        // Save this for later
        std::string keeper = clear_current();


        // Added Constraint.
        op->condition.accept(this);
        std::string cond_str = clear_current();

        op->true_value.accept(this);
        std::string true_str = clear_current();

        op->false_value.accept(this);
        std::string false_str = clear_current();

        names_of_indicators[indicator_name] = std::make_pair(std::make_pair(std::move(var_name), std::move(cond_str)),
                                                             std::make_pair(std::move(true_str), std::move(false_str)));

        // Re-add what the expression is.
        current << keeper;
    }

    void visit(const Variable *op) override {
        present_exprs.insert(op->name);
        current << op->name;
    }


    Scope<Interval> scope;
public:
    ExprToJuliaLP(const Expr &expr) : scope(make_symbolic_scope(expr)) {}

    std::string compile_result(bool upper) {
        std::string objective = clear_current();

        current << "# TODO: DECLARE A MODEL\n\n";
        current << "# TODO: change M\n\n";

        current << "M = " << (1 << 20) << "\n";

        current << "# Variable declarations\n";

        // Declare variables.
        for (const auto &str : present_exprs) {
            current << "@variable(model, " << str << ")\n";
        }

        for (const auto &p : names_to_maxs) {
            current << "@variable(model, " << p.first << ")\n";
        }

        for (const auto &p : names_to_mins) {
            current << "@variable(model, " << p.first << ")\n";
        }

        for (const auto &p : names_to_conditionals) {
            // current << "@variable(model, 0 <= " << p.second << " <= 1)\n";
            current << "@variable(model, " << p.second << ", Bin)\n";
        }

        current << "\n# Indicator variables for selects\n";
        for (const auto &p : names_of_indicators) {
            current << "@variable(model, " << p.first << ", Bin)\n";
            current << "@variable(model, " << p.second.first.first << ")\n";
        }

        current << "\n# Add maximum constraints\n";

        // Construct min constraints
        for (const auto &p : names_to_maxs) {
            current << "@constraint(model, " << p.first << " >= " << p.second.first << ")\n";
            current << "@constraint(model, " << p.first << " >= " << p.second.second << ")\n";

            // Big M method
            current << "# Big M variable constraints\n";
            current << "@constraint(model, " << p.first << " <= " << p.second.first << " + (M * " << names_to_conditionals[p.first] << "))\n";
            current << "@constraint(model, " << p.first << " <= " << p.second.second << " + (M * (1 - " << names_to_conditionals[p.first] << ")))\n\n";
        }

        current << "# Add minimum constraints\n";

        for (const auto &p : names_to_mins) {
            current << "@constraint(model, " << p.first << " <= " << p.second.first << ")\n";
            current << "@constraint(model, " << p.first << " <= " << p.second.second << ")\n";

            // Big M method
            current << "# Big M variable constraints\n";
            current << "@constraint(model, " << p.first << " >= " << p.second.first << " - (M * " << names_to_conditionals[p.first] << "))\n";
            current << "@constraint(model, " << p.first << " >= " << p.second.second << " - (M * (1 - " << names_to_conditionals[p.first] << ")))\n\n";
        }

        /*
        current << "# Add possibly correlated (loose) constraints.\n";
        current << "# Some of these will be trivial, because I didn't come up with a smarter loop.\n\n";

        // This is n^2, oops....

        for (const auto &p1 : possibly_correlated_expressions) {
            Expr e1 = p1.second;
            for (const auto &p2 : possibly_correlated_expressions) {
                Expr e2 = p2.second;
                if (e1.same_as(e2)) {
                  continue;
                } else {
                    Expr simple_diff = simplify(e1 - e2);
                    Interval interval = find_constant_bounds(simple_diff, scope);
                    if (interval.has_lower_bound()) {
                        current << "@constraint(model, " << p1.first << " - " << p2.first <<
                            " >= " << interval.min << ")\n";
                    }

                    if (interval.has_upper_bound()) {
                        current << "@constraint(model, " << p1.first << " - " << p2.first <<
                            " <= " << interval.max << ")\n";
                    }
                }
            }
            current << "# Finished with: " << p1.first << "'s correlated differences\n\n"; 
        }
        */

        current << "# Add select binary constraints\n";

        for (const auto &p : names_of_indicators) {
            // Indicator variable implies condition
            std::string var_name = p.second.first.first;
            std::string cond_value = p.second.first.second;
            std::string true_value = p.second.second.first;
            std::string false_value = p.second.second.second;
            current << "@constraint(model, " << p.first << " => { " << cond_value << " })\n";
            current << "@constraint(model, !" << p.first << " => { !(" << cond_value << ") })\n";
            // Indicator variable implies true equality
            current << "@constraint(model, " << p.first << " => { " << var_name << " == " << true_value << "})\n";
            current << "@constraint(model, !" << p.first << " => { " << var_name << " == " << false_value << "})\n";
        }

        current << "\n# Now optimize.\n";

        current << "@objective(model, " << (upper ? "Max" : "Min") << ", " << objective << ")\n";
        current << "print(model)\n";
        current << "optimize!(model)\n\n";
        return clear_current();
    }
};



std::string expr_to_julia_lp(const Expr &expr, bool upper) {
    ExprToJuliaLP to_lp(expr);
    expr.accept(&to_lp);
    return to_lp.compile_result(upper);
}