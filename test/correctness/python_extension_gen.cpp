#include <stdio.h>
#include <iostream>

#include "Halide.h"

#include "test/common/halide_test_dirs.h"

using namespace Halide;

void print_file(const std::string &name) {
    printf("Contents of file %s\n<-----\n", name.c_str());
    FILE *f = fopen(name.c_str(), "rb");
    for (int in_char = getc(f); in_char != EOF; in_char = getc(f)) {
        putchar(in_char);
    }
    fclose(f);
    printf("----->\n");
}

int main(int argc, char **argv) {
    Param<int8_t> offset;
    ImageParam input(Int(32), 1, "input");
    Var x("x");
    Func f("f");
    f(x) = input(x) + offset;

    Target t = get_target_from_environment();
    t.set_feature(Target::CPlusPlusMangling);

    std::string test_dir = Internal::get_test_tmp_dir() + "halide_python";

    f.compile_to_python_extension(test_dir, { input, offset }, "org::halide::halide_python::f", t);

    std::string python_file = test_dir + ".py.c";
    Internal::assert_file_exists(python_file);

    if (argc > 1) {
        print_file(python_file);
    }

    printf("Success!\n");

    exit(0);
}
