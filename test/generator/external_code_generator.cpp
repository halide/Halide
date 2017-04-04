#include "Halide.h"

extern "C" unsigned char halide_internal_external_code_extern_32[];
extern "C" int halide_internal_external_code_extern_32_length;
extern "C" unsigned char halide_internal_external_code_extern_64[];
extern "C" int halide_internal_external_code_extern_64_length;

namespace {

class ExternalCode : public Halide::Generator<ExternalCode> {
public:
    Input<Buffer<int32_t>> input{ "input", 2 };
    Output<Buffer<float>> output{ "output", 2 };
    HalidePureExtern_1(float, gen_extern_tester, float);
 
    void generate() {
        Var x("x"), y("y");
        Func f("f");

        Target target = get_target();
        unsigned char *code;
        int code_length;

        if (target.bits == 64) {
            code = halide_internal_external_code_extern_64;
            code_length = halide_internal_external_code_extern_64_length;
        } else {
            code = halide_internal_external_code_extern_32;
            code_length = halide_internal_external_code_extern_32_length;
        }
        std::vector<uint8_t> code_vector(code, code + code_length);
        get_externs_map()->insert({"org.halide-lang.extern_code_extern",
              Halide::ExternalCode::bitcode_wrapper(target, code_vector, "org.halide-lang.extern_code_extern")});
        
        output(x, y) = gen_extern_tester(cast<float>(input(x, y)));
    }

    void schedule() {
    }
};

Halide::RegisterGenerator<ExternalCode> register_my_gen{"external_code"};

}  // namespace
