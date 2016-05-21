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

void auto_schedule_functions(const std::vector<Function> &outputs,
                             const Target &target) {
  // Compute an environment
  map<string, Function> env;
  for (Function f : outputs) {
    map<string, Function> more_funcs = find_transitive_calls(f);
    env.insert(more_funcs.begin(), more_funcs.end());
  }

  // Compute a realization order
  vector<string> order = realization_order(outputs, env);

  // TODO: Modify the schedules
}

}
}
