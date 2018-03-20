#include "Halide.h"
#include <stdio.h>

using namespace Halide;

struct event {
    char func_name[16];
    int parent_id;
    int event_type;
    int type_code, bits, width, value_index;
    int dimensions;
    int coordinates[4];
    float value[4];
};

event trace[1024];
int n_trace = 0;

// Print an event in a human-readable way
void print_event(event e) {
    assert(e.dimensions <= 4 && e.width <= 4);

    const char *event_types[] = {"Load",
                                 "Store",
                                 "Begin realization",
                                 "End realization",
                                 "Produce",
                                 "End Produce",
                                 "Consume",
                                 "End consume",
                                 "Begin pipeline",
                                 "End pipeline",
                                 "Pipeline layout info",
                                 "Pipeline metadata"};
    assert(e.event_type >= 0 && e.event_type <= 11);
    printf("%d %s ", e.event_type, event_types[e.event_type]);

    printf("%s.%d[", e.func_name, e.value_index);
    for (int i = 0; i < e.dimensions; i++) {
        if (i > 0) printf(", ");
        printf("%d", e.coordinates[i]);
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
    printf("{\"%s\", %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, {%f, %f, %f, %f}},\n",
           e.func_name, e.parent_id, e.event_type, e.type_code, e.bits, e.width, e.value_index,
           e.dimensions, e.coordinates[0], e.coordinates[1], e.coordinates[2], e.coordinates[3],
           e.value[0], e.value[1], e.value[2], e.value[3]);
}

// Are two floats nearly equal?
bool float_match(float a, float b) {
    return ((a - 0.001f) < b) && ((a + 0.001f) > b);
}

// Are two events equal?
bool events_match(event a, event b) {
    return (!strcmp(a.func_name, b.func_name) &&
            a.parent_id == b.parent_id &&
            a.event_type == b.event_type &&
            a.type_code == b.type_code &&
            a.bits == b.bits &&
            a.width == b.width &&
            a.value_index == b.value_index &&
            a.dimensions == b.dimensions &&
            a.coordinates[0] == b.coordinates[0] &&
            a.coordinates[1] == b.coordinates[1] &&
            a.coordinates[2] == b.coordinates[2] &&
            a.coordinates[3] == b.coordinates[3] &&
            float_match(a.value[0], b.value[0]) &&
            float_match(a.value[1], b.value[1]) &&
            float_match(a.value[2], b.value[2]) &&
            float_match(a.value[3], b.value[3]));
}

int my_trace(void *user_context, const halide_trace_event_t *ev) {
    assert(ev->dimensions <= 4 && ev->type.lanes <= 4);

    // Record this event in the trace array
    event e;
    memset(&e, 0, sizeof(e));
    assert(strlen(ev->func) < 16);
    strcpy(e.func_name, ev->func);
    e.parent_id = ev->parent_id;
    e.event_type = ev->event;
    e.type_code = ev->type.code;
    e.bits = ev->type.bits;
    e.width = ev->type.lanes;
    e.value_index = ev->value_index;
    e.dimensions = ev->dimensions;
    for (int i = 0; i < ev->dimensions; i++) {
        e.coordinates[i] = ev->coordinates[i];
    }
    Type t = Type(ev->type).with_lanes(1);
    for (int i = 0; i < ev->type.lanes; i++) {
        if (t == Float(32)) {
            e.value[i] = ((const float *)(ev->value))[i];
        } else if (t == UInt(8)) {
            e.value[i] = ((const uint8_t *)(ev->value))[i];
        } else {
            // Other types are possible, but not for this specific trace
            std::cerr << "Unexpected base type in trace: " << t << "\n";
            exit(1);
        }
    }
    trace[n_trace++] = e;
    return n_trace;
}

int main(int argc, char **argv) {
    constexpr int kSize = 10;

    Target t = get_jit_target_from_environment()
        .with_feature(Target::TraceStores)
        .with_feature(Target::TraceLoads)
        .with_feature(Target::TraceRealizations);

    Buffer<float> sin_in_buf(kSize + 1);
    for (int i = 0; i < kSize + 1; i++) {
        sin_in_buf(i) = sin(i*0.1f);
    }
    ImageParam sin_in(Float(32), 1, "sin_in");
    sin_in.set(sin_in_buf);

    Buffer<float> cos_in(kSize + 1, "cos_in");
    for (int i = 0; i < kSize + 1; i++) {
        cos_in(i) = cos(i*0.1f);
    }

    Func output("output"), intermediate("intermediate");
    Var x;
    intermediate(x) = Tuple(sin_in(x), cos_in(x));
    output(x) = intermediate(x)[0] + intermediate(x+1)[1];

    output.vectorize(x, 4);

    intermediate.vectorize(x, 4);
    intermediate.store_root().compute_at(output, x);

    output.set_custom_trace(&my_trace);
    output.realize(kSize, t);

    // The golden trace, recorded when this test was written
    event correct_trace[] = {
        {"output", 0, 8, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 1, 10, 1, 8, 1, 0, 2, {0, 0, 0, 0}, {1.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 1, 10, 1, 8, 1, 0, 2, {0, 10, 0, 0}, {2.000000, 0.000000, 0.000000, 0.000000}},
        {"sin_in", 1, 10, 1, 8, 1, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"cos_in", 1, 10, 1, 8, 1, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 1, 2, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 1, 2, 3, 0, 0, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 6, 4, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 4, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"sin_in", 1, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000, 0.099833, 0.198669, 0.295520}},
        {"intermediate", 9, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000, 0.099833, 0.198669, 0.295520}},
        {"cos_in", 1, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {1.000000, 0.995004, 0.980067, 0.955337}},
        {"intermediate", 9, 1, 2, 32, 4, 1, 4, {0, 1, 2, 3}, {1.000000, 0.995004, 0.980067, 0.955337}},
        {"sin_in", 1, 0, 2, 32, 4, 0, 4, {1, 2, 3, 4}, {0.099833, 0.198669, 0.295520, 0.389418}},
        {"intermediate", 9, 1, 2, 32, 4, 0, 4, {1, 2, 3, 4}, {0.099833, 0.198669, 0.295520, 0.389418}},
        {"cos_in", 1, 0, 2, 32, 4, 0, 4, {1, 2, 3, 4}, {0.995004, 0.980067, 0.955337, 0.921061}},
        {"intermediate", 9, 1, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004, 0.980067, 0.955337, 0.921061}},
        {"intermediate", 9, 5, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 6, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 19, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000, 0.099833, 0.198669, 0.295520}},
        {"intermediate", 19, 0, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004, 0.980067, 0.955337, 0.921061}},
        {"output", 8, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.995004, 1.079900, 1.154006, 1.216581}},
        {"intermediate", 19, 7, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 4, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"sin_in", 1, 0, 2, 32, 4, 0, 4, {5, 6, 7, 8}, {0.479426, 0.564642, 0.644218, 0.717356}},
        {"intermediate", 24, 1, 2, 32, 4, 0, 4, {5, 6, 7, 8}, {0.479426, 0.564642, 0.644218, 0.717356}},
        {"cos_in", 1, 0, 2, 32, 4, 0, 4, {5, 6, 7, 8}, {0.877583, 0.825336, 0.764842, 0.696707}},
        {"intermediate", 24, 1, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583, 0.825336, 0.764842, 0.696707}},
        {"intermediate", 24, 5, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 6, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 30, 0, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {0.389418, 0.479426, 0.564642, 0.644218}},
        {"intermediate", 30, 0, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583, 0.825336, 0.764842, 0.696707}},
        {"output", 8, 1, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {1.267001, 1.304761, 1.329485, 1.340924}},
        {"intermediate", 30, 7, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 4, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"sin_in", 1, 0, 2, 32, 4, 0, 4, {7, 8, 9, 10}, {0.644218, 0.717356, 0.783327, 0.841471}},
        {"intermediate", 35, 1, 2, 32, 4, 0, 4, {7, 8, 9, 10}, {0.644218, 0.717356, 0.783327, 0.841471}},
        {"cos_in", 1, 0, 2, 32, 4, 0, 4, {7, 8, 9, 10}, {0.764842, 0.696707, 0.621610, 0.540302}},
        {"intermediate", 35, 1, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842, 0.696707, 0.621610, 0.540302}},
        {"intermediate", 35, 5, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 6, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 41, 0, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {0.564642, 0.644218, 0.717356, 0.783327}},
        {"intermediate", 41, 0, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842, 0.696707, 0.621610, 0.540302}},
        {"output", 8, 1, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {1.329485, 1.340924, 1.338966, 1.323629}},
        {"intermediate", 41, 7, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 8, 5, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"intermediate", 7, 3, 3, 0, 0, 0, 2, {0, 11, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 6, 3, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
        {"output", 1, 9, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000, 0.000000, 0.000000, 0.000000}},
    };

    int correct_trace_length = sizeof(correct_trace)/sizeof(correct_trace[0]);

    int n = n_trace > correct_trace_length ? n_trace : correct_trace_length;
    for (int i = 0; i < n; i++) {
        event recorded, correct;
        memset(&recorded, 0, sizeof(recorded));
        memset(&correct, 0, sizeof(correct));
        if (i < n_trace) recorded = trace[i];
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
