#ifndef HALIDE_HEXAGON_PROFILER_H
#define HALIDE_HEXAGON_PROFILER_H

struct halide_profiler_state;

extern "C" int halide_hexagon_remote_poll_profiler_func(int *out);
extern "C" halide_profiler_state *halide_profiler_get_state();

#endif
