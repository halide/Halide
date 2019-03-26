#include "Halide.h"
#include "test/common/check_call_graphs.h"

#include <stdio.h>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;

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
        : Bound(min_0, max_0, min_1, max_1, 0, 0) {}
    Bound(int32_t min_0, int32_t max_0)
        : Bound(min_0, max_0, 0, 0, 0, 0) {}
    Bound() : Bound(-1, -1, -1, -1, -1, -1) {}
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
                   "Instead it is: %d\n", event.c_str(), fname.c_str(), i, b.min[i], b.max[i],
                   coordinates[idx]);
            return false;
        }
    }
    return true;
}

// A trace that check the region accessed by stores/loads of a buffer
int my_trace(void *user_context, const halide_trace_event_t *e) {
    string fname = std::string(e->func);
    if (e->event == halide_trace_store) {
        std::lock_guard<std::mutex> lock(stores_mutex);
        const auto &iter = stores.find(fname);
        if (iter != stores.end()) {
            const Bound &b = iter->second;
            if (!check_coordinates(b, e->coordinates, e->dimensions, e->type.lanes, "store", fname)) {
                exit(-1);
            }
        }
        stores_total++;
    } else if (e->event == halide_trace_load) {
        std::lock_guard<std::mutex> lock(loads_mutex);
        const auto &iter = loads.find(fname);
        if (iter != loads.end()) {
            const Bound &b = iter->second;
            if (!check_coordinates(b, e->coordinates, e->dimensions, e->type.lanes, "load", fname)) {
                exit(-1);
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
        im_ref = h.realize(200, 200);
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
            {h.name(), Bound()}, // There shouldn't be any load from h
        };
        h.set_custom_trace(&my_trace);

        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int fuse_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);
        im_ref = h.realize(100, 100, 100);
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
            {f.name(), Bound(2, 101, -1, 98, 3, 102)},
            {g.name(), Bound(-5, 94, -6, 93, 2, 101)},
            {h.name(), Bound(0, 99, 0, 99, 0, 99)},
        };
        loads = {
            {f.name(), Bound(2, 101, -1, 98, 3, 102)},
            {g.name(), Bound(-5, 94, -6, 93, 2, 101)},
            {h.name(), Bound()}, // There shouldn't be any load from h
        };
        h.set_custom_trace(&my_trace);

        im = h.realize(100, 100, 100);
    }

    auto func = [im_ref](int x, int y, int z) {
        return im_ref(x, y, z);
    };
    if (check_image(im, func)) {
        return -1;
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
        im_ref = q.realize(200, 200);
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
            {q.name(), Bound()}, // There shouldn't be any load from q
        };
        q.set_custom_trace(&my_trace);

        im = q.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
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
            {f.name(), Bound()}, // There shouldn't be any load from f
            {g.name(), Bound()}, // There shouldn't be any load from g
        };

        Pipeline p({f, g});
        p.set_custom_trace(&my_trace);
        p.realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
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
        im_ref = r.realize(167, 167);
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
            {r.name(), Bound()}, // There shouldn't be any load from r
        };
        r.set_custom_trace(&my_trace);

        im = r.realize(167, 167);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
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
        im_ref = h.realize(200, 200);
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
            {h.name(), Bound()}, // There shouldn't be any load from h
        };
        h.set_custom_trace(&my_trace);

        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int rgb_yuv420_test() {
    // Somewhat approximating the behavior of rgb -> yuv420 (downsample by half in the u and v channels)
    const int size = 256;
    Buffer<int> y_im(size, size), u_im(size/2, size/2), v_im(size/2, size/2);
    Buffer<int> y_im_ref(size, size), u_im_ref(size/2, size/2), v_im_ref(size/2, size/2);

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
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2*clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2*rgb_x(x, y, z) + rgb_x(x, y - 1, z))/16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) +  25 * input(x, y, 2) + 128) >> 8) +  16;
        u_part(x, y) = (( -38 * rgb(2*x, 2*y, 0) -  74 * rgb(2*x, 2*y, 1) + 112 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;
        v_part(x, y) = (( 112 * rgb(2*x, 2*y, 0) -  94 * rgb(2*x, 2*y, 1) -  18 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;

        y_part.vectorize(x, 8);
        u_part.vectorize(x, 8);
        v_part.vectorize(x, 8);
        loads_total = 0;
        stores_total = 0;
        Pipeline p({y_part, u_part, v_part});
        p.set_custom_trace(&my_trace);
        p.realize({y_im_ref, u_im_ref, v_im_ref},
                  get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));
        load_count_ref = loads_total;
        store_count_ref = stores_total;
    }

    {
        Var x("x"), y("y"), z("z");
        Func y_part("y_part"), u_part("u_part"), v_part("v_part"), rgb("rgb"), rgb_x("rgb_x");

        Func clamped = BoundaryConditions::repeat_edge(input);
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2*clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2*rgb_x(x, y, z) + rgb_x(x, y - 1, z))/16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) +  25 * input(x, y, 2) + 128) >> 8) +  16;
        u_part(x, y) = (( -38 * rgb(2*x, 2*y, 0) -  74 * rgb(2*x, 2*y, 1) + 112 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;
        v_part(x, y) = (( 112 * rgb(2*x, 2*y, 0) -  94 * rgb(2*x, 2*y, 1) -  18 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;

        Var xi("xi"), yi("yi");
        y_part.tile(x, y, xi, yi, 16, 2, TailStrategy::RoundUp);
        u_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);
        v_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);

        y_part.unroll(yi);
        y_part.vectorize(xi);
        u_part.vectorize(xi);
        v_part.vectorize(xi);

        u_part.compute_with(y_part, x, LoopAlignStrategy::AlignEnd);
        v_part.compute_with(u_part, x, LoopAlignStrategy::AlignEnd);

        Expr width = v_part.output_buffer().width();
        Expr height = v_part.output_buffer().height();
        width = (width/8)*8;

        u_part.bound(x, 0, width).bound(y, 0, height);
        v_part.bound(x, 0, width).bound(y, 0, height);
        y_part.bound(x, 0, 2*width).bound(y, 0, 2*height);

        rgb_x.fold_storage(y, 4);
        rgb_x.store_root();
        rgb_x.compute_at(y_part, y).vectorize(x, 8);
        rgb.compute_at(y_part, y).vectorize(x, 8);

        stores = {
            {rgb_x.name(), Bound(0, size - 1, -1, size - 2, 0, 2)},
            {rgb.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {y_part.name(), Bound(0, size - 1, 0, size - 1)},
            {u_part.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
            {v_part.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
        };
        loads = {
            {rgb_x.name(), Bound(0, size - 1, -1, size - 2, 0, 2)},
            {rgb.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {y_part.name(), Bound()}, // There shouldn't be any load from y_part
            {u_part.name(), Bound()}, // There shouldn't be any load from u_part
            {v_part.name(), Bound()}, // There shouldn't be any load from v_part
        };

        loads_total = 0;
        stores_total = 0;
        Pipeline p({y_part, u_part, v_part});
        p.set_custom_trace(&my_trace);
        p.realize({y_im, u_im, v_im}, get_jit_target_from_environment().with_feature(Target::TraceLoads).with_feature(Target::TraceStores));
        bool too_many_memops = false;
        if (stores_total > 2 * store_count_ref) {
            printf("Store count for correctness_compute_with rgb to yuv420 case exceeds reference by more than 2x. (Reference: %lu, compute_with: %lu).\n",
                   store_count_ref, stores_total);
            too_many_memops = true;
        }
        if (loads_total > 2 * load_count_ref) {
            printf("Load count for correctness_compute_with rgb to yuv420 case exceeds reference by more than 2x. (Reference: %lu, compute_with: %lu).\n",
                   load_count_ref, loads_total);
            too_many_memops = true;
        }
        if (too_many_memops) {
            return -1;
        }
        
    }

    auto y_func = [y_im_ref](int x, int y) {
        return y_im_ref(x, y);
    };
    if (check_image(y_im, y_func)) {
        return -1;
    }

    auto u_func = [u_im_ref](int x, int y) {
        return u_im_ref(x, y);
    };
    if (check_image(u_im, u_func)) {
        return -1;
    }

    auto v_func = [v_im_ref](int x, int y) {
        return v_im_ref(x, y);
    };
    if (check_image(v_im, v_func)) {
        return -1;
    }

    return 0;
}

int vectorize_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize(111, 111);
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
            {f.name(), Bound(-1, 109, 1, 111)},
            {g.name(), Bound(2, 112, -2, 108)},
            {h.name(), Bound(0, 110, 0, 110)},
        };
        loads = {
            {f.name(), Bound(-1, 109, 1, 111)},
            {g.name(), Bound(2, 112, -2, 108)},
            {h.name(), Bound()}, // There shouldn't be any load from h
        };
        h.set_custom_trace(&my_trace);

        im = h.realize(111, 111);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
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
        im_ref = h.realize(200, 200);
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
        h.set_custom_trace(&my_trace);

        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
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

        Realization r(f_im, g_im);
        Pipeline({f, g}).realize(r);
        r[0].copy_to_host();
        r[1].copy_to_host();
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    return 0;
}

int mixed_tile_factor_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

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

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("input");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

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
            {g.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
            {h.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()}, // There shouldn't be any load from f
            {g.name(), Bound()}, // There shouldn't be any load from g
            {h.name(), Bound()}, // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.set_custom_trace(&my_trace);
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
    }

    return 0;
}

int multi_tile_mixed_tile_factor_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

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

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

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
            {g.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
            {h.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()}, // There shouldn't be any load from f
            {g.name(), Bound()}, // There shouldn't be any load from g
            {h.name(), Bound()}, // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.set_custom_trace(&my_trace);
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
    }

    return 0;
}

int only_some_are_tiled_test() {
    const int size = 256;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

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

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

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
            {g.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
            {h.name(), Bound(0, size/2 - 1, 0, size/2 - 1)},
        };
        loads = {
            {input.name(), Bound(0, size - 1, 0, size - 1, 0, 2)},
            {f.name(), Bound()}, // There shouldn't be any load from f
            {g.name(), Bound()}, // There shouldn't be any load from g
            {h.name(), Bound()}, // There shouldn't be any load from h
        };

        Pipeline p({f, g, h});
        p.set_custom_trace(&my_trace);
        p.realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
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
        im_ref = h.realize(200, 200);
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
            {h.name(), Bound()}, // There shouldn't be any load from h
        };
        h.set_custom_trace(&my_trace);

        tile.set(true);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
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
        f2(x, y) = input(x, y)*input(x, y);
        g1(x, y) = f1(x, y) + x + y;
        g2(x, y) = f1(x, y)*f2(x, y);
        Pipeline({g1, g2}).realize({g1_im_ref, g2_im_ref});
    }

    {
        Var x("x"), y("y");
        Func input("input"), f1("f1"), f2("f2"), g1("g1"), g2("g2");

        input(x, y) = x + y;
        f1(x, y) = input(x, y) + 20;
        f2(x, y) = input(x, y)*input(x, y);
        g1(x, y) = f1(x, y) + x + y;
        g2(x, y) = f1(x, y)*f2(x, y);

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
            {g1.name(), Bound()}, // There shouldn't be any load from g1
            {g2.name(), Bound()}, // There shouldn't be any load from g2
        };

        Pipeline p({g1, g2});
        p.set_custom_trace(&my_trace);
        p.realize({g1_im, g2_im});
    }

    auto g1_func = [g1_im_ref](int x, int y) {
        return g1_im_ref(x, y);
    };
    if (check_image(g1_im, g1_func)) {
        return -1;
    }

    auto g2_func = [g2_im_ref](int x, int y) {
        return g2_im_ref(x, y);
    };
    if (check_image(g2_im, g2_func)) {
        return -1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running split reorder test\n");
    if (split_test() != 0) {
        return -1;
    }

    printf("Running fuse test\n");
    if (fuse_test() != 0) {
        return -1;
    }

    printf("Running multiple fuse group test\n");
    if (multiple_fuse_group_test() != 0) {
        return -1;
    }

    printf("Running multiple outputs test\n");
    if (multiple_outputs_test() != 0) {
        return -1;
    }

    printf("Running double split fuse test\n");
    if (double_split_fuse_test() != 0) {
        return -1;
    }

    printf("Running vectorize test\n");
    if (vectorize_test() != 0) {
        return -1;
    }

    /*
     * Note: we are deprecating skipping parts of a fused group in favor of
     *       cloning funcs in particular stages via a new (clone_)in overload.
     * TODO: remove this code when the new clone_in is implemented.
     */
//    printf("Running some are skipped test\n");
//    if (some_are_skipped_test() != 0) {
//        return -1;
//    }

    printf("Running rgb to yuv420 test\n");
    if (rgb_yuv420_test() != 0) {
        return -1;
    }

    printf("Running with specialization test\n");
    if (with_specialization_test() != 0) {
        return -1;
    }

    printf("Running fuse compute at test\n");
    if (fuse_compute_at_test() != 0) {
        return -1;
    }

    printf("Running nested compute with test\n");
    if (nested_compute_with_test() != 0) {
        return -1;
    }

    printf("Running mixed tile factor test\n");
    if (mixed_tile_factor_test() != 0) {
        return -1;
    }

    printf("Running only some are tiled test\n");
    if (only_some_are_tiled_test() != 0) {
        return -1;
    }

    printf("Running multiple outputs on gpu test\n");
    if (multiple_outputs_on_gpu_test() != 0) {
        return -1;
    }

    printf("Running multi tile mixed tile factor test\n");
    if (multi_tile_mixed_tile_factor_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
