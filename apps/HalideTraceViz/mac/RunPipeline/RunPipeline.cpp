//
//  main.cpp
//  RunPipeline
//
//  Created by Jaime Rios on 2016-06-08.
//  Copyright © 2016 mPerpetuo, inc. All rights reserved.
//
//  Code inspired from Halide tutorial, at:
//    http://halide-lang.org/tutorials/tutorial_lesson_10_aot_compilation_run.html


#include <cassert>
#include <iostream>
#include <vector>
#include "brighten.h"

int main(int argc, const char * argv[])
{
    std::cout << "Run pipeline\n";
    
    const auto width  = 16 * 4;
    const auto height = 16 * 4;
    
    auto input = std::vector<uint8_t>(width*height, 0);
    
    for (auto y = 0; y < width; y++)
    {
        for (auto x = 0; x < height; x++)
        {
            auto val = x ^ (y + 1);
            const auto index = y * height + x;
            input[index] = val;
        }
    }
    
    auto output = std::vector<uint8_t>(width*height, 0);
    assert(output.size() != 0);
    
    auto input_buf  = buffer_t{0};
    auto output_buf = buffer_t{0};
    
    input_buf.host  = input.data();
    output_buf.host = output.data();
    
    input_buf.stride[0] = output_buf.stride[0] = 1;
    
    input_buf.stride[1] = output_buf.stride[1] = width;
    
    input_buf.extent[0] = output_buf.extent[0] = width;
    input_buf.extent[1] = output_buf.extent[1] = height;
    
    input_buf.elem_size = output_buf.elem_size = 1;
    
    auto offset = 1;
    auto error = brighten(&input_buf, offset, &output_buf);
    (void)error;
    
    return 0;
}
