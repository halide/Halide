#ifndef HALIDE_INTERNAL_REGION_COSTS_H
#define HALIDE_INTERNAL_REGION_COSTS_H

/** \file
 *
 * Defines RegionCosts - used by the auto scheduler to query the cost of
 * computing function regions, and related classes to analyze pipeline
 * functions.
 */

#include<set>
#include<limits>

#include "IR.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "Expr.h"
#include "Function.h"
#include "Interval.h"
#include "Bounds.h"
#include "Reduction.h"
#include "Definition.h"
#include "Inline.h"
#include "Simplify.h"
#include "FindCalls.h"
#include "RealizationOrder.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::set;
using std::vector;
using std::pair;
using std::make_pair;

typedef map<string, Interval> DimBounds;

const int debug_level = 3;
const int64_t unknown = std::numeric_limits<int64_t>::min();

/** Visitor for keeping track of functions that are called and the arguments
 * with which they are called. */
struct FindAllCalls : public IRVisitor {
    set<string> funcs_called;
    vector<pair<string, vector<Expr>>> call_args;
    using IRVisitor::visit;

    void visit(const Call *call) {
        if (call->call_type == Call::Halide || call->call_type == Call::Image) {
            funcs_called.insert(call->name);
            pair<string, vector<Expr>> arg_exprs = make_pair(call->name, call->args);
            call_args.push_back(arg_exprs);
        }
        for (size_t i = 0; (i < call->args.size()); i++) {
            call->args[i].accept(this);
        }
    }
};

/** Visitor for keeping track of the all the input images accessed their types. */
struct FindImageInputs : public IRVisitor {
    map<string, Type> input_type;
    using IRVisitor::visit;

    void visit(const Call *call) {
        if (call->call_type == Call::Image) {
            input_type[call->name] = call->type;
        }
        for (size_t i = 0; (i < call->args.size()); i++) {
            call->args[i].accept(this);
        }
    }
};

struct Cost {
    // Estimate of cycles spent doing arithmetic.
    int64_t arith;
    // Estimate of bytes loaded.
    int64_t memory;

    Cost(int64_t arith, int64_t memory) :
        arith(arith), memory(memory) {}

    Cost() {
        arith = unknown;
        memory = unknown;
    }
};

/** Auto scheduling component which is used to assign costs for computing a
 * region of a function or one of its stages.*/
struct RegionCosts {
    /** Environment map which contains all the functions in the pipeline. */
    const map<string, Function> &env;
    /** Map containing the cost of computing a value in each stage of a
     * function.  The number of entries in the vector is equal to the number of
     * stages in a function. */
    map<string, vector<Cost>> func_cost;
    map<string, Type> inputs;

    /** Returns the cost of producing a region (specified by bounds) of a
     * function stage (specified by func and stage). inlines specifies names of
     * all the inlined functions. */
    Cost stage_region_cost(string func, int stage, DimBounds &bounds,
                           const set<string> &inlines = set<string>());

    /** Returns the cost of producing a region of a function stage (specified
     * by func and stage). inlines specifies names of all the inlined
     * functions. */
    Cost stage_region_cost(string func, int stage, Box &region,
                           const set<string> &inlines = set<string>());

    /** Returns the cost of producing a region of function func. Adds up the
     * cost of all the stages of func required to produce the region. inlines
     * specifies names of all the inlined functions. */
    Cost region_cost(string func, Box &region,
                     const set<string> &inlines = set<string>());

    /** Same as region cost but computes the total cost of a many function
     * regions. */
    Cost region_cost(map<string, Box> &regions,
                     const set<string> &inlines = set<string>());

    /** Computes the cost of producing a single value of each stage of the f.
     * inlines specifies names of all the inlined functions. Returns a vector
     * of costs. Each entry in the vector corresponds to stage in f. */
    vector<Cost> get_func_cost(const Function &f,
                               const set<string> &inlines = set<string>());

    /** Computes the memory costs for computing a region (specified by bounds)
     * of a function stage (specified by func and stage). Returns a map
     * containing the costs incurred to access each of the functions required
     * to produce func. */
    map<string, int64_t>
        stage_detailed_load_costs(string func, int stage, DimBounds &bounds,
                                  const set<string> &inlines = set<string>());

    /** Returns a map containing the costs incurred to access each of the functions
     * required to produce a single value of a function stage. */
    map<string, int64_t>
        stage_detailed_load_costs(string func, int stage,
                                  const set<string> &inlines = set<string>());

    /** Same as stage_detailed_load_costs above but computes the cost for a region
     * of func. */
    map<string, int64_t>
        detailed_load_costs(string func, const Box &region,
                            const set<string> &inlines = set<string>());

    /** Same as detailed_load_costs above but computes the cost for many function
     * regions and aggregates them. */
    map<string, int64_t>
        detailed_load_costs(const map<string, Box> &regions,
                            const set<string> &inlines = set<string>());

    /** Returns the size of the region of func in bytes. */
    int64_t region_size(string func, const Box &region);

    /** Returns the size of the peak amount of memory allocated in bytes. Takes
     * the realization order of the function regions and the early free mechanism
     * into account while computing the peak footprint. */
    int64_t region_footprint(const map<string, Box> &regions,
                             const set<string> &inlined = set<string>());

    /** Returns the size of the input region in bytes. */
    int64_t input_region_size(string input, const Box &region);

    /** Returns the total size of the many input regions in bytes. */
    int64_t input_region_size(const map<string, Box> &input_regions);

    /** Displays the cost of each function in the pipeline. */
    void disp_func_costs();

    /** Construct a region cost object for the pipeline. env is a map of all
     * the functions in the pipeline.*/
    RegionCosts(const map<string, Function> &env);
};

/** Returns the size of a interval. */
int64_t get_extent(const Interval &i);

/** Returns the size of an n-d box. */
int64_t box_size(const Box &b);

void disp_regions(const map<string, Box> &regions, int dlevel = debug_level);

/** Returns the appropriate definition based on the stage of a function. */
Definition get_stage_definition(const Function &f, int stage_num);

/** Adds partial load costs to the corresponding function in the result costs. */
void combine_load_costs(map<string, int64_t> &result,
                        const map<string, int64_t> &partial);

/** Returns the required bounds of an intermediate stage (f, stage_num) of
 * function f given the bounds of the pure dimensions of f. */
DimBounds get_stage_bounds(Function f, int stage_num,
                           const DimBounds &pure_bounds);

/** Returns the required bounds for all the stages of the function f. Each entry
 * in the return vector corresponds to a stage.*/
vector<DimBounds> get_stage_bounds(Function f, const DimBounds &pure_bounds);

/** Recursively inlines all the functions in the set inlines into the
 * expression e and returns the resulting expression. */
Expr perform_inline(Expr e, const map<string, Function> &env,
                    const set<string> &inlines = set<string>());

}
}

#endif
