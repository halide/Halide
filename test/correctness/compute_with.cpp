#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cstdio>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

struct Bound {
    int32_t min[3];
    int32_t max[3];

    Bound(int32_t min_0, int32_t max_0, int32_t min_1, int32_t max_1,
          int32_t min_2, int32_t max_2) {
        min[0] = min_0;
        max[0] = max_0;
        min[1] = min_1;
        max[1] = max_1;
        min[2] = min_2;
        max[2] = max_2;
    }
    Bound(int32_t min_0, int32_t max_0, int32_t min_1, int32_t max_1)
        : Bound(min_0, max_0, min_1, max_1, 0, 0) {
    }
    Bound(int32_t min_0, int32_t max_0)
        : Bound(min_0, max_0, 0, 0, 0, 0) {
    }
    Bound()
        : Bound(-1, -1, -1, -1, -1, -1) {
    }
};

map<string, Bound> stores, loads;
uint64_t loads_total = 0, stores_total = 0;

// These mutexes (mutices?) are only needed for accessing stores/loads
// from the my_trace callback (which can be called by multiple threads);
// the ordinary code that initializes stores/loads is single-threaded
// and has no contention.
std::mutex stores_mutex, loads_mutex;

// Return true if the coordinate values in 'coordinates' are within the bound 'b'
bool check_coordinates(const Bound &b, const int32_t *coordinates, int32_t dims, int32_t lanes,
                       string event, string fname) {
    for (int32_t idx = 0; idx < dims; ++idx) {
        int32_t i = idx / lanes;
        if ((coordinates[idx] < b.min[i]) || (coordinates[idx] > b.max[i])) {
            printf("Bounds on %s to %s at dimension %d were supposed to be between [%d, %d]\n"
                   "Instead it is: %d\n",
                   event.c_str(), fname.c_str(), i, b.min[i], b.max[i],
                   coordinates[idx]);
            return false;
        }
    }
    return true;
}

// A trace that check the region accessed by stores/loads of a buffer
int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    string fname = std::string(e->func);
    if (e->event == halide_trace_store) {
        std::lock_guard<std::mutex> lock(stores_mutex);
        const auto &iter = stores.find(fname);
        if (iter != stores.end()) {
            const Bound &b = iter->second;
            if (!check_coordinates(b, e->coordinates, e->dimensions, e->type.lanes, "store", fname)) {
                exit(1);
            }
        }
        stores_total++;
    } else if (e->event == halide_trace_load) {
        std::lock_guard<std::mutex> lock(loads_mutex);
        const auto &iter = loads.find(fname);
        if (iter != loads.end()) {
            const Bound &b = iter->second;
            if (!check_coordinates(b, e->coordinates, e->dimensions, e->type.lanes, "load", fname)) {
                exit(1);
            }
        }
        loads_total++;
    }
    return 0;
}

int split_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize({200, 200});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 7);
        g.split(x, xo, xi, 7);
        g.compute_with(f, xo, LoopAlignStrategy::AlignEnd);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(-1, 198, 1, 200)},
            {g.name(), Bound(2, 201, -2, 197)},
            {h.name(), Bound(0, 199, 0, 199)},
        };
        loads = {
            {f.name(), Bound(-1, 198, 1, 200)},
            {g.name(), Bound(2, 201, -2, 197)},
            {h.name(), Bound()},  // There shouldn't be any load from h
        };
        h.jit_handlers().custom_trace = &my_trace;

        im = h.realize({200, 200});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int fuse_test() {
    const int size = 20;
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);
        im_ref = h.realize({size, size, size});
    }

    {
        Var x("x"), y("y"), z("z"), t("t");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);

        f.compute_root();
        g.compute_root();

        f.fuse(x, y, t).parallel(t);
        g.fuse(x, y, t).parallel(t);
        g.compute_with(f, t, LoopAlignStrategy::AlignEnd);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(2, size + 1, -1, size - 2, 3, size + 2)},
            {g.name(), Bound(-5, size - 6, -6, size - 7, 2, size + 1)},
            {h.name(), Bound(0, size - 1, 0, size - 1, 0, size - 1)},
        };
        loads = {
            {f.name(), Bound(2, size + 1, -1, size - 2, 3, size + 2)},
            {g.name(), Bound(-5, size - 6, -6, size - 7, 2, size + 1)},
            {h.name(), Bound()},  // There shouldn't be any load from h
        };
        h.jit_handlers().custom_trace = &my_trace;

        im = h.realize({size, size, size});
    }

    auto func = [im_ref](int x, int y, int z) {
        return im_ref(x, y, z);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int multiple_fuse_group_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);
        im_ref = q.realize({200, 200});
    }

    {
        Var x("x"), y("y"), t("t");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();
        p.compute_root();

        p.fuse(x, y, t).parallel(t);
        h.fuse(x, y, t).parallel(t);
        h.compute_with(p, t);
        h.update(0).unscheduled();
        h.update(1).unscheduled();
        h.update(2).unscheduled();

        f.update(0).compute_with(g, y, LoopAlignStrategy::AlignEnd);
        f.compute_with(g, x);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        p.trace_loads().trace_stores();
        q.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(0, 199, 0, 199)},
            {g.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound(0, 199, 0, 199)},
            {p.name(), Bound(0, 199, 0, 199)},
            {q.name(), Bound(0, 199, 0, 199)},
        };
        loads = {
            {f.name(), Bound(0, 199, 0, 199)},
            {g.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound(0, 199, 0, 199)},
            {p.name(), Bound(0, 199, 0, 199)},
            {q.name(), Bound()},  // There shouldn't be any load from q
        };
        q.jit_handlers().custom_trace = &my_trace;

        im = q.realize({200, 200});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int multiple_outputs_test() {
    const int f_size = 4;
    const int g_size = 6;
    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);

        input.compute_at(f, y);
        g.compute_with(f, y, LoopAlignStrategy::AlignStart);

        input.trace_loads().trace_stores();
        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        stores = {
            {input.name(), Bound(0, std::max(f_size, g_size) - 1, 0, std::max(f_size, g_size) - 1)},
            {f.name(), Bound(0, f_size - 1, 0, f_size - 1)},
            {g.name(), Bound(0, g_size - 1, 0, g_size - 1)},
        };
        loads = {
            {input.name(), Bound(0, std::max(f_size, g_size) - 1, 0, std::max(f_size, g_size) - 1)},
            {f.name(), Bound()},  // There shouldn't be any load from f
            {g.name(), Bound()},  // There shouldn't be any load from g
        };

        Pipeline p({f, g});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int fuse_compute_at_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q"), r("r");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        p(x, y) = h(x, y) + 2;
        q(x, y) = x * y;
        r(x, y) = p(x, y - 1) + q(x - 1, y);
        im_ref = r.realize({167, 167});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q"), r("r");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        p(x, y) = h(x, y) + 2;
        q(x, y) = x * y;
        r(x, y) = p(x, y - 1) + q(x - 1, y);

        f.compute_at(h, y);
        g.compute_at(h, y);
        h.compute_at(p, y);
        p.compute_root();
        q.compute_root();
        q.compute_with(p, x, LoopAlignStrategy::AlignEnd);

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 8);
        g.split(x, xo, xi, 8);
        g.compute_with(f, xo, LoopAlignStrategy::AlignStart);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        p.trace_loads().trace_stores();
        q.trace_loads().trace_stores();
        r.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(-1, 165, 0, 166)},
            {g.name(), Bound(2, 168, -3, 163)},
            {h.name(), Bound(0, 166, -1, 165)},
            {p.name(), Bound(0, 166, -1, 165)},
            {q.name(), Bound(-1, 165, 0, 166)},
            {r.name(), Bound(0, 166, 0, 166)},
        };
        loads = {
            {f.name(), Bound(-1, 165, 0, 166)},
            {g.name(), Bound(2, 168, -3, 163)},
            {h.name(), Bound(0, 166, -1, 165)},
            {p.name(), Bound(0, 166, -1, 165)},
            {q.name(), Bound(-1, 165, 0, 166)},
            {r.name(), Bound()},  // There shouldn't be any load from r
        };
        r.jit_handlers().custom_trace = &my_trace;

        im = r.realize({167, 167});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int double_split_fuse_test() {
    Buffer<int> im_ref, im;
    {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");

        f(x, y) = x + y;
        g(x, y) = 2 + x - y;
        h(x, y) = f(x, y) + g(x, y) + 10;
        im_ref = h.realize({200, 200});
    }

    {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi"), t("t");

        f(x, y) = x + y;
        g(x, y) = 2 + x - y;
        h(x, y) = f(x, y) + g(x, y) + 10;

        f.split(x, xo, xi, 37, TailStrategy::GuardWithIf);
        g.split(x, xo, xi, 37, TailStrategy::GuardWithIf);
        f.split(xo, xoo, xoi, 5, TailStrategy::GuardWithIf);
        g.split(xo, xoo, xoi, 5, TailStrategy::GuardWithIf);
        f.fuse(xoi, xi, t);
        g.fuse(xoi, xi, t);
        f.compute_at(h, y);
        g.compute_at(h, y);
        g.compute_with(f, t, LoopAlignStrategy::AlignEnd);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(0, 199, 0, 199)},
            {g.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound(0, 199, 0, 199)},
        };
        loads = {
            {f.name(), Bound(0, 199, 0, 199)},
            {g.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound()},  // There shouldn't be any load from h
        };
        h.jit_handlers().custom_trace = &my_trace;

        im = h.realize({200, 200});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int rgb_yuv420_test() {
    // Somewhat approximating the behavior of rgb -> yuv420 (downsample by half in the u and v channels)
    const int size = 64;
    Buffer<int> y_im(size, size), u_im(size / 2, size / 2), v_im(size / 2, size / 2);
    Buffer<int> y_im_ref(size, size), u_im_ref(size / 2, size / 2), v_im_ref(size / 2, size / 2);

    // Compute a random image
    Buffer<int> input(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                input(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    uint64_t load_count_ref, store_count_ref;
    {
        Var x("x"), y("y"), z("z");
        Func y_part("y_part"), u_part("u_part"), v_part("v_part"), rgb("rgb"), rgb_x("rgb_x");

        Func clamped = BoundaryConditions::repeat_edge(input);
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2 * clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2 * rgb_x(x, y, z) + rgb_x(x, y + 1, z)) / 16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) + 25 * input(x, y, 2) + 128) >> 8) + 16;
        u_part(x, y) = ((-38 * rgb(2 * x, 2 * y, 0) - 74 * rgb(2 * x, 2 * y, 1) + 112 * rgb(2 * x, 2 * y, 2) + 128) >> 8) + 128;
        v_part(x, y) = ((112 * rgb(2 * x, 2 * y, 0) - 94 * rgb(2 * x, 2 * y, 1) - 18 * rgb(2 * x, 2 * y, 2) + 128) >> 8) + 128;

        y_part.vectorize(x, 8);
        u_part.vectorize(x, 8);
        v_part.vectorize(x, 8);

        loads_total = 0;
        stores_total = 0;
        Pipeline p({y_part, u_part, v_part});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({y_im_ref, u_im_ref, v_im_ref}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));
        load_count_ref = loads_total;
        store_count_ref = stores_total;
    }

    {
        Var x("x"), y("y"), z("z");
        Func y_part("y_part"), u_part("u_part"), v_part("v_part"), rgb("rgb"), rgb_x("rgb_x");

        Func clamped = BoundaryConditions::repeat_edge(input);
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2 * clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2 * rgb_x(x, y, z) + rgb_x(x, y + 1, z)) / 16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) + 25 * input(x, y, 2) + 128) >> 8) + 16;
        u_part(x, y) = ((-38 * rgb(2 * x, 2 * y, 0) - 74 * rgb(2 * x, 2 * y, 1) + 112 * rgb(2 * x, 2 * y, 2) + 128) >> 8) + 128;
        v_part(x, y) = ((112 * rgb(2 * x, 2 * y, 0) - 94 * rgb(2 * x, 2 * y, 1) - 18 * rgb(2 * x, 2 * y, 2) + 128) >> 8) + 128;

        Var xi("xi"), yi("yi");
        y_part.tile(x, y, xi, yi, 16, 2, TailStrategy::RoundUp);
        u_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);
        v_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);

        y_part.unroll(yi);
        y_part.vectorize(xi, 8);
        u_part.vectorize(xi);
        v_part.vectorize(xi);

        u_part.compute_with(y_part, x, LoopAlignStrategy::AlignEnd);
        v_part.compute_with(u_part, x, LoopAlignStrategy::AlignEnd);

        Expr width = v_part.output_buffer().width();
        Expr height = v_part.output_buffer().height();
        width = (width / 8) * 8;

        u_part.bound(x, 0, width).bound(y, 0, height);
        v_part.bound(x, 0, width).bound(y, 0, height);
        y_part.bound(x, 0, 2 * width).bound(y, 0, 2 * height);
        rgb.bound(z, 0, 3);

        rgb_x.fold_storage(y, 4);
        rgb_x.store_root();
        rgb_x.compute_at(y_part, y).vectorize(x, 8);
        rgb.compute_at(y_part, y).vectorize(x, 8);

        stores = {
            {rgb_x.name(), Bound(0, size - 1, -1, size - 1, 0, 2)},
            {rgb.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {y_part.name(), Bound(0, size - 1, 0, size - 1)},
            {u_part.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
            {v_part.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
        };
        loads = {
            {rgb_x.name(), Bound(0, size - 1, -1, size - 1, 0, 2)},
            {rgb.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {y_part.name(), Bound()},  // There shouldn't be any load from y_part
            {u_part.name(), Bound()},  // There shouldn't be any load from u_part
            {v_part.name(), Bound()},  // There shouldn't be any load from v_part
        };

        loads_total = 0;
        stores_total = 0;
        Pipeline p({y_part, u_part, v_part});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({y_im, u_im, v_im}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));

        bool too_many_memops = false;
        // Store count for reference:
        // y_part: width * height
        // u_part: (width / 2) * (height / 2)
        // v_part: (width / 2) * (height / 2)
        // Total: width * height * 1.5
        // Store count for compute_with:
        // rgb: width * (height / 2) * 3 [we only need every other line of rgb for u, v]
        // rgb_x: width * height * 3
        // y_part: width * height
        // u_part: (width / 2) * (height / 2)
        // v_part: (width / 2) * (height / 2)
        // Total: width * height * 6
        // Note: each of the items above also needs to be divided by vector_width, but it doesn't change
        // the ratio between reference and compute_with.
        // It should be 4x based on above, but let's make it 5x to account for boundary condtions for rgb_x.
        if (stores_total > 5 * store_count_ref) {
            printf("Store count for correctness_compute_with rgb to yuv420 case exceeds reference by more than 5x. (Reference: %llu, compute_with: %llu).\n",
                   (unsigned long long)store_count_ref, (unsigned long long)stores_total);
            too_many_memops = true;
        }
        // Reference should have more loads, because everything is recomputed.
        // TODO: Bizarrely, https://github.com/halide/Halide/pull/5479 caused the
        // reference loads to decrease by around 2x, which causes the compute_with
        // result to have more loads than the reference. I think this is because a
        // lot of shifts have side-effecty trace calls in them, which are not dead
        // code eliminated as they "should" be. So, this test was erroneously
        // passing before that PR.
        if (loads_total >= 2 * load_count_ref) {
            printf("Load count for correctness_compute_with rgb to yuv420 case exceeds reference. (Reference: %llu, compute_with: %llu).\n",
                   (unsigned long long)load_count_ref, (unsigned long long)loads_total);
            too_many_memops = true;
        }
        if (too_many_memops) {
            return 1;
        }
    }

    auto y_func = [y_im_ref](int x, int y) {
        return y_im_ref(x, y);
    };
    if (check_image(y_im, y_func)) {
        return 1;
    }

    auto u_func = [u_im_ref](int x, int y) {
        return u_im_ref(x, y);
    };
    if (check_image(u_im, u_func)) {
        return 1;
    }

    auto v_func = [v_im_ref](int x, int y) {
        return v_im_ref(x, y);
    };
    if (check_image(v_im, v_func)) {
        return 1;
    }

    return 0;
}

int vectorize_test() {
    const int width = 111;
    const int height = 31;
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize({width, height});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 8);
        g.split(x, xo, xi, 8);
        f.vectorize(xi);
        g.vectorize(xi);
        g.compute_with(f, xi, LoopAlignStrategy::AlignEnd);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(-1, width - 2, 1, height)},
            {g.name(), Bound(2, width + 1, -2, height - 3)},
            {h.name(), Bound(0, width - 1, 0, height - 1)},
        };
        loads = {
            {f.name(), Bound(-1, width - 2, 1, height)},
            {g.name(), Bound(2, width + 1, -2, height - 3)},
            {h.name(), Bound()},  // There shouldn't be any load from h
        };
        h.jit_handlers().custom_trace = &my_trace;

        im = h.realize({width, height});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

/*
int some_are_skipped_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p");

        f(x, y) = x + y;
        g(x, y) = x - y;
        p(x, y) = x * y;
        h(x, y) = f(x, y) + g(x + 2, y - 2);
        h(x, y) += f(x - 1, y + 1) + p(x, y);
        im_ref = h.realize({200, 200});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p");

        f(x, y) = x + y;
        g(x, y) = x - y;
        p(x, y) = x * y;
        h(x, y) = f(x, y) + g(x + 2, y - 2);
        h(x, y) += f(x - 1, y + 1) + p(x, y);

        f.compute_at(h, y);
        g.compute_at(h, y);
        p.compute_at(h, y);

        p.compute_with(f, x);
        g.compute_with(f, x);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        p.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(-1, 199, 0, 200)},
            {g.name(), Bound(0, 201, -2, 197)},
            {p.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound(0, 199, 0, 199)},
        };
        loads = {
            {f.name(), Bound(-1, 199, 0, 200)},
            {g.name(), Bound(0, 201, -2, 197)},
            {p.name(), Bound(0, 199, 0, 199)},
            {h.name(), Bound(0, 199, 0, 199)},
        };
        h.jit_handlers().custom_trace = &my_trace;

        im = h.realize({200, 200});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}
*/

int multiple_outputs_on_gpu_test() {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("No GPU feature enabled in target. Skipping test\n");
        return 0;
    }

    const int f_size = 550;
    const int g_size = 110;
    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);

        input.compute_root();
        Var xi("xi"), yi("yi");
        f.compute_root().gpu_tile(x, y, xi, yi, 8, 8);
        g.compute_root().gpu_tile(x, y, xi, yi, 8, 8);

        g.compute_with(f, x, LoopAlignStrategy::AlignEnd);

        Realization r({f_im, g_im});
        Pipeline({f, g}).realize(r);
        r[0].copy_to_host();
        r[1].copy_to_host();
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int mixed_tile_factor_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size / 2, size / 2), h_im(size / 2, size / 2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size / 2, size / 2), h_im_ref(size / 2, size / 2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("input_ref");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        g.tile(x, y, xi, yi, 7, 9, TailStrategy::GuardWithIf);
        h.tile(x, y, xi, yi, 4, 16, TailStrategy::RoundUp);

        g.compute_with(f, yi, LoopAlignStrategy::AlignEnd);
        h.compute_with(g, yi, LoopAlignStrategy::AlignStart);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        input.trace_loads().trace_stores();
        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound(0, size - 1, 0, size - 1)},
            {g.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
            {h.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()},  // There shouldn't be any load from f
            {g.name(), Bound()},  // There shouldn't be any load from g
            {h.name(), Bound()},  // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    return 0;
}

int multi_tile_mixed_tile_factor_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size / 2, size / 2), h_im(size / 2, size / 2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size / 2, size / 2), h_im_ref(size / 2, size / 2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("A_ref");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        g.tile(x, y, xi, yi, 7, 9, TailStrategy::GuardWithIf);
        h.tile(x, y, xi, yi, 4, 16, TailStrategy::RoundUp);

        Var xii("xii"), yii("yii");
        f.tile(xi, yi, xii, yii, 8, 8, TailStrategy::ShiftInwards);
        g.tile(xi, yi, xii, yii, 16, 8, TailStrategy::GuardWithIf);
        h.tile(xi, yi, xii, yii, 4, 16, TailStrategy::GuardWithIf);

        g.compute_with(f, yii, LoopAlignStrategy::AlignStart);
        h.compute_with(g, yii, LoopAlignStrategy::AlignEnd);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        input.trace_loads().trace_stores();
        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound(0, size - 1, 0, size - 1)},
            {g.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
            {h.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()},  // There shouldn't be any load from f
            {g.name(), Bound()},  // There shouldn't be any load from g
            {h.name(), Bound()},  // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    return 0;
}

int only_some_are_tiled_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size / 2, size / 2), h_im(size / 2, size / 2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size / 2, size / 2), h_im_ref(size / 2, size / 2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("A_ref");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2 * A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2 * x, 2 * y, 1) + 2 * input(2 * x, 2 * y, 2);
        h(x, y) = input(2 * x, 2 * y, 2) + 3 * input(2 * x, 2 * y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);

        g.compute_with(f, y, LoopAlignStrategy::AlignEnd);
        h.compute_with(g, y);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        input.trace_loads().trace_stores();
        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound(0, size - 1, 0, size - 1)},
            {g.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
            {h.name(), Bound(0, size / 2 - 1, 0, size / 2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()},  // There shouldn't be any load from f
            {g.name(), Bound()},  // There shouldn't be any load from g
            {h.name(), Bound()},  // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    return 0;
}

int with_specialization_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize({200, 200});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Param<bool> tile;
        Var xo("xo"), xi("xi");
        f.specialize(tile).split(x, xo, xi, 7);

        g.compute_with(f, y, LoopAlignStrategy::AlignEnd);

        f.trace_loads().trace_stores();
        g.trace_loads().trace_stores();
        h.trace_loads().trace_stores();
        stores = {
            {f.name(), Bound(-1, 198, 1, 200)},
            {g.name(), Bound(2, 201, -2, 197)},
            {h.name(), Bound(0, 199, 0, 199)},
        };
        loads = {
            {f.name(), Bound(-1, 198, 1, 200)},
            {g.name(), Bound(2, 201, -2, 197)},
            {h.name(), Bound()},  // There shouldn't be any load from h
        };
        h.jit_handlers().custom_trace = &my_trace;

        tile.set(true);
        im = h.realize({200, 200});
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int nested_compute_with_test() {
    const int g1_size = 20;
    const int g2_size = 10;
    Buffer<int> g1_im(g1_size, g1_size + 5), g2_im(g2_size, g2_size + 10);
    Buffer<int> g1_im_ref(g1_size, g1_size + 5), g2_im_ref(g2_size, g2_size + 10);

    {
        Var x("x"), y("y");
        Func input("input"), f1("f1"), f2("f2"), g1("g1"), g2("g2");

        input(x, y) = x + y;
        f1(x, y) = input(x, y) + 20;
        f2(x, y) = input(x, y) * input(x, y);
        g1(x, y) = f1(x, y) + x + y;
        g2(x, y) = f1(x, y) * f2(x, y);
        Pipeline({g1, g2}).realize({g1_im_ref, g2_im_ref});
    }

    {
        Var x("x"), y("y");
        Func input("input"), f1("f1"), f2("f2"), g1("g1"), g2("g2");

        input(x, y) = x + y;
        f1(x, y) = input(x, y) + 20;
        f2(x, y) = input(x, y) * input(x, y);
        g1(x, y) = f1(x, y) + x + y;
        g2(x, y) = f1(x, y) * f2(x, y);

        input.compute_at(f1, y);
        f2.compute_with(f1, y, LoopAlignStrategy::AlignEnd);
        f1.compute_at(g1, y);
        f2.compute_at(g1, y);
        g2.compute_with(g1, x, LoopAlignStrategy::AlignStart);

        f1.trace_loads().trace_stores();
        f2.trace_loads().trace_stores();
        g1.trace_loads().trace_stores();
        g2.trace_loads().trace_stores();
        stores = {
            {f1.name(), Bound(0, std::max(g1_size, g2_size) - 1, 0, std::max(g1_size + 4, g2_size + 9))},
            {f2.name(), Bound(0, g2_size - 1, 0, g2_size + 9)},
            {g1.name(), Bound(0, g1_size - 1, 0, g1_size + 4)},
            {g2.name(), Bound(0, g2_size - 1, 0, g2_size + 9)},
        };
        loads = {
            {f1.name(), Bound(0, std::max(g1_size, g2_size) - 1, 0, std::max(g1_size + 4, g2_size + 9))},
            {f2.name(), Bound(0, g2_size - 1, 0, g2_size + 9)},
            {g1.name(), Bound()},  // There shouldn't be any load from g1
            {g2.name(), Bound()},  // There shouldn't be any load from g2
        };

        Pipeline p({g1, g2});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({g1_im, g2_im});
    }

    auto g1_func = [g1_im_ref](int x, int y) {
        return g1_im_ref(x, y);
    };
    if (check_image(g1_im, g1_func)) {
        return 1;
    }

    auto g2_func = [g2_im_ref](int x, int y) {
        return g2_im_ref(x, y);
    };
    if (check_image(g2_im, g2_func)) {
        return 1;
    }
    return 0;
}

int update_stage_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        f.compute_root();
        g.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        g.compute_root();
        f.compute_root();

        f.update(0).unscheduled();
        f.update(1).compute_with(g.update(0), y);

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

// two in row.
int update_stage2_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        f.compute_root();
        g.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        g.compute_root();
        f.compute_root();

        f.update(0).compute_with(g.update(0), y);
        f.update(1).compute_with(g.update(0), y);

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int update_stage3_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        f.compute_root();
        g.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        g.compute_root();
        f.compute_root();

        f.compute_with(g, y);
        f.update(0).compute_with(g, y);
        f.update(1).compute_with(g, y);

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int update_stage_pairwise_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        f.compute_root();
        g.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        g.compute_root();
        f.compute_root();

        f.compute_with(g, y);
        f.update(0).compute_with(g.update(0), y);
        f.update(1).compute_with(g.update(1), y);

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int update_stage_pairwise_zigzag_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);
        g(x, y) = 4 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);
        f(x, y) = 8 + base * f(x, y);

        f.compute_root();
        g.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);
        g(x, y) = 4 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);
        f(x, y) = 8 + base * f(x, y);

        g.compute_root();
        f.compute_root();

        f.compute_with(g, y);
        g.update(0).compute_with(f.update(0), y);
        f.update(1).compute_with(g.update(1), y);
        g.update(2).compute_with(f.update(2), y);

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);

        Pipeline p({f, g});
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int update_stage_diagonal_test() {
    const int f_size = 128;
    const int g_size = 128;
    const int h_size = 128;
    const int base = 31;

    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size), h_im(h_size, h_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size), h_im_ref(h_size, h_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        h(x, y) = 10;
        h(x, y) = 11 + base * h(x, y);
        h(x, y) = 12 + base * h(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);
        h.bound(x, 0, h_size).bound(y, 0, h_size);

        Pipeline p({f, g, h});
        p.realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        g(x, y) = 1;
        g(x, y) = 2 + base * g(x, y);
        g(x, y) = 3 + base * g(x, y);

        f(x, y) = 5;
        f(x, y) = 6 + base * f(x, y);
        f(x, y) = 7 + base * f(x, y);

        h(x, y) = 10;
        h(x, y) = 11 + base * h(x, y);
        h(x, y) = 12 + base * h(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();

        f.update(1).compute_with(g.update(0), y);
        g.update(0).compute_with(h, y);
        f.update(0).unscheduled();
        g.update(1).unscheduled();

        g.bound(x, 0, g_size).bound(y, 0, g_size);
        f.bound(x, 0, f_size).bound(y, 0, f_size);
        h.bound(x, 0, h_size).bound(y, 0, h_size);

        Pipeline p({f, g, h});
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    return 0;
}

int update_stage_rfactor_test() {
    Func f0, f1, cost;
    Var x;
    f0(x) = x;
    f1(x) = x;

    RDom r(0, 100);
    cost() = 0;
    cost() += f0(r.x);
    cost() += f1(r.x);

    f0.compute_root();
    f1.compute_root();

    // Move the reductions into their own Funcs
    Func tmp1 = cost.update(0).rfactor({});
    Func tmp2 = cost.update(1).rfactor({});

    tmp1.compute_root();
    tmp2.compute_root();

    // Now that they're independent funcs, we can fuse the loops using compute_with
    tmp1.update().compute_with(tmp2.update(), r.x);

    Buffer<int> result = cost.realize();

    const int reference = 9900;
    if (result(0) != reference) {
        printf("Wrong result: expected %d, got %d\n", reference, result(0));
        return 1;
    }

    return 0;
}

int vectorize_inlined_test() {
    const int f_size = 128;
    const int g_size = 256;
    Buffer<int> h_im(f_size, f_size, 5), g_im(g_size, g_size);
    Buffer<int> h_im_ref(f_size, f_size, 5), g_im_ref(g_size, g_size);

    uint64_t load_count_ref, store_count_ref;
    {
        Var x("x"), y("y"), c("c"), xi("xi"), yi("yi"), yii("yii"), yo("yo");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y) = x;
        f(x, y, c) = c * input(x, y);
        h(x, y, c) = f(x, y, c);

        Func inl("inl");
        inl(x, y) = f(x / 2, y / 2, 0);
        inl(x, y) += f(x / 2, y / 2, 2);
        g(x, y) = inl(x, y);

        g.split(y, yo, y, 32 * 2, TailStrategy::RoundUp)
            .split(y, y, yi, 2, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_root();

        h.reorder(x, c, y)
            .split(y, yo, y, 32, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_root();

        g.bound(y, 0, g_size);
        h.bound(y, 0, f_size).bound(c, 0, 5);

        loads_total = 0;
        stores_total = 0;
        Pipeline p({h, g});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({h_im_ref, g_im_ref}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));
        load_count_ref = loads_total;
        store_count_ref = stores_total;
    }

    {
        Var x("x"), y("y"), c("c"), xi("xi"), yi("yi"), yii("yii"), yo("yo");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y) = x;
        f(x, y, c) = c * input(x, y);
        h(x, y, c) = f(x, y, c);

        Func inl("inl");
        inl(x, y) = f(x / 2, y / 2, 0);
        inl(x, y) += f(x / 2, y / 2, 2);
        g(x, y) = inl(x, y);

        g.split(y, yo, y, 32 * 2, TailStrategy::RoundUp)
            .split(y, y, yi, 2, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_with(h, y, LoopAlignStrategy::AlignEnd);

        h.reorder(x, c, y)
            .split(y, yo, y, 32, TailStrategy::RoundUp)
            .split(y, y, yi, 1, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_root();

        g.bound(y, 0, g_size);
        h.bound(y, 0, f_size).bound(c, 0, 5);

        loads_total = 0;
        stores_total = 0;
        Pipeline p({h, g});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({h_im, g_im}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));

        bool too_many_memops = false;
        if (stores_total != store_count_ref) {
            printf("Store count should be equal between compute_root and compute_with schedules\n");
            too_many_memops = true;
        }
        if (loads_total != load_count_ref) {
            printf("Load count should be equal between compute_root and compute_with schedules\n");
            too_many_memops = true;
        }

        if (too_many_memops) {
            return 1;
        }
    }

    auto h_func = [h_im_ref](int x, int y, int c) {
        return h_im_ref(x, y, c);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int mismatching_splits_test() {
    const int h_size = 128;
    const int g_size = 256;
    Buffer<int> h_im(h_size, h_size, 5), g_im(g_size, g_size);
    Buffer<int> h_im_ref(h_size, h_size, 5), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y"), c("c"), xi("xi"), yi("yi"), yii("yii"), yo("yo");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y) = x;
        f(x, y, c) = c * input(x, y);
        h(x, y, c) = f(x, y, c);
        g(x, y) = f(x / 2, y / 2, 2);

        g.bound(y, 0, g_size);
        h.bound(y, 0, h_size).bound(c, 0, 5);

        Pipeline p({h, g});

        p.realize({h_im_ref, g_im_ref});
    }

    {
        Var x("x"), y("y"), c("c"), xi("xi"), yi("yi"), yii("yii"), yo("yo");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y) = x;
        f(x, y, c) = c * input(x, y);
        h(x, y, c) = f(x, y, c);
        g(x, y) = f(x / 2, y / 2, 2);

        g
            .split(y, yo, y, 32 * 2, TailStrategy::RoundUp)
            .split(y, y, yi, 2, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_with(h, y, LoopAlignStrategy::AlignStart);

        h
            .reorder(x, c, y)
            .split(y, yo, y, 32, TailStrategy::RoundUp)
            .vectorize(x, 4, TailStrategy::GuardWithIf)
            .compute_root();

        g.bound(y, 0, g_size);
        h.bound(y, 0, h_size).bound(c, 0, 5);

        Pipeline p({h, g});

        p.realize({h_im, g_im});
    }

    auto h_func = [h_im_ref](int x, int y, int z) {
        return h_im_ref(x, y, z);
    };
    if (check_image(h_im, h_func)) {
        return 1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return 1;
    }

    return 0;
}

int different_arg_num_compute_at_test() {
    const int width = 16;
    const int height = 16;
    const int channels = 3;

    Buffer<int> buffer_a_ref(width, height, channels), buffer_b_ref(channels);
    Buffer<int> buffer_a(width, height, channels), buffer_b(channels);
    // Reference.
    {
        Var x("x"), y("y"), c("c");
        Func big("big"), output_a("output_a"), reduce_big("reduce_big"), output_b("output_b");

        big(x, y, c) = Halide::count_leading_zeros(x + y + c);
        RDom r(0, width, 0, height);
        reduce_big(c) = c;
        output_a(x, y, c) = 7 * big(x, y, c) / reduce_big(c);
        output_b(c) = reduce_big(c) * 5;

        Pipeline p({output_a, output_b});
        p.realize({buffer_a_ref, buffer_b_ref});
    }
    // Compute_with.
    {
        Var x("x"), y("y"), c("c");
        Func big("big"), output_a("output_a"), reduce_big("reduce_big"), output_b("output_b");

        big(x, y, c) = Halide::count_leading_zeros(x + y + c);
        RDom r(0, width, 0, height);
        reduce_big(c) = c;
        output_a(x, y, c) = 7 * big(x, y, c) / reduce_big(c);
        output_b(c) = reduce_big(c) * 5;

        output_b.compute_with(output_a, c);
        big.compute_at(output_a, c);
        reduce_big.compute_at(output_a, c);

        output_a.bound(x, 0, width).bound(y, 0, width).bound(c, 0, channels);
        output_b.bound(c, 0, channels);

        loads_total = 0;
        stores_total = 0;
        Pipeline p({output_a, output_b});
        p.jit_handlers().custom_trace = &my_trace;
        p.realize({buffer_a, buffer_b}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));

        bool too_many_memops = false;
        // Store count:
        // big: width * height * channels
        // reduce_big: channels
        // output_a: width * height * channels
        // output_b: channels
        // Total: 2 * width * height * channels + 2 * channels
        // Load count:
        // big: width * height * channels
        // reduce_big: width * height * channels + channels
        // output_a: 0
        // output_b: 0
        // Total: 2 * width * height * channels + channels
        uint64_t expected_store_count = 2 * width * height * channels + 2 * channels;
        uint64_t expected_load_count = 2 * width * height * channels + channels;
        if (stores_total != expected_store_count) {
            printf("Store count for different_arg_num_compute_at_test is not as expected. (Expected: %llu, compute_with: %llu).\n",
                   (unsigned long long)expected_store_count, (unsigned long long)stores_total);
            too_many_memops = true;
        }
        if (loads_total != expected_load_count) {
            printf("Load count for different_arg_num_compute_at_test is not as expected. (Expected: %llu, compute_with: %llu).\n",
                   (unsigned long long)expected_load_count, (unsigned long long)loads_total);
            too_many_memops = true;
        }
        if (too_many_memops) {
            return 1;
        }
    }

    auto buffer_a_func = [buffer_a_ref](int x, int y, int c) {
        return buffer_a_ref(x, y, c);
    };
    if (check_image(buffer_a, buffer_a_func)) {
        return 1;
    }

    for (int i = 0; i < buffer_b.width(); i++) {
        if (buffer_b(i) != buffer_b_ref(i)) {
            printf("Mismatch %d %d %d\n", i, buffer_b(i), buffer_b_ref(i));
            return 1;
        }
    }

    return 0;
}

int store_at_different_levels_test() {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = 3 * x + 2 * y;
    consumer(x, y) = producer1(x, y - 1) + producer1(x, y + 1) + producer2(x, y - 1) + producer2(x, y + 1);
    consumer.compute_root();

    producer1.compute_at(consumer, y);
    producer2.store_root().compute_at(consumer, y).compute_with(producer1, y);

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize({16, 16});

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = 8 * x + 6 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    return 0;
}

int rvar_bounds_test() {
    ImageParam input(Int(16), 2, "input");
    Var x{"x"}, y{"y"};
    Func input_c{"input_c"};
    Func add_1{"add_1"};
    Func mul_2{"mul_2"};
    Func sum_1{"sum_1"};
    Func sum_2{"sum_2"};
    Func total_sum{"total_sum"};
    RDom r(input);

    // algorithm
    input_c(x, y) = input(x, y);

    add_1(x, y) = input_c(x, y) + 1;

    mul_2(x, y) = input_c(x, y) * 2;

    sum_1() = cast<int16_t>(0);
    sum_2() = cast<int16_t>(0);

    sum_1() += add_1(r.x, r.y);
    sum_2() += mul_2(r.x, r.y);

    total_sum() = sum_1() + sum_2();

    input.dim(0).set_bounds(0, 32);
    input.dim(1).set_bounds(0, 64);

    // CPU schedule.
    int h_factor = 8;
    int w_factor = 8;

    RVar rxOuter("rxOuter"), rxInner("rxInner");
    RVar ryOuter("ryOuter"), ryInner("ryInner");

    RVar r_sum_x(sum_1.update(0).get_schedule().dims()[0].var);
    RVar r_sum_y(sum_1.update(0).get_schedule().dims()[1].var);

    sum_1.update(0).tile(r_sum_x, r_sum_y, rxOuter, ryOuter, rxInner, ryInner, w_factor, h_factor);

    RVar r_sum_x_2(sum_2.update(0).get_schedule().dims()[0].var);
    RVar r_sum_y_2(sum_2.update(0).get_schedule().dims()[1].var);

    sum_2.update(0).tile(r_sum_x_2, r_sum_y_2, rxOuter, ryOuter, rxInner, ryInner, w_factor, h_factor);

    add_1.compute_at(sum_2, rxOuter);
    mul_2.compute_at(sum_2, rxOuter);

    input_c.compute_at(sum_2, rxOuter);

    sum_1.update(0).compute_with(sum_2.update(0), rxOuter);
    sum_1.compute_root();
    sum_2.compute_root();
    total_sum.compute_root();

    class CheckAllocationSize : public IRMutator {

    protected:
        using IRMutator::visit;

        Stmt visit(const Allocate *op) override {
            if ((op->name == "input_c") && (op->constant_allocation_size() != 64)) {
                printf("Expected allocation size for input_c is 64, but is %d instead\n", op->constant_allocation_size());
                exit(1);
            }
            return IRMutator::visit(op);
        }
    };

    total_sum.add_custom_lowering_pass(new CheckAllocationSize());

    Buffer<int16_t> in(32, 64);
    in.fill(1);
    input.set(in);

    Buffer<int16_t> result = total_sum.realize();

    if (result() != 8192) {
        return 1;
    }

    return 0;
}

// Test for the issue described in https://github.com/halide/Halide/issues/6367.
int two_compute_at_test() {
    ImageParam input1(Int(16), 2, "input1");
    Func output1("output1"), output2("output2"), output3("output3");
    Var k{"k"};

    Func intermediate{"intermediate"};
    Func output1_value{"output1_value"};
    Func output3_value{"output3_value"};

    intermediate(k) = input1(k, 0) * input1(k, 1);
    output1_value(k) = intermediate(k) * intermediate(k);
    output1(k) = output1_value(k);
    output2(k) = output1_value(k) + output1_value(k);
    output3_value(k) = input1(k, 0) + 2;
    output3(k) = output3_value(k);

    Expr num = input1.dim(0).extent();
    input1.dim(0).set_bounds(0, num);
    input1.dim(1).set_bounds(0, 2);
    output1.output_buffer().dim(0).set_bounds(0, num);
    output2.output_buffer().dim(0).set_bounds(0, num);
    output3.output_buffer().dim(0).set_bounds(0, num);

    intermediate
        .vectorize(k, 8)
        .compute_at(output1_value, k)
        .bound_storage(k, 8)
        .store_in(MemoryType::Register);

    output1_value
        .vectorize(k, 8)
        .compute_at(output2, k)
        .bound_storage(k, 8)
        .store_in(MemoryType::Register);

    output1
        .vectorize(k, 8)
        .compute_with(output2, k);

    output2
        .vectorize(k, 8);

    output3_value
        .vectorize(k, 8)
        .compute_at(output3, k)
        .bound_storage(k, 8)
        .store_in(MemoryType::Register);

    output3
        .vectorize(k, 8)
        .compute_with(output2, k);

    Pipeline p({output1, output2, output3});
    p.compile_jit();

    Buffer<int16_t> in(8, 2);
    Buffer<int16_t> o1(8), o2(8), o3(8);
    for (int iy = 0; iy < in.height(); iy++) {
        for (int ix = 0; ix < in.width(); ix++) {
            in(ix, iy) = ix + iy;
        }
    }
    input1.set(in);
    p.realize({o1, o2, o3});

    for (int x = 0; x < 8; x++) {
        int val = (x * (x + 1)) * (x * (x + 1));
        if (o1(x) != val) {
            printf("o1(%d) = %d instead of %d\n",
                   x, o1(x), val);
            return 1;
        }
        if (o2(x) != 2 * val) {
            printf("o2(%d) = %d instead of %d\n",
                   x, o2(x), 2 * val);
            return 1;
        }
        if (o3(x) != x + 2) {
            printf("o2(%d) = %d instead of %d\n",
                   x, o3(x), x + 2);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"split reorder test", split_test},
        {"fuse test", fuse_test},
        {"multiple fuse group test", multiple_fuse_group_test},
        {"multiple outputs test", multiple_outputs_test},
        {"double split fuse test", double_split_fuse_test},
        {"vectorize test", vectorize_test},
        //
        // Note: we are deprecating skipping parts of a fused group in favor of
        //       cloning funcs in particular stages via a new (clone_)in overload.
        // TODO: remove this code when the new clone_in is implemented.
        //
        // {"some are skipped test", some_are_skipped_test},
        {"rgb to yuv420 test", rgb_yuv420_test},
        {"with specialization test", with_specialization_test},
        {"fuse compute at test", fuse_compute_at_test},
        {"nested compute with test", nested_compute_with_test},
        {"mixed tile factor test", mixed_tile_factor_test},
        // NOTE: disabled because it generates OOB (see #4751 for discussion).
        // {"only some are tiled test", only_some_are_tiled_test},
        {"multiple outputs on gpu test", multiple_outputs_on_gpu_test},
        {"multi tile mixed tile factor test", multi_tile_mixed_tile_factor_test},
        {"update stage test", update_stage_test},
        {"update stage2 test", update_stage2_test},
        {"update stage3 test", update_stage3_test},
        {"update stage pairwise test", update_stage_pairwise_test},
        // I think this should work, but there is an overzealous check somewhere.
        // {"update stage pairwise zigzag test", update_stage_pairwise_zigzag_test},
        {"update stage diagonal test", update_stage_diagonal_test},
        {"update stage rfactor test", update_stage_rfactor_test},
        {"vectorize inlined test", vectorize_inlined_test},
        {"mismatching splits test", mismatching_splits_test},
        {"different arg number compute_at test", different_arg_num_compute_at_test},
        {"store_at different levels test", store_at_different_levels_test},
        {"rvar bounds test", rvar_bounds_test},
        {"two_compute_at test", two_compute_at_test},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
