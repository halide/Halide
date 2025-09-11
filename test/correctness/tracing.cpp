#include "Halide.h"

#include <gtest/gtest.h>
#include <iomanip>

using namespace Halide;

namespace {
struct event {
    char func;
    int parent_id;
    int event_type;
    int type_code, bits, width, value_index;
    int num_int_args;
    int int_args[4];
    float value[4];
    std::string trace_tag;
};

// Print an event in a human-readable way
std::ostream &operator<<(std::ostream &out, const event &e) {
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
    out << e.event_type << " " << event_types[e.event_type] << " "
        << e.func << "." << e.value_index << "[";
    for (int i = 0; i < e.num_int_args; i++) {
        if (i > 0) {
            out << ", ";
        }
        out << e.int_args[i];
    }
    out << "] [";
    for (int i = 0; i < e.width; i++) {
        if (i > 0) {
            out << ", ";
        }
        out << e.value[i];
    }
    out << "] \"" << e.trace_tag << "\"\n";
    return out;
}

// Print an event in a way suitable for inclusion in source code
struct trace_source {
    event e;
};

std::ostream &operator<<(std::ostream &out, const trace_source &ts) {
    const auto &[e] = ts;
    return out << "{" << static_cast<int>(e.func) << ", " << e.parent_id << ", " << e.event_type << ", "
               << e.type_code << ", " << e.bits << ", " << e.width << ", " << e.value_index << ", " << e.num_int_args << ", "
               << "{" << e.int_args[0] << ", " << e.int_args[1] << ", " << e.int_args[2] << ", " << e.int_args[3] << "}, "
               << std::fixed << "{" << e.value[0] << "f, " << e.value[1] << "f, " << e.value[2] << "f, " << e.value[3] << "f}, "
               << "\"" << e.trace_tag << "\"" << "},\n";
}

// Are two floats nearly equal?
bool float_match(float a, float b) {
    return std::abs(a - b) < 0.001f;
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
            a.trace_tag == b.trace_tag);
}

struct TraceTestContext : JITUserContext {
    std::vector<event> trace;

    TraceTestContext() {
        handlers.custom_trace = custom_trace;
    }

    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *ev) {
        assert(ev->dimensions <= 4 && ev->type.lanes <= 4);
        event e = {};
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
            e.value[i] = static_cast<const float *>(ev->value)[i];
        }
        if (ev->trace_tag) {
            e.trace_tag = ev->trace_tag;
        } else {
            e.trace_tag = "";
        }

        auto *self = static_cast<TraceTestContext *>(ctx);
        self->trace.push_back(e);
        return self->trace.size();
    }
};

void check_trace_correct(const std::vector<event> &recorded_trace, const std::vector<event> &correct_trace) {
    const auto n_recorded = recorded_trace.size();
    const auto n_correct = correct_trace.size();
    for (int i = 0; i < std::max(n_correct, n_recorded); i++) {
        event recorded = i < n_recorded ? recorded_trace[i] : event{};
        event correct = i < n_correct ? correct_trace[i] : event{};
        if (events_match(recorded, correct)) {
            continue;
        }

        constexpr int radius = 2;

        // Uh oh. Maybe it's just a reordered load.
        bool reordered = recorded.event_type == 0 && correct.event_type == 0 && [&] {
            for (int j = std::max(i - radius, 0); j <= std::min(i + radius, (int)n_correct - 1); j++) {
                if (i != j && events_match(recorded, correct_trace[j])) {
                    return true;  // Phew.
                }
            }
            return false;
        }();
        if (reordered) {
            continue;
        }

        testing::Message msg;
        msg << "Traces differ at event " << i << ":\n"
            << "-------------------------------\n"
               "Correct trace:\n";
        for (int j = 0; j < correct_trace.size(); j++) {
            msg << (j == i ? " ===> " : "") << correct_trace[j];
        }
        msg << "-------------------------------\n"
               "Trace encountered:\n";
        for (int j = 0; j < n_recorded; j++) {
            msg << (j == i ? " ===> " : "") << trace_source{recorded_trace[j]};
        }
        FAIL() << msg;
    }
}

class TracingTest : public ::testing::Test {
protected:
    Target target = get_jit_target_from_environment();
    TraceTestContext ctx;

    Buffer<float> input_buf{10};
    ImageParam input{Float(32), 1, "i"};
    Func f{"f"}, g{"g"};

    void SetUp() override {
        input_buf.fill(0.f);
        input.set(input_buf);

        Var x;
        g(x) = Tuple(sin(x * 0.1f), cos(x * 0.1f));
        f(x) = g(x)[0] + g(x + 1)[1] + input(x);

        f.vectorize(x, 4);
        g.store_root().compute_at(f, x);
        g.vectorize(x, 4);
    }
};

}  // namespace

TEST_F(TracingTest, TracePipeline) {
    // Check that Target::TracePipeline works.
    f.realize(&ctx, {10}, target.with_feature(Target::TracePipeline));

    // The golden trace, recorded when this test was written
    std::vector<event> correct_pipeline_trace{
        {102, 0, 8, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {102, 1, 9, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
    };
    check_trace_correct(ctx.trace, correct_pipeline_trace);
}

TEST_F(TracingTest, TraceStoresLoadsRealizations) {
    g.add_trace_tag("g whiz");
    g.trace_stores().trace_loads().trace_realizations();

    f.trace_stores();
    f.trace_realizations();
    f.add_trace_tag("arbitrary data on f");
    // All non-null characters are OK
    f.add_trace_tag("more:arbitrary \xff data on f?");

    input.trace_loads();

    f.realize(&ctx, {10}, target);

    // The golden trace, recorded when this test was written
    std::vector<event> correct_trace{
        {102, 0, 8, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, ""},
        {105, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "func_type_and_dim: 1 2 32 1 1 0 10"},
        {103, 1, 10, 3, 0, 0, 0, 0, {0, 0, 0, 0}, {0.000000f, 0.000000f, 0.000000f, 0.000000f}, "func_type_and_dim: 2 2 32 1 2 32 1 1 0 11"},
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
    check_trace_correct(ctx.trace, correct_trace);
}
