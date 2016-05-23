#include "AutoSchedule.h"
#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Simplify.h"
#include "Target.h"
#include "Function.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;


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
}

}
}
