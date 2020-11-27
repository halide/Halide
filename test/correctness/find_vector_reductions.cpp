#include "Halide.h"
#include <array>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class CountVectorReductions : public IRVisitor {
    using IRVisitor::visit;

    void visit(const VectorReduce *op) {
        vector_reductions++;
        IRVisitor::visit(op);
    }

public:
    int vector_reductions = 0;
};

int count_vector_reductions(const Expr &e) {
    CountVectorReductions counter;
    e.accept(&counter);
    return counter.vector_reductions;
}

void randomly_permute(std::vector<Expr> &x) {
    for (int i = 0; i < (int)x.size() * 2; i++) {
        int a = rand() % x.size();
        int b = rand() % x.size();
        std::swap(x[a], x[b]);
    }
}

Var x("x");
Var y_stride("y_stride");

std::vector<Expr> make_interleaving_exprs(int factor, int lanes, bool allow_broadcast = false) {
    std::vector<Expr> result;

    if (allow_broadcast && rand() % 2 == 0) {
        result = make_interleaving_exprs(factor, 1, false);
        for (Expr &i : result) {
            i = Broadcast::make(i, lanes);
        }
        return result;
    }

    int y = rand();
    if (rand() % 2 == 0) {
        // Make a series of loads that interleave cleanly.
        for (int j = 0; j < factor; j++) {
            Expr index = y_stride * y + x * lanes * factor + j;
            if (lanes > 1) {
                index = Ramp::make(index, factor, lanes);
            }
            result.push_back(Load::make(Int(32, lanes), "f", index, Buffer<>(), Parameter(), const_true(lanes), ModulusRemainder()));
        }
    } else {
        // Make a series of slices of a vector that interleave cleanly.
        Expr index = Ramp::make(y_stride * y, 1, lanes * factor);
        Expr base_vec = Variable::make(Int(32, lanes * factor), "v" + std::to_string(y));
        for (int j = 0; j < factor; j++) {
            result.push_back(Shuffle::make_slice(base_vec, j, factor, lanes));
        }
    }
    return result;
}

std::vector<Expr> make_interleaving_mul(int factor, int lanes) {
    std::vector<Expr> a = make_interleaving_exprs(factor, lanes);
    std::vector<Expr> b = make_interleaving_exprs(factor, lanes, true);

    std::vector<Expr> result;
    for (int i = 0; i < (int)a.size(); i++) {
        result.push_back(a[i] * b[i]);
    }

    return result;
}

void make_vector_reduction(int factor, int lanes, std::vector<Expr> &operands) {
    std::vector<Expr> interleaving;

    switch (rand() % 2) {
        case 0: interleaving = make_interleaving_exprs(factor, lanes); break;
        case 1: interleaving = make_interleaving_mul(factor, lanes); break;
    }

    operands.insert(operands.end(), interleaving.begin(), interleaving.end());
}

void test_find_vector_reductions(const std::vector<Expr> &terms, int vector_reductions) {
    Expr sum = terms.front();
    for (int i = 1; i < (int)terms.size(); ++i) {
        sum += terms[i];
    }

    Expr reduced = find_vector_reductions(sum);
    if (count_vector_reductions(reduced) != vector_reductions) {
        std::cout << "Failed to find an expected vector reduction!\n";
        std::cout << reduced << "\n";
        exit(-1);
    }
}

int main(int argc, char **argv) {
    std::vector<int> factors = {2, 3, 4, 5, 6};
    int lanes = 1;
    for (int i : factors) {
        lanes *= i;
    }

    // This test generates a bunch of loads and slices that can interleave cleanly and puts them in a large list.
    // By the end of this test, operands.size() = test_reps * product(factors), which is a pretty ludicriously
    // large expression, testing for bad algorithmic complexity in find_vector_reductions.
    const int test_reps = 5;
    std::vector<Expr> operands;
    int count = 0;
    for (int reps = 0; reps < test_reps; reps++) {
        for (int i = 0; i < (int)factors.size(); ++i) {
            ++count;
            make_vector_reduction(factors[i], lanes, operands);
            randomly_permute(operands);
            test_find_vector_reductions(operands, count);
        }
    }

    printf("Success!\n");
    return 0;
}
