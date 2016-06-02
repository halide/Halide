#include <stdlib.h>

#include "IR.h"
#include "IRMutator.h"
#include "Definition.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;

struct DefinitionContents {
    mutable RefCount ref_count;
    bool is_init;
    std::vector<Expr> values, args;
    Schedule schedule;
    ReductionDomain domain;
    std::vector<Specialization> specializations;

    DefinitionContents() : is_init(true) {}

    void accept(IRVisitor *visitor) const {
        for (Expr val : values) {
            val.accept(visitor);
        }
        for (Expr arg : args) {
            arg.accept(visitor);
        }

        schedule.accept(visitor);

        if (domain.defined()) {
            domain.accept(visitor);
        }

        for (const Specialization &s : specializations) {
            if (s.condition.defined()) {
                s.condition.accept(visitor);
            }
            s.definition.accept(visitor);
        }
    }

    void mutate(IRMutator *mutator) {
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = mutator->mutate(values[i]);
        }
        for (size_t i = 0; i < args.size(); ++i) {
            args[i] = mutator->mutate(args[i]);
        }

        schedule.mutate(mutator);

        if (domain.defined()) {
            domain.mutate(mutator);
        }

        for (Specialization &s : specializations) {
            if (s.condition.defined()) {
                s.condition = mutator->mutate(s.condition);
            }
            s.definition.mutate(mutator);
        }
    }
};

template<>
EXPORT RefCount &ref_count<DefinitionContents>(const DefinitionContents *d) {
    return d->ref_count;
}

template<>
EXPORT void destroy<DefinitionContents>(const DefinitionContents *d) {
    delete d;
}

Definition::Definition() : contents(new DefinitionContents) {}

Definition::Definition(const IntrusivePtr<DefinitionContents> &ptr) : contents(ptr) {
    internal_assert(ptr.defined())
        << "Can't construct Function from undefined DefinitionContents ptr\n";
    internal_assert(!contents->is_init || !contents->domain.defined())
        << "Init definition should not have a reduction domain\n";
    internal_assert((!contents->domain.defined() && !contents->schedule.reduction_domain().defined()) ||
                    (contents->domain.defined() && contents->schedule.reduction_domain().same_as(contents->domain)))
        << "Definition should point to the same reduction domain as its schedule\n";
}

Definition::Definition(const std::vector<Expr> &args, const std::vector<Expr> &values,
                       const ReductionDomain &rdom, bool is_init) : contents(new DefinitionContents) {
    contents->is_init = is_init;
    contents->values = values;
    contents->args = args;
    contents->domain = rdom;
    // Definition's domain is the same as the one pointed by its schedule.
    contents->schedule.set_reduction_domain(contents->domain);

    internal_assert(!contents->is_init || !contents->domain.defined())
        << "Init definition should not have a reduction domain\n";
}

// Return deep-copy of Definition
Definition Definition::deep_copy(
        std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> &copied_map) const {
    internal_assert(contents.defined()) << "Cannot deep-copy undefined Definition\n";

    Definition copy;
    copy.contents->is_init = contents->is_init;
    copy.contents->values = contents->values;
    copy.contents->args = contents->args;
    copy.contents->schedule = contents->schedule.deep_copy(copied_map);

    // Definition's domain is the same as the one pointed by its schedule.
    internal_assert(!contents->is_init || !contents->domain.defined())
        << "Init definition should not have a reduction domain\n";

    internal_assert((!contents->domain.defined() && !contents->schedule.reduction_domain().defined()) ||
                    (contents->domain.defined() && contents->schedule.reduction_domain().same_as(contents->domain)))
        << "Definition should point to the same reduction domain as its schedule\n";
    // We don't need to deep-copy the reduction domain since we've already done
    // it when deep-copying the schedule above
    copy.contents->domain = copy.schedule().reduction_domain();

    // Deep-copy specializations
    for (const Specialization &s : contents->specializations) {
        Specialization s_copy;
        s_copy.condition = s.condition;
        s_copy.definition = s.definition.deep_copy(copied_map);
        copy.contents->specializations.push_back(std::move(s_copy));
    }
    return copy;
}

bool Definition::is_init() const {
    internal_assert(!contents->is_init || !contents->domain.defined())
        << "Init definition shouldn't have reduction domain\n";
    return contents->is_init;
}

void Definition::accept(IRVisitor *visitor) const {
    contents->accept(visitor);
}

void Definition::mutate(IRMutator *mutator) {
    contents->mutate(mutator);
}

std::vector<Expr> &Definition::args() {
    return contents->args;
}

const std::vector<Expr> &Definition::args() const {
    return contents->args;
}

std::vector<Expr> &Definition::values() {
    return contents->values;
}

const std::vector<Expr> &Definition::values() const {
    return contents->values;
}

Schedule &Definition::schedule() {
    return contents->schedule;
}

const Schedule &Definition::schedule() const {
    return contents->schedule;
}

const ReductionDomain &Definition::domain() const {
    return contents->domain;
}

void Definition::set_domain(const ReductionDomain &d) {
    contents->domain = d;
}

std::vector<Specialization> &Definition::specializations() {
    return contents->specializations;
}

const std::vector<Specialization> &Definition::specializations() const {
    return contents->specializations;
}

const Specialization &Definition::add_specialization(Expr condition) {
    Specialization s;
    s.condition = condition;
    s.definition.contents->is_init = contents->is_init;
    s.definition.contents->values = contents->values;
    s.definition.contents->args   = contents->args;
    s.definition.contents->domain = contents->domain;

    // The sub-schedule inherits everything about its parent except for its specializations.
    s.definition.contents->schedule.store_level()      = contents->schedule.store_level();
    s.definition.contents->schedule.compute_level()    = contents->schedule.compute_level();
    s.definition.contents->schedule.splits()           = contents->schedule.splits();
    s.definition.contents->schedule.dims()             = contents->schedule.dims();
    s.definition.contents->schedule.storage_dims()     = contents->schedule.storage_dims();
    s.definition.contents->schedule.bounds()           = contents->schedule.bounds();
    s.definition.contents->schedule.set_reduction_domain(contents->schedule.reduction_domain());
    s.definition.contents->schedule.memoized()         = contents->schedule.memoized();
    s.definition.contents->schedule.touched()          = contents->schedule.touched();
    s.definition.contents->schedule.allow_race_conditions() = contents->schedule.allow_race_conditions();

    contents->specializations.push_back(s);
    return contents->specializations.back();
}

}
}
