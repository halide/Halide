// Tracy glue -- Marcos Slomp (@slomp)

#ifndef TRACY_PROFILER_GLUE_H
#define TRACY_PROFILER_GLUE_H
// clang-format off

#if TRACY_GLUE_ENABLE
    #ifndef TRACY_ENABLE
    #define TRACY_ENABLE
    #endif
    // TRACY_GLUE_PATH:
    // - must NOT be enclosed with quotes
    // - must use only forward slashes
    // - must end in a forward slash
    #ifndef TRACY_GLUE_PATH
    #define TRACY_GLUE_PATH <my-path-to-tracy>
    #endif
#else
    #ifdef TRACY_ENABLE
    #undef TRACY_ENABLE
    #endif
#endif

#ifdef TRACY_ENABLE

#ifndef TRACY_GLUE_PATH
#define TRACY_GLUE_PATH
#endif // TRACY_GLUE_PATH

#define TRACY_GLUE_STRINGIFY(x) #x
#define TRACY_GLUE_PARROT(...) __VA_ARGS__
#define TRACY_GLUE_CONCAT(path,file) TRACY_GLUE_PARROT(path)TRACY_GLUE_PARROT(file)
#define TRACY_GLUE_INCLUDE_STRING(file) TRACY_GLUE_STRINGIFY(file)
#define TRACY_GLUE_INCLUDE(path, file) TRACY_GLUE_INCLUDE_STRING( TRACY_GLUE_CONCAT(path, file) )
#define TRACY_GLUE_INCLUDE_FILE(file) TRACY_GLUE_INCLUDE(TRACY_GLUE_PATH, file)

#include TRACY_GLUE_INCLUDE_FILE(public/tracy/Tracy.hpp)

#ifdef TRACY_GLUE_ENABLE_VULKAN
#include TRACY_GLUE_INCLUDE_FILE(TracyVulkan.hpp)
#endif

#ifdef TRACY_GLUE_ENABLE_D3D11
#include TRACY_GLUE_INCLUDE_FILE(TracyD3D11.hpp)
#endif

#ifdef TRACY_GLUE_ENABLE_D3D12
#include TRACY_GLUE_INCLUDE_FILE(TracyD3D12.hpp)
#endif

#ifdef TRACY_GLUE_ENABLE_OPENGL
#include TRACY_GLUE_INCLUDE_FILE(TracyOpenGL.hpp)
#endif

#ifdef TRACY_GLUE_IMPLEMENTATION
#include TRACY_GLUE_INCLUDE_FILE(public/TracyClient.cpp)
#endif

#undef TRACY_GLUE_INCLUDE_FILE
#undef TRACY_GLUE_INCLUDE
#undef TRACY_GLUE_INCLUDE_STRING
#undef TRACY_GLUE_CONCAT
#undef TRACY_GLUE_PARROT
#undef TRACY_GLUE_STRINGIFY

// NOTE(marcos): custom Tracy Zones (shortcuts)

#define TracyThreadName(name) tracy::SetThreadName(name)

#define ZoneString(s)                                      \
    std::string tracy_zone_text_##__LINE__ = std::move(s); \
    ZoneText((tracy_zone_text_##__LINE__).c_str(), (tracy_zone_text_##__LINE__).size())

#define ZoneTextL(s) ZoneText(((s) ? (s) : ""), ((s) ? strlen(s) : 0))
#define ZoneTextVL(z, s) ZoneTextV(z, ((s) ? (s) : ""), ((s) ? strlen(s) : 0))

#define TracyPulse(name, baseline, pulse) \
    TracyPlot(name, baseline);            \
    TracyPlot(name, pulse);               \
    TracyPlot(name, baseline);

#define TracyTick(name, from, to) \
    TracyPlot(name, from);        \
    TracyPlot(name, to);

#define TracyOnly(...) __VA_ARGS__

#define ZoneTransientArgs(name, stackdepth, active) \
    __LINE__, __FILE__, strlen(__FILE__), __FUNCTION__, strlen(__FUNCTION__), name, strlen(name), stackdepth, active

#else // TRACY_ENABLE

#define TracyNoOp ((void)0)

#define ZoneScoped
#define ZoneScopedS(...)
#define ZoneScopedNC(...)
#define ZoneScopedN(...)
#define ZoneScopedC(...)
#define ZoneNamedNC(...)
#define ZoneNamedN(...)
#define ZoneValue(...)
#define ZoneColor(...)
#define ZoneName(...)
#define ZoneText(...)
#define ZoneTransientN(...)
#define ZoneColorV(...) TracyNoOp

#define TracyPlot(...)
#define TracyMessage(...) TracyNoOp
#define TracyMessageC(...) TracyNoOp
#define TracyMessageL(...) TracyNoOp
#define TracyMessageLC(...) TracyNoOp

#define FrameMark
#define FrameMarkNamed(...)

// Vulkan
#define TracyVkZone(...)
#define TracyVkZoneC(...)
#define TracyVkCollect(...)

// D3D11
#define TracyD3D11Zone(...)
#define TracyD3D11Collect(...)

// OpenGL
#define TracyGpuZone(...)
#define TracyGpuCollect

// Shortcuts
#define TracyThreadName(...) TracyNoOp
#define ZoneString(...)
#define ZoneTextL(...)
#define ZoneTextVL(...)
#define TracyPulse(...)
#define TracyTick(...)
#define TracyOnly(...)
#define ZoneTransientArgs(...)

#endif // TRACY_ENABLE

// clang-format on
#endif // TRACY_PROFILER_GLUE_H
