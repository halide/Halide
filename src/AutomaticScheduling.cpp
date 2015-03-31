#include "AutomaticScheduling.h"
#include "Bounds.h"
#include "Expr.h"
#include "FindCalls.h"
#include "Func.h"
#include "Schedule.h"
#include "Simplify.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::unique_ptr;
using std::vector;
using std::map;

namespace {

const unsigned UNDEFINED_FOOTPRINT_SIZE = ~0U;

/** Convenience class representing the callgraph for a pipeline. */
class CallGraph {
public:
    CallGraph(Function root) {
        set<string> visited;
        construct(root, visited);
    }

    /** Return list of functions directly calling function f. */
    const vector<Function> &callers(Function f) {
        return call_to_caller[f.name()];
    }

    /** Return list of functions directly called by f. */
    const vector<Function> &calls(Function f) {
        return call_to_callee[f.name()];
    }

    /** Return list of functions transitively called by f. This does
     * not include f. */
    vector<Function> transitive_calls(Function f) {
        const vector<Function> &dir_calls = calls(f);
        set<string> visited;
        vector<Function> result(dir_calls.begin(), dir_calls.end());
        visited.insert(f.name());
        for (Function call : dir_calls) {
            transitive_calls_helper(call, result, visited);
        }
        return result;
    }
private:
    map<string, Function> name_to_func;
    map<string, vector<Function> > call_to_callee;
    map<string, vector<Function> > call_to_caller;

    void construct(Function f, set<string> &visited) {
        visited.insert(f.name());
        map<string, Function> calls = find_direct_calls(f);
        for (auto call_entry : calls) {
            string name = call_entry.first;
            Function func = call_entry.second;
            name_to_func[name] = func;
            call_to_callee[f.name()].push_back(func);
            call_to_caller[func.name()].push_back(f);

            if (visited.find(name) == visited.end()) {
                construct(func, visited);
            }
        }
    }

    void transitive_calls_helper(Function f, vector<Function> &result, set<string> &visited) {
        visited.insert(f.name());
        const vector<Function> &dir_calls = calls(f);
        for (Function call : dir_calls) {
            if (visited.find(call.name()) == visited.end()) {
                result.push_back(call);
                transitive_calls_helper(call, result, visited);
            }
        }
    }
};

/** Recursively reset the schedules for the given function and all of
 * the functions it calls. */
class ResetSchedules {
public:
    ResetSchedules(Function root) {
        reset_schedule(root);
        CallGraph cg(root);
        vector<Function> calls = cg.transitive_calls(root);
        for (Function call : calls) {
            reset_schedule(call);
        }
    }
    
private:
    /** Hackish way of resetting a function schedule to the
     * default. Mostly yanked from Function::define(). */
    void reset_schedule(Function f) {
        Schedule &olds = f.schedule();
        std::vector<Bound> oldbounds(olds.bounds().begin(), olds.bounds().end());
        Schedule &s = f.schedule();
        s = Schedule();
        s.bounds().insert(s.bounds().begin(), oldbounds.begin(), oldbounds.end());

        for (size_t i = 0; i < f.args().size(); i++) {
            Dim d = {f.args()[i], ForType::Serial, DeviceAPI::Parent, true};
            f.schedule().dims().push_back(d);
            f.schedule().storage_dims().push_back(f.args()[i]);
        }

        // Add the dummy outermost dim
        {
            Dim d = {Var::outermost().name(), ForType::Serial, DeviceAPI::Parent, true};
            f.schedule().dims().push_back(d);
        }
    }
};

/** Return min/max bounds for each dimension of the given function
 * across all callsites. If the bounds cannot be computed, returns an
 * empty vector. */
vector<Interval> get_function_bounds(Function f, CallGraph &cg) {
    vector<Interval> result;
    result.resize(f.dimensions());
    vector<bool> initialized;
    initialized.resize(f.dimensions(), false);
    const vector<Function> &callers = cg.callers(f);
    if (callers.empty()) {
        // Return undefined bounds for uncalled functions.
        result.clear();
        return result;
    }
    for (Function caller : callers) {
        const vector<Expr> outputs = caller.values();
        internal_assert(outputs.size() == 1) << "Unhandled number of outputs";
        Box b = boxes_required(outputs[0])[f.name()];
        const unsigned dim = b.bounds.size();
        if (dim == 0) {
            // Unable to compute the bounds for the function. Return undefined bounds.
            result.clear();
            return result;
        }
        internal_assert((int)dim == f.dimensions());
        for (unsigned i = 0; i < dim; ++i) {
            Interval interval = b.bounds[i];
            if (!initialized[i]) {
                initialized[i] = true;
                result[i] = interval;
            } else {
                result[i].min = min(result[i].min, interval.min);
                result[i].max = max(result[i].max, interval.max);
            }
        }
    }
    return result;
}


/** Return the footprint (required region) of the given function over
 * all callsites. This is a minimum of 1 when a function is
 * pointwise. If the footprint cannot be calculated, returns
 * UNDEFINED_FOOTPRINT_SIZE. */
unsigned calculate_footprint_size(Function f, CallGraph &cg) {
    vector<Interval> bounds = get_function_bounds(f, cg);
    if (bounds.size() == 0) {
        // Unable to calculate function bounds.
        return UNDEFINED_FOOTPRINT_SIZE;
    }
    Expr footprint = 1;
    for (Interval i : bounds) {
        internal_assert(i.min.defined() && i.max.defined());
        Expr diff = i.max - i.min + 1;
        footprint *= diff;
    }
    const int *result = as_const_int(simplify(footprint));
    if (result) {
        return (unsigned)*result;
    } else {
        return UNDEFINED_FOOTPRINT_SIZE;
    }
}

} // end anonymous namespace

void ComputeRootAllStencils::apply(Func root) {
    // Construct a callgraph for the pipeline.
    CallGraph cg(root.function());
    vector<Function> all_functions = cg.transitive_calls(root.function());
    for (Function f : all_functions) {
        unsigned footprint = calculate_footprint_size(f, cg);
        if (footprint > 1) {
            Func wrapper(f);
            wrapper.store_root().compute_root();
        }
    }
}

void ParallelizeOuter::apply(Func root) {
    CallGraph cg(root.function());
    vector<Function> all_functions = cg.transitive_calls(root.function());
    all_functions.push_back(root.function());
    for (Function f : all_functions) {
        if (!f.schedule().compute_level().is_inline() || f.same_as(root.function())) {
            Func wrapper(f);
            Dim outer = f.schedule().dims()[f.schedule().dims().size() - 1];
            Var v(outer.var);
            wrapper.parallel(v);
        }
    }
}

void VectorizeInner::apply(Func root) {
    CallGraph cg(root.function());
    vector<Function> all_functions = cg.transitive_calls(root.function());
    all_functions.push_back(root.function());
    for (Function f : all_functions) {
        if (!f.schedule().compute_level().is_inline() || f.same_as(root.function())) {
            Func wrapper(f);
            Dim inner = f.schedule().dims()[0];
            Var v(inner.var);
            unsigned factor = 128 / f.output_types()[0].bits;
            wrapper.vectorize(v, factor);
        }
    }
}

void apply_automatic_schedule(Func root, AutoScheduleStrategy strategy, bool reset_schedules) {
    unique_ptr<AutoScheduleStrategyImpl> impl;
    switch (strategy) {
    case AutoScheduleStrategy::ComputeRootAllStencils:
        impl.reset(new ComputeRootAllStencils());
        break;
    case AutoScheduleStrategy::ParallelizeOuter:
        impl.reset(new ParallelizeOuter());
        break;
    case AutoScheduleStrategy::VectorizeInner:
        impl.reset(new VectorizeInner());
        break;
    }
    internal_assert(impl != NULL);
    // Reset all user-specified schedules.
    if (reset_schedules) ResetSchedules reset(root.function());
    impl->apply(root);
}

}
}
