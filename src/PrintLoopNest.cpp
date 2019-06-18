#include "PrintLoopNest.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IRPrinter.h"
#include "RealizationOrder.h"
#include "ScheduleFunctions.h"
#include "Simplify.h"
#include "SimplifySpecializations.h"
#include "Target.h"
#include "WrapCalls.h"

#include <tuple>

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

class PrintLoopNest : public IRVisitor {
public:
    PrintLoopNest(std::ostream &output, const map<string, Function> &e) :
        out(output), env(e), indent(0) {}
private:
    std::ostream &out;
    const map<string, Function> &env;
    int indent;

    Scope<Expr> constants;

    using IRVisitor::visit;

    void do_indent() {
        for (int i = 0; i < indent; i++) {
            out << ' ';
        }
    }

    string simplify_var_name(const string &s) {
        return simplify_name(s, false);
    }

    string simplify_func_name(const string &s) {
        return simplify_name(s, true);
    }

    string simplify_name(const string &s, bool is_func) {
        // Trim the function name and stage number from the for loop,
        // as well as any uniqueness $n suffixes on variables.
        std::ostringstream trimmed_name;

        bool keep = is_func;
        int dot_count = 0;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '.') {
                dot_count++;
                if (dot_count >= 2) {
                    if (dot_count == 2) {
                        i++;
                    }
                    keep = true;
                }
            }
            if (s[i] == '$') {
                keep = false;
            }
            if (keep) {
                trimmed_name << s[i];
            }
        }

        return trimmed_name.str();
    }

    void visit(const For *op) override {
        do_indent();

        out << op->for_type << ' ' << simplify_var_name(op->name);

        // If the min or extent are constants, print them. At this
        // stage they're all variables.
        Expr min_val = op->min, extent_val = op->extent;
        const Variable *min_var = min_val.as<Variable>();
        const Variable *extent_var = extent_val.as<Variable>();
        if (min_var && constants.contains(min_var->name)) {
            min_val = constants.get(min_var->name);
        }

        if (extent_var && constants.contains(extent_var->name)) {
            extent_val = constants.get(extent_var->name);
        }

        if (extent_val.defined() && is_const(extent_val) &&
            min_val.defined() && is_const(min_val)) {
            Expr max_val = simplify(min_val + extent_val - 1);
            out << " in [" << min_val << ", " << max_val << "]";
        }

        out << op->device_api;

        out << ":\n";
        indent += 2;
        op->body.accept(this);
        indent -= 2;
    }

    void visit(const Realize *op) override {
        // If the storage and compute levels for this function are
        // distinct, print the store level too.
        auto it = env.find(op->name);
        if (it != env.end() &&
            !(it->second.schedule().store_level() ==
              it->second.schedule().compute_level())) {
            do_indent();
            out << "store " << simplify_func_name(op->name) << ":\n";
            indent += 2;
            op->body.accept(this);
            indent -= 2;
        } else {
            op->body.accept(this);
        }
    }

    void visit(const ProducerConsumer *op) override {
        do_indent();
        if (op->is_producer) {
            out << "produce " << simplify_func_name(op->name) << ":\n";
        } else {
            out << "consume " << simplify_func_name(op->name) << ":\n";
        }
        indent += 2;
        op->body.accept(this);
        indent -= 2;
    }

    void visit(const Provide *op) override {
        do_indent();
        out << simplify_func_name(op->name) << "(...) = ...\n";
    }

    void visit(const LetStmt *op) override {
        if (is_const(op->value)) {
            constants.push(op->name, op->value);
            op->body.accept(this);
            constants.pop(op->name);
        } else {
            op->body.accept(this);
        }
    }
};

string print_loop_nest(const vector<Function> &output_funcs) {
    // Do the first part of lowering:

    // Compute an environment
    map<string, Function> env;
    for (const Function& f : output_funcs) {
        populate_environment(f, env);
    }

    // Create a deep-copy of the entire graph of Funcs.
    vector<Function> outputs;
    std::tie(outputs, env) = deep_copy(output_funcs, env);

    // Output functions should all be computed and stored at root.
    for (const Function& f: outputs) {
        Func(f).compute_root().store_root();
    }

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Substitute in wrapper Funcs
    env = wrap_func_calls(env);

    // Compute a realization order and determine group of functions which loops
    // are to be fused together
    const auto &fused_groups = realization_order(outputs, env).second;

    // Try to simplify the RHS/LHS of a function definition by propagating its
    // specializations' conditions
    simplify_specializations(env);

    // For the purposes of printing the loop nest, we don't want to
    // worry about which features are and aren't enabled.
    Target target = get_host_target();
    for (DeviceAPI api : all_device_apis) {
        target.set_feature(target_feature_for_device_api(DeviceAPI(api)));
    }

    bool any_memoized = false;
    // Schedule the functions.
    Stmt s = schedule_functions(outputs, fused_groups, env, target, any_memoized);

    // Now convert that to pseudocode
    std::ostringstream sstr;
    PrintLoopNest pln(sstr, env);
    s.accept(&pln);
    return sstr.str();
}

}  // namespace Internal
}  // namespace Halide
