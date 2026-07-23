Hallmark (HAlide LLM Advanced Research Kit) is Halide-written execution engine for Gemini/Gemma models;
it serves as a testbed for writing efficient ML kernels in Halide.

To build with CMake:

- build and install Halide locally to ${HALIDE_INSTALL}
- cd apps/hallmark
- mkdir build && cd build
- cmake .. -DHalide_DIR=${HALIDE_INSTALL}/lib/cmake/Halide -DCMAKE_BUILD_TYPE=Release
- cd build && ninja (or make)

To run the tests:
- ./build/test/llm_generator_test --model_path=/path/to/model.tflite

To run the benchmarks:
- ./build/test/llm_generator_bench --model_path=/path/to/model.tflite --benchmark_filter=all

