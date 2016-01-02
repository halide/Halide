// This file should be auto-generated in the future
#include "my_library.h"

#include <stdio.h>

using namespace Halide;

namespace my_library {

Func vignette(Func in, Expr center_x, Expr center_y, Expr radius) {
    assert(center_x.type() == Float(32) &&
           center_y.type() == Float(32) &&
           radius.type() == Float(32));
    assert(in.output_types().size() == 1 &&
           in.output_types()[0] == Float(32));
    Func f;
    f.define_extern("vignette_impl", {in, center_x, center_y, radius}, Float(32), 2);
    return f;
}

Func flip(Func in, Expr total_width) {
    assert(total_width.type() == Int(32));
    assert(in.output_types().size() == 1 &&
           in.output_types()[0] == Float(32));
    Func f;
    f.define_extern("flip_impl", {in, total_width}, Float(32), 2);
    return f;
}

}
