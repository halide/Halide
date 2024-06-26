#include "Halide.h"
#include <stdio.h>

namespace {

using namespace Halide;

struct event {
    char func;
    int parent_id;
    int event_type;
    int type_code, bits, width, value_index;
    int num_int_args;
    int int_args[4];
    float value[4];
    // trace_tag can actually be arbitrarily long, but for testing
    // purposes we'll keep it short, to avoid dynamic allocations
    char trace_tag[64];
};

event trace[1024];
int n_trace = 0;

// Print an event in a human-readable way
void print_event(const event &e) {
    assert(e.num_int_args <= 4 && e.width <= 4);

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
                                 "Tag"};
    assert(e.event_type >= 0 && e.event_type <= 10);
    printf("%d %s ", e.event_type, event_types[e.event_type]);

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
    printf("] \"%s\"\n", e.trace_tag);
}

// Print an event in a way suitable for inclusion in source code
void print_event_source(const event &e) {
    printf("{%d, %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, {%ff, %ff, %ff, %ff}, \"%s\"},\n",
           e.func, e.parent_id, e.event_type, e.type_code, e.bits, e.width, e.value_index,
           e.num_int_args, e.int_args[0], e.int_args[1], e.int_args[2], e.int_args[3],
           e.value[0], e.value[1], e.value[2], e.value[3], e.trace_tag);
}

// Are two floats nearly equal?
bool float_match(float a, float b) {
    return ((a - 0.001f) < b) && ((a + 0.001f) > b);
}

// Are two events equal?
bool events_match(const event &a, const event &b) {
    return (a.func == b.func &&
            a.parent_id == b.parent_id &&
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
            float_match(a.value[3], b.value[3]) &&
            !strcmp(a.trace_tag, b.trace_tag));
}

int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {
    assert(ev->dimensions <= 4 && ev->type.lanes <= 4);

    // Record this event in the trace array
    event e = {0};
    e.func = ev->func[0];
    e.parent_id = ev->parent_id;
    e.event_type = ev->event;
    e.type_code = ev->type.code;
    e.bits = ev->type.bits;
    e.width = ev->type.lanes;
    e.value_index = ev->value_index;
    e.num_int_args = ev->dimensions;
    for (int i = 0; i < ev->dimensions; i++) {
        e.int_args[i] = ev->coordinates[i];
    }
    for (int i = 0; i < ev->type.lanes; i++) {
        e.value[i] = ((const float *)(ev->value))[i];
    }
    if (ev->trace_tag) {
        assert(strlen(ev->trace_tag) < sizeof(e.trace_tag));
        strcpy(e.trace_tag, ev->trace_tag);
    } else {
        e.trace_tag[0] = 0;
    }
    trace[n_trace++] = e;
    return n_trace;
}

bool check_trace_correct(event *correct_trace, int correct_trace_length) {
    int n = n_trace > correct_trace_length ? n_trace : correct_trace_length;
    for (int i = 0; i < n; i++) {
        event recorded = {0};
        if (i < n_trace) recorded = trace[i];
        event correct = {0};
        if (i < correct_trace_length) correct = correct_trace[i];

        if (!events_match(recorded, correct)) {
            constexpr int radius_max = 2;

            // Uh oh. Maybe it's just a reordered load.
            bool reordered = false;
            for (int radius = 1; radius <= radius_max; ++radius) {
                if (i >= radius &&
                    events_match(recorded, correct_trace[i - radius]) &&
                    recorded.event_type == 0 &&
                    correct.event_type == 0) {
                    // Phew.
                    reordered = true;
                    break;
                }

                if (i < correct_trace_length - radius &&
                    events_match(recorded, correct_trace[i + radius]) &&
                    recorded.event_type == 0 &&
                    correct.event_type == 0) {
                    // Phew.
                    reordered = true;
                    break;
                }
            }
            if (reordered) {
                continue;
            }

            printf("Traces differs at event %d:\n"
                   "-------------------------------\n"
                   "Correct trace:\n",
                   i);
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
            return false;
        }
    }
    return true;
}

void reset_trace() {
    n_trace = 0;
}

}  // namespace

int main(int argc, char **argv) {
    ImageParam input(Float(32), 1, "i");

    Buffer<float> input_buf(10);
    input_buf.fill(0.f);
    input.set(input_buf);

    Func f("f"), g("g");
    Var x;
    g(x) = Tuple(sin(x * 0.1f), cos(x * 0.1f));
    f(x) = g(x)[0] + g(x + 1)[1] + input(x);

    f.vectorize(x, 4);
    g.store_root().compute_at(f, x);
    g.vectorize(x, 4);

    f.jit_handlers().custom_trace = &my_trace;

    // Check that Target::TracePipeline works.
    f.realize({10}, get_jit_target_from_environment().with_feature(Target::TracePipeline));

    // The golden trace, recorded when this test was written
    event correct_pipeline_trace[] = {
        {102, 0, 8, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 1, 9, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
    };
    if (!check_trace_correct(correct_pipeline_trace, 2)) {
        return 1;
    }

    // Test a more interesting trace.
    reset_trace();

    g.add_trace_tag("g whiz");
    g.trace_stores().trace_loads().trace_realizations();

    f.trace_stores();
    f.trace_realizations();
    f.add_trace_tag("arbitrary data on f");
    // All non-null characters are OK
    f.add_trace_tag("more:arbitrary \xff data on f?");

    input.trace_loads();

    f.realize({10}, get_jit_target_from_environment());

    // The golden trace, recorded when this test was written
    event correct_trace[] = {
        {102, 0, 8, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "func_type_and_dim: 2 2 32 1 2 32 1 1 0 11"},
        {105, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "func_type_and_dim: 1 2 32 1 1 0 10"},
        {102, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "func_type_and_dim: 1 2 32 1 1 0 10"},
        {102, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "arbitrary data on f"},
        {102, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "more:arbitrary \xff data on f?"},
        {103, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "g whiz"},
        {102, 1, 2, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 1, 2, 3, 0, 0, 0, 2, {0, 11, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 8, 4, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 4, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 11, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000f, 0.099833f, 0.198669f, 0.295520f}, ""},
        {103, 11, 1, 2, 32, 4, 1, 4, {0, 1, 2, 3}, {1.000000f, 0.995004f, 0.980067f, 0.955337f}, ""},
        {103, 11, 1, 2, 32, 4, 0, 4, {1, 2, 3, 4}, {0.099833f, 0.198669f, 0.295520f, 0.389418f}, ""},
        {103, 11, 1, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004f, 0.980067f, 0.955337f, 0.921061f}, ""},
        {103, 11, 5, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 6, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {105, 1, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 17, 0, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.000000f, 0.099833f, 0.198669f, 0.295520f}, ""},
        {103, 17, 0, 2, 32, 4, 1, 4, {1, 2, 3, 4}, {0.995004f, 0.980067f, 0.955337f, 0.921061f}, ""},
        {102, 10, 1, 2, 32, 4, 0, 4, {0, 1, 2, 3}, {0.995004f, 1.079900f, 1.154006f, 1.216581f}, ""},
        {103, 17, 7, 3, 0, 0, 0, 2, {0, 5, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 4, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 23, 1, 2, 32, 4, 0, 4, {5, 6, 7, 8}, {0.479426f, 0.564642f, 0.644218f, 0.717356f}, ""},
        {103, 23, 1, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583f, 0.825336f, 0.764842f, 0.696707f}, ""},
        {103, 23, 5, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 6, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {105, 1, 0, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 27, 0, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {0.389418f, 0.479426f, 0.564642f, 0.644218f}, ""},
        {103, 27, 0, 2, 32, 4, 1, 4, {5, 6, 7, 8}, {0.877583f, 0.825336f, 0.764842f, 0.696707f}, ""},
        {102, 10, 1, 2, 32, 4, 0, 4, {4, 5, 6, 7}, {1.267001f, 1.304761f, 1.329485f, 1.340924f}, ""},
        {103, 27, 7, 3, 0, 0, 0, 2, {5, 4, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 4, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 33, 1, 2, 32, 4, 0, 4, {7, 8, 9, 10}, {0.644218f, 0.717356f, 0.783327f, 0.841471f}, ""},
        {103, 33, 1, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842f, 0.696707f, 0.621610f, 0.540302f}, ""},
        {103, 33, 5, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 6, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {105, 1, 0, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 37, 0, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {0.564642f, 0.644218f, 0.717356f, 0.783327f}, ""},
        {103, 37, 0, 2, 32, 4, 1, 4, {7, 8, 9, 10}, {0.764842f, 0.696707f, 0.621610f, 0.540302f}, ""},
        {102, 10, 1, 2, 32, 4, 0, 4, {6, 7, 8, 9}, {1.329485f, 1.340924f, 1.338966f, 1.323629f}, ""},
        {103, 37, 7, 3, 0, 0, 0, 2, {9, 2, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 10, 5, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {103, 9, 3, 3, 0, 0, 0, 2, {0, 11, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 8, 3, 3, 0, 0, 0, 2, {0, 10, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 1, 9, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
    };

    int correct_trace_length = sizeof(correct_trace) / sizeof(correct_trace[0]);
    if (!check_trace_correct(correct_trace, correct_trace_length)) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
