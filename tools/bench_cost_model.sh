set -e
make -j bin/host/runtime.a bin/builtin_pipelines.generator

HL_DEBUG_CODEGEN=0 ./bin/builtin_pipelines.generator \
    -g halide_autoscheduler_train_cost_model \
    -e assembly,static_library,h,stmt,html \
    -o tmp \
    target=x86-64-avx2-no_runtime-debug-profile

g++ -std=c++11 \
    -I tmp -I include \
    tools/RunGenMain.cpp \
    tools/RunGenStubs.cpp \
    tmp/halide_autoscheduler_train_cost_model.a \
    bin/host/runtime.a \
    -DHL_RUNGEN_FILTER_HEADER='"halide_autoscheduler_train_cost_model.h"' \
    -lpng -lz -lpthread -ldl -ljpeg \
    -o bench_train_cost_model

./bench_train_cost_model --benchmarks=all \
    --benchmark_min_time=1 \
    --default_input_buffers=random:0:estimate \
    --default_input_scalars=estimate \
    --output_extents=estimate \
    --verbose

