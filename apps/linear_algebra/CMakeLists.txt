cmake_minimum_required(VERSION 3.22)
project(linear_algebra)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Find BLAS-es
set(DEFAULT_BLAS "")
set(BLAS_TARGETS "")
set(BLAS_VENDORS OpenBLAS ATLAS Apple Generic)

# ATLAS is weird and has extra requirements
find_library(CBLAS_LIBRARY cblas)
set(ATLAS_EXTRA_LIBS ${CBLAS_LIBRARY})

message(STATUS "Checking for available CBLAS implementations")
foreach (BLA_VENDOR IN LISTS BLAS_VENDORS)
    find_package(BLAS QUIET)
    if (NOT BLAS_FOUND
        OR ("${BLA_VENDOR}" STREQUAL "ATLAS" AND NOT CBLAS_LIBRARY)
        OR ("${BLA_VENDOR}" STREQUAL "Generic" AND BLAS_TARGETS))
        message(STATUS "${BLA_VENDOR}: Missing")
    else ()
        list(APPEND BLAS_LIBRARIES ${${BLA_VENDOR}_EXTRA_LIBS})

        message(STATUS "${BLA_VENDOR}: Found ${BLAS_LIBRARIES}")
        add_library(BLAS_${BLA_VENDOR} INTERFACE)
        add_library(${BLA_VENDOR}::${BLA_VENDOR} ALIAS BLAS_${BLA_VENDOR})

        target_link_libraries(BLAS_${BLA_VENDOR} INTERFACE ${BLAS_LIBRARIES})
        target_link_options(BLAS_${BLA_VENDOR} INTERFACE ${BLAS_LINKER_FLAGS})
        target_include_directories(BLAS_${BLA_VENDOR} SYSTEM INTERFACE include) # Use CBlas header in our own tree.

        if (NOT DEFAULT_BLAS)
            set(DEFAULT_BLAS ${BLA_VENDOR}::${BLA_VENDOR})
        endif ()

        list(APPEND BLAS_TARGETS ${BLA_VENDOR})
    endif ()
endforeach ()

if (NOT BLAS_TARGETS)
    message(FATAL_ERROR "Could not find any BLAS libraries! Searched among ${BLAS_VENDORS}")
endif ()

# Load in the rest of the project.
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)
