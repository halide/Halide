// NOTE(marcos): this file is a trivial wrapper around "windows_clock.cpp"
// this wrapper is used as a mean to "decorate" the module such that the proper
// build flags and target triple can be determined based on the prefix (windows_)
// and suffix (_x86) when building the runtime module.

#include "windows_clock.cpp"
