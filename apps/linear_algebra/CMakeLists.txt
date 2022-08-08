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
set(found_blases "")
set(known_vendors OpenBLAS ATLAS Apple Intel10_64_dyn Generic)

message(STATUS "Checking for available CBLAS implementations")
foreach (BLA_VENDOR IN LISTS known_vendors)
    find_package(BLAS QUIET)

    # Fail early if not found
    if (NOT BLAS_FOUND)
        message(STATUS "${BLA_VENDOR}: Missing")
        continue()
    endif ()

    # ATLAS is weird and has extra requirements
    if (BLA_VENDOR STREQUAL "ATLAS")
        find_library(CBLAS_LIBRARY cblas)
        if (NOT CBLAS_LIBRARY)
            message(STATUS "${BLA_VENDOR}: Missing dependency on CBLAS (hint: set CBLAS_LIBRARY)")
            continue()
        endif ()
        list(APPEND BLAS_LIBRARIES "${CBLAS_LIBRARY}")
    endif ()

    # Don't use "Generic" BLAS if any good BLAS is available.
    if (BLA_VENDOR STREQUAL "Generic" AND found_blases)
        message(STATUS "${BLA_VENDOR}: Not considered")
        continue()
    endif ()

    message(STATUS "${BLA_VENDOR}: Found ${BLAS_LIBRARIES}")

    add_library(BLAS::${BLA_VENDOR} INTERFACE IMPORTED)
    target_link_libraries(BLAS::${BLA_VENDOR} INTERFACE ${BLAS_LIBRARIES})
    target_link_options(BLAS::${BLA_VENDOR} INTERFACE ${BLAS_LINKER_FLAGS})
    target_include_directories(BLAS::${BLA_VENDOR} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include") # Use CBlas header in our own tree.

    list(APPEND found_blases ${BLA_VENDOR})
endforeach ()

if (NOT found_blases)
    message(FATAL_ERROR "Could not find any BLAS libraries! Searched among ${known_vendors}")
endif ()

# Load in the rest of the project.
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)
