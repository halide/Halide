// TODO(https://github.com/halide/Halide/issues/7248):
//
// For now, we must build the webgpu runtime two ways:
// - once for the native API (Dawn)
// - once for the Emscripten API (Chrome)
//
// Once the API discrepancies are resolved we can
// go back to building this in a sane manner, but for now,
// we use this sad-but-effective approach.

#ifndef BITS_64
// Don't emit compiler messages (via #pragma message) here: some environments might
// treat compiler messages as warnings, which would then be escalated to errors since
// we generally build with warnings-as-errors enabled.
#else
#define HALIDE_RUNTIME_WEBGPU_NATIVE_API 1
#include "webgpu.cpp"
#endif
