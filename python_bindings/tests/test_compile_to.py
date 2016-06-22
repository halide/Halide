#!/usr/bin/python3

import os.path

import halide as h

def main():
    x = h.Var("x")

    f = h.Func("f")

    f[x] = 100 * x

    args = h.ArgumentsVector()

    f.compile_to_bitcode("f.bc", args, "f")
    assert os.path.isfile("f.bc")

    f.compile_to_c("f.cpp", args, "f")
    assert os.path.isfile("f.cpp")

    f.compile_to_object("f.o", args, "f")
    assert os.path.isfile("f.o")

    f.compile_to_header("f.h", args, "f")
    assert os.path.isfile("f.h")

    f.compile_to_assembly("f.S", args, "f")
    assert os.path.isfile("f.S")

    f.compile_to_lowered_stmt("f.txt", args)
    assert os.path.isfile("f.txt")

    f.compile_to_file("f_all", args)
    assert os.path.isfile("f_all.h")
    assert os.path.isfile("f_all.o")

    print("Success!")

    return 0

if __name__ == "__main__":
    main()
