#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(int x, int y) {
    count++;
    return 0;
}
HalideExtern_2(int, call_counter, int, int);

// Checks that every access to an allocation in registers has a constant
// index, and records the size of each such allocation.
class CheckConstantRegisterIndices : public IRMutator {
    using IRMutator::visit;

    std::set<std::string> registers;

    void check(const std::string &name, const Expr &index) {
        if (!registers.count(name)) {
            return;
        }
        bool uses_var = false;
        visit_with(index, [&](auto *self, const Variable *op) { uses_var = true; });
        if (uses_var) {
            std::cout << "Non-constant index into " << name << ": " << index << "\n";
            failed = true;
        }
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type == MemoryType::Register) {
            registers.insert(op->name);
            int64_t total = 1;
            for (const auto &e : op->extents) {
                total *= as_const_int(e).value_or(0);
            }
            sizes[op->name] = (int)total;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        check(op->name, op->index);
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        check(op->name, op->index);
        return IRMutator::visit(op);
    }

public:
    bool failed = false;
    std::map<std::string, int> sizes;
};

int check_registers(Func out, const std::string &producer, int expected_size, Buffer<int> &result,
                    const std::vector<int> &sizes) {
    CheckConstantRegisterIndices checker;
    out.add_custom_lowering_pass(&checker, nullptr);
    result = out.realize(sizes);
    if (checker.failed) {
        return 1;
    }
    auto it = checker.sizes.find(producer);
    if (it == checker.sizes.end()) {
        printf("%s was not stored in registers\n", producer.c_str());
        return 1;
    }
    if (it->second != expected_size) {
        printf("%s was stored in %d registers instead of %d\n", producer.c_str(), it->second, expected_size);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Var x("x"), y("y"), yo("yo"), yi("yi");

    for (bool up : {true, false}) {
        // A window of three values slides in either direction and is held in
        // exactly three registers.
        count = 0;
        Func f("f"), g("g");
        f(x) = call_counter(x, 0) + x;
        Expr i = up ? x : 100 - x;
        g(x) = f(i - 1) + f(i) + f(i + 1);
        f.store_root().compute_at(g, x).store_in(MemoryType::Register);
        Buffer<int> out;
        if (check_registers(g, f.name(), 3, out, {20})) {
            return 1;
        }
        for (int j = 0; j < 20; j++) {
            int correct = 3 * (up ? j : 100 - j);
            if (out(j) != correct) {
                printf("Sliding %s: out(%d) = %d instead of %d\n", up ? "up" : "down", j, out(j), correct);
                return 1;
            }
        }
        if (count != 22) {
            printf("Sliding %s: f was called %d times instead of %d times\n", up ? "up" : "down", count, 22);
            return 1;
        }
    }

    {
        // Sliding over x inside a loop over y folds the stored window over
        // x, which must be restored to its unrotated state at the end of
        // each x loop for the next iteration of y to use it.
        Func f("f"), g("g");
        f(x, y) = x * 3 + y;
        g(x, y) = f(x, y) + f(x - 1, y) + f(x, y - 1);
        g.split(y, yo, yi, 4);
        f.store_at(g, yo).compute_at(g, x).store_in(MemoryType::Register);
        g.unroll(yi);
        Buffer<int> out;
        if (check_registers(g, f.name(), 4, out, {12, 12})) {
            return 1;
        }
        for (int j = 0; j < 12; j++) {
            for (int i = 0; i < 12; i++) {
                int correct = (i * 3 + j) * 3 - 4;
                if (out(i, j) != correct) {
                    printf("Restoring: out(%d, %d) = %d instead of %d\n", i, j, out(i, j), correct);
                    return 1;
                }
            }
        }
    }

    for (bool unroll : {false, true}) {
        // Sliding over an outer loop while computing at an inner loop of
        // constant extent.
        count = 0;
        Func f("f"), g("g");
        f(x, y) = call_counter(x, y) + x + y;
        g(x, y) = f(x, y) + f(x, y + 1) + f(x, y + 2);
        f.store_at(g, x).compute_at(g, y).store_in(MemoryType::Register);
        g.reorder(y, x).bound(y, 0, 16);
        if (unroll) {
            g.unroll(y);
        }
        Buffer<int> out;
        if (check_registers(g, f.name(), 3, out, {12, 16})) {
            return 1;
        }
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 12; i++) {
                int correct = 3 * (i + j) + 3;
                if (out(i, j) != correct) {
                    printf("Outer loop: out(%d, %d) = %d instead of %d\n", i, j, out(i, j), correct);
                    return 1;
                }
            }
        }
        if (count != 12 * 18) {
            printf("Outer loop: f was called %d times instead of %d times\n", count, 12 * 18);
            return 1;
        }
    }

    {
        // A chain of two windows. The loop is rewound far enough to warm up
        // both, so the second one's producer only runs on the later of those
        // iterations.
        count = 0;
        Func f("f"), g("g"), h("h");
        f(x) = call_counter(x, 0) + x;
        g(x) = f(x) + f(x - 1);
        h(x) = g(x) + g(x - 1);
        f.store_root().compute_at(h, x).store_in(MemoryType::Register);
        g.store_root().compute_at(h, x).store_in(MemoryType::Register);
        Buffer<int> out;
        if (check_registers(h, f.name(), 2, out, {20})) {
            return 1;
        }
        for (int j = 0; j < 20; j++) {
            int correct = 4 * j - 4;
            if (out(j) != correct) {
                printf("Chain: out(%d) = %d instead of %d\n", j, out(j), correct);
                return 1;
            }
        }
        if (count != 22) {
            printf("Chain: f was called %d times instead of %d times\n", count, 22);
            return 1;
        }
    }

    {
        // A vectorized producer and consumer. The window is a whole number
        // of vectors, so the producer computes one vector per iteration.
        count = 0;
        Func f("f"), g("g");
        f(x) = call_counter(x, 0) + x;
        g(x) = f(x + 3) + f(x - 1);
        f.store_root().compute_at(g, x).store_in(MemoryType::Register).vectorize(x, 4);
        g.vectorize(x, 4, TailStrategy::RoundUp);
        Buffer<int> out;
        if (check_registers(g, f.name(), 8, out, {100})) {
            return 1;
        }
        for (int j = 0; j < 100; j++) {
            if (out(j) != 2 * j + 2) {
                printf("Vectorized: out(%d) = %d instead of %d\n", j, out(j), 2 * j + 2);
                return 1;
            }
        }
    }

    {
        // The consumer contains a deep chain of lets, each of which uses the
        // previous one twice. Planning the rotation must not inline them.
        const int depth = 40;
        Func f("f"), g("g");
        f(x) = x;
        Expr e = x;
        for (int k = 0; k < depth; k++) {
            e = (e + e / 3) % 1000;
        }
        g(x) = f(x - 1) + f(x + 1) + e;
        f.store_root().compute_at(g, x).store_in(MemoryType::Register);
        Buffer<int> out;
        if (check_registers(g, f.name(), 3, out, {20})) {
            return 1;
        }
        for (int j = 0; j < 20; j++) {
            int correct = j;
            for (int k = 0; k < depth; k++) {
                correct = (correct + correct / 3) % 1000;
            }
            correct += 2 * j;
            if (out(j) != correct) {
                printf("Let chain: out(%d) = %d instead of %d\n", j, out(j), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
