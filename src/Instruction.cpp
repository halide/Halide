#include "Instruction.h"

#include <utility>

#include "Error.h"
#include "Func.h"
#include "IR.h"

namespace Halide {

namespace Internal {

/** Storage backing an Instruction handle. Held via IntrusivePtr. */
struct InstructionContents {
    mutable RefCount ref_count;

    std::string name;
    std::function<Pipeline()> spec_fn;
    std::function<Stmt(const MatchContext &)> emit_fn;
    std::set<Target::Feature> required_features;
};

template<>
RefCount &ref_count<InstructionContents>(const InstructionContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<InstructionContents>(const InstructionContents *p) {
    delete p;
}

}  // namespace Internal

// ---------------------------------------------------------------------------
// Instruction
// ---------------------------------------------------------------------------

Instruction::Builder Instruction::declare(const std::string &name) {
    return Builder(name);
}

const std::string &Instruction::name() const {
    user_assert(contents.defined()) << "Called name() on an undefined Instruction.\n";
    return contents->name;
}

const std::set<Target::Feature> &Instruction::required_features() const {
    user_assert(contents.defined())
        << "Called required_features() on an undefined Instruction.\n";
    return contents->required_features;
}

Pipeline Instruction::spec() const {
    user_assert(contents.defined()) << "Called spec() on an undefined Instruction.\n";
    user_assert((bool)contents->spec_fn)
        << "Instruction \"" << contents->name
        << "\" has no spec thunk. Did you forget to call Builder::spec(...)?\n";
    return contents->spec_fn();
}

Internal::Stmt Instruction::emit(const MatchContext &ctx) const {
    user_assert(contents.defined()) << "Called emit() on an undefined Instruction.\n";
    user_assert((bool)contents->emit_fn)
        << "Instruction \"" << contents->name
        << "\" has no emit callback. Did you forget to call Builder::emit(...)?\n";
    return contents->emit_fn(ctx);
}

// ---------------------------------------------------------------------------
// Instruction::Builder
// ---------------------------------------------------------------------------

Instruction::Builder::Builder(std::string name)
    : name_(std::move(name)) {
    user_assert(!name_.empty()) << "Instruction name must be non-empty.\n";
}

Instruction::Builder &Instruction::Builder::spec(std::function<Pipeline()> spec_fn) {
    user_assert((bool)spec_fn)
        << "Instruction \"" << name_ << "\": spec thunk must be non-null.\n";
    spec_fn_ = std::move(spec_fn);
    return *this;
}

Instruction::Builder &Instruction::Builder::spec(std::function<Func()> spec_fn) {
    user_assert((bool)spec_fn)
        << "Instruction \"" << name_ << "\": spec thunk must be non-null.\n";
    auto wrapped = [fn = std::move(spec_fn)]() -> Pipeline {
        Func f = fn();
        return Pipeline({f});
    };
    spec_fn_ = std::move(wrapped);
    return *this;
}

Instruction::Builder &Instruction::Builder::require(std::set<Target::Feature> features) {
    required_features_ = std::move(features);
    return *this;
}

Instruction::Builder &Instruction::Builder::emit(
    std::function<Internal::Stmt(const MatchContext &)> emit_fn) {
    user_assert((bool)emit_fn)
        << "Instruction \"" << name_ << "\": emit callback must be non-null.\n";
    emit_fn_ = std::move(emit_fn);
    return *this;
}

Instruction Instruction::Builder::build() {
    user_assert(!name_.empty()) << "Instruction has no name (was build() called twice?).\n";
    user_assert((bool)spec_fn_)
        << "Instruction \"" << name_ << "\" was built without a spec thunk.\n";
    user_assert((bool)emit_fn_)
        << "Instruction \"" << name_ << "\" was built without an emit callback.\n";

    auto *c = new Internal::InstructionContents;
    c->name = std::move(name_);
    c->spec_fn = std::move(spec_fn_);
    c->emit_fn = std::move(emit_fn_);
    c->required_features = std::move(required_features_);

    // Reset the builder so accidental reuse fails the next user_assert.
    name_.clear();
    spec_fn_ = nullptr;
    emit_fn_ = nullptr;
    required_features_.clear();

    return Instruction(Internal::IntrusivePtr<Internal::InstructionContents>(c));
}

}  // namespace Halide
