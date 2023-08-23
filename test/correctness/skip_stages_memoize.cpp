#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int buffer_index = 0;
bool set_toggle1 = false;
bool set_toggle2 = false;

int single_toggle_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (!set_toggle1) {
        std::string buffer_name = "f1_" + std::to_string(buffer_index);
        if ((e->event == halide_trace_store) && (std::string(e->func) == buffer_name)) {
            printf("set_toggle1 is false; %s's producer should never have had been executed.\n",
                   buffer_name.c_str());
            exit(1);
        }
    }
    return 0;
}

int double_toggle_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (!set_toggle1) {
        std::string buffer_name = "f1_" + std::to_string(buffer_index);
        if ((e->event == halide_trace_store) && (std::string(e->func) == buffer_name)) {
            printf("set_toggle1 is false; %s's producer should never have had been executed.\n",
                   buffer_name.c_str());
            exit(1);
        }
    } else if (!set_toggle2) {
        std::string buffer_name = "f2_" + std::to_string(buffer_index);
        if ((e->event == halide_trace_store) && (std::string(e->func) == buffer_name)) {
            printf("set_toggle2 is false; %s's producer should never have had been executed.\n",
                   buffer_name.c_str());
            exit(1);
        }
    }
    return 0;
}

int check_correctness_single(const Buffer<int> &out, bool toggle) {
    for (int x = 0; x < out.width(); ++x) {
        int correct = 1;
        if (toggle) {
            correct = 2 * x;
        }
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return 1;
        }
    }
    return 0;
}

int check_correctness_double(const Buffer<int> &out, bool toggle1, bool toggle2) {
    for (int x = 0; x < out.width(); ++x) {
        int correct;
        if (toggle1 && toggle2) {
            correct = 2 * x;
        } else if (toggle1 && !toggle2) {
            correct = x;
        } else if (!toggle1 && toggle2) {
            correct = x + 1;
        } else {
            correct = 1;
        }
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return 1;
        }
    }
    return 0;
}

int single_memoize_test(int index) {
    buffer_index = index;

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Var x;

    f1(x) = 2 * x;
    f2(x) = select(toggle, f1(x), 1);

    f1.compute_root().memoize();

    f2.jit_handlers().custom_trace = &single_toggle_trace;
    f1.trace_stores();

    f2.compile_jit();

    for (bool toggle_val : {false, true}) {
        set_toggle1 = toggle_val;
        toggle.set(set_toggle1);
        Buffer<int> out = f2.realize({10});
        if (check_correctness_single(out, set_toggle1) != 0) {
            return 1;
        }
    }
    return 0;
}

int tuple_memoize_test(int index) {
    buffer_index = index;

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Var x;

    f1(x) = Tuple(2 * x, 2 * x);
    f2(x) = Tuple(select(toggle, f1(x)[0], 1),
                  select(toggle, f1(x)[1], 1));

    f1.compute_root().memoize();

    f2.jit_handlers().custom_trace = &single_toggle_trace;
    f1.trace_stores();

    f2.compile_jit();

    for (bool toggle_val : {false, true}) {
        set_toggle1 = toggle_val;
        toggle.set(set_toggle1);
        Realization out = f2.realize({128});
        Buffer<int> out0 = out[0];
        Buffer<int> out1 = out[1];

        if (check_correctness_single(out0, set_toggle1) != 0) {
            return 1;
        }
        if (check_correctness_single(out1, set_toggle1) != 0) {
            return 1;
        }
    }
    return 0;
}

int non_trivial_allocate_predicate_test(int index) {
    buffer_index = index;

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Func f3("f3_" + std::to_string(index));
    Var x;

    // Generate allocate f1[...] if toggle
    f1(x) = 2 * x;
    f2(x) = select(toggle, f1(x), 1);
    f3(x) = select(toggle, f2(x), 1);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f3.jit_handlers().custom_trace = &double_toggle_trace;
    f1.trace_stores();
    f2.trace_stores();

    f3.compile_jit();

    for (bool toggle_val : {false, true}) {
        set_toggle1 = toggle_val;
        set_toggle2 = toggle_val;
        toggle.set(set_toggle1);
        Buffer<int> out = f3.realize({10});
        if (check_correctness_single(out, set_toggle1) != 0) {
            return 1;
        }
    }
    return 0;
}

int double_memoize_test(int index) {
    buffer_index = index;

    Param<bool> toggle1, toggle2;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Func f3("f3_" + std::to_string(index));
    Var x;

    f1(x) = x;
    f2(x) = x;
    f3(x) = select(toggle1, f1(x), 1) + select(toggle2, f2(x), 0);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f3.jit_handlers().custom_trace = &double_toggle_trace;
    f1.trace_stores();
    f2.trace_stores();

    f3.compile_jit();

    for (int toggle_val1 = 0; toggle_val1 <= 1; toggle_val1++) {
        for (int toggle_val2 = 0; toggle_val2 <= 1; toggle_val2++) {
            set_toggle1 = toggle_val1;
            set_toggle2 = toggle_val2;
            toggle1.set(set_toggle1);
            toggle2.set(toggle_val2);
            Buffer<int> out = f3.realize({10});
            if (check_correctness_double(out, set_toggle1, set_toggle2) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running single_memoize_test\n");
    if (single_memoize_test(0) != 0) {
        return 1;
    }

    printf("Running tuple_memoize_test\n");
    if (tuple_memoize_test(1) != 0) {
        return 1;
    }

    printf("Running non_trivial_allocate_predicate_test\n");
    if (non_trivial_allocate_predicate_test(2) != 0) {
        return 1;
    }

    printf("Running double_memoize_test\n");
    if (double_memoize_test(3) != 0) {
        return 1;
    }

    Internal::JITSharedRuntime::release_all();

    printf("Success!\n");
    return 0;
}
