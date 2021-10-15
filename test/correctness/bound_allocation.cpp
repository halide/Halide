#include "Halide.h"

using namespace Halide;

class FindAllocations : public Internal::IRMutator {
public:
    std::map<std::string, int> allocation_size;

private:
    using Internal::IRMutator::visit;

    Internal::Stmt visit(const Internal::Allocate *op) override {
        if (op->extents.size() == 1) {
            const auto *size = Internal::as_const_int(op->extents[0]);
            if (size) {
                // Trim of the suffix.
                std::string name = op->name.substr(0, op->name.find("$"));
                allocation_size[name] = *size;
            }
        }
        return Internal::IRMutator::visit(op);
    }
};

int main(int argc, char **argv) {
    // Test for a constant bound.
    {
        Func f("f"), g("g");
        Var x("x"), y("y");
        f(x, y) = x + y;
        g(x, y) = 2 * f(x, y);

        f.compute_at(g, y);
        const int fixed_alloc_size = 16;
        f.bound_allocation(fixed_alloc_size);
        FindAllocations s;
        g.add_custom_lowering_pass(&s, []() {});
        Module m = g.compile_to_module({});
        if (s.allocation_size["f"] != fixed_alloc_size) {
            std::cerr << "Allocation size for f doesn't match one which was set explicitly \n";
            return -1;
        }

        // Also check that output is correct.
        Buffer<int> im = g.realize({10, 10});
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = 2 * (x + y);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }
    // Test for multiple bounds.
    {
        Func f("f"), h("h"), g("g");
        Var x("x"), y("y");
        f(x, y) = x + y;
        h(x, y) = x - 2 * y;
        g(x, y) = 2 * f(x, y) + 3 * h(x, y);

        f.compute_at(g, y);
        h.compute_root();
        const int fixed_alloc_size_f = 16, fixed_alloc_size_h = 10 * 10;
        f.bound_allocation(fixed_alloc_size_f);
        h.bound_allocation(fixed_alloc_size_h);
        FindAllocations s;
        g.add_custom_lowering_pass(&s, []() {});
        Module m = g.compile_to_module({});
        if (s.allocation_size["f"] != fixed_alloc_size_f) {
            std::cerr << "Allocation size for f doesn't match one which was set explicitly \n";
            return -1;
        }

        if (s.allocation_size["h"] != fixed_alloc_size_h) {
            std::cerr << "Allocation size for h doesn't match one which was set explicitly \n";
            return -1;
        }

        // Also check that output is correct.
        Buffer<int> im = g.realize({10, 10});
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = 2 * (x + y) + 3 * (x - 2 * y);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }
    // Test for expression bound.
    {
        ImageParam input(Int(32), 2);
        Func f("f"), g("g");
        Var x("x"), y("y");
        f(x, y) = input(x, y) + x + y;
        g(x, y) = 2 * f(x, y);

        f.compute_at(g, y);
        f.bound_allocation(input.width());

        Buffer<int> input_buffer(10, 10);
        input_buffer.fill(10);
        input.set(input_buffer);

        // Also check that output is correct.
        Buffer<int> im = g.realize({10, 10});
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = 2 * (x + y + 10);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}