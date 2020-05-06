#ifndef HALIDE_COMPILER_LOGGER_H_
#define HALIDE_COMPILER_LOGGER_H_

/** \file
 * Defines an interface used to gather and log compile-time information, stats, etc
 * for use in evaluating internal Halide compilation rules and efficiency.
 *
 * The 'standard' implementation simply logs all gathered data to
 * a local file (in JSON form), but the entire implementation can be
 * replaced by custom definitions if you have unusual logging needs.
 */

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "Expr.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CompilerLogger {
public:
    /** The "Phase" of compilation, used for some calls */
    enum class Phase {
        HalideLowering,
        LLVM,
    };

    CompilerLogger() = default;
    virtual ~CompilerLogger() = default;

    /** Record when a particular simplifier rule matches.
     */
    virtual void record_matched_simplifier_rule(const std::string &rulename) = 0;

    /** Record when an expression is non-monotonic in a loop variable.
     */
    virtual void record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) = 0;

    /** Record when can_prove() fails, but cannot find a counterexample.
     */
    virtual void record_failed_to_prove(Expr failed_to_prove, Expr original_expr) = 0;

    /** Record total size (in bytes) of final generated object code (e.g., file size of .o output).
     */
    virtual void record_object_code_size(uint64_t bytes) = 0;

    /** Record the compilation time (in seconds) for a given phase.
     */
    virtual void record_compilation_time(Phase phase, double duration) = 0;

    /**
     * Emit all the gathered data to the given stream. This may be called multiple times.
     */
    virtual std::ostream &emit_to_stream(std::ostream &o) = 0;
};

/** Set the active CompilerLogger object, replacing any existing one.
 * It is legal to pass in a nullptr (which means "don't do any compiler logging").
 * Returns the previous CompilerLogger (if any). */
std::unique_ptr<CompilerLogger> set_compiler_logger(std::unique_ptr<CompilerLogger> compiler_logger);

/** Return the currently active CompilerLogger object. If set_compiler_logger()
 * has never been called, a nullptr implementation will be returned.
 * Do not save the pointer returned! It is intended to be used for immediate
 * calls only. */
CompilerLogger *get_compiler_logger();

/** JSONCompilerLogger is a basic implementation of the CompilerLogger interface
 * that saves logged data, then logs it all in JSON format in emit_to_stream().
 */
class JSONCompilerLogger : public CompilerLogger {
public:
    JSONCompilerLogger() = default;

    JSONCompilerLogger(
        const std::string &generator_name,
        const std::string &function_name,
        const std::string &autoscheduler_name,
        const Target &target,
        const std::string &generator_args,
        bool obfuscate_exprs);

    void record_matched_simplifier_rule(const std::string &rulename) override;
    void record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) override;
    void record_failed_to_prove(Expr failed_to_prove, Expr original_expr) override;
    void record_object_code_size(uint64_t bytes) override;
    void record_compilation_time(Phase phase, double duration) override;

    std::ostream &emit_to_stream(std::ostream &o) override;

protected:
    const std::string generator_name;
    const std::string function_name;
    const std::string autoscheduler_name;
    const Target target;
    const std::string generator_args;
    const bool obfuscate_exprs{false};

    std::map<std::string, int64_t> matched_simplifier_rules;

    // Maps loop_var -> list of Exprs that were nonmonotonic for that loop_var
    std::map<std::string, std::vector<Expr>> non_monotonic_loop_vars;

    // List of (unprovable simplfied Expr, original version of that Expr passed to can_prove()).
    std::vector<std::pair<Expr, Expr>> failed_to_prove_exprs;

    // Total code size generated, in bytes.
    uint64_t object_code_size{0};

    // Map of the time take for each phase of compilation.
    std::map<Phase, double> compilation_time;

    void obfuscate();
    void emit();
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_COMPILER_LOGGER_H_
