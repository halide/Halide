#ifndef HALIDE_INSTRUCTION_H
#define HALIDE_INSTRUCTION_H

/** \file
 *
 * Defines Instruction, a user-defined object declaring a substitutable
 * pure-Halide spec paired with a hardware/library emission. Instructions
 * are committed at scheduling time via Func::implement_with.
 *
 * Phase 3: target-feature check + transfer of contractual bounds from
 * spec Funcs onto matched user Funcs runs at lowering time (see
 * ApplyImplementWith.h). Structural matching, match-parameter binding,
 * and emit substitution are still future work. See
 * docs/implement_with/DESIGN.md and IMPLEMENTATION_STATUS.md.
 */

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Expr.h"
#include "IntrusivePtr.h"
#include "Pipeline.h"
#include "Target.h"

namespace Halide {

class Func;
class Instruction;
class MatchContext;

namespace Internal {

/** Backing state for a MatchContext produced by the implement_with
 * emit pass (Phase 5). Owned via shared_ptr so MatchContext stays
 * cheap to copy. Internal-only; emit callbacks see only the
 * MatchContext accessors. */
struct MatchContextContents {
    /** spec input/output Func name -> matched user-side name. Sourced
     * from the matcher's func_rename. */
    std::map<std::string, std::string> func_rename;
    /** spec bare Var name -> matched user-side bare Var. Parsed from
     * the matcher's For-loop name bindings. */
    std::map<std::string, std::string> var_rename;
    /** The compile Target. */
    Target target;
    /** The user-side primary Func name (the Func the directive sat on). */
    std::string user_primary_name;
    /** The spec-side primary output Func name (post-uniquification). */
    std::string spec_primary_name;
};

}  // namespace Internal

/** Behavior on structural match failure. v1 supports Strict only. */
enum class ImplementMode {
    /** Hard error if the spec cannot be structurally matched at the
     * named loop level. */
    Strict,
    /** Reserved for v2: fall back to ordinary lowering on match failure. */
    Soft,
};

/** Context passed to an Instruction's emit callback at lowering time.
 *
 * v1 exposes a name-keyed view of the structural-match result: given a
 * spec-pattern Func name (e.g. "a"), `input(name)` returns the user-side
 * buffer/Func name the matcher bound it to. Similarly for `output(name)`
 * and `var(name)`. The compile Target is available via `target()`.
 *
 * The emit callback uses these names to construct its own
 * Halide IR (Loads, Stores, Calls, etc.). Richer accessors that
 * pre-build canonical Load/Store Exprs at the spec's vector width are
 * deferred to a future iteration once concrete emit patterns surface
 * the right abstraction.
 *
 * Phase 7 (v1.5) will add `param(name)` for affine match parameters. */
class MatchContext {
public:
    MatchContext() = default;
    explicit MatchContext(std::shared_ptr<const Internal::MatchContextContents> c);

    /** User-side Func/buffer name bound to the spec-pattern input named
     * `spec_name`. Errors with a descriptive message if no binding was
     * recorded. */
    const std::string &input(const std::string &spec_name) const;

    /** User-side Func/buffer name bound to the spec-pattern output named
     * `spec_name`. For single-output specs the typical caller passes the
     * primary output's name; multi-output specs may pass any output. */
    const std::string &output(const std::string &spec_name) const;

    /** User-side Var name bound to the spec-pattern bare Var named
     * `spec_var`. Useful when an emit callback needs to construct an
     * index expression keyed off a user loop variable. */
    const std::string &var(const std::string &spec_var) const;

    /** The compile Target. */
    const Target &target() const;

    /** True iff this MatchContext is backed by a successful match. A
     * default-constructed MatchContext returns false here. */
    bool defined() const {
        return (bool)contents;
    }

private:
    std::shared_ptr<const Internal::MatchContextContents> contents;
};

namespace Internal {
struct InstructionContents;

/** Returns true when the calling code is executing inside an
 * Instruction::Builder::spec thunk. Used by FuncRef::operator Expr()
 * to permit calls to undefined spec-input Funcs. Not part of the
 * public Halide API. */
bool in_spec_thunk();

}  // namespace Internal

/** A user-defined instruction declaration. Value-type wrapper around a
 * refcounted contents block. Construct via Instruction::declare. */
class Instruction {
    Internal::IntrusivePtr<Internal::InstructionContents> contents;

public:
    class Builder;

    Instruction() = default;
    explicit Instruction(Internal::IntrusivePtr<Internal::InstructionContents> c)
        : contents(std::move(c)) {
    }

    /** Begin declaring a new Instruction with the given user-visible name. */
    static Builder declare(const std::string &name);

    /** True iff this handle points at a built Instruction. */
    bool defined() const {
        return contents.defined();
    }

    /** User-visible name of the instruction. */
    const std::string &name() const;

    /** Target features that must all be enabled for this instruction to be
     * legal at the use site. */
    const std::set<Target::Feature> &required_features() const;

    /** Run the spec thunk and return the resulting Pipeline. The output
     * Funcs in the returned Pipeline are marked as spec-pattern (see §4.7
     * of the design doc): they carry match-pattern definitions and
     * contractual schedules, but cannot be realized or compiled. */
    Pipeline spec() const;

    /** Invoke the emit callback. Phase 1: callable directly for testing but
     * not wired into lowering. */
    Internal::Stmt emit(const MatchContext &ctx) const;
};

/** Builder for declaring an Instruction. Methods are chainable and may be
 * called in any order; build() finalizes the declaration. */
class Instruction::Builder {
    std::string name_;
    std::function<Pipeline()> spec_fn_;
    std::function<Internal::Stmt(const MatchContext &)> emit_fn_;
    std::set<Target::Feature> required_features_;

public:
    explicit Builder(std::string name);

    /** Set the spec thunk: a zero-arg function returning the Pipeline that
     * describes what this instruction computes. The Funcs in the Pipeline
     * are spec-pattern Funcs whose bodies define the match pattern and
     * whose schedules express the contract (bounds, layout, vector width,
     * etc.). Spec-pattern Funcs should be declared with explicit names —
     * e.g. `Func a("a")` — since those names are the keys MatchContext
     * uses to expose matched buffers to the emit callback. v1 supports
     * constant bounds only. */
    Builder &spec(std::function<Pipeline()> spec_fn);

    /** Convenience overload: a thunk returning a single Func is implicitly
     * wrapped as Pipeline({f}). */
    Builder &spec(std::function<Func()> spec_fn);

    /** Set the Target features that must all be enabled for this
     * instruction to be considered legal at the use site.
     *
     * Note: this method is named `require` rather than `requires` to
     * remain a valid identifier in C++20+, where `requires` is a reserved
     * keyword. The design doc spells this `.requires(...)`. */
    Builder &require(std::set<Target::Feature> features);

    /** Set the lowering-time emission callback. Given a MatchContext
     * describing the matched region, returns the Stmt to substitute. */
    Builder &emit(std::function<Internal::Stmt(const MatchContext &)> emit_fn);

    /** Finalize the Instruction. The builder is consumed; callers should
     * not reuse it. */
    Instruction build();
};

namespace Internal {

/** A recorded implement_with directive sitting on a StageSchedule.
 * Consumed at lowering time by apply_implement_with_directives (Phase 3).
 * Structural matching and lowering substitution are still future work. */
struct ImplementWithDirective {
    /** The Instruction this stage was committed to. */
    Instruction instruction;
    /** Name of the user-Var (or RVar) naming the loop level at which the
     * spec pipeline must structurally match the user's lowered IR. */
    std::string loop_var_name;
    /** True if loop_var_name names an RVar rather than a Var. */
    bool loop_var_is_rvar = false;
    /** Co-outputs for multi-output instructions. Empty for single-output
     * instructions. Stored by name; resolution to Functions happens later.
     * (For a Tuple-valued primary Func, co_outputs stays empty — the
     * components are implicit.) */
    std::vector<std::string> co_output_names;
    /** Strict or Soft. v1: Strict only. */
    ImplementMode mode = ImplementMode::Strict;
};

}  // namespace Internal

}  // namespace Halide

#endif  // HALIDE_INSTRUCTION_H
