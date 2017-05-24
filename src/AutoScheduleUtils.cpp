#include "AutoScheduleUtils.h"
#include "Inline.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::set;
using std::vector;

Expr get_extent(const Interval &i) {
    if (!i.is_bounded()) {
        return Expr();
    }
    Expr extent = simplify(i.max - i.min + 1);
    // TODO(psuriana): should make this deal with symbolic constants
    if (is_const(extent)) {
        // The extent only makes sense when the max >= min otherwise
        // it is considered to be zero.
        return simplify(max(make_zero(Int(64)), extent));
    }
    return Expr();
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
    if (stage_num == 0) {
        return f.definition();
    }
    internal_assert((int)f.updates().size() >= stage_num);
    return f.update(stage_num - 1);
}

DimBounds get_stage_bounds(Function f, int stage_num, const DimBounds &pure_bounds) {
    DimBounds bounds;
    Definition def = get_stage_definition(f, stage_num);

    // Assume that the domain of the pure vars across all the update
    // definitions is the same. This may not be true and can result in
    // over estimation of the extent.
    for (const auto &b : pure_bounds) {
        bounds[b.first] = b.second;
    }

    for (const auto &rvar : def.schedule().rvars()) {
        Expr lower = SubstituteVarEstimates().mutate(rvar.min);
        Expr upper = SubstituteVarEstimates().mutate(rvar.min + rvar.extent - 1);
        bounds.emplace(rvar.var, Interval(simplify(lower),simplify(upper)));
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
                    const set<string> &inlines) {
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
        const set<string> &calls = find.funcs_called;
        // Check if any of the calls are in the set of functions to be inlined.
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
                    buf = arg.image_param.get_buffer();
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

}
}
