#include "Halide.h"
#include <map>
#include <mutex>
#include <stdio.h>

using namespace Halide;

// Every begin event must be matched by exactly one end event, even when later
// lowering passes make the bodies of produce and consume nodes conditional.

std::mutex open_events_mutex;
std::map<int, int> open_events;
int next_id = 0;
int unmatched_ends = 0;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    std::lock_guard<std::mutex> lock(open_events_mutex);
    int id = next_id++;
    switch (e->event) {
    case halide_trace_begin_realization:
    case halide_trace_produce:
    case halide_trace_consume:
        open_events[id] = e->event;
        break;
    case halide_trace_end_realization:
    case halide_trace_end_produce:
    case halide_trace_end_consume: {
        auto it = open_events.find(e->parent_id);
        if (it == open_events.end() || it->second + 1 != e->event) {
            unmatched_ends++;
        } else {
            open_events.erase(it);
        }
        break;
    }
    default:
        break;
    }
    return id;
}

bool check(const char *name, Func out) {
    open_events.clear();
    unmatched_ends = 0;
    out.jit_handlers().custom_trace = my_trace;
    out.realize({16, 32});

    if (unmatched_ends != 0) {
        printf("%s: %d end events did not match a begin event\n", name, unmatched_ends);
        return false;
    }
    if (!open_events.empty()) {
        printf("%s: %d begin events were never ended:\n", name, (int)open_events.size());
        for (const auto &it : open_events) {
            printf("  event id %d, type %d\n", it.first, it.second);
        }
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    bool ok = true;
    Var x("x"), y("y"), yo("yo"), yi("yi");

    {
        // Sliding window guards consumers during warm-up iterations.
        Func f("f"), g("g"), h("h");
        f(x, y) = x + y;
        g(x, y) = f(x, y - 1) + f(x, y + 1);
        h(x, y) = g(x, y - 1) + g(x, y + 1);

        h.split(y, yo, yi, 8).parallel(yo);
        f.store_at(h, yo).compute_at(h, yi);
        g.store_at(h, yo).compute_at(h, yi);
        for (Func fn : {f, g, h}) {
            fn.trace_realizations();
        }
        ok &= check("sliding window", h);
    }

    {
        // Skip stages guards producers of conditionally-used Funcs.
        Param<bool> use_f("use_f");
        Func f("f"), g("g"), h("h");
        f(x, y) = x + y;
        g(x, y) = select(use_f, f(x, y), 0);
        h(x, y) = g(x, y) + 1;

        f.compute_at(h, y);
        g.compute_at(h, y);
        for (Func fn : {f, g, h}) {
            fn.trace_realizations();
        }
        for (bool b : {false, true}) {
            use_f.set(b);
            ok &= check("skip stages", h);
        }
    }

    if (!ok) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
