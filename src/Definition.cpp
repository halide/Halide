#include <stdlib.h>

#include "IR.h"
#include "IROperator.h"
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
    Expr predicate;
    std::vector<Expr> values, args;
    Schedule schedule;
    std::vector<Specialization> specializations;

    DefinitionContents() : is_init(true), predicate(const_true()) {}

    void accept(IRVisitor *visitor) const {
        if (predicate.defined()) {
            predicate.accept(visitor);
        }

        for (Expr val : values) {
            val.accept(visitor);
        }
        for (Expr arg : args) {
            arg.accept(visitor);
        }

        schedule.accept(visitor);

        for (const Specialization &s : specializations) {
            if (s.condition.defined()) {
                s.condition.accept(visitor);
            }
            s.definition.accept(visitor);
        }
    }

    void mutate(IRMutator *mutator) {
        if (predicate.defined()) {
            predicate = mutator->mutate(predicate);
        }

        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = mutator->mutate(values[i]);
        }
        for (size_t i = 0; i < args.size(); ++i) {
            args[i] = mutator->mutate(args[i]);
        }

        schedule.mutate(mutator);

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
}

Definition::Definition(const std::vector<Expr> &args, const std::vector<Expr> &values,
                       const ReductionDomain &rdom, bool is_init)
                       : contents(new DefinitionContents) {
    contents->is_init = is_init;
    contents->values = values;
    contents->args = args;
    if (rdom.defined()) {
        contents->predicate = rdom.predicate();
        for (size_t i = 0; i < rdom.domain().size(); i++) {
            contents->schedule.rvars().push_back(rdom.domain()[i]);
        }
    }
}

// Return deep-copy of Definition
Definition Definition::deep_copy(
        std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> &copied_map) const {
    internal_assert(contents.defined()) << "Cannot deep-copy undefined Definition\n";

    Definition copy;
    copy.contents->is_init = contents->is_init;
    copy.contents->predicate = contents->predicate;
    copy.contents->values = contents->values;
    copy.contents->args = contents->args;
    copy.contents->schedule = contents->schedule.deep_copy(copied_map);

    // Deep-copy specializations
    for (const Specialization &s : contents->specializations) {
        Specialization s_copy;
        s_copy.condition = s.condition;
        s_copy.definition = s.definition.deep_copy(copied_map);
        s_copy.failure_message = s.failure_message;
        copy.contents->specializations.push_back(std::move(s_copy));
    }
    return copy;
}

bool Definition::is_init() const {
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

Expr &Definition::predicate() {
    return contents->predicate;
}

const Expr &Definition::predicate() const {
    return contents->predicate;
}

std::vector<Expr> Definition::split_predicate() const {
    std::vector<Expr> predicates;
    split_into_ands(contents->predicate, predicates);
    return predicates;
}

Schedule &Definition::schedule() {
    return contents->schedule;
}

const Schedule &Definition::schedule() const {
    return contents->schedule;
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
    s.definition.contents->predicate = contents->predicate;
    s.definition.contents->values = contents->values;
    s.definition.contents->args   = contents->args;

    // The sub-schedule inherits everything about its parent except for its specializations.
    s.definition.contents->schedule.store_level()      = contents->schedule.store_level();
    s.definition.contents->schedule.compute_level()    = contents->schedule.compute_level();
    s.definition.contents->schedule.rvars()            = contents->schedule.rvars();
    s.definition.contents->schedule.splits()           = contents->schedule.splits();
    s.definition.contents->schedule.dims()             = contents->schedule.dims();
    s.definition.contents->schedule.storage_dims()     = contents->schedule.storage_dims();
    s.definition.contents->schedule.bounds()           = contents->schedule.bounds();
    s.definition.contents->schedule.prefetches()       = contents->schedule.prefetches();
    s.definition.contents->schedule.wrappers()         = contents->schedule.wrappers();
    s.definition.contents->schedule.memoized()         = contents->schedule.memoized();
    s.definition.contents->schedule.touched()          = contents->schedule.touched();
    s.definition.contents->schedule.allow_race_conditions() = contents->schedule.allow_race_conditions();

    contents->specializations.push_back(s);
    return contents->specializations.back();
}

}
}
