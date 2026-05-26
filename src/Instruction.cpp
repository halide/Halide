#include "Instruction.h"

#include <utility>

#include "Error.h"
#include "Func.h"
#include "IR.h"
#include "Target.h"

namespace Halide {

namespace Internal {

// ---------------------------------------------------------------------------
// Spec-thunk context flag
// ---------------------------------------------------------------------------

static thread_local int g_spec_thunk_depth = 0;

bool in_spec_thunk() {
    return g_spec_thunk_depth > 0;
}

namespace {
struct SpecThunkScope {
    SpecThunkScope() {
        ++g_spec_thunk_depth;
    }
    ~SpecThunkScope() {
        --g_spec_thunk_depth;
    }
};
}  // namespace

// ---------------------------------------------------------------------------

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
    Internal::SpecThunkScope scope;
    Pipeline p = contents->spec_fn();
    for (Func &f : p.outputs()) {
        f.function().mark_as_spec_pattern();
    }
    return p;
}

Internal::Stmt Instruction::emit(const MatchContext &ctx) const {
    user_assert(contents.defined()) << "Called emit() on an undefined Instruction.\n";
    user_assert((bool)contents->emit_fn)
        << "Instruction \"" << contents->name
        << "\" has no emit callback. Did you forget to call Builder::emit(...)?\n";
    return contents->emit_fn(ctx);
}

// ---------------------------------------------------------------------------
// MatchContext
// ---------------------------------------------------------------------------

namespace {

// Halide uniquifies user-visible Func names process-wide: `Func a("a")`
// produces a Function whose stored name is "a$N" for some N. The
// matcher's func_rename uses those uniquified names as keys. The spec
// author, however, naturally writes the *original* name in their emit
// callback (`ctx.input("a")`). Bridge the gap by matching either the
// exact key or any key of the form "<spec_name>$<digits>".
const std::string *
lookup_with_uniquify_suffix(const std::map<std::string, std::string> &m,
                            const std::string &spec_name) {
    auto it = m.find(spec_name);
    if (it != m.end()) {
        return &it->second;
    }
    const std::string prefix = spec_name + "$";
    const std::string *match = nullptr;
    int matches_found = 0;
    for (const auto &kv : m) {
        if (kv.first.size() > prefix.size() &&
            kv.first.compare(0, prefix.size(), prefix) == 0) {
            // Ensure the part after '$' is all digits.
            bool all_digits = true;
            for (size_t i = prefix.size(); i < kv.first.size(); ++i) {
                if (kv.first[i] < '0' || kv.first[i] > '9') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                match = &kv.second;
                ++matches_found;
            }
        }
    }
    // Ambiguity (multiple "<name>$N" entries) means the spec author
    // probably redeclared `Func a("a")` more than once in the same
    // spec thunk. v1 returns the last seen; callers can disambiguate
    // by using more distinctive names.
    (void)matches_found;
    return match;
}

}  // namespace

MatchContext::MatchContext(std::shared_ptr<const Internal::MatchContextContents> c)
    : contents(std::move(c)) {
}

const std::string &MatchContext::input(const std::string &spec_name) const {
    user_assert(contents)
        << "MatchContext::input(\"" << spec_name << "\") called on a "
        << "default-constructed MatchContext. The emit callback's argument "
        << "must come from the implement_with lowering pass.\n";
    const std::string *r =
        lookup_with_uniquify_suffix(contents->func_rename, spec_name);
    user_assert(r != nullptr)
        << "MatchContext: no binding recorded for input/output spec Func \""
        << spec_name << "\". Check that the spec-pattern Func is named "
        << "(via Func(\"" << spec_name << "\")) and that the structural "
        << "matcher saw its body.\n";
    return *r;
}

const std::string &MatchContext::output(const std::string &spec_name) const {
    user_assert(contents)
        << "MatchContext::output(\"" << spec_name << "\") called on a "
        << "default-constructed MatchContext.\n";
    // The spec primary's name (post-uniquification) is stashed
    // explicitly so even when func_rename does not record a binding
    // for it (e.g. the canonical-form prefix rewrote the
    // ProducerConsumer for the primary out), the common emit pattern
    // "give me the primary output's user-side name" works. Match
    // against both the post-uniquify form and the original
    // pre-uniquify form (so `ctx.output("out")` works when the spec
    // declared `Func out("out")`).
    if (spec_name == contents->spec_primary_name) {
        return contents->user_primary_name;
    }
    {
        const std::string prefix = spec_name + "$";
        if (contents->spec_primary_name.size() > prefix.size() &&
            contents->spec_primary_name.compare(0, prefix.size(), prefix) == 0) {
            return contents->user_primary_name;
        }
    }
    const std::string *r =
        lookup_with_uniquify_suffix(contents->func_rename, spec_name);
    user_assert(r != nullptr)
        << "MatchContext: no binding recorded for spec output Func \""
        << spec_name << "\".\n";
    return *r;
}

const std::string &MatchContext::var(const std::string &spec_var) const {
    user_assert(contents)
        << "MatchContext::var(\"" << spec_var << "\") called on a "
        << "default-constructed MatchContext.\n";
    auto it = contents->var_rename.find(spec_var);
    user_assert(it != contents->var_rename.end())
        << "MatchContext: no binding recorded for spec Var \"" << spec_var
        << "\". The matcher only records bindings for Vars that surface "
        << "as For-loop names, Let bindings, or Variable nodes in the "
        << "matched region.\n";
    return it->second;
}

const Target &MatchContext::target() const {
    user_assert(contents)
        << "MatchContext::target() called on a default-constructed MatchContext.\n";
    return contents->target;
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
