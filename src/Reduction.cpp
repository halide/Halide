#include "Reduction.h"

#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Var.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

void check(const Expr &pred, std::vector<Expr> &expected) {
    std::vector<Expr> result;
    split_into_ands(pred, result);
    bool is_equal = true;

    if (result.size() != expected.size()) {
        is_equal = false;
    } else {
        for (size_t i = 0; i < expected.size(); ++i) {
            if (!equal(simplify(result[i]), simplify(expected[i]))) {
                is_equal = false;
                break;
            }
        }
    }

    if (!is_equal) {
        std::cout << "Expect predicate " << pred << " to be split into:\n";
        for (const auto &e : expected) {
            std::cout << "  " << e << "\n";
        }
        std::cout << "Got:\n";
        for (const auto &e : result) {
            std::cout << "  " << e << "\n";
        }
        internal_error << "\n";
    }
}

}  // namespace

void split_predicate_test() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");

    {
        std::vector<Expr> expected;
        expected.push_back(z < 10);
        check(z < 10, expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((x < y) || (x == 10));
        check((x < y) || (x == 10), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back(x == 10);
        check((x < y) && (x == 10), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back(x == 10);
        expected.push_back(y == z);
        check((x < y) && (x == 10) && (y == z), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        check((w == 1) || ((x == 10) && (y == z)), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        check((x < y) && ((w == 1) || ((x == 10) && (y == z))), expected);
    }

    std::cout << "Split predicate test passed\n";
}

struct ReductionDomainContents {
    mutable RefCount ref_count;
    std::vector<ReductionVariable> domain;
    Expr predicate;
    bool frozen = false;

    ReductionDomainContents()
        : predicate(const_true()) {
    }

    // Pass an IRVisitor through to all Exprs referenced in the ReductionDomainContents
    void accept(IRVisitor *visitor) {
        for (const ReductionVariable &rvar : domain) {
            if (rvar.min.defined()) {
                rvar.min.accept(visitor);
            }
            if (rvar.extent.defined()) {
                rvar.extent.accept(visitor);
            }
        }
        if (predicate.defined()) {
            predicate.accept(visitor);
        }
    }

    // Pass an IRMutator through to all Exprs referenced in the ReductionDomainContents
    void mutate(IRMutator *mutator) {
        for (ReductionVariable &rvar : domain) {
            if (rvar.min.defined()) {
                rvar.min = mutator->mutate(rvar.min);
            }
            if (rvar.extent.defined()) {
                rvar.extent = mutator->mutate(rvar.extent);
            }
        }
        if (predicate.defined()) {
            predicate = mutator->mutate(predicate);
        }
    }
};

template<>
RefCount &ref_count<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {
    delete p;
}

ReductionDomain::ReductionDomain(const std::vector<ReductionVariable> &domain)
    : contents(new ReductionDomainContents) {
    contents->domain = domain;
}

ReductionDomain::ReductionDomain(const std::vector<ReductionVariable> &domain, const Expr &predictate, bool frozen)
    : contents(new ReductionDomainContents) {
    contents->domain = domain;
    contents->predicate = predictate;
    contents->frozen = frozen;
}

ReductionDomain ReductionDomain::deep_copy() const {
    if (!contents.defined()) {
        return ReductionDomain();
    }
    ReductionDomain copy(contents->domain);
    copy.contents->predicate = contents->predicate;
    copy.contents->frozen = contents->frozen;
    return copy;
}

const std::vector<ReductionVariable> &ReductionDomain::domain() const {
    return contents->domain;
}

namespace {
class DropSelfReferences : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        if (op->reduction_domain.defined()) {
            user_assert(op->reduction_domain.same_as(domain))
                << "An RDom's predicate may only refer to its own RVars, "
                << " not the RVars of some other RDom. "
                << "Cannot set the predicate to : " << predicate << "\n";
            return Variable::make(op->type, op->name);
        } else {
            return op;
        }
    }

public:
    Expr predicate;
    const ReductionDomain &domain;
    DropSelfReferences(Expr p, const ReductionDomain &d)
        : predicate(std::move(p)), domain(d) {
    }
};
}  // namespace

void ReductionDomain::set_predicate(const Expr &p) {
    // The predicate can refer back to the RDom. We need to break
    // those cycles to prevent a leak.
    contents->predicate = DropSelfReferences(p, *this).mutate(p);
}

void ReductionDomain::where(Expr predicate) {
    set_predicate(simplify(contents->predicate && std::move(predicate)));
}

Expr ReductionDomain::predicate() const {
    return contents->predicate;
}

std::vector<Expr> ReductionDomain::split_predicate() const {
    std::vector<Expr> predicates;
    split_into_ands(contents->predicate, predicates);
    return predicates;
}

void ReductionDomain::freeze() {
    contents->frozen = true;
}

bool ReductionDomain::frozen() const {
    return contents->frozen;
}

void ReductionDomain::accept(IRVisitor *visitor) const {
    if (contents.defined()) {
        contents->accept(visitor);
    }
}

void ReductionDomain::mutate(IRMutator *mutator) {
    if (contents.defined()) {
        contents->mutate(mutator);
    }
}

}  // namespace Internal
}  // namespace Halide
