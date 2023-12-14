#include "Schedule.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "IRMutator.h"
#include "Var.h"

namespace {

const char *const root_looplevel_name = "__root";
const char *const inline_looplevel_name = "";
const char *const undefined_looplevel_name = "__undefined_loop_level_var_name";

}  // namespace

namespace Halide {

namespace Internal {

struct LoopLevelContents {
    mutable RefCount ref_count;

    // Note: func_name is empty for inline or root.
    std::string func_name;
    // If set to -1, this loop level does not refer to a particular stage of the
    // function. 0 refers to initial stage, 1 refers to the 1st update stage, etc.
    int stage_index;
    // TODO: these two fields should really be VarOrRVar,
    // but cyclical include dependencies make this challenging.
    std::string var_name;
    bool is_rvar;
    bool locked;

    LoopLevelContents(const std::string &func_name,
                      const std::string &var_name,
                      bool is_rvar,
                      int stage_index,
                      bool locked)
        : func_name(func_name), stage_index(stage_index), var_name(var_name),
          is_rvar(is_rvar), locked(locked) {
    }
};

template<>
RefCount &ref_count<LoopLevelContents>(const LoopLevelContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<LoopLevelContents>(const LoopLevelContents *p) {
    delete p;
}

}  // namespace Internal

LoopLevel::LoopLevel(const std::string &func_name, const std::string &var_name,
                     bool is_rvar, int stage_index, bool locked)
    : contents(new Internal::LoopLevelContents(func_name, var_name, is_rvar, stage_index, locked)) {
}

LoopLevel::LoopLevel(const Internal::Function &f, const VarOrRVar &v, int stage_index)
    : LoopLevel(f.name(), v.name(), v.is_rvar, stage_index, false) {
}

LoopLevel::LoopLevel(const Func &f, const VarOrRVar &v, int stage_index)
    : LoopLevel(f.function().name(), v.name(), v.is_rvar, stage_index, false) {
}

// Note that even 'undefined' LoopLevels get a LoopLevelContents; this is deliberate,
// as we want to be able to create an undefined LoopLevel, pass it to another function
// to use, then mutate it afterwards via 'set()'.
LoopLevel::LoopLevel()
    : LoopLevel("", undefined_looplevel_name, false, -1, false) {
}

void LoopLevel::check_defined() const {
    internal_assert(defined());
}

void LoopLevel::check_locked() const {
    // A LoopLevel can be in one of two states:
    //   - Unlocked (the default state): An unlocked LoopLevel can be mutated freely (via the set() method),
    //     but cannot be inspected (calls to func(), var(), is_inlined(), is_root(), etc.
    //     will assert-fail). This is the only sort of LoopLevel that most user code will ever encounter.
    //   - Locked: Once a LoopLevel is locked, it can be freely inspected, but no longer mutated.
    //     Halide locks all LoopLevels during the lowering process to ensure that no user
    //     code (e.g. custom passes) can interfere with invariants.
    user_assert(contents->locked)
        << "Cannot inspect an unlocked LoopLevel: "
        << contents->func_name << "." << contents->var_name
        << "\n";
}

void LoopLevel::check_defined_and_locked() const {
    check_defined();
    check_locked();
}

void LoopLevel::set(const LoopLevel &other) {
    // Don't check locked(), since we don't care if it's defined() or not
    user_assert(!contents->locked)
        << "Cannot call set() on a locked LoopLevel: "
        << contents->func_name << "." << contents->var_name
        << "\n";
    contents->func_name = other.contents->func_name;
    contents->stage_index = other.contents->stage_index;
    contents->var_name = other.contents->var_name;
    contents->is_rvar = other.contents->is_rvar;
}

LoopLevel &LoopLevel::lock() {
    contents->locked = true;

    // If you have an undefined LoopLevel at the point we're
    // locking it (i.e., start of lowering), you've done something wrong,
    // so let's give a more useful error message.
    user_assert(defined())
        << "There should be no undefined LoopLevels at the start of lowering. "
        << "(Did you mean to use LoopLevel::inlined() instead of LoopLevel() ?)";

    return *this;
}

bool LoopLevel::defined() const {
    check_locked();
    return contents->var_name != undefined_looplevel_name;
}

std::string LoopLevel::func() const {
    check_defined_and_locked();
    return contents->func_name;
}

int LoopLevel::stage_index() const {
    check_defined_and_locked();
    internal_assert(contents->stage_index >= 0);
    return contents->stage_index;
}

VarOrRVar LoopLevel::var() const {
    check_defined_and_locked();
    internal_assert(!is_inlined() && !is_root());
    return VarOrRVar(contents->var_name, contents->is_rvar);
}

/*static*/
LoopLevel LoopLevel::inlined() {
    return LoopLevel("", inline_looplevel_name, false, -1);
}

bool LoopLevel::is_inlined() const {
    // It's OK to be undefined (just return false).
    check_locked();
    return contents->var_name == inline_looplevel_name;
}

/*static*/
LoopLevel LoopLevel::root() {
    return LoopLevel("", root_looplevel_name, false, -1);
}

bool LoopLevel::is_root() const {
    // It's OK to be undefined (just return false).
    check_locked();
    return contents->var_name == root_looplevel_name;
}

int LoopLevel::get_stage_index() const {
    return contents->stage_index;
}

std::string LoopLevel::func_name() const {
    return contents->func_name;
}

std::string LoopLevel::var_name() const {
    return contents->var_name;
}

bool LoopLevel::is_rvar() const {
    return contents->is_rvar;
}

bool LoopLevel::locked() const {
    return contents->locked;
}

std::string LoopLevel::to_string() const {
    check_defined_and_locked();
    if (contents->stage_index == -1) {
        return contents->func_name + "." + contents->var_name;
    } else {
        return contents->func_name + ".s" + std::to_string(contents->stage_index) + "." + contents->var_name;
    }
}

bool LoopLevel::match(const std::string &loop) const {
    check_defined_and_locked();
    if (contents->stage_index == -1) {
        return Internal::starts_with(loop, contents->func_name + ".") &&
               Internal::ends_with(loop, "." + contents->var_name);
    } else {
        std::string prefix = contents->func_name + ".s" + std::to_string(contents->stage_index) + ".";
        return Internal::starts_with(loop, prefix) &&
               Internal::ends_with(loop, "." + contents->var_name);
    }
}

bool LoopLevel::match(const LoopLevel &other) const {
    check_defined_and_locked();
    other.check_defined_and_locked();
    return (contents->func_name == other.contents->func_name &&
            (contents->var_name == other.contents->var_name ||
             Internal::ends_with(contents->var_name, "." + other.contents->var_name) ||
             Internal::ends_with(other.contents->var_name, "." + contents->var_name)) &&
            (contents->stage_index == other.contents->stage_index));
}

bool LoopLevel::operator==(const LoopLevel &other) const {
    check_defined_and_locked();
    other.check_defined_and_locked();
    return (contents->func_name == other.contents->func_name) &&
           (contents->stage_index == other.contents->stage_index) &&
           (contents->var_name == other.contents->var_name);
}

namespace Internal {

typedef std::map<FunctionPtr, FunctionPtr> DeepCopyMap;

/** A schedule for a halide function, which defines where, when, and
 * how it should be evaluated. */
struct FuncScheduleContents {
    mutable RefCount ref_count;

    LoopLevel store_level, compute_level, hoist_storage_level;
    std::vector<StorageDim> storage_dims;
    std::vector<Bound> bounds;
    std::vector<Bound> estimates;
    std::map<std::string, Internal::FunctionPtr> wrappers;
    MemoryType memory_type = MemoryType::Auto;
    bool memoized = false;
    bool async = false;
    // This is an extent of the ring buffer and expected to be a positive integer.
    Expr ring_buffer;
    Expr memoize_eviction_key;

    FuncScheduleContents()
        : store_level(LoopLevel::inlined()), compute_level(LoopLevel::inlined()), hoist_storage_level(LoopLevel::inlined()) {
    }

    // Pass an IRMutator through to all Exprs referenced in the FuncScheduleContents
    void mutate(IRMutator *mutator) {
        for (Bound &b : bounds) {
            if (b.min.defined()) {
                b.min = mutator->mutate(b.min);
            }
            if (b.extent.defined()) {
                b.extent = mutator->mutate(b.extent);
            }
            if (b.modulus.defined()) {
                b.modulus = mutator->mutate(b.modulus);
            }
            if (b.remainder.defined()) {
                b.remainder = mutator->mutate(b.remainder);
            }
        }
        for (Bound &b : estimates) {
            if (b.min.defined()) {
                b.min = mutator->mutate(b.min);
            }
            if (b.extent.defined()) {
                b.extent = mutator->mutate(b.extent);
            }
            if (b.modulus.defined()) {
                b.modulus = mutator->mutate(b.modulus);
            }
            if (b.remainder.defined()) {
                b.remainder = mutator->mutate(b.remainder);
            }
        }
    }
};

template<>
RefCount &ref_count<FuncScheduleContents>(const FuncScheduleContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<FuncScheduleContents>(const FuncScheduleContents *p) {
    delete p;
}

/** A schedule for a sigle halide stage_index, which defines where, when, and
 * how it should be evaluated. */
struct StageScheduleContents {
    mutable RefCount ref_count;

    std::vector<ReductionVariable> rvars;
    std::vector<Split> splits;
    std::vector<Dim> dims;
    std::vector<PrefetchDirective> prefetches;
    FuseLoopLevel fuse_level;
    std::vector<FusedPair> fused_pairs;
    bool touched = false;
    bool allow_race_conditions = false;
    bool atomic = false;
    bool override_atomic_associativity_test = false;

    StageScheduleContents()
        : fuse_level(FuseLoopLevel()) {
    }

    // Pass an IRMutator through to all Exprs referenced in the StageScheduleContents
    void mutate(IRMutator *mutator) {
        for (ReductionVariable &r : rvars) {
            if (r.min.defined()) {
                r.min = mutator->mutate(r.min);
            }
            if (r.extent.defined()) {
                r.extent = mutator->mutate(r.extent);
            }
        }
        for (Split &s : splits) {
            if (s.factor.defined()) {
                s.factor = mutator->mutate(s.factor);
            }
        }
        for (PrefetchDirective &p : prefetches) {
            if (p.offset.defined()) {
                p.offset = mutator->mutate(p.offset);
            }
        }
    }
};

template<>
RefCount &ref_count<StageScheduleContents>(const StageScheduleContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<StageScheduleContents>(const StageScheduleContents *p) {
    delete p;
}

FuncSchedule::FuncSchedule()
    : contents(new FuncScheduleContents) {
}

FuncSchedule FuncSchedule::deep_copy(
    std::map<FunctionPtr, FunctionPtr> &copied_map) const {

    internal_assert(contents.defined()) << "Cannot deep-copy undefined FuncSchedule\n";
    FuncSchedule copy;
    copy.contents->store_level = contents->store_level;
    copy.contents->compute_level = contents->compute_level;
    copy.contents->hoist_storage_level = contents->hoist_storage_level;
    copy.contents->storage_dims = contents->storage_dims;
    copy.contents->bounds = contents->bounds;
    copy.contents->estimates = contents->estimates;
    copy.contents->memory_type = contents->memory_type;
    copy.contents->memoized = contents->memoized;
    copy.contents->memoize_eviction_key = contents->memoize_eviction_key;
    copy.contents->async = contents->async;
    copy.contents->ring_buffer = contents->ring_buffer;

    // Deep-copy wrapper functions.
    for (const auto &iter : contents->wrappers) {
        FunctionPtr &copied_func = copied_map[iter.second];
        internal_assert(copied_func.defined()) << Function(iter.second).name() << "\n";
        copy.contents->wrappers[iter.first] = copied_func;
    }
    internal_assert(copy.contents->wrappers.size() == contents->wrappers.size());
    return copy;
}

MemoryType FuncSchedule::memory_type() const {
    return contents->memory_type;
}

MemoryType &FuncSchedule::memory_type() {
    return contents->memory_type;
}

bool &FuncSchedule::memoized() {
    return contents->memoized;
}

bool FuncSchedule::memoized() const {
    return contents->memoized;
}

Expr &FuncSchedule::memoize_eviction_key() {
    return contents->memoize_eviction_key;
}

Expr FuncSchedule::memoize_eviction_key() const {
    return contents->memoize_eviction_key;
}

bool &FuncSchedule::async() {
    return contents->async;
}

bool FuncSchedule::async() const {
    return contents->async;
}

Expr &FuncSchedule::ring_buffer() {
    return contents->ring_buffer;
}

Expr &FuncSchedule::ring_buffer() const {
    return contents->ring_buffer;
}

std::vector<StorageDim> &FuncSchedule::storage_dims() {
    return contents->storage_dims;
}

const std::vector<StorageDim> &FuncSchedule::storage_dims() const {
    return contents->storage_dims;
}

std::vector<Bound> &FuncSchedule::bounds() {
    return contents->bounds;
}

const std::vector<Bound> &FuncSchedule::bounds() const {
    return contents->bounds;
}

std::vector<Bound> &FuncSchedule::estimates() {
    return contents->estimates;
}

const std::vector<Bound> &FuncSchedule::estimates() const {
    return contents->estimates;
}

std::map<std::string, Internal::FunctionPtr> &FuncSchedule::wrappers() {
    return contents->wrappers;
}

const std::map<std::string, Internal::FunctionPtr> &FuncSchedule::wrappers() const {
    return contents->wrappers;
}

void FuncSchedule::add_wrapper(const std::string &f,
                               const Internal::FunctionPtr &wrapper) {
    if (contents->wrappers.count(f)) {
        if (f.empty()) {
            user_warning << "Replacing previous definition of global wrapper in function \""
                         << f << "\"\n";
        } else {
            internal_error << "Wrapper redefinition in function \"" << f << "\" is not allowed\n";
        }
    }
    contents->wrappers[f] = wrapper;
}

LoopLevel &FuncSchedule::store_level() {
    return contents->store_level;
}

LoopLevel &FuncSchedule::compute_level() {
    return contents->compute_level;
}

LoopLevel &FuncSchedule::hoist_storage_level() {
    return contents->hoist_storage_level;
}

const LoopLevel &FuncSchedule::store_level() const {
    return contents->store_level;
}

const LoopLevel &FuncSchedule::compute_level() const {
    return contents->compute_level;
}

const LoopLevel &FuncSchedule::hoist_storage_level() const {
    return contents->hoist_storage_level;
}

void FuncSchedule::accept(IRVisitor *visitor) const {
    for (const Bound &b : bounds()) {
        if (b.min.defined()) {
            b.min.accept(visitor);
        }
        if (b.extent.defined()) {
            b.extent.accept(visitor);
        }
        if (b.modulus.defined()) {
            b.modulus.accept(visitor);
        }
        if (b.remainder.defined()) {
            b.remainder.accept(visitor);
        }
    }
    for (const Bound &b : estimates()) {
        if (b.min.defined()) {
            b.min.accept(visitor);
        }
        if (b.extent.defined()) {
            b.extent.accept(visitor);
        }
        if (b.modulus.defined()) {
            b.modulus.accept(visitor);
        }
        if (b.remainder.defined()) {
            b.remainder.accept(visitor);
        }
    }
    if (memoize_eviction_key().defined()) {
        memoize_eviction_key().accept(visitor);
    }
}

void FuncSchedule::mutate(IRMutator *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

StageSchedule::StageSchedule()
    : contents(new StageScheduleContents) {
}

StageSchedule::StageSchedule(const std::vector<ReductionVariable> &rvars, const std::vector<Split> &splits,
                             const std::vector<Dim> &dims, const std::vector<PrefetchDirective> &prefetches,
                             const FuseLoopLevel &fuse_level, const std::vector<FusedPair> &fused_pairs,
                             bool touched, bool allow_race_conditions, bool atomic, bool override_atomic_associativity_test)
    : contents(new StageScheduleContents) {
    contents->rvars = rvars;
    contents->splits = splits;
    contents->dims = dims;
    contents->prefetches = prefetches;
    contents->fuse_level = fuse_level;
    contents->fused_pairs = fused_pairs;
    contents->touched = touched;
    contents->allow_race_conditions = allow_race_conditions;
    contents->atomic = atomic;
    contents->override_atomic_associativity_test = override_atomic_associativity_test;
}

StageSchedule StageSchedule::get_copy() const {
    internal_assert(contents.defined()) << "Cannot copy undefined Schedule\n";
    StageSchedule copy;
    copy.contents->rvars = contents->rvars;
    copy.contents->splits = contents->splits;
    copy.contents->dims = contents->dims;
    copy.contents->prefetches = contents->prefetches;
    copy.contents->fuse_level = contents->fuse_level;
    copy.contents->fused_pairs = contents->fused_pairs;
    copy.contents->touched = contents->touched;
    copy.contents->allow_race_conditions = contents->allow_race_conditions;
    copy.contents->atomic = contents->atomic;
    copy.contents->override_atomic_associativity_test = contents->override_atomic_associativity_test;
    return copy;
}

bool &StageSchedule::touched() {
    return contents->touched;
}

bool StageSchedule::touched() const {
    return contents->touched;
}

std::vector<ReductionVariable> &StageSchedule::rvars() {
    return contents->rvars;
}

const std::vector<ReductionVariable> &StageSchedule::rvars() const {
    return contents->rvars;
}

const std::vector<Split> &StageSchedule::splits() const {
    return contents->splits;
}

std::vector<Split> &StageSchedule::splits() {
    return contents->splits;
}

std::vector<Dim> &StageSchedule::dims() {
    return contents->dims;
}

const std::vector<Dim> &StageSchedule::dims() const {
    return contents->dims;
}

std::vector<PrefetchDirective> &StageSchedule::prefetches() {
    return contents->prefetches;
}

const std::vector<PrefetchDirective> &StageSchedule::prefetches() const {
    return contents->prefetches;
}

FuseLoopLevel &StageSchedule::fuse_level() {
    return contents->fuse_level;
}

const FuseLoopLevel &StageSchedule::fuse_level() const {
    return contents->fuse_level;
}

std::vector<FusedPair> &StageSchedule::fused_pairs() {
    return contents->fused_pairs;
}

const std::vector<FusedPair> &StageSchedule::fused_pairs() const {
    return contents->fused_pairs;
}

bool &StageSchedule::allow_race_conditions() {
    return contents->allow_race_conditions;
}

bool StageSchedule::allow_race_conditions() const {
    return contents->allow_race_conditions;
}

bool &StageSchedule::atomic() {
    return contents->atomic;
}

bool StageSchedule::atomic() const {
    return contents->atomic;
}

bool &StageSchedule::override_atomic_associativity_test() {
    return contents->override_atomic_associativity_test;
}

bool StageSchedule::override_atomic_associativity_test() const {
    return contents->override_atomic_associativity_test;
}

void StageSchedule::accept(IRVisitor *visitor) const {
    for (const ReductionVariable &r : rvars()) {
        if (r.min.defined()) {
            r.min.accept(visitor);
        }
        if (r.extent.defined()) {
            r.extent.accept(visitor);
        }
    }
    for (const Split &s : splits()) {
        if (s.factor.defined()) {
            s.factor.accept(visitor);
        }
    }
    for (const PrefetchDirective &p : prefetches()) {
        if (p.offset.defined()) {
            p.offset.accept(visitor);
        }
    }
}

void StageSchedule::mutate(IRMutator *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

}  // namespace Internal
}  // namespace Halide
