// TODO(https://github.com/halide/Halide/issues/7248):
//
// For now, we must build the webgpu runtime two ways:
// - once for the native API (Dawn)
// - once for the Emscripten API (Chrome)
//
// Once the API discrepancies are resolved we can
// go back to building this in a sane manner, but for now,
// we use this sad-but-effective approach.

#define HALIDE_RUNTIME_WEBGPU_NATIVE_API 0
#include "webgpu.cpp"
