#include "Var.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Reduction.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

/* Split AND predicate into vector of ANDs. */
class SplitAndPredicate : public IRVisitor {
public:
    std::vector<Expr> predicates;

    using IRVisitor::visit;

    void visit(const And *op) {
        std::vector<Expr> old;
        old.swap(predicates);

        const And *and_a = op->a.as<And>();
        const And *and_b = op->b.as<And>();

        std::vector<Expr> predicates_a;
        if (and_a) {
            op->a.accept(this);
            predicates_a.swap(predicates);
        } else {
            predicates_a.push_back(op->a);
        }

        if (and_b) {
            op->b.accept(this);
        } else {
            predicates.push_back(op->b);
        }

        predicates.insert(predicates.end(), predicates_a.begin(), predicates_a.end());
        predicates.insert(predicates.end(), old.begin(), old.end());
    }
};

std::vector<Expr> split_predicate_helper(Expr pred) {
    pred = simplify(pred);
    if (equal(const_true(), pred)) {
        return {};
    }
    if (!pred.as<And>()) {
        return {pred};
    }
    SplitAndPredicate split;
    pred.accept(&split);
    return split.predicates;
}


void check(Expr pred, std::vector<Expr> &expected) {
    std::vector<Expr> result = split_predicate_helper(pred);
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

}

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
        expected.push_back(x == 10);
        expected.push_back(x < y);
        check((x < y) && (x == 10), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(y == z);
        expected.push_back(x == 10);
        expected.push_back(x < y);
        check((x < y) && (x == 10) && (y == z), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        check((w == 1) || ((x == 10) && (y == z)), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        expected.push_back(x < y);
        check((x < y) && ((w == 1) || ((x == 10) && (y == z))), expected);
    }

    std::cout << "Split predicate test passed" << std::endl;
}

struct ReductionDomainContents {
    mutable RefCount ref_count;
    std::vector<ReductionVariable> domain;
    Expr predicate;
    bool frozen;

    ReductionDomainContents() : predicate(const_true()), frozen(false) {}
};

template<>
EXPORT RefCount &ref_count<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {delete p;}

ReductionDomain::ReductionDomain(const std::vector<ReductionVariable> &domain) :
    contents(new ReductionDomainContents) {
    contents.ptr->domain = domain;
}

const std::vector<ReductionVariable> &ReductionDomain::domain() const {
    return contents.ptr->domain;
}

void ReductionDomain::where(Expr predicate) {
    contents.ptr->predicate = simplify(contents.ptr->predicate && predicate);
}

Expr ReductionDomain::predicate() const {
    return contents.ptr->predicate;
}

std::vector<Expr> ReductionDomain::split_predicate() const {
    std::vector<Expr> predicates = split_predicate_helper(contents.ptr->predicate);
    return predicates;
}

void ReductionDomain::freeze() {
    contents.ptr->frozen = true;
}

bool ReductionDomain::frozen() const {
    return contents.ptr->frozen;
}

}
}
