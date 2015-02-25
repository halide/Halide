#include "Halide.h"
#include <stdio.h>

using namespace Halide;

struct event {
    char func;
    int event_type;
    int type_code, bits, width, value_index;
    int num_int_args;
    int int_args[4];
    float value[4];
};

event trace[1024];
int n_trace = 0;

// Print an event in a human-readable way
void print_event(event e) {
    assert(e.num_int_args <= 4 && e.width <= 4);

    const char *event_types[] = {"Load",
                                 "Store",
                                 "Begin realization",
                                 "End realization",
                                 "Produce",
                                 "Update",
                                 "Consume",
                                 "End consume"};
    assert(e.event_type >= 0 && e.event_type <= 8);
    printf("%s ", event_types[e.event_type]);

    printf("%c.%d[", e.func, e.value_index);
    for (int i = 0; i < e.num_int_args; i++) {
        if (i > 0) printf(", ");
        printf("%d", e.int_args[i]);
    }
    printf("] [");
    for (int i = 0; i < e.width; i++) {
        if (i > 0) printf(", ");
        printf("%f", e.value[i]);
    }
    printf("]\n");
}

// Print an event in a way suitable for inclusion in source code
void print_event_source(event e) {
    printf("{%d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, {%f, %f, %f, %f}},\n",
           e.func, e.event_type, e.type_code, e.bits, e.width, e.value_index,
           e.num_int_args, e.int_args[0], e.int_args[1], e.int_args[2], e.int_args[3],
           e.value[0], e.value[1], e.value[2], e.value[3]);
}

// Are two floats nearly equal?
bool float_match(float a, float b) {
    return ((a - 0.001f) < b) && ((a + 0.001f) > b);
}

// Are two events equal?
bool events_match(event a, event b) {
    return (a.func == b.func &&
            a.event_type == b.event_type &&
            a.type_code == b.type_code &&
            a.bits == b.bits &&
            a.width == b.width &&
            a.value_index == b.value_index &&
            a.num_int_args == b.num_int_args &&
            a.int_args[0] == b.int_args[0] &&
            a.int_args[1] == b.int_args[1] &&
            a.int_args[2] == b.int_args[2] &&
            a.int_args[3] == b.int_args[3] &&
            float_match(a.value[0], b.value[0]) &&
            float_match(a.value[1], b.value[1]) &&
            float_match(a.value[2], b.value[2]) &&
            float_match(a.value[3], b.value[3]));
}

int my_trace(void *user_context, const halide_trace_event *ev) {

    assert(ev->dimensions <= 4 && ev->vector_width <= 4);

    // Record this event in the trace array
    event e = {0};
    e.func = ev->func[0];
    e.event_type = ev->event;
    e.type_code = ev->type_code;
    e.bits = ev->bits;
    e.width = ev->vector_width;
    e.value_index = ev->value_index;
    e.num_int_args = ev->dimensions;
    for (int i = 0; i < ev->dimensions; i++) {
        e.int_args[i] = ev->coordinates[i];
    }
    for (int i = 0; i < ev->vector_width; i++) {
        e.value[i] = ((const float *)(ev->value))[i];
    }
    trace[n_trace++] = e;

    return 0;
}

int main(int argc, char **argv) {

    Func f("f"), g("g");
    Var x;
    g(x) = Tuple(sin(x*0.1f), cos(x*0.1f));
    f(x) = g(x)[0] + g(x+1)[1];

    f.vectorize(x, 4);
    f.trace_stores();
    f.trace_realizations();

    g.vectorize(x, 4);
    g.store_root().compute_at(f, x);
    g.trace_stores().trace_loads().trace_realizations();

    f.set_custom_trace(&my_trace);
    f.realize(10);

    // The golden trace, recorded when this test was written
    event correct_trace[] = {
        {102, 2, 0, 32, 1, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 2, 0, 32, 1, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {102, 4, 0, 32, 1, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 4, 0, 32, 1, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000, 0.099833, 0.198669, 0.295520}},
        {103, 1, 2, 32, 4, 1, 4, {0, 1, 2, 3}, {1.000000, 0.995004, 0.980067, 0.955337}},
        {103, 1, 2, 32, 4, 0, 4, {1, 2, 3, 4}, {0.099833, 0.198669, 0.295520, 0.389418}},
        {103, 1, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004, 0.980067, 0.955337, 0.921061}},
        {103, 6, 0, 32, 1, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 0, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004, 0.980067, 0.955337, 0.921061}},
        {103, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000, 0.099833, 0.198669, 0.295520}},
        {102, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.995004, 1.079900, 1.154006, 1.216581}},
        {103, 7, 0, 32, 1, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 4, 0, 32, 1, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 1, 2, 32, 4, 0, 4, {5, 6, 7, 8}, {0.479426, 0.564642, 0.644218, 0.717356}},
        {103, 1, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583, 0.825336, 0.764842, 0.696707}},
        {103, 6, 0, 32, 1, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 0, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583, 0.825336, 0.764842, 0.696707}},
        {103, 0, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {0.389418, 0.479426, 0.564642, 0.644218}},
        {102, 1, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {1.267001, 1.304761, 1.329485, 1.340924}},
        {103, 7, 0, 32, 1, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 4, 0, 32, 1, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 1, 2, 32, 4, 0, 4, {7, 8, 9, 10}, {0.644218, 0.717356, 0.783327, 0.841471}},
        {103, 1, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842, 0.696707, 0.621610, 0.540302}},
        {103, 6, 0, 32, 1, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 0, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842, 0.696707, 0.621610, 0.540302}},
        {103, 0, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {0.564642, 0.644218, 0.717356, 0.783327}},
        {102, 1, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {1.329485, 1.340924, 1.338966, 1.323629}},
        {103, 7, 0, 32, 1, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {102, 6, 0, 32, 1, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {102, 7, 0, 32, 1, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {103, 3, 0, 32, 1, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {102, 3, 0, 32, 1, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}}};

    int correct_trace_length = sizeof(correct_trace)/sizeof(correct_trace[0]);

    int n = n_trace > correct_trace_length ? n_trace : correct_trace_length;
    for (int i = 0; i < n; i++) {
        event recorded;
        if (i < n_trace) recorded = trace[i];
        event correct;
        if (i < correct_trace_length) correct = correct_trace[i];

        if (!events_match(recorded, correct)) {
            // Uh oh. Maybe it's just a reordered load.
            if (i > 0 && events_match(recorded, correct_trace[i-1]) &&
                recorded.event_type == 0 && correct.event_type == 0) {
                // Phew.
                continue;
            }

            if (i < correct_trace_length-1 &&
                events_match(recorded, correct_trace[i+1]) &&
                recorded.event_type == 0 && correct.event_type == 0) {
                // Phew.
                continue;
            }

            printf("Traces differs at event %d:\n"
                   "-------------------------------\n"
                   "Correct trace:\n", i);
            for (int j = 0; j < correct_trace_length; j++) {
                if (j == i) printf(" ===> ");
                print_event(correct_trace[j]);
            }
            printf("-------------------------------\n"
                   "Trace encountered:\n");
            for (int j = 0; j < n_trace; j++) {
                if (j == i) printf(" ===> ");
                print_event_source(trace[j]);
            }
            printf("-------------------------------\n");
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}

