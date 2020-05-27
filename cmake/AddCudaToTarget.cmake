function(add_cuda_to_target TARGET VISIBILITY)
    if (TARGET CUDA::cuda_driver)
        target_link_libraries(${TARGET} ${VISIBILITY} CUDA::cuda_driver)
        return()
    endif ()

    find_package(CUDAToolkit QUIET)
    if (TARGET CUDA::cuda_driver)
        target_link_libraries(${TARGET} ${VISIBILITY} CUDA::cuda_driver)
        return()
    endif ()

    # Find the package for the CUDA_TOOLKIT_ROOT_DIR hint.
    find_package(CUDA QUIET)
    if (NOT CUDA_FOUND)
        set(CUDA_TOOLKIT_ROOT_DIR)
    endif ()

    # Find the CUDA driver library by doing what the CUDAToolkit module from
    # CMake 3.17 does.
    find_library(CUDA_DRIVER_LIBRARY
                 NAMES cuda_driver cuda
                 HINTS ${CUDA_TOOLKIT_ROOT_DIR} ENV CUDA_PATH
                 PATH_SUFFIXES nvidia/current lib64 lib/x64 lib)
    if (NOT CUDA_DRIVER_LIBRARY)
        # Don't try any stub directories until we have exhausted all other search locations.
        find_library(CUDA_DRIVER_LIBRARY
                     NAMES cuda_driver cuda
                     HINTS ${CUDA_TOOLKIT_ROOT_DIR} ENV CUDA_PATH
                     PATH_SUFFIXES lib64/stubs lib/x64/stubs lib/stubs stubs)
    endif ()
    mark_as_advanced(CUDA_DRIVER_LIBRARY)

    if (NOT CUDA_DRIVER_LIBRARY)
        message(WARNING "CUDA driver library not found on system.")
        return()
    endif ()

    target_include_directories(${TARGET} ${VISIBILITY} ${CUDA_INCLUDE_DIRS})
    target_link_libraries(${TARGET} ${VISIBILITY} ${CUDA_LIBRARIES} ${CUDA_DRIVER_LIBRARY})
endfunction()
