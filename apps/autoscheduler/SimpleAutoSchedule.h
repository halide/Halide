#ifndef HALIDE_SIMPLE_AUTO_SCHEDULE_H
#define HALIDE_SIMPLE_AUTO_SCHEDULE_H

/** \file
 *  A less sophisticated automatic scheduler (compare to AutoSchedule)
 *  It inlines some trivial and element-wise functions (as in AutoSchedule),
 *  tiles on the rest and parallelize.
 *  It also recognize large reduction and try to rfactor() to increase parallelism.
 *  In addition it supports GPU scheduling.
 */

#include "Halide.h"

#include <string>
#include <vector>
#include <map>
#include <set>

namespace Halide {

struct SimpleAutoscheduleOptions {
    bool gpu = false;
    int cpu_tile_width = 16;
    int cpu_tile_height = 16;
    int gpu_tile_width = 16;
    int gpu_tile_height = 16;
    int gpu_tile_channel = 4;
    int unroll_rvar_size = 0;
};

/**
 *  Given one or more Funcs, 
 *  and an estimation of the values of the variable parameters (e.g. bounds
 *  of the inputs if you're compiling in a generator) and
 *  function bounds (in {min, max}), automatically schedule all the dependencies.
 */
void simple_autoschedule(std::vector<Func> &outputs,
                         const std::map<std::string, Expr> &parameters,
                         const std::vector<std::vector<std::pair<int, int>>> &output_bounds,
                         const SimpleAutoscheduleOptions &options = SimpleAutoscheduleOptions());
void simple_autoschedule(Func &output,
                         const std::map<std::string, Expr> &parameters,
                         const std::vector<std::pair<int, int>> &output_bounds,
                         const SimpleAutoscheduleOptions &options = SimpleAutoscheduleOptions());

namespace Internal {

void simple_autoschedule_test();

}

}

#endif
