#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "IRMutator.h"
#include "Schedule.h"
#include "Var.h"

namespace {

const char * const root_looplevel_name = "__root";
const char * const inline_looplevel_name = "";
const char * const undefined_looplevel_name = "__undefined_loop_level_var_name";

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
      is_rvar(is_rvar), locked(locked) {}
};

template<>
EXPORT RefCount &ref_count<LoopLevelContents>(const LoopLevelContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<LoopLevelContents>(const LoopLevelContents *p) {
    delete p;
}

}  // namespace Internal

LoopLevel::LoopLevel(const std::string &func_name, const std::string &var_name,
                     bool is_rvar, int stage_index, bool locked)
    : contents(new Internal::LoopLevelContents(func_name, var_name, is_rvar, stage_index, locked)) {}

LoopLevel::LoopLevel(const Internal::Function &f, VarOrRVar v, int stage_index)
    : LoopLevel(f.name(), v.name(), v.is_rvar, stage_index, false) {}

LoopLevel::LoopLevel(const Func &f, VarOrRVar v, int stage_index)
    : LoopLevel(f.function().name(), v.name(), v.is_rvar, stage_index, false) {}

// Note that even 'undefined' LoopLevels get a LoopLevelContents; this is deliberate,
// as we want to be able to create an undefined LoopLevel, pass it to another function
// to use, then mutate it afterwards via 'set()'.
LoopLevel::LoopLevel() : LoopLevel("", undefined_looplevel_name, false, -1, false) {}

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

    LoopLevel store_level, compute_level;
    std::vector<StorageDim> storage_dims;
    std::vector<Bound> bounds;
    std::vector<Bound> estimates;
    std::map<std::string, Internal::FunctionPtr> wrappers;
    bool memoized;

    FuncScheduleContents() :
        store_level(LoopLevel::inlined()), compute_level(LoopLevel::inlined()),
        memoized(false) {};

    // Pass an IRMutator2 through to all Exprs referenced in the FuncScheduleContents
    void mutate(IRMutator2 *mutator) {
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

    std::vector<std::string> get_schedule_list() {
        std::vector<std::string> schedules;

        // Declare compute and storage levels. Store/compute inline should
        // be the default, so we don't have to do anything
        compute_level.lock();
        store_level.lock();
        if (compute_level.is_root()) {
            schedules.push_back("compute_root()");
        } else if (!compute_level.is_inlined()) {
            std::ostringstream oss;
            oss << "compute_at(" << compute_level.func() << ", " << compute_level.var().name() << ")";
            schedules.push_back(oss.str());
        }

        if (store_level.is_root()) {
            schedules.push_back("store_root()");
        } else if (!store_level.is_inlined()) {
            std::ostringstream oss;
            oss << "store_at(" << compute_level.func() << ", " << compute_level.var().name() << ")";
            schedules.push_back(oss.str());
        }

        if (memoized) {
            schedules.push_back("memoize()");
        }

        std::ostringstream storage_order;
        storage_order << "reorder_storage(";
        for (size_t i = 0; i < storage_dims.size(); ++i) {
            const StorageDim &s = storage_dims[i];
            if (i > 0) {
                storage_order << ", ";
            }
            storage_order << s.var;
            if (s.alignment.defined()) {
                std::ostringstream oss;
                oss << "align_storage(" << s.var << ", " << s.alignment << ")";
                schedules.push_back(oss.str());
            }
            if (s.fold_factor.defined()) {
                std::ostringstream oss;
                oss << "fold_storage(" << s.var << ", " << s.fold_factor << ", " << s.fold_forward << ")";
                schedules.push_back(oss.str());
            }
        }
        storage_order << ")";
        schedules.push_back(storage_order.str());

        for (const Bound &b : bounds) {
            std::ostringstream oss;
            if (!b.modulus.defined() && !b.remainder.defined()) {
                oss << "bound(" << b.var << ", " << b.min << ", " << b.extent << ")";
            } else {
                internal_assert(!b.min.defined() && !b.extent.defined());
                oss << "align_bounds(" << b.var << ", " << b.min << ", " << b.extent << ")";
            }
            schedules.push_back(oss.str());
        }

        for (const Bound &e : estimates) {
            std::ostringstream oss;
            internal_assert(!e.modulus.defined() && !e.remainder.defined());
            oss << "estimate(" << e.var << ", " << e.min << ", " << e.extent << ")";
            schedules.push_back(oss.str());
        }

        // TODO(psuriana): How do you handle wrappers?

        return schedules;
    }
};

template<>
EXPORT RefCount &ref_count<FuncScheduleContents>(const FuncScheduleContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<FuncScheduleContents>(const FuncScheduleContents *p) {
    delete p;
}

namespace {
std::ostream& operator<<(std::ostream& stream, const LoopAlignStrategy strategy){
    switch(strategy){
        case LoopAlignStrategy::AlignStart:
            stream << "LoopAlignStrategy::AlignStart";
            break;
        case LoopAlignStrategy::AlignEnd:
            stream << "LoopAlignStrategy::AlignEnd";
            break;
        case LoopAlignStrategy::NoAlign:
            stream << "LoopAlignStrategy::NoAlign";
            break;
        case LoopAlignStrategy::Auto:
            stream << "LoopAlignStrategy::Auto";
            break;
        default:
            internal_assert(false);
    }
    return stream;
}
} // anonymous namespace


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
    bool touched;
    bool allow_race_conditions;

    StageScheduleContents() : fuse_level(FuseLoopLevel()), touched(false),
                              allow_race_conditions(false) {};

    // Pass an IRMutator2 through to all Exprs referenced in the StageScheduleContents
    void mutate(IRMutator2 *mutator) {
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

    std::vector<std::string> get_schedule_list() {
        std::vector<std::string> schedules;

        if (allow_race_conditions) {
            schedules.push_back("allow_race_conditions()");
        }

        for (const Split &s : splits) {
            std::ostringstream oss;
            if (s.is_split()) {
                oss << "split(" << s.old_var << ", " << s.outer << ", " << s.inner << ", " << s.factor;
                switch (s.tail) {
                    case TailStrategy::RoundUp:
                        oss << ", TailStrategy::RoundUp)";
                        break;
                    case TailStrategy::GuardWithIf:
                        oss << ", TailStrategy::GuardWithIf)";
                        break;
                    case TailStrategy::ShiftInwards:
                        oss << ", TailStrategy::ShiftInwards)";
                        break;
                    case TailStrategy::Auto:
                        oss << ")";
                        break;
                    default:
                        internal_assert(false);
                    }
            } else if (s.is_fuse()) {
                oss << "fuse(" << s.inner << ", " << s.outer << ", " << s.old_var << ")";
            } else if (s.is_rename()) {
                oss << "rename(" << s.old_var << ", " << s.outer << ")";
            } else { // Purify
                // TODO(psuriana): How do you re-generate rfactor?
                user_assert(false) << "Cannot generate schedule resulting from rfactor";
            }
            schedules.push_back(oss.str());
        }

        std::ostringstream dims_order;
        dims_order << "reorder(";
        for (int i = 0; i < (int)dims.size() - 1; ++i) { // Ignore __outermost
            const Dim &d = dims[i];
            if (i > 0) {
                dims_order << ", ";
            }
            dims_order << d.var;

            // Mark device API
            std::string device_api;
            switch (d.device_api)  {
                case DeviceAPI::Hexagon:
                    device_api = "DeviceAPI::Hexagon";
                    schedules.push_back("hexagon(" + d.var + ")");
                    break;
                default:
                    // Other device APIs (excluding Host and None) are
                    // assigned by gpu_XXX()
                    break;
            }

            // Mark for-loop type
            std::ostringstream oss;
            switch (d.for_type)  {
                case ForType::Parallel:
                    oss << "parallel(" << d.var << ")";
                    break;
                case ForType::Vectorized:
                    oss << "vectorize(" << d.var << ")";
                    break;
                case ForType::Unrolled:
                    oss << "unroll(" << d.var << ")";
                    break;
                case ForType::GPUBlock:
                    oss << "gpu_blocks(" << d.var << ", " << device_api << ")";
                    break;
                case ForType::GPUThread:
                    oss << "gpu_threads(" << d.var << ", " << device_api << ")";
                    break;
                default:
                    break;
            }
            if (!oss.str().empty()) {
                schedules.push_back(oss.str());
            }
        }
        dims_order << ")";
        schedules.push_back(dims_order.str());

        for (const PrefetchDirective &p : prefetches) {
            std::ostringstream oss;
            oss << "prefetch(" << p.name << ", " << p.var << ", " << p.offset << ", ";
            switch (p.strategy) {
                case PrefetchBoundStrategy::Clamp:
                    oss << "PrefetchBoundStrategy::Clamp";
                    break;
                case PrefetchBoundStrategy::GuardWithIf:
                    oss << "PrefetchBoundStrategy::GuardWithIf";
                    break;
                case PrefetchBoundStrategy::NonFaulting:
                    oss << "TailStrategy::NonFaulting";
                    break;
                default:
                    internal_assert(false);
            }
            oss << ")";
            schedules.push_back(oss.str());
        }

        // Is this stage computed with some other stage?
        if (!fuse_level.level.is_inlined() && !fuse_level.level.is_root()) {
            std::ostringstream oss;
            oss << "compute_with(" << fuse_level.level.func();
            if (fuse_level.level.stage_index() > 0) {
                oss << ".update(" << fuse_level.level.stage_index() << ")";
            }
            oss << ", " << fuse_level.level.var().name() << ", {";
            for (auto iter = fuse_level.align.begin(); iter != fuse_level.align.end(); ++iter) {
                oss << "{" << iter->first << ", " << iter->second << "}";
                if (std::next(iter) != fuse_level.align.end()) {
                    oss << ", ";
                }
            }
            oss << "})";
            schedules.push_back(oss.str());
        }

        return schedules;
    }
};

template<>
EXPORT RefCount &ref_count<StageScheduleContents>(const StageScheduleContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<StageScheduleContents>(const StageScheduleContents *p) {
    delete p;
}


FuncSchedule::FuncSchedule() : contents(new FuncScheduleContents) {}

FuncSchedule FuncSchedule::deep_copy(
        std::map<FunctionPtr, FunctionPtr> &copied_map) const {

    internal_assert(contents.defined()) << "Cannot deep-copy undefined FuncSchedule\n";
    FuncSchedule copy;
    copy.contents->store_level = contents->store_level;
    copy.contents->compute_level = contents->compute_level;
    copy.contents->storage_dims = contents->storage_dims;
    copy.contents->bounds = contents->bounds;
    copy.contents->estimates = contents->estimates;
    copy.contents->memoized = contents->memoized;

    // Deep-copy wrapper functions.
    for (const auto &iter : contents->wrappers) {
        FunctionPtr &copied_func = copied_map[iter.second];
        internal_assert(copied_func.defined()) << Function(iter.second).name() << "\n";
        copy.contents->wrappers[iter.first] = copied_func;
    }
    internal_assert(copy.contents->wrappers.size() == contents->wrappers.size());
    return copy;
}

bool &FuncSchedule::memoized() {
    return contents->memoized;
}

bool FuncSchedule::memoized() const {
    return contents->memoized;
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

const LoopLevel &FuncSchedule::store_level() const {
    return contents->store_level;
}

const LoopLevel &FuncSchedule::compute_level() const {
    return contents->compute_level;
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
}

void FuncSchedule::mutate(IRMutator2 *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

std::vector<std::string> FuncSchedule::get_schedule_list() {
    internal_assert(contents.defined());
    return contents->get_schedule_list();
}


StageSchedule::StageSchedule() : contents(new StageScheduleContents) {}

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

void StageSchedule::mutate(IRMutator2 *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

std::vector<std::string> StageSchedule::get_schedule_list() {
    internal_assert(contents.defined());
    return contents->get_schedule_list();
}

}  // namespace Internal
}  // namespace Halide
