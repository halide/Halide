#include "Halide.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace Halide;

struct Box2D {
    int min[2], max[2];
};

// The parent of each scope-opening event.
std::map<int, int> parents;
// The most recent bounds_required region for each (parent id, Func).
std::map<std::pair<int, std::string>, Box2D> bounds_required;
int next_id = 1;
int stores_inside = 0, stores_outside = 0, errors = 0;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    int id = next_id++;
    std::string func = e->func;
    switch (e->event) {
    case halide_trace_begin_pipeline:
    case halide_trace_begin_realization:
    case halide_trace_produce:
    case halide_trace_consume:
        parents[id] = e->parent_id;
        break;
    case halide_trace_bounds_required: {
        Box2D r;
        for (int i = 0; i < 2; i++) {
            r.min[i] = e->coordinates[2 * i];
            r.max[i] = e->coordinates[2 * i] + e->coordinates[2 * i + 1] - 1;
        }
        bounds_required[{e->parent_id, func}] = r;
        break;
    }
    case halide_trace_store: {
        if (func != "o") {
            break;
        }
        int x = e->coordinates[0], y = e->coordinates[1];
        bool inside = true, any = false;
        for (int p = e->parent_id; p != 0; p = parents[p]) {
            auto it = bounds_required.find({p, func});
            if (it == bounds_required.end()) {
                continue;
            }
            any = true;
            const Box2D &r = it->second;
            inside &= (x >= r.min[0] && x <= r.max[0] && y >= r.min[1] && y <= r.max[1]);
        }
        if (!any) {
            printf("No bounds_required event encloses store to o(%d, %d)\n", x, y);
            errors++;
        } else if (inside) {
            stores_inside++;
            int value = *(const int *)e->value;
            if (value != 2 * (x + y)) {
                printf("o(%d, %d) = %d instead of %d\n", x, y, value, 2 * (x + y));
                errors++;
            }
        } else {
            stores_outside++;
        }
        break;
    }
    default:
        break;
    }
    return id;
}

int main(int argc, char **argv) {
    Var x, y, xo, xi;
    Func d("d"), o("o"), c("c"), p("p");
    d(x, y) = x + y;
    o(x, y) = d(x - 1, y) + d(x + 1, y);
    c(x, y) = o(x, y);
    p(x, y) = c(x - 1, y) + c(x + 1, y);

    // The RoundUp split of c computes o past the region of it that c
    // requires, from values of d that were never computed.
    p.bound(x, 0, 16).bound(y, 0, 2);
    c.compute_at(p, y).split(x, xo, xi, 8, TailStrategy::RoundUp);
    o.compute_at(c, xo).trace_stores().trace_bounds_required();
    d.compute_at(p, y);
    for (Func f : {d, o, c, p}) {
        f.trace_realizations();
    }

    p.jit_handlers().custom_trace = &my_trace;
    p.realize({16, 2});

    if (errors) {
        return 1;
    }
    // o is required on [-1, 16] in each row.
    if (stores_inside != 18 * 2) {
        printf("Expected %d stores to o inside the required region, got %d\n", 18 * 2, stores_inside);
        return 1;
    }
    // The last tile of c covers [15, 22].
    if (stores_outside != 6 * 2) {
        printf("Expected %d stores to o outside the required region, got %d\n", 6 * 2, stores_outside);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
