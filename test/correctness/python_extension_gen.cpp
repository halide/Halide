#include <stdio.h>
#include <iostream>

#include "Halide.h"

#include "test/common/halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam buffer_u8(UInt(8), 2, "buffer_u8");
    ImageParam buffer_u16(UInt(16), 2, "buffer_u16");
    ImageParam buffer_u32(UInt(32), 2, "buffer_u32");
    ImageParam buffer_i8(Int(8), 2, "buffer_i8");
    ImageParam buffer_i16(Int(16), 2, "buffer_i16");
    ImageParam buffer_i32(Int(32), 2, "buffer_i32");
    Param<int8_t> int_param8;
    Param<int16_t> int_param16;
    Param<int32_t> int_param32;
    Param<int64_t> int_param64;
    Param<uint8_t> uint_param8;
    Param<uint16_t> uint_param16;
    Param<uint32_t> uint_param32;
    Param<uint64_t> uint_param64;
    Param<float> float_param;
    Param<double> double_param;

    std::vector<Argument> params{
      buffer_u8, buffer_u16, buffer_u32,
      buffer_i8, buffer_i16, buffer_i32,
      int_param8, int_param16, int_param32, int_param64,
      uint_param8, uint_param16, uint_param32, uint_param64,
      float_param, double_param};

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = buffer_u8(x, y) + int_param8;

    Target t = get_target_from_environment();
    t.set_feature(Target::CPlusPlusMangling);

    std::string pyext_filename = Internal::get_test_tmp_dir() + "halide_python.py.c";
    std::string header_filename = Internal::get_test_tmp_dir() + "halide_python.h";
    std::string c_filename = Internal::get_test_tmp_dir() + "halide_python.cc";

    f.compile_to_c(c_filename, params,
                   "org::halide::halide_python::f", t);
    f.compile_to_header(header_filename, params,
                   "org::halide::halide_python::f", t);
    f.compile_to_python_extension(pyext_filename, params,
                   "org::halide::halide_python::f", t);

    Internal::assert_file_exists(header_filename);
    Internal::assert_file_exists(c_filename);
    Internal::assert_file_exists(pyext_filename);

    exit(0);
}
