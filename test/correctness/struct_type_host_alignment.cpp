#include "Halide.h"
#include "halide_test_dirs.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace Halide;

// A struct-typed buffer's host pointer is assumed aligned as a C compiler
// aligns the struct (its most aligned naturally aligned field), not to the
// struct's size: struct types are packed, and a record whose size is a
// multiple of the vector width (here 128 bytes) says nothing about where an
// array of them starts. Assuming it did let vector loads from such a buffer
// be emitted as aligned loads (e.g. x86's vmovdqa64), which fault on a
// buffer only aligned for its fields.

int main(int argc, char **argv) {
    Type record = Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 126}});

    struct {
        Type type;
        int alignment;
    } cases[] = {{record, 2},
                 {Type::Struct({{"tag", UInt(8)}, {"v", Float(32), 31}}), 1},
                 {Type::Struct({{"s", Float(32)}, {"q", Int(8), 60}}), 4}};
    for (const auto &c : cases) {
        ImageParam p(c.type, 1);
        if (p.host_alignment() != c.alignment) {
            printf("%s: host_alignment() is %d, expected %d\n",
                   p.name().c_str(), p.host_alignment(), c.alignment);
            return 1;
        }
    }

    // A 64-byte vector of each record's bytes starting at byte 64 of the
    // record: aligned to 64 only if the buffer itself were.
    ImageParam in(record, 1, "in");
    Var x("x");
    RDom r(0, 64);
    Func sum("sum");
    sum(x) = cast<uint8_t>(0);
    sum(x) += field(in(x), "qs")[r + 62];
    sum.update().atomic().vectorize(r);

    std::string file = Internal::get_test_tmp_dir() + "struct_type_host_alignment.ll";
    sum.compile_to_llvm_assembly(file, {in}, "sum",
                                 Target("x86-64-linux-avx512_sapphirerapids-no_asserts-no_runtime"));
    std::ifstream ll(file);
    std::stringstream ss;
    ss << ll.rdbuf();
    std::string ir = ss.str();
    if (ir.find("load <64 x i8>") == std::string::npos) {
        printf("No 64-byte vector load in %s\n", file.c_str());
        return 1;
    }
    for (const char *bad : {"align 64", "align 32", "align 16", "align 8", "align 4"}) {
        size_t pos = ir.find("load <64 x i8>");
        for (; pos != std::string::npos; pos = ir.find("load <64 x i8>", pos + 1)) {
            std::string line = ir.substr(pos, ir.find('\n', pos) - pos);
            if (line.find(bad) != std::string::npos) {
                printf("Over-aligned load from a struct buffer: %s\n", line.c_str());
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
