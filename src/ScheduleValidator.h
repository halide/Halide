#ifndef HALIDE_SCHEDULE_VALIDATOR_H
#define HALIDE_SCHEDULE_VALIDATOR_H

/** \file
 *
 * Defines ScheduleValidator, which statically checks a ScheduleEditor's
 * directive list against the Funcs it will be applied to.
 */

#include <string>
#include <utility>
#include <vector>

#include "Func.h"
#include "Pipeline.h"
#include "ScheduleEditor.h"

namespace Halide {

/** Statically checks a list of scheduling directives against the Funcs they
 * will be applied to, without building or mutating anything. It replays the
 * list symbolically -- tracking which loop vars each Func/stage has as splits,
 * fuses, tiles, and renames introduce and remove them -- and records references
 * to Funcs or vars that don't exist, out-of-range stages, and Func-level
 * directives placed on an update stage.
 *
 * This catches the common structural mistakes. It does not attempt Halide's
 * full legality analysis (tail-strategy legality, reduction associativity, GPU
 * constraints, ...), which only a real apply() can decide. Validate against the
 * unscheduled Funcs you intend to apply to, before applying. */
class ScheduleValidator {
public:
    struct Issue {
        enum class Kind {
            MissingFunc,        // references a Func not in the pipeline
            MissingVar,         // references a loop var that doesn't exist here
            InvalidStage,       // stage index out of range
            FuncLevelOnUpdate,  // whole-Func directive with stage != 0
            Malformed,          // wrong number/kind of operands
            BadFactor,          // non-positive split/tile factor
            DuplicateVar,       // the same var named twice where it must be distinct
            VarCollision,       // a newly introduced var shadows an existing one
        };

        Kind kind;
        size_t directive = 0;  // index into the ScheduleEditor's list
        std::string func;      // the Func the directive targets
        std::string name;      // the missing Func/var name, when applicable
        std::string message;   // human-readable description
    };

    /** Validate `directives` against `funcs` (plus everything reachable from
     * them). Pass a ScheduleEditor's list via editor.directives(). */
    ScheduleValidator(const ScheduleDirectives &directives, const std::vector<Func> &funcs);

    /** Validate against the outputs (and reachable Funcs) of a pipeline. */
    ScheduleValidator(const ScheduleDirectives &directives, const Pipeline &p);

    // ------------------------------------------------------------------
    // Simple API.
    // ------------------------------------------------------------------

    /** True if no problems were found. */
    bool is_valid() const {
        return issues_.empty();
    }
    explicit operator bool() const {
        return is_valid();
    }

    // ------------------------------------------------------------------
    // Query what is wrong.
    // ------------------------------------------------------------------

    /** Every problem found, in directive order. */
    const std::vector<Issue> &issues() const {
        return issues_;
    }

    /** Problems attributed to the directive at `index`. */
    std::vector<Issue> issues_for(size_t index) const;

    /** Unique names of Funcs the schedule references but the pipeline lacks. */
    std::vector<std::string> missing_funcs() const;

    /** Unique (func, var) references that don't exist at their point of use. */
    std::vector<std::pair<std::string, std::string>> missing_vars() const;

    /** A human-readable, multi-line summary. Empty when the schedule is valid. */
    std::string report() const;

private:
    std::vector<Issue> issues_;
};

}  // namespace Halide

#endif  // HALIDE_SCHEDULE_VALIDATOR_H
