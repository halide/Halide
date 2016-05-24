#include<algorithm>

#include "AutoSchedule.h"
#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"
#include "Function.h"
#include "Bounds.h"
#include "Var.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::deque;
using std::pair;
using std::make_pair;


void set_schedule_defaults(map<string, Function> &env) {
  // Changing the default to compute root.

  // TODO: This ignores existing schedules specified by
  // the user atm and needs to be addressed when we have
  // decided on a mechanism to inform the auto scheduler
  // not to mess with an user specified schedule.
  for (auto& kv : env) {
    // The shedule is marked touched when a user modifies
    // the schedule. The idea is to keep the user specified
    // schedule intact as much as possible.
    // TODO: However, user specified schedules can have
    // non-local effects and will not be captured by touched.
    // For example:
    // f.compute_at(g, y) now specifies constraints on the
    // schedule of f as well as the schedule of g i.e., the
    // variable y in g cannot be split or reordered since that
    // may change user intent.
    //
    // Open question is how to deal with the constraints induced
    // by user specified schedules.
    kv.second.schedule().store_level().func = "";
    kv.second.schedule().store_level().var = "__root";
    kv.second.schedule().compute_level().func = "";
    kv.second.schedule().compute_level().var = "__root";

    // Initializing the schedules for update definitions
    for (size_t u = 0; u < kv.second.updates().size(); u++) {
      kv.second.update_schedule(u).store_level().func = "";
      kv.second.update_schedule(u).store_level().var = "__root";
      kv.second.update_schedule(u).compute_level().func = "";
      kv.second.update_schedule(u).compute_level().var = "__root";
    }
  }
}

bool check_estimates_on_outputs(const vector<Function> &outputs) {
  bool estimates_avail = true;
  for (auto &out : outputs) {
    const vector<Bound> &estimates = out.schedule().estimates();
    if (estimates.size() != out.args().size()) {
      estimates_avail = false;
      break;
    }
    vector<string> vars = out.args();

    for (unsigned int i = 0; i < estimates.size(); i++) {
      if (std::find(vars.begin(), vars.end(), estimates[i].var) == vars.end()
          || !((estimates[i].min.as<IntImm>()) &&
               (estimates[i].extent.as<IntImm>())))  {
        estimates_avail = false;
        break;
      }
    }
  }
  return estimates_avail;
}

void simplify_box(Box& b) {
  for (unsigned int i = 0; i < b.size(); i++) {
    b[i].min = simplify(b[i].min);
    b[i].max = simplify(b[i].max);
  }
}

struct DependenceAnalysis {

  const map<string, Function> &env;
  const FuncValueBounds &func_val_bounds;

  // TODO: Build a cache for bounds queries

  DependenceAnalysis(map<string, Function> &_env,
                     const FuncValueBounds &_func_val_bounds):
                     env(_env), func_val_bounds(_func_val_bounds) {}

  /* Compute the regions of producers required to compute a region of the function
     'f' given concrete sizes of the tile in each dimension. */
  map<string, Box> regions_required(Function f,
                                    const vector<Interval> &conc_bounds){

    map<string, Box> regions;
    // Add the function and its region to the queue
    deque< pair<Function, vector<Interval> > > f_queue;
    f_queue.push_back(make_pair(f, conc_bounds));

    // Recursively compute the regions required
    while(!f_queue.empty()) {

      Function curr_f = f_queue.front().first;
      vector<Interval> curr_bounds = f_queue.front().second;
      f_queue.pop_front();

      for (auto &val: curr_f.values()) {
        map<string, Box> curr_regions;
        Scope<Interval> curr_scope;
        int interval_index = 0;

        for (auto& arg: curr_f.args()) {
          Interval simple_bounds = Interval(simplify(curr_bounds[interval_index].min),
                                            simplify(curr_bounds[interval_index].max));
          curr_scope.push(arg, simple_bounds);
          interval_index++;
        }

        curr_regions = boxes_required(val, curr_scope, func_val_bounds);
        for (auto& reg: curr_regions) {
          // Merge region with an existing region for the function in
          // the global map
          if (regions.find(reg.first) == regions.end())
            regions[reg.first] = reg.second;
          else
            merge_boxes(regions[reg.first], reg.second);

          if (env.find(reg.first) != env.end())
            f_queue.push_back(make_pair(env.at(reg.first),
                                        reg.second.bounds));
        }
      }
      // TODO: Currently not handling updates
    }

    // Simplify
    map<string, Box> concrete_regions;

    for (auto &f_reg : regions) {
      simplify_box(f_reg.second);

      Box concrete_box;
      for (unsigned int i = 0; i < f_reg.second.size(); i++) {
        Expr lower = f_reg.second[i].min;
        Expr upper = f_reg.second[i].max;

        // Use the estimates if the lower and upper bounds cannot be
        // determined
        if (!lower.as<IntImm>()) {
          const Function &curr_f = env.at(f_reg.first);
          for (auto &b: curr_f.schedule().estimates()) {
            unsigned int num_pure_args = curr_f.args().size();
            if (i < num_pure_args && b.var == curr_f.args()[i])
              lower = Expr(b.min.as<IntImm>()->value);
          }
        }

        if (!upper.as<IntImm>()) {
          const Function &curr_f = env.at(f_reg.first);
          for (auto &b: curr_f.schedule().estimates()) {
            unsigned int num_pure_args = curr_f.args().size();
            if (i < num_pure_args && b.var == curr_f.args()[i]) {
              const IntImm * bmin = b.min.as<IntImm>();
              const IntImm * bextent = b.extent.as<IntImm>();
              upper = Expr(bmin->value + bextent->value - 1);
            }
          }
        }

        Interval concrete_bounds = Interval(lower, upper);
        concrete_box.push_back(concrete_bounds);
      }
      concrete_regions[f_reg.first] = concrete_box;
    }
    return concrete_regions;
  }



  /* Compute the redundant regions computed while computing a tile of the function
     'f' given sizes of the tile in each dimension. */
  map<string, Box> redundant_regions(Function f, int dir,
                                     const vector<Interval> &conc_bounds){

    map<string, Box> regions = regions_required(f, conc_bounds);

    vector<Interval> shifted_bounds;
    int num_pure_args = f.args().size();
    for (int arg = 0; arg < num_pure_args; arg++) {
      if (dir == arg) {
        Expr len = conc_bounds[arg].max - conc_bounds[arg].min + 1;
        Interval bound = Interval(conc_bounds[arg].min + len,
                                  conc_bounds[arg].max + len);
        shifted_bounds.push_back(bound);
      }
      else
        shifted_bounds.push_back(conc_bounds[arg]);
    }

    map<string, Box> regions_shifted = regions_required(f, shifted_bounds);

    map<string, Box> overalps;
    for (auto& reg: regions) {
      if (regions_shifted.find(reg.first) == regions.end()) {
        // It will be interesting to log cases where this actually
        // happens
        continue;
      } else {
        Box b = reg.second;
        Box b_shifted = regions_shifted[reg.first];
        // The boxes should be of the same size
        assert(b.size() == b_shifted.size());
        // The box used makes things complicated ignoring it for now
        Box b_intersect;
        for (unsigned int i = 0 ; i < b.size(); i++)
          b_intersect.push_back(interval_intersect(b[i], b_shifted[i]));
        // A function should appear once in the regions and therefore cannot
        // already be present in the overlaps map
        assert(overalps.find(reg.first) == overalps.end());
        overalps[reg.first] = b_intersect;
      }
    }
    // Simplify
    for (auto &f : overalps)
      simplify_box(f.second);

    return overalps;
  }


  map<string, Box>
      concrete_dep_regions(string name, vector<Interval> &bounds) {
        return regions_required(env.at(name), bounds);
      }

  vector< map<string, Box> >
      concrete_overlap_regions(string name, vector<Interval> &bounds) {

        vector< map<string, Box> > conc_overlaps;
        size_t num_args = env.at(name).args().size();

        for (size_t dir = 0; dir < num_args; dir++) {
          map<string, Box> conc_reg =
              redundant_regions(env.at(name), dir, bounds);
          conc_overlaps.push_back(conc_reg);
        }
        return conc_overlaps;
      }
};


void generate_schedules(const std::vector<Function> &outputs,
                        const Target &target) {
  // Compute an environment
  map<string, Function> env;
  for (Function f : outputs) {
    map<string, Function> more_funcs = find_transitive_calls(f);
    env.insert(more_funcs.begin(), more_funcs.end());
  }

  // Compute a realization order
  vector<string> order = realization_order(outputs, env);

  // Set the schedule defaults for each funtion in the environment
  set_schedule_defaults(env);

  // Compute the expression costs for each function in the pipeline

  // Dependence analysis to compute all the regions of upstream functions
  // required to compute a region of the function

  FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

  bool estimates_avail = check_estimates_on_outputs(outputs);

  // Inform the user that estimates of output sizes were not available on
  // the outputs of the pipeline.
  user_assert(estimates_avail) << "Please provide estimates for each \
                               dimension of the pipeline output functions.";

  map<string, vector<string> > update_args;
  set<string> reductions;
  DependenceAnalysis analy(env, func_val_bounds);

  // Show bounds of all the functions in the pipeline given estimates
  // on outputs. Also report fuctions where the bounds could not be inferred.
}

}
}
