#pragma once
#include <Halide.h>

namespace {

Halide::AutoschedulerParams get_autoscheduler_params(const bool using_experimental_gpu_schedule) {
    return {
        "Mullapudi2016",                                                              //
        {{"experimental_gpu_schedule", using_experimental_gpu_schedule ? "1" : "0"}}  //
    };
}

}  // namespace