#include "IR.h"
#include "Schedule.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {

using std::vector;

/** A schedule for a halide function, which defines where, when, and
 * how it should be evaluated. */
struct ScheduleContents {
    mutable RefCount ref_count;

    LoopLevel store_level, compute_level;
    vector<Split> splits;
    vector<Dim> dims;
    vector<std::string> storage_dims;
    vector<Bound> bounds;
    vector<Specialization> specializations;
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

Schedule Schedule::copy() const {
    IntrusivePtr<ScheduleContents> c(new ScheduleContents);

    // Copy over everything but the ref count
    c.ptr->store_level      = contents.ptr->store_level;
    c.ptr->compute_level    = contents.ptr->compute_level;
    c.ptr->splits           = contents.ptr->splits;
    c.ptr->dims             = contents.ptr->dims;
    c.ptr->storage_dims     = contents.ptr->storage_dims;
    c.ptr->bounds           = contents.ptr->bounds;
    c.ptr->reduction_domain = contents.ptr->reduction_domain;
    c.ptr->memoized         = contents.ptr->memoized;
    c.ptr->touched          = contents.ptr->touched;
    c.ptr->allow_race_conditions = contents.ptr->allow_race_conditions;

    // Recursively copy the specializations
    for (Specialization &s : contents.ptr->specializations) {
        Specialization s_copy = s;
        s_copy.schedule = Schedule(s.schedule).copy().contents;
        c.ptr->specializations.push_back(s_copy);
    }

    return Schedule(c);
}

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

const vector<Split> &Schedule::splits() const {
    return contents.ptr->splits;
}

vector<Split> &Schedule::splits() {
    return contents.ptr->splits;
}

vector<Dim> &Schedule::dims() {
    return contents.ptr->dims;
}

const vector<Dim> &Schedule::dims() const {
    return contents.ptr->dims;
}

vector<std::string> &Schedule::storage_dims() {
    return contents.ptr->storage_dims;
}

const vector<std::string> &Schedule::storage_dims() const {
    return contents.ptr->storage_dims;
}

vector<Bound> &Schedule::bounds() {
    return contents.ptr->bounds;
}

const vector<Bound> &Schedule::bounds() const {
    return contents.ptr->bounds;
}

const vector<Specialization> &Schedule::specializations() const {
    return contents.ptr->specializations;
}

const Specialization &Schedule::add_specialization(Expr condition) {
    Specialization s;
    s.condition = condition;
    // The sub-schedule inherits everything about its parent except for its specializations.
    vector<Specialization> tmp;
    contents.ptr->specializations.swap(tmp);
    s.schedule = copy().contents;
    contents.ptr->specializations.swap(tmp);
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
