include(FetchContent)
FetchContent_Declare(pybind11 GIT_REPOSITORY https://github.com/pybind/pybind11.git GIT_TAG v2.4.3)
FetchContent_MakeAvailable(pybind11)

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
