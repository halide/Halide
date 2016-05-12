#include "IR.h"
#include "Schedule.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {


/** A schedule for a halide function, which defines where, when, and
 * how it should be evaluated. */
struct ScheduleContents {
    mutable RefCount ref_count;

    LoopLevel store_level, compute_level;
    std::vector<Split> splits;
    std::vector<Dim> dims;
    std::vector<StorageDim> storage_dims;
    std::vector<Bound> bounds;
    std::vector<Specialization> specializations;
    ReductionDomain reduction_domain;
    bool memoized;
    bool touched;
    bool allow_race_conditions;

    ScheduleContents() : memoized(false), touched(false), allow_race_conditions(false) {};
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

const std::vector<Specialization> &Schedule::specializations() const {
    return contents->specializations;
}

const Specialization &Schedule::add_specialization(Expr condition) {
    Specialization s;
    s.condition = condition;
    s.schedule = IntrusivePtr<ScheduleContents>(new ScheduleContents);

    // The sub-schedule inherits everything about its parent except for its specializations.
    s.schedule->store_level      = contents->store_level;
    s.schedule->compute_level    = contents->compute_level;
    s.schedule->splits           = contents->splits;
    s.schedule->dims             = contents->dims;
    s.schedule->storage_dims     = contents->storage_dims;
    s.schedule->bounds           = contents->bounds;
    s.schedule->reduction_domain = contents->reduction_domain;
    s.schedule->memoized         = contents->memoized;
    s.schedule->touched          = contents->touched;
    s.schedule->allow_race_conditions = contents->allow_race_conditions;

    contents->specializations.push_back(s);
    return contents->specializations.back();
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
    for (const Specialization &s : specializations()) {
        s.condition.accept(visitor);
    }
}

}
}
