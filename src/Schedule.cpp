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
    return contents.ptr->memoized;
}

bool Schedule::memoized() const {
    return contents.ptr->memoized;
}

bool &Schedule::touched() {
    return contents.ptr->touched;
}

bool Schedule::touched() const {
    return contents.ptr->touched;
}

const std::vector<Split> &Schedule::splits() const {
    return contents.ptr->splits;
}

std::vector<Split> &Schedule::splits() {
    return contents.ptr->splits;
}

std::vector<Dim> &Schedule::dims() {
    return contents.ptr->dims;
}

const std::vector<Dim> &Schedule::dims() const {
    return contents.ptr->dims;
}

std::vector<StorageDim> &Schedule::storage_dims() {
    return contents.ptr->storage_dims;
}

const std::vector<StorageDim> &Schedule::storage_dims() const {
    return contents.ptr->storage_dims;
}

std::vector<Bound> &Schedule::bounds() {
    return contents.ptr->bounds;
}

const std::vector<Bound> &Schedule::bounds() const {
    return contents.ptr->bounds;
}

const std::vector<Specialization> &Schedule::specializations() const {
    return contents.ptr->specializations;
}

const Specialization &Schedule::add_specialization(Expr condition) {
    Specialization s;
    s.condition = condition;
    s.schedule = IntrusivePtr<ScheduleContents>(new ScheduleContents);

    // The sub-schedule inherits everything about its parent except for its specializations.
    s.schedule.ptr->store_level      = contents.ptr->store_level;
    s.schedule.ptr->compute_level    = contents.ptr->compute_level;
    s.schedule.ptr->splits           = contents.ptr->splits;
    s.schedule.ptr->dims             = contents.ptr->dims;
    s.schedule.ptr->storage_dims     = contents.ptr->storage_dims;
    s.schedule.ptr->bounds           = contents.ptr->bounds;
    s.schedule.ptr->reduction_domain = contents.ptr->reduction_domain;
    s.schedule.ptr->memoized         = contents.ptr->memoized;
    s.schedule.ptr->touched          = contents.ptr->touched;
    s.schedule.ptr->allow_race_conditions = contents.ptr->allow_race_conditions;

    contents.ptr->specializations.push_back(s);
    return contents.ptr->specializations.back();
}

LoopLevel &Schedule::store_level() {
    return contents.ptr->store_level;
}

LoopLevel &Schedule::compute_level() {
    return contents.ptr->compute_level;
}

const LoopLevel &Schedule::store_level() const {
    return contents.ptr->store_level;
}

const LoopLevel &Schedule::compute_level() const {
    return contents.ptr->compute_level;
}


const ReductionDomain &Schedule::reduction_domain() const {
    return contents.ptr->reduction_domain;
}

void Schedule::set_reduction_domain(const ReductionDomain &d) {
    contents.ptr->reduction_domain = d;
}

bool &Schedule::allow_race_conditions() {
    return contents.ptr->allow_race_conditions;
}

bool Schedule::allow_race_conditions() const {
    return contents.ptr->allow_race_conditions;
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
