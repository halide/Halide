#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// The check has to go in the Halide namespace, because get_source_location looks for the first thing outside of it
namespace Halide {
bool paths_equal(const std::string &path1, const std::string &path2) {
    bool one_is_first = path1.size() >= path2.size();
    const std::string &first(one_is_first ? path1 : path2);
    const std::string &second(one_is_first ? path2 : path1);

    if (first.empty() || first.size() == second.size()) {
        return first == second;
    }
    size_t length_delta = first.size() - second.size();
    char sep = first[length_delta - 1];
    return (sep == '/' || sep == '\\') &&
           first.substr(length_delta) == second;
}

void check(const void *var, const std::string &type,
           const std::string &correct_name,
           const std::string &correct_file, int line) {
    std::string correct_loc = correct_file + ":" + std::to_string(line);
    std::string loc = Halide::Internal::Introspection::get_source_location();
    std::string name = Halide::Internal::Introspection::get_variable_name(var, type);

    if (!paths_equal(correct_name, name)) {
        printf("Mispredicted name: %s vs %s\n",
               name.c_str(), correct_name.c_str());
        exit(1);
    }

    if (!paths_equal(loc, correct_loc)) {
        printf("Mispredicted source location: %s vs %s\n",
               loc.c_str(), correct_loc.c_str());
        exit(1);
    }
}
}  // namespace Halide

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
    Bar(int x)
        : bar_int(x) {
    }
    ~Bar() {
    }
    void check_bar() {
        check(this, "Foo::_::Bar", "b", __FILE__, __LINE__);
        check(&bar_int, "Foo::_::Bar::bint", "b.bar_int", __FILE__, __LINE__);
    }
    int get() {
        return bar_int * 2;
    }
};

int g(int x) {
    Bar b(x * 7);
    b.check_bar();
    return b.get();
}

}  // namespace

int f(int x) {
    static float static_float_in_f = 0.3f;
    int y = g(x) + g(x - 1);
    check(&y, "int", "y", __FILE__, __LINE__);
    check(&static_float_in_f, "float", "static_float_in_f", __FILE__, __LINE__);
    return y - 1;
}

}  // namespace Foo

typedef float fancy_float;

struct HeapObject {
    float f;
    fancy_float f2;
    int i;
    struct {
        char c;
        double d;
        int i_array[20];
    } inner;
    HeapObject *ptr;
    struct inner2 {
        int a[5];
    };
    inner2 inner2_array[10];
};

int main(int argc, char **argv) {
    bool result = HalideIntrospectionCanary::test(&HalideIntrospectionCanary::test_a);

    if (result) {
        printf("Halide C++ introspection claims to be working with this build config\n");
    } else {
        printf("[SKIP] Halide C++ introspection doesn't claim to work with this build config.\n");
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

    // .. unless they're members of explicitly registered objects
    HeapObject *obj = new HeapObject;
    static HeapObject *dummy_heap_object_ptr = nullptr;
    check(&dummy_heap_object_ptr, "HeapObject \\*", "dummy_heap_object_ptr", __FILE__, __LINE__);
    Halide::Internal::Introspection::register_heap_object(obj, sizeof(HeapObject), &dummy_heap_object_ptr);
    check(&(obj->f), "float", "f", __FILE__, __LINE__);
    check(&(obj->f2), "fancy_float", "f2", __FILE__, __LINE__);
    check(&(obj->f2), "float", "f2", __FILE__, __LINE__);
    check(&(obj->i), "int", "i", __FILE__, __LINE__);
    check(&(obj->inner.c), "char", "inner.c", __FILE__, __LINE__);
    check(&(obj->inner.d), "double", "inner.d", __FILE__, __LINE__);
    check(&(obj->ptr), "HeapObject \\*", "ptr", __FILE__, __LINE__);
    // TODO:
    check(&(obj->inner.i_array[10]), "int", "inner.i_array[10]", __FILE__, __LINE__);
    check(&(obj->inner2_array[4].a[2]), "int", "inner2_array[4].a[2]", __FILE__, __LINE__);
    check(&(obj->inner.i_array), "int .20.", "inner.i_array", __FILE__, __LINE__);
    Halide::Internal::Introspection::deregister_heap_object(obj, sizeof(HeapObject));
    delete obj;

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
    check(&global_struct, "SomeStruct", "global_struct", __FILE__, __LINE__);
    check(&global_struct.global_struct_a, "int", "global_struct.global_struct_a", __FILE__, __LINE__);
    check(&global_struct.global_struct_b, "int", "global_struct.global_struct_b", __FILE__, __LINE__);

    check(&global_array[4], "float", "global_array[4]", __FILE__, __LINE__);

    check(&SomeStruct::static_float, "float", "SomeStruct::static_float", __FILE__, __LINE__);

    check(&SomeStruct::static_member_double_array[5], "double", "SomeStruct::static_member_double_array[5]", __FILE__, __LINE__);

    check(&SomeStruct::substruct.a, "int", "SomeStruct::substruct.a", __FILE__, __LINE__);

    // Check that we can query front-end objects for their source locations
    {
        std::string loc;
        Func f;
        Var x;
        f(x) = x;
        loc = std::string(__FILE__) + ":" + std::to_string(__LINE__ - 1);
        assert(paths_equal(f.source_location(), loc));

        f(x) += 1;
        loc = std::string(__FILE__) + ":" + std::to_string(__LINE__ - 1);
        assert(paths_equal(f.update().source_location(), loc));
    }

    printf("Success!\n");

    return 0;
}
