#include "Halide.h"

extern "C" unsigned char external_code_extern_bitcode_32[];
extern "C" int external_code_extern_bitcode_32_length;
extern "C" unsigned char external_code_extern_bitcode_64[];
extern "C" int external_code_extern_bitcode_64_length;
extern "C" unsigned char external_code_extern_cpp_source[];
extern "C" int external_code_extern_cpp_source_length;

namespace {

class ExternalCode : public Halide::Generator<ExternalCode> {
public:
    GeneratorParam<bool> external_code_is_bitcode{"external_code_is_bitcode", true};
    Input<Buffer<int32_t, 2>> input{"input"};
    Output<Buffer<float, 2>> output{"output"};
    HalidePureExtern_1(float, gen_extern_tester, float);

    void generate() {
        Var x("x"), y("y");
        Func f("f");

        unsigned char *code;
        int code_length;
        const char *name = "org.halide-lang.extern_code_extern";
        if (external_code_is_bitcode) {
            Target target = get_target();
            if (target.bits == 64) {
                code = external_code_extern_bitcode_64;
                code_length = external_code_extern_bitcode_64_length;
            } else {
                code = external_code_extern_bitcode_32;
                code_length = external_code_extern_bitcode_32_length;
            }
            std::vector<uint8_t> code_vector(code, code + code_length);
            get_externs_map()->insert({name,
                                       Halide::ExternalCode::bitcode_wrapper(target, code_vector, name)});
        } else {
            code = external_code_extern_cpp_source;
            code_length = external_code_extern_cpp_source_length;
            std::vector<uint8_t> code_vector(code, code + code_length);
            get_externs_map()->insert({name,
                                       Halide::ExternalCode::c_plus_plus_code_wrapper(code_vector, name)});
        }

        output(x, y) = gen_extern_tester(cast<float>(input(x, y)));
    }

    void schedule() {
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ExternalCode, external_code)
