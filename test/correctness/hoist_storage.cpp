#include "Halide.h"

using namespace Halide;

int malloc_count = 0;
int malloc_total_size = 0;

void *custom_malloc(JITUserContext *user_context, size_t x) {
    malloc_count++;
    malloc_total_size += x;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void custom_free(JITUserContext *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    // Constant bound for allocation extents.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.compute_root();
        f.compute_at(g, x)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 3 * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Same as above, but uses hoist_storage_root.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.compute_root();
        f.compute_at(g, x)
            .hoist_storage_root()
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 3 * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Constant bound for allocation extents.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

        f.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Allocation extents depend on the loop variables, so needs bounds analysis to lift the allocation out.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        f.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Allocation extents depend on the loop variables, so needs bounds analysis to lift the allocation out.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        f.compute_at(g, xo)
            .hoist_storage(g, yo)
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 8;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = expected_malloc_count * 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two functions are hoisted at the same level.
    {
        Func f("f"), h("h"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        h(x, y) = 2 * x + 3 * y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        f.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);
        h.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 2;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 16 * 16 * sizeof(int32_t) + 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 5 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two functions are hoisted, but at different loop levels.
    {
        Func f("f"), h("h"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        h(x, y) = 2 * x + 3 * y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        f.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);
        h.compute_at(g, xo)
            .hoist_storage(g, yo)
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1 + 8;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 8 * 16 * 16 * sizeof(int32_t) + 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 5 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // There are two functions, but only one is hoisted.
    {
        Func f("f"), h("h"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        h(x, y) = 2 * x + 3 * y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        f.compute_at(g, xo)
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);
        h.compute_at(g, xo)
            .hoist_storage(g, yo)
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 64 + 8;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 8 * 16 * 16 * sizeof(int32_t) + 64 * 18 * 18 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 5 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Test with specialize.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.compute_root();
        g.specialize(g.output_buffer().width() > 64).vectorize(x, 4);
        f.compute_at(g, x)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out(128, 128);
        g.realize(out);

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = (4 + 3 - 1) * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Also, check that we can lift after sliding window.
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.compute_root();
        f.compute_at(g, x)
            .store_at(g, y)
            .hoist_storage(g, Var::outermost())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out = g.realize({128, 128});

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 4 * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Hoisted Tuple storage
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = {x + y, x + y};
        g(x, y) = f(x - 1, y - 1)[0] + f(x + 1, y + 1)[1];

        g.compute_root();
        f.compute_at(g, x)
            .hoist_storage(LoopLevel::root())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out(128, 128);
        g.realize(out);

        const int expected_malloc_count = 2;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = 2 * 3 * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.compute_root();
        g.specialize(g.output_buffer().width() > 64).vectorize(x, 4);
        f.compute_at(g, x)
            .hoist_storage(LoopLevel::root())
            // Store in heap to make sure that custom malloc is called.
            .store_in(MemoryType::Heap);

        g.jit_handlers().custom_malloc = custom_malloc;
        g.jit_handlers().custom_free = custom_free;

        malloc_count = 0;
        malloc_total_size = 0;

        Buffer<int> out(128, 128);
        g.realize(out);

        const int expected_malloc_count = 1;
        if (malloc_count != expected_malloc_count) {
            std::cerr << "Wrong number of mallocs. "
                      << "Expected " << expected_malloc_count << " got " << malloc_count << "\n";
            exit(1);
        }

        const int expected_malloc_total_size = (4 + 3 - 1) * 3 * sizeof(int32_t);
        if (malloc_total_size != expected_malloc_total_size) {
            std::cerr << "Wrong allocation size "
                      << "Expected " << expected_malloc_total_size << " got " << malloc_total_size << "\n";
            exit(1);
        }

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    printf("Success!\n");
    return 0;
}
