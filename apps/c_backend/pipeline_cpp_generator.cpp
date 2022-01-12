#include "Halide.h"

namespace {

using namespace Halide;
using Internal::Call;

// TODO: this is using Halide::Internal, which is naughty for apps outside of Halide proper.
// We should find (or add) a way to do this properly, or move this apps into internal tests.
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

class PipelineCpp : public Halide::Generator<PipelineCpp> {
public:
    Input<Buffer<uint16_t, 2>> input{"input"};
    Output<Buffer<uint16_t, 2>> output{"output"};

    void generate() {
        Var x, y;

        assert(get_target().has_feature(Target::CPlusPlusMangling));

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

        output(x, y) = cast<uint16_t>(add_all_the_things);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(PipelineCpp, pipeline_cpp)
