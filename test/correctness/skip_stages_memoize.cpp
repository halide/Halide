#include "Halide.h"
#include <stdio.h>

using namespace Halide;

std::string buffer_name = "";
bool set_toggle = false;

int my_trace(void *user_context, const halide_trace_event *e) {
    if (!set_toggle) {
        if ((e->event == halide_trace_store) && (std::string(e->func) == buffer_name)) {
            printf("set_toggle is false; %s's producer should never have had been executed.\n",
                   buffer_name.c_str());
            exit(-1);
        }
    }
    return 0;
}

int check_correctness(const Image<int> &out, bool toggle) {
    for (int x = 0; x < out.width(); ++x) {
        int correct = 1;
        if (toggle) {
            correct = 2*x;
        }
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return -1;
        }
    }
    return 0;
}

int single_memoize_test(bool toggle_val, int index) {
    buffer_name = "f1_" + std::to_string(index);

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Var x;

    f1(x) = 2*x;
    f2(x) = select(toggle, f1(x), 1);

    f1.compute_root().memoize();

    f2.set_custom_trace(&my_trace);
    f1.trace_stores();

    f2.compile_jit();

    set_toggle = toggle_val;
    toggle.set(set_toggle);
    Image<int> out = f2.realize(10);
    if (check_correctness(out, set_toggle) != 0) {
        return -1;
    }
    return 0;
}

int tuple_memoize_test(bool toggle_val, int index) {
    buffer_name = "f1_" + std::to_string(index);

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Var x, y;

    f1(x, y) = Tuple(2*x, 2*x);
    f2(x, y) = Tuple(select(toggle, f1(x, y)[0], 1),
                     select(toggle, f1(x, y)[1], 1));

    f1.compute_root().memoize();

    f2.set_custom_trace(&my_trace);
    f1.trace_stores();

    f2.compile_jit();

    set_toggle = toggle_val;
    toggle.set(set_toggle);
    Realization out = f2.realize(128, 128);
    Image<int> out0 = out[0];
    Image<int> out1 = out[1];

    if (check_correctness(out0, set_toggle) != 0) {
        return -1;
    }
    if (check_correctness(out1, set_toggle) != 0) {
        return -1;
    }
    return 0;
}

int multiple_memoize_test(bool toggle_val, int index) {
    buffer_name = "f1_" + std::to_string(index);

    Param<bool> toggle;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Func f3("f3_" + std::to_string(index));
    Var x;

    f1(x) = 2*x;
    f2(x) = select(toggle, f1(x), 1);
    f3(x) = select(toggle, f2(x), 1);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f3.set_custom_trace(&my_trace);
    f1.trace_stores();

    f3.compile_jit();

    set_toggle = toggle_val;
    toggle.set(set_toggle);
    Image<int> out = f3.realize(10);
    if (check_correctness(out, set_toggle) != 0) {
        return -1;
    }
    return 0;
}

int multiple_memoize_test2(bool toggle_val, int index) {
    buffer_name = "f1_" + std::to_string(index);

    Param<bool> toggle1, toggle2;
    Func f1("f1_" + std::to_string(index)), f2("f2_" + std::to_string(index));
    Func f3("f3_" + std::to_string(index));
    Var x;

    f1(x) = x;
    f2(x) = x;
    f3(x) = select(toggle1, f1(x), 1) + select(toggle2, f2(x), 0);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f3.set_custom_trace(&my_trace);
    f1.trace_stores();

    f3.compile_jit();

    set_toggle = toggle_val;
    toggle1.set(set_toggle);
    toggle2.set(set_toggle);
    Image<int> out = f3.realize(10);
    if (check_correctness(out, set_toggle) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    /*printf("Running single_memoize_test with toggle set to TRUE\n");
    if (single_memoize_test(true, 0) != 0) {
        return -1;
    }

    printf("Running single_memoize_test with toggle set to FALSE\n");
    if (single_memoize_test(false, 1) != 0) {
        return -1;
    }

    printf("Running tuple_memoize_test with toggle set to TRUE\n");
    if (tuple_memoize_test(true, 0) != 0) {
        return -1;
    }

    printf("Running tuple_memoize_test with toggle set to FALSE\n");
    if (tuple_memoize_test(false, 0) != 0) {
        return -1;
    }*/

    /*printf("Running multiple_memoize_test with toggle set to TRUE\n");
    if (multiple_memoize_test(true, 0) != 0) {
        return -1;
    }*/

    printf("Running multiple_memoize_test with toggle set to FALSE\n");
    if (multiple_memoize_test(false, 0) != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;

}
