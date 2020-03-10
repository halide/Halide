#include "Halide.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdio.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("Skipping test for WebAssembly as it does not support ExternalCode::bitcode_wrapper().\n");
        return 0;
    }

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = 42;

    Target target = get_jit_target_from_environment();

    std::string bitcode_file = Internal::get_test_tmp_dir() + "extern.bc";
    f.compile_to_bitcode(bitcode_file, {}, "extern", target);

    std::vector<uint8_t> bitcode;
    std::ifstream bitcode_stream(bitcode_file, std::ios::in | std::ios::binary);
    bitcode_stream.seekg(0, std::ios::end);
    bitcode.resize(bitcode_stream.tellg());
    bitcode_stream.seekg(0, std::ios::beg);
    bitcode_stream.read(reinterpret_cast<char *>(&bitcode[0]), bitcode.size());

    ExternalCode external_code =
        ExternalCode::bitcode_wrapper(target, bitcode, "extern");

    Func f_extern;
    f_extern.define_extern("extern", {}, type_of<int32_t>(), 2);

    Func result;
    result(x, y) = f_extern(x, y);

    Module module = result.compile_to_module({}, "forty_two", target);

    module.append(external_code);

    auto forty_two = module.get_function_by_name("forty_two");

    Internal::JITModule jit_module(module, forty_two, {});

    auto main_function = (int (*)(halide_buffer_t * buf)) jit_module.main_function();
    Buffer<int32_t> buf(16, 16);

    int ret_code = main_function(buf.raw_buffer());

    assert(ret_code == 0);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            assert(buf(i, j) == 42);
        }
    }

    printf("Success!\n");
    return 0;
}
