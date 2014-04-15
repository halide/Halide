#include <Halide.h>
#include <stdio.h>

// The check has to go in the Halide namespace, because get_source_location looks for the first thing outside of it
namespace Halide {
void check(const void *var, const std::string &type,
           const std::string &correct_name,
           const std::string &correct_file, int line) {
    std::string correct_loc = correct_file + ":" + Halide::Internal::int_to_string(line);
    std::string loc = Halide::Internal::get_source_location();
    std::string name = Halide::Internal::get_variable_name(var, type);

    if (name != correct_name) {
        printf("Mispredicted name: %s vs %s\n",
               name.c_str(), correct_name.c_str());
        exit(-1);
    }

    if (loc != correct_loc) {
        printf("Mispredicted source location: %s vs %s\n",
               loc.c_str(), correct_loc.c_str());
        exit(-1);
    }
}
}

using Halide::check;

namespace Foo {



namespace {
struct Bar {
    typedef int bint;
    bint bar_int;
    Bar(int x) : bar_int(x) {
        check(this, "Foo::{anonymous}::Bar", "b", __FILE__, __LINE__);
        check(&bar_int, "Foo::{anonymous}::Bar::bint", "b.bar_int", __FILE__, __LINE__);
    }
    ~Bar() {
        check(this, "Foo::{anonymous}::Bar", "b", __FILE__, __LINE__);
        check(&bar_int, "Foo::{anonymous}::Bar::bint", "b.bar_int", __FILE__, __LINE__);
    }
    int get() {
        return bar_int * 2;
    }
};

int g(int x) {
    Bar b(x*7);
    return b.get();
}

}



int f(int x) {
    int y = g(x) + g(x-1);
    check(&y, "int", "y", __FILE__, __LINE__);
    return y - 1;
}

}

int main(int argc, char **argv) {
    bool result = HalideIntrospectionCanary::test();

    if (result) {
        printf("Halide C++ introspection claims to be working with this build config\n");
    } else {
        printf("Halide C++ introspection doesn't claim to work with this build config. Not continuing.\n");
        return 0;
    }

    printf("Continuing with further tests...\n");

    Foo::f(17);

    // Make sure it works all the way up to main
    int secret_int = 5;
    check(&secret_int, "int", "secret_int", __FILE__, __LINE__);

    // Make sure it rejects heap variables
    int *on_the_heap = new int;
    check(on_the_heap, "int", "", __FILE__, __LINE__);
    delete on_the_heap;

    // Make sure it works for arrays.
    float an_array[17];
    check(&an_array[5], "float", "an_array[5]", __FILE__, __LINE__);

    printf("Success!\n");

    return 0;
}
