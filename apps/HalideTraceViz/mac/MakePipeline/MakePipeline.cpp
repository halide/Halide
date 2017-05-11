//
//  main.cpp
//  MakePipeline
//
//  Created by Jaime Rios on 2016-06-08.
//  Copyright © 2016 mPerpetuo, inc. All rights reserved.
//
//  Code inspired from Halide tutorial, at:
//       http://halide-lang.org/tutorials/tutorial_lesson_10_aot_compilation_generate.html

#include <iostream>
#include "Halide.h"

int main(int argc, const char * argv[])
{
    auto x      = Halide::Var{"x"};
    auto y      = Halide::Var{"y"};
    auto input  = Halide::ImageParam(Halide::type_of<uint8_t>(), 2, std::string{"input"});
    auto offset = Halide::Param<uint8_t>{"offset"};
    
    auto brighten = Halide::Func{"output"};
    brighten(x, y) = input(x, y) + offset;
    
    //brighten.compute_root();
    brighten.vectorize(x, 16).parallel(y);
    
    auto args  = std::vector<Halide::Argument>{input, offset};
    
    brighten.compile_to_file("brighten", args);
    
    return 0;
}
