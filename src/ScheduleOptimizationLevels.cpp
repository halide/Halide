#include "Bounds.h"
#include "Expr.h"
#include "FindCalls.h"
#include "Schedule.h"
#include "ScheduleOptimizationLevels.h"
#include "Simplify.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;
using std::map;

namespace {

const unsigned UNDEFINED_FOOTPRINT_SIZE = ~0U;

/** Return the optimization level controlled by HL_SCHED_OPT
 * environment variable. */
ScheduleOptimization::Level get_optimization_level() {
    char *level = getenv("HL_SCHED_OPT");
    int i = level ? atoi(level) : 0;
    switch (i) {
    case 0:
        return ScheduleOptimization::LEVEL_0;
    case 1:
        return ScheduleOptimization::LEVEL_1;
    case 2:
        return ScheduleOptimization::LEVEL_2;
    default:
        internal_assert(false);
        return ScheduleOptimization::LEVEL_0;
    }
}

/** Return the optimization corresponding to the given level. */
ScheduleOptimization *get_optimization(ScheduleOptimization::Level level) {
    switch (level) {
    case ScheduleOptimization::LEVEL_0:
        return new OptimizationLevel0();
    case ScheduleOptimization::LEVEL_1:
        return new OptimizationLevel1();
    case ScheduleOptimization::LEVEL_2:
        return new OptimizationLevel2();
    }
    return NULL;
}

/** Convenience class representing the callgraph for a pipeline. */
class CallGraph {
public:
    CallGraph(Function root) {
        set<string> visited;
        construct(root, visited);
    }

    /** Return list of functions directly calling function f. */
    const vector<Function> &callers(Function f) {
        map<string, vector<Function> >::const_iterator I = call_to_caller.find(f.name());
        if (I == call_to_caller.end()) {
            call_to_caller[f.name()] = vector<Function>();
            I = call_to_caller.find(f.name());
        }
        return I->second;
    }

    /** Return list of functions directly called by f. */
    const vector<Function> &calls(Function f) {
        map<string, vector<Function> >::const_iterator I = call_to_callee.find(f.name());
        if (I == call_to_callee.end()) {
            call_to_callee[f.name()] = vector<Function>();
            I = call_to_callee.find(f.name());
        }
        return I->second;
    }

    /** Return list of functions transitively called by f. This does
     * not include f. */
    vector<Function> transitive_calls(Function f) {
        const vector<Function> &dir_calls = calls(f);
        set<string> visited;
        vector<Function> result(dir_calls.begin(), dir_calls.end());
        visited.insert(f.name());
        for (vector<Function>::const_iterator I = dir_calls.begin(), E = dir_calls.end(); I != E; ++I) {
            transitive_calls_helper(*I, result, visited);
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
        for (map<string, Function>::iterator I = calls.begin(), E = calls.end(); I != E; ++I) {
            string name = I->first;
            Function func = I->second;
            name_to_func[name] = func;
            call_to_callee[f.name()].push_back(func);
            call_to_caller[func.name()].push_back(f);

            if (visited.find(I->first) == visited.end()) {
                construct(I->second, visited);
            }
        }
    }

    void transitive_calls_helper(Function f, vector<Function> &result, set<string> &visited) {
        visited.insert(f.name());
        const vector<Function> &dir_calls = calls(f);
        for (vector<Function>::const_iterator I = dir_calls.begin(), E = dir_calls.end(); I != E; ++I) {
            Function call = *I;
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
        for (std::vector<Function>::iterator I = calls.begin(), E = calls.end(); I != E; ++I) {
            reset_schedule(*I);
        }
    }
    
private:
    /** Hackish way of resetting a function schedule to the
     * default. Mostly yanked from Function::define(). */
    void reset_schedule(Function f) {
        Schedule &olds = f.schedule();
        std::vector<Bound> oldbounds(olds.bounds().begin(), olds.bounds().end());
        f.schedule() = Schedule();
        Schedule &s = f.schedule();
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
    vector<Function> callers = cg.callers(f);
    if (callers.empty()) {
        // Return undefined bounds for uncalled functions.
        result.clear();
        return result;
    }
    for (vector<Function>::const_iterator I = callers.begin(), E = callers.end(); I != E; ++I) {
        const vector<Expr> outputs = (*I).values();
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
    for (vector<Interval>::iterator I = bounds.begin(), E = bounds.end(); I != E; ++I) {
        const Interval &i = *I;
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


void OptimizationLevel1::apply(Func func) {
    Function root = func.function();
    // Reset all user-specified schedules.
    ResetSchedules reset(root);
    // Construct a callgraph for the pipeline.
    CallGraph cg(root);
    vector<Function> all_functions = cg.transitive_calls(root);
    for (vector<Function>::iterator I = all_functions.begin(), E = all_functions.end(); I != E; ++I) {
        Function f = *I;
        Func wrapper(f);
        unsigned footprint = calculate_footprint_size(f, cg);
        if (footprint == 1) {
            wrapper.compute_inline();
        } else {
            wrapper.store_root().compute_root();
        }
    }
}

void OptimizationLevel2::apply(Func func) {
    Function root = func.function();
    OptimizationLevel1 *lvl1 = new OptimizationLevel1();
    lvl1->apply(func);
    // Construct a callgraph for the pipeline.
    CallGraph cg(root);
    vector<Function> all_functions = cg.transitive_calls(root);
    parallelize_outer(root);
    vectorize_inner(root);
    for (vector<Function>::iterator I = all_functions.begin(), E = all_functions.end(); I != E; ++I) {
        Function f = *I;
        if (f.schedule().compute_level() == LoopLevel::root()) {
            parallelize_outer(f);
            vectorize_inner(f);
        }
    }
}

void OptimizationLevel2::parallelize_outer(Function f) {
    Func wrapper(f);
    Dim outer = f.schedule().dims()[f.schedule().dims().size() - 1];
    Var v(outer.var);
    wrapper.parallel(v);
}

void OptimizationLevel2::vectorize_inner(Function f) {
    Func wrapper(f);
    Dim inner = f.schedule().dims()[0];
    Var v(inner.var);
    unsigned factor = 128 / f.output_types()[0].bits;
    wrapper.vectorize(v, factor);
}

void apply_schedule_optimization(Func func) {
    ScheduleOptimization::Level level = get_optimization_level();
    ScheduleOptimization *opt = get_optimization(level);
    internal_assert(opt);
    opt->apply(func);
}

}
}
