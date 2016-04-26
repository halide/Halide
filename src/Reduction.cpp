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
void split_predicate_helper(Expr pred, std::vector<Expr> &result) {
    if (const And *a = pred.as<And>()) {
        split_predicate_helper(a->a, result);
        split_predicate_helper(a->b, result);
    } else if (!is_one(pred)) {
        result.push_back(pred);
    }
}

void check(Expr pred, std::vector<Expr> &expected) {
    std::vector<Expr> result;
    split_predicate_helper(pred, result);
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
    std::vector<Expr> predicates;
    split_predicate_helper(contents.ptr->predicate, predicates);
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
