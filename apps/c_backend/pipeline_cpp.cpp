#include "Halide.h"

using namespace Halide;
using Internal::Call;

Expr make_call_cpp_extern_toplevel(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "cpp_extern_toplevel", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}

Expr make_call_cpp_extern_namespace(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace1::cpp_extern", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}

Expr make_call_cpp_extern_shared_namespace_1(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace2::cpp_extern_1", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_shared_namespace_2(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace2::cpp_extern_2", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_shared_namespace_3(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace2::cpp_extern_3", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}

Expr make_call_cpp_extern_nested_namespace_outer(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_outer::cpp_extern", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_nested_namespace_inner(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_outer::namespace_inner::cpp_extern", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}

Expr make_call_cpp_extern_shared_nested_1(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_shared_outer::cpp_extern_1", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_shared_nested_2(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_shared_outer::cpp_extern_2", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_shared_nested_3(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_shared_outer::inner::cpp_extern_1", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}
Expr make_call_cpp_extern_shared_nested_4(Expr arg1, Expr arg2) {
    return Call::make(type_of<int>(), "namespace_shared_outer::inner::cpp_extern_2", {cast<int>(arg1), cast<float>(arg2)}, Call::ExternCPlusPlus);
}

// Make sure extern "C" works
HalideExtern_2(int, an_extern_c_func, int, float);

int main(int argc, char **argv) {
    Func f;
    ImageParam input(UInt(16), 2);
    Var x, y;

    Expr add_all_the_things = cast<int>(0);
    add_all_the_things += make_call_cpp_extern_toplevel(input(x, y), x + y);

    add_all_the_things += make_call_cpp_extern_namespace(input(x, y), x + y);

    add_all_the_things += make_call_cpp_extern_shared_namespace_1(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_shared_namespace_2(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_shared_namespace_3(input(x, y), x + y);

    add_all_the_things += make_call_cpp_extern_nested_namespace_outer(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_nested_namespace_inner(input(x, y), x + y);

    add_all_the_things += make_call_cpp_extern_shared_nested_1(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_shared_nested_2(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_shared_nested_3(input(x, y), x + y);
    add_all_the_things += make_call_cpp_extern_shared_nested_4(input(x, y), x + y);

    add_all_the_things += an_extern_c_func(cast<int32_t>(input(x, y)), cast<float>(x + y));

    f(x, y) = cast<uint16_t>(add_all_the_things);

    std::vector<Argument> args;
    args.push_back(input);

    Target t = get_host_target();
    t.set_feature(Target::CPlusPlusMangling);

    f.compile_to_header("pipeline_cpp_native.h", args, "pipeline_cpp_native", t);
    f.compile_to_header("pipeline_cpp_cpp.h", args, "pipeline_cpp_cpp", t);
    f.compile_to_object("pipeline_cpp_native.o", args, "pipeline_cpp_native", t);
    f.compile_to_c("pipeline_cpp_cpp.cpp", args, "pipeline_cpp_cpp", t);
    return 0;
}
