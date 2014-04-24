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

int global_int = 7;

struct SomeStruct {
    int global_struct_a;
    int global_struct_b;
    static float static_float;
    static double static_member_double_array[17];
    static struct SubStruct {
        int a;
    } substruct;
} global_struct;

float SomeStruct::static_float = 3.0f;
double SomeStruct::static_member_double_array[17] = {0};
SomeStruct::SubStruct SomeStruct::substruct = {0};

float global_array[7];

namespace Foo {

int global_int_in_foo = 8;

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
    static float static_float_in_f = 0.3f;
    int y = g(x) + g(x-1);
    check(&y, "int", "y", __FILE__, __LINE__);
    check(&static_float_in_f, "float", "static_float_in_f", __FILE__, __LINE__);
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

    // Check what happens with lexical blocks which may reuse stack positions
    {
        int block_a = 3;
        (void)block_a;
        check(&block_a, "int", "block_a", __FILE__, __LINE__);
    }

    {
        int block_b = 3;
        (void)block_b;
        check(&block_b, "int", "block_b", __FILE__, __LINE__);
    }

    {
        int block_c = 3;
        (void)block_c;
        check(&block_c, "int", "block_c", __FILE__, __LINE__);
    }

    // Check we can name globals
    check(&global_int, "int", "global_int", __FILE__, __LINE__);
    check(&Foo::global_int_in_foo, "int", "Foo::global_int_in_foo", __FILE__, __LINE__);

    // Check we can name members of globals
    check(&global_struct.global_struct_a, "int", "global_struct.global_struct_a", __FILE__, __LINE__);
    check(&global_struct.global_struct_b, "int", "global_struct.global_struct_b", __FILE__, __LINE__);

    check(&global_array[4], "float", "global_array[4]", __FILE__, __LINE__);

    check(&SomeStruct::static_float, "float", "SomeStruct::static_float", __FILE__, __LINE__);

    check(&SomeStruct::static_member_double_array[5], "double", "SomeStruct::static_member_double_array[5]", __FILE__, __LINE__);

    check(&SomeStruct::substruct.a, "int", "SomeStruct::substruct.a", __FILE__, __LINE__);

    printf("Success!\n");

    return 0;
}
