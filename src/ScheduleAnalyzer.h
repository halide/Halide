#ifndef HALIDE_SCHEDULE_ANALYZER_H
#define HALIDE_SCHEDULE_ANALYZER_H

/** \file
 *
 * Defines ScheduleAnalyzer, which reads the existing schedule of a Func or
 * Pipeline and reconstructs it as a ScheduleDirectives list.
 */

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Func.h"
#include "Pipeline.h"
#include "ScheduleEditor.h"

namespace Halide {

/** Traverses a Func or Pipeline and reconstructs its existing schedule as a
 * ScheduleDirectives list (a best-effort, semantically-matching representation
 * that a ScheduleEditor could re-apply), together with a model of the Funcs and
 * their loop vars that can be queried by name or by usage.
 *
 * Because the internal schedule is normalized, some sugar is recovered in
 * lowered form -- e.g. a tile() appears as splits plus a reorder, and
 * vectorize(x, n) appears as a split plus a vectorized inner dim. specialize()
 * scopes are recovered (each inner directive carries its condition path).
 * Structural transforms that rewrite the algorithm rather than the loop nest --
 * rfactor(), in(), and clone_in() -- cannot be recovered as operations; only the
 * Funcs they produced are analyzed. */
class ScheduleAnalyzer {
public:
    explicit ScheduleAnalyzer(const Func &func);
    explicit ScheduleAnalyzer(const std::vector<Func> &funcs);
    explicit ScheduleAnalyzer(const Pipeline &pipeline);

    /** Analyze an existing directive list directly, rather than reading it from
     * a Func's schedule. The list becomes directives(); the Func and var queries
     * are derived from what the directives reference and introduce. */
    explicit ScheduleAnalyzer(const ScheduleDirectives &directives);

    /** The directives representing the analyzed schedule(s), in order. */
    const ScheduleDirectives &directives() const {
        return directives_;
    }

    // ------------------------------------------------------------------
    // Query the Funcs found (by logical base name -- see base_name()).
    // ------------------------------------------------------------------

    /** The base names of every Func analyzed (outputs plus everything reachable
     * from them), sorted. */
    std::vector<std::string> func_names() const;

    /** Whether a Func with the given name was found. */
    bool has_func(const std::string &func) const;

    /** Number of definitions of a Func: 1 (pure) plus its update stages. Returns
     * 0 for an unknown Func. */
    int stage_count(const std::string &func) const;

    // ------------------------------------------------------------------
    // Query loop vars (as they exist in the analyzed schedule).
    // ------------------------------------------------------------------

    /** The loop-var names of `func` at `stage` (0 == pure definition), sorted.
     * Empty for an unknown Func/stage. */
    std::vector<std::string> vars(const std::string &func, int stage = 0) const;

    /** Whether `var` is a loop var of `func` at any stage. */
    bool has_var(const std::string &func, const std::string &var) const;

    // ------------------------------------------------------------------
    // Query directives by usage.
    // ------------------------------------------------------------------

    /** Indices (into directives()) of the directives targeting `func`, in
     * order. */
    std::vector<size_t> directive_indices_for(const std::string &func) const;

    /** Indices (into directives()) of the directives targeting stage `stage` of
     * `func`, in order. */
    std::vector<size_t> directive_indices_for(const std::string &func, int stage) const;

    /** Halide uniquifies duplicate Func names within a process ("f" -> "f$2");
     * this strips the "$N" suffix to recover the logical name. */
    static std::string base_name(const std::string &name);

    // ------------------------------------------------------------------
    // Serialization.
    // ------------------------------------------------------------------

    /** Print a directive list as C++, one scheduling statement per line. */
    static std::string to_source(const ScheduleDirectives &directives);

    /** Print this analyzer's directives as C++ (see the static overload). */
    std::string to_source() const {
        return to_source(directives_);
    }

    /** Serialize a directive list to a JSON string. Scheduling expressions (split
     * factors, extents, tile sizes) must be integer constants -- the common
     * case; a non-integer Expr (e.g. a specialize() condition or a Param-based
     * bound) raises an error. Funcs are not serialized; a loaded schedule is
     * applied to an existing pipeline. */
    static std::string to_json(const ScheduleDirectives &directives);

    /** Serialize this analyzer's directives to JSON (see the static overload). */
    std::string to_json() const {
        return to_json(directives_);
    }

    /** Parse a JSON document produced by to_json() into a directive list. Feed
     * it to a ScheduleEditor to apply it to a pipeline, or to a ScheduleAnalyzer
     * to query or re-render it. */
    static ScheduleDirectives from_json(const std::string &json);

    // ------------------------------------------------------------------
    // Model access (used by ScheduleValidator).
    // ------------------------------------------------------------------

    /** Per-stage loop-var sets of every Func, keyed by base name. Index 0 is the
     * pure definition, index i the (i-1)-th update. */
    using VarModel = std::map<std::string, std::vector<std::set<std::string>>>;
    const VarModel &var_model() const {
        return vars_;
    }

private:
    ScheduleDirectives directives_;
    VarModel vars_;
};

}  // namespace Halide

#endif  // HALIDE_SCHEDULE_ANALYZER_H
