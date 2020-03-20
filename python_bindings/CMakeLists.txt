find_package(pybind11 REQUIRED)

if (APPLE)
  set(HALIDE_PY_LINK_FLAG "-Wl,-undefined,dynamic_lookup")
elseif (NOT MSVC)
  set(HALIDE_PY_LINK_FLAG "-rdynamic")
endif()

if (NOT HALIDE_ENABLE_RTTI)
  if (NOT MSVC)
    set(HALIDE_PY_RTTI_FLAG "-fno-rtti")
  else()
    set(HALIDE_PY_RTTI_FLAG "/GR-")
  endif()
endif()

add_subdirectory(src)

if (WITH_TESTS)
  include(test.cmake)
endif()
