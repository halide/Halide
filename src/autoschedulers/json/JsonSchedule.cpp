#include "HalidePlugin.h"

#include <fstream>
#include <sstream>

// An "autoscheduler" that doesn't search for a schedule at all: it loads a JSON
// schedule (produced by Halide::ScheduleAnalyzer::to_json) and applies it to the
// pipeline, in place of whatever schedule the Generator would otherwise use.
//
// Because it plugs into the standard autoscheduler slot, a Generator's
// schedule() method no-ops when it is autoscheduler-aware (guards on
// using_autoscheduler()), so the directives apply to the unscheduled pipeline --
// cleanly replacing the manual schedule. Select it the usual way:
//
//   -p /path/to/libautoschedule_jsonschedule.so
//   autoscheduler.name=JsonSchedule autoscheduler.json_path=my_schedule.json
//
// A literal schedule may be passed inline with autoscheduler.json=... instead of
// autoscheduler.json_path=...

namespace Halide {
namespace Internal {
namespace Autoscheduler {

namespace {

struct JsonSchedule {
    void operator()(const Pipeline &pipeline, const Target &target,
                    const AutoschedulerParams &params, AutoSchedulerResults *results) {
        internal_assert(params.name == "JsonSchedule");

        // Pull the schedule JSON from either a file path or an inline string.
        std::map<std::string, std::string> extra = params.extra;
        std::string json;
        if (auto it = extra.find("json_path"); it != extra.end()) {
            std::ifstream f(it->second, std::ios::binary);
            user_assert(f.good()) << "JsonSchedule: could not open autoscheduler.json_path '"
                                  << it->second << "'.";
            std::ostringstream ss;
            ss << f.rdbuf();
            json = ss.str();
            extra.erase(it);
        } else if (auto it2 = extra.find("json"); it2 != extra.end()) {
            json = it2->second;
            extra.erase(it2);
        } else {
            user_error << "JsonSchedule requires autoscheduler.json_path=FILE "
                          "(or autoscheduler.json=STRING).";
        }
        user_assert(extra.empty())
            << "JsonSchedule: only json_path and json are accepted as parameters.";

        // Apply the JSON schedule to the pipeline's Funcs.
        ScheduleDirectives directives = ScheduleAnalyzer::from_json(json);
        ScheduleEditor(directives).apply(pipeline);

        results->autoscheduler_params = params;
        results->schedule_source = ScheduleAnalyzer::to_source(directives);
    }
};

REGISTER_AUTOSCHEDULER(JsonSchedule)

}  // namespace
}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
