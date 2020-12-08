// NOTE(marcos): this file is a trivial wrapper around the "core" d3d12 back-end
// located in "d3d12compute.cpp"; this wrapper is used as a mean to "decorate" the
// module such that the proper build flags and target triple can be determined based
// on the prefix (windows_) and suffix (_arm) when building the runtime module.

#ifndef BITS_64
// Don't emit compiler messages (via #pragma message) here: some environments might
// treat compiler messages as warnings, which would then be escalated to errors since
// we generally build with warnings-as-errors enabled.
#else
#define HALIDE_D3D12_PLATFORM Windows_ARM64
#include "d3d12compute.cpp"
#endif
