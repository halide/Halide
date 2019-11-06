#include <sstream>

#include "AutoScheduleUtils.h"
#include "IREquality.h"
#include "ImageParam.h"
#include "Inline.h"
#include "Param.h"
#include "Simplify.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {
class SubstituteVarEstimates : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Variable *var) override {
        if (var->param.defined() && var->param.is_buffer()) {
            // This is a var associated with an ImageParam object. This
            // should be something of the form XXX.min.[dim_index] or
            // XXX.extent.[dim_index]
            std::vector<std::string> v = split_string(var->name, ".");
            user_assert(v.size() >= 3);
            int d = string_to_int(v[v.size() - 1]);
            if (v[v.size() - 2] == "min") {
                Expr est = var->param.min_constraint_estimate(d);
                return est.defined() ? est : var;
            } else {
                internal_assert(v[v.size() - 2] == "extent");
                Expr est = var->param.extent_constraint_estimate(d);
                return est.defined() ? est : var;
            }
        } else if (var->param.defined() && !var->param.is_buffer() &&
                   var->param.estimate().defined()) {
            // This is a var from a Param object
            return var->param.estimate();
        } else {
            return var;
        }
    }
};
}  // anonymous namespace

Expr substitute_var_estimates(Expr e) {
    if (!e.defined()) return e;
    return simplify(SubstituteVarEstimates().mutate(e));
}

Stmt substitute_var_estimates(Stmt s) {
    if (!s.defined()) return s;
    return simplify(SubstituteVarEstimates().mutate(s));
}

int string_to_int(const string &s) {
    std::istringstream iss(s);
    int i;
    iss >> i;
    user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << s;
    return i;
}

Expr get_extent(const Interval &i) {
    if (!i.is_bounded()) {
        return Expr();
    }
    return simplify(i.max - i.min + 1);
}

Expr box_size(const Box &b) {
    Expr size = make_one(Int(64));
    for (size_t i = 0; i < b.size(); i++) {
        Expr extent = get_extent(b[i]);
        if (extent.defined() && size.defined()) {
            size *= extent;
        } else if (is_zero(extent)) {
            return make_zero(Int(64));
        } else {
            return Expr();
        }
    }
    return simplify(size);
}

void combine_load_costs(map<string, Expr> &result, const map<string, Expr> &partial) {
    for (const auto &kv : partial) {
        auto iter = result.find(kv.first);
        if (iter == result.end()) {
            result.emplace(kv.first, kv.second);
        } else {
            if (!iter->second.defined()) {
                continue;
            } else if (!kv.second.defined()) {
                iter->second = Expr();
            } else {
                iter->second = simplify(iter->second + kv.second);
            }
        }
    }
}

Definition get_stage_definition(const Function &f, int stage_num) {
    internal_assert(!f.has_extern_definition());
    if (stage_num == 0) {
        return f.definition();
    }
    internal_assert((int)f.updates().size() >= stage_num);
    return f.update(stage_num - 1);
}

vector<Dim> &get_stage_dims(const Function &f, int stage_num) {
    static vector<Dim> outermost_only =
        {{Var::outermost().name(), ForType::Serial, DeviceAPI::None, Dim::Type::PureVar}};
    if (f.has_extern_definition()) {
        return outermost_only;
    }
    Definition def = get_stage_definition(f, stage_num);
    internal_assert(def.defined());
    return def.schedule().dims();
}

DimBounds get_stage_bounds(Function f, int stage_num, const DimBounds &pure_bounds) {
    DimBounds bounds;
    // Assume that the domain of the pure vars across all the update
    // definitions is the same. This may not be true and can result in
    // over estimation of the extent.
    for (const auto &b : pure_bounds) {
        bounds[b.first] = b.second;
    }

    if (!f.has_extern_definition()) {
        Definition def = get_stage_definition(f, stage_num);
        for (const auto &rvar : def.schedule().rvars()) {
            Expr lower = substitute_var_estimates(rvar.min);
            Expr upper = substitute_var_estimates(rvar.min + rvar.extent - 1);
            bounds.emplace(rvar.var, Interval(lower, upper));
        }
    }

    return bounds;
}

vector<DimBounds> get_stage_bounds(Function f, const DimBounds &pure_bounds) {
    vector<DimBounds> stage_bounds;
    size_t num_stages = f.updates().size() + 1;
    for (size_t s = 0; s < num_stages; s++) {
        stage_bounds.push_back(get_stage_bounds(f, s, pure_bounds));
    }
    return stage_bounds;
}

Expr perform_inline(Expr e, const map<string, Function> &env,
                    const set<string> &inlines,
                    const vector<string> &order) {
    if (inlines.empty()) {
        return e;
    }

    bool funcs_to_inline = false;
    Expr inlined_expr = e;

    do {
        funcs_to_inline = false;
        // Find all the function calls in the current expression.
        FindAllCalls find;
        inlined_expr.accept(&find);
        const set<string> &calls_unsorted = find.funcs_called;

        vector<string> calls(calls_unsorted.begin(), calls_unsorted.end());
        // Sort 'calls' based on the realization order in descending order
        // if provided (i.e. last to be realized comes first).
        if (!order.empty()) {
            std::sort(calls.begin(), calls.end(),
                      [&order](const string &lhs, const string &rhs) {
                          const auto &iter_lhs = std::find(order.begin(), order.end(), lhs);
                          const auto &iter_rhs = std::find(order.begin(), order.end(), rhs);
                          return iter_lhs > iter_rhs;
                      });
        }

        // Check if any of the calls are in the set of functions to be inlined.
        // Inline from the last function to be realized to avoid extra
        // inlining works.
        for (const auto &call : calls) {
            if (inlines.find(call) != inlines.end()) {
                Function prod_func = env.at(call);
                // Impure functions cannot be inlined.
                internal_assert(prod_func.is_pure());
                // Inline the function call and set the flag to check for
                // further inlining opportunities.
                inlined_expr = inline_function(inlined_expr, prod_func);
                funcs_to_inline = true;
                break;
            }
        }
    } while (funcs_to_inline);

    return inlined_expr;
}

set<string> get_parents(Function f, int stage) {
    set<string> parents;
    if (f.has_extern_definition()) {
        internal_assert(stage == 0);
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                string prod_name = Function(arg.func).name();
                parents.insert(prod_name);
            } else if (arg.is_expr()) {
                FindAllCalls find;
                arg.expr.accept(&find);
                parents.insert(find.funcs_called.begin(), find.funcs_called.end());
            } else if (arg.is_image_param() || arg.is_buffer()) {
                Buffer<> buf;
                if (arg.is_image_param()) {
                    buf = arg.image_param.buffer();
                } else {
                    buf = arg.buffer;
                }
                parents.insert(buf.name());
            }
        }
    } else {
        FindAllCalls find;
        Definition def = get_stage_definition(f, stage);
        def.accept(&find);
        parents.insert(find.funcs_called.begin(), find.funcs_called.end());
    }
    return parents;
}

void disp_regions(const map<string, Box> &regions) {
    for (const auto &reg : regions) {
        debug(0) << reg.first << " -> ";
        debug(0) << reg.second;
        debug(0) << "\n";
    }
}

namespace {
void check(Expr input, Expr expected) {
    Expr result = simplify(substitute_var_estimates(input));
    expected = simplify(expected);
    if (!equal(result, expected)) {
        internal_error
            << "\nsubstitute_var_estimates() failure:\n"
            << "Input: " << input << '\n'
            << "Result: " << result << '\n'
            << "Expected result: " << expected << '\n';
    }
}
}  // anonymous namespace

void propagate_estimate_test() {
    Param<int> p;
    p.set_estimate(10);

    ImageParam img(Int(32), 2);
    img.dim(0).set_estimate(-3, 33);
    img.dim(1).set_estimate(5, 55);

    Var x("x"), y("y");
    check(p + x + y, x + y + 10);
    check(img.dim(0).min() + img.dim(1).min() + x, x + 2);
    check(img.dim(0).extent() + img.dim(1).min() + img.dim(1).extent() * x, 55 * x + 38);

    std::cout << "Propagate estimate test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide
