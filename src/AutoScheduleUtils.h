#ifndef HALIDE_INTERNAL_AUTO_SCHEDULE_UTILS_H
#define HALIDE_INTERNAL_AUTO_SCHEDULE_UTILS_H

/** \file
 *
 * Defines util functions that used by auto scheduler.
 */

#include <set>
#include <limits>

#include "Bounds.h"
#include "Interval.h"
#include "IRVisitor.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

typedef std::map<std::string, Interval> DimBounds;

const int64_t unknown = std::numeric_limits<int64_t>::min();

/** Visitor for keeping track of functions that are directly called and the
 * arguments with which they are called. */
class FindAllCalls : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *call) {
        if (call->call_type == Call::Halide || call->call_type == Call::Image) {
            funcs_called.insert(call->name);
            call_args.push_back(std::make_pair(call->name, call->args));
        }
        for (size_t i = 0; i < call->args.size(); i++) {
            call->args[i].accept(this);
        }
    }
public:
    std::set<std::string> funcs_called;
    std::vector<std::pair<std::string, std::vector<Expr>>> call_args;
};

/** Return a map of estimates on some variables. */
class GetVarEstimates : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *var) {
        if (var->param.defined() && !var->param.is_buffer() &&
            var->param.get_estimate().defined()) {
            var_estimates[var->param.name()] = var->param.get_estimate();
        }
    }
public:
    std::map<std::string, Expr> var_estimates;
};


/** Substitute every variable with its estimate if specified. */
class SubstituteVarEstimates: public IRMutator {
    using IRMutator::visit;

    void visit(const Variable *var) {
        if (var->param.defined() && !var->param.is_buffer() &&
            var->param.get_estimate().defined()) {
            expr = var->param.get_estimate();
        } else {
            expr = var;
        }
    }
};

/** Return the size of an interval. Return an undefined expr if the interval
 * is unbounded. */
Expr get_extent(const Interval &i);

/** Return the size of an n-d box. */
Expr box_size(const Box &b);

/** Helper function to print the bounds of a region. */
void disp_regions(const std::map<std::string, Box> &regions);

/** Return the corresponding definition of a function given the stage. */
Definition get_stage_definition(const Function &f, int stage_num);

/** Add partial load costs to the corresponding function in the result costs. */
void combine_load_costs(std::map<std::string, Expr> &result,
                        const std::map<std::string, Expr> &partial);

/** Return the required bounds of an intermediate stage (f, stage_num) of
 * function 'f' given the bounds of the pure dimensions. */
DimBounds get_stage_bounds(Function f, int stage_num, const DimBounds &pure_bounds);

/** Return the required bounds for all the stages of the function 'f'. Each entry
 * in the returned vector corresponds to a stage. */
std::vector<DimBounds> get_stage_bounds(Function f, const DimBounds &pure_bounds);

/** Recursively inline all the functions in the set 'inlines' into the
 * expression 'e' and return the resulting expression. */
Expr perform_inline(Expr e, const std::map<std::string, Function> &env,
                    const std::set<std::string> &inlines = std::set<std::string>());

/** Return all functions that are directly called by a function stage (f, stage). */
std::set<std::string> get_parents(Function f, int stage);

/** Return value of element within a map. This will assert if the element is not
 * in the map. */
// @{
template<typename K, typename V>
V get_element(const std::map<K, V> &m, const K &key) {
    const auto &iter = m.find(key);
    internal_assert(iter != m.end());
    return iter->second;
}

template<typename K, typename V>
V &get_element(std::map<K, V> &m, const K &key) {
    const auto &iter = m.find(key);
    internal_assert(iter != m.end());
    return iter->second;
}
// @}

}
}

#endif
