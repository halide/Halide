#include "IR.h"
#include "IRMutator.h"
#include "Schedule.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {

typedef std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> DeepCopyMap;

IntrusivePtr<FunctionContents> deep_copy_function_contents_helper(
    const IntrusivePtr<FunctionContents> &src,
    DeepCopyMap &copied_map);

/** A schedule for a halide function, which defines where, when, and
 * how it should be evaluated. */
struct ScheduleContents {
    mutable RefCount ref_count;

    LoopLevel store_level, compute_level;
    std::vector<Split> splits;
    std::vector<Dim> dims;
    std::vector<StorageDim> storage_dims;
    std::vector<Bound> bounds;
    std::map<std::string, IntrusivePtr<Internal::FunctionContents>> wrappers;
    ReductionDomain reduction_domain;
    bool memoized;
    bool touched;
    bool allow_race_conditions;

    ScheduleContents() : memoized(false), touched(false), allow_race_conditions(false) {};

    // Pass an IRMutator through to all Exprs referenced in the ScheduleContents
    void mutate(IRMutator *mutator) {
        for (Split &s : splits) {
            if (s.factor.defined()) {
                s.factor = mutator->mutate(s.factor);
            }
        }
        for (Bound &b : bounds) {
            if (b.min.defined()) {
                b.min = mutator->mutate(b.min);
            }
            if (b.extent.defined()) {
                b.extent = mutator->mutate(b.extent);
            }
        }
        reduction_domain.mutate(mutator);
    }
};


template<>
EXPORT RefCount &ref_count<ScheduleContents>(const ScheduleContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<ScheduleContents>(const ScheduleContents *p) {
    delete p;
}

Schedule::Schedule() : contents(new ScheduleContents) {}

Schedule Schedule::deep_copy(
        std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> &copied_map) const {

    internal_assert(contents.defined()) << "Cannot deep-copy undefined Schedule\n";
    Schedule copy;
    copy.contents->store_level = contents->store_level;
    copy.contents->compute_level = contents->compute_level;
    copy.contents->splits = contents->splits;
    copy.contents->dims = contents->dims;
    copy.contents->storage_dims = contents->storage_dims;
    copy.contents->bounds = contents->bounds;
    copy.contents->reduction_domain = contents->reduction_domain.deep_copy();
    copy.contents->memoized = contents->memoized;
    copy.contents->touched = contents->touched;
    copy.contents->allow_race_conditions = contents->allow_race_conditions;

    // Deep-copy wrapper functions. If function has already been deep-copied before,
    // i.e. it's in the 'copied_map', use the deep-copied version from the map instead
    // of creating a new deep-copy
    for (const auto &iter : contents->wrappers) {
        IntrusivePtr<FunctionContents> &copied_func = copied_map[iter.second];
        if (copied_func.defined()) {
            copy.contents->wrappers[iter.first] = copied_func;
        } else {
            copy.contents->wrappers[iter.first] = deep_copy_function_contents_helper(iter.second, copied_map);
            copied_map[iter.second] = copy.contents->wrappers[iter.first];
        }
    }
    internal_assert(copy.contents->wrappers.size() == contents->wrappers.size());
    return copy;
}

bool &Schedule::memoized() {
    return contents->memoized;
}

bool Schedule::memoized() const {
    return contents->memoized;
}

bool &Schedule::touched() {
    return contents->touched;
}

bool Schedule::touched() const {
    return contents->touched;
}

const std::vector<Split> &Schedule::splits() const {
    return contents->splits;
}

std::vector<Split> &Schedule::splits() {
    return contents->splits;
}

std::vector<Dim> &Schedule::dims() {
    return contents->dims;
}

const std::vector<Dim> &Schedule::dims() const {
    return contents->dims;
}

std::vector<StorageDim> &Schedule::storage_dims() {
    return contents->storage_dims;
}

const std::vector<StorageDim> &Schedule::storage_dims() const {
    return contents->storage_dims;
}

std::vector<Bound> &Schedule::bounds() {
    return contents->bounds;
}

const std::vector<Bound> &Schedule::bounds() const {
    return contents->bounds;
}

const std::map<std::string, IntrusivePtr<Internal::FunctionContents>> &Schedule::wrappers() const {
    return contents->wrappers;
}

void Schedule::add_wrapper(const std::string &f,
                           const IntrusivePtr<Internal::FunctionContents> &wrapper) {
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

LoopLevel &Schedule::store_level() {
    return contents->store_level;
}

LoopLevel &Schedule::compute_level() {
    return contents->compute_level;
}

const LoopLevel &Schedule::store_level() const {
    return contents->store_level;
}

const LoopLevel &Schedule::compute_level() const {
    return contents->compute_level;
}

const ReductionDomain &Schedule::reduction_domain() const {
    return contents->reduction_domain;
}

void Schedule::set_reduction_domain(const ReductionDomain &d) {
    contents->reduction_domain = d;
}

bool &Schedule::allow_race_conditions() {
    return contents->allow_race_conditions;
}

bool Schedule::allow_race_conditions() const {
    return contents->allow_race_conditions;
}

void Schedule::accept(IRVisitor *visitor) const {
    for (const Split &s : splits()) {
        if (s.factor.defined()) {
            s.factor.accept(visitor);
        }
    }
    for (const Bound &b : bounds()) {
        if (b.min.defined()) {
            b.min.accept(visitor);
        }
        if (b.extent.defined()) {
            b.extent.accept(visitor);
        }
    }
}

void Schedule::mutate(IRMutator *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

}
}
