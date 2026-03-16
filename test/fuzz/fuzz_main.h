#ifndef HALIDE_BACKEND_STDLIB_MAIN_H
#define HALIDE_BACKEND_STDLIB_MAIN_H

namespace Halide {
class FuzzingContext;
using FuzzFunction = int (*)(FuzzingContext &);

int fuzz_main(int argc, char **argv, FuzzFunction main_fn);
}  // namespace Halide

#endif  // HALIDE_BACKEND_STDLIB_MAIN_H
