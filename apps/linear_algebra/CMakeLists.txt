set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# This could be made to use any number of blas header files
#  or we could supply our own cblas.h
# Another option is to use a more robust FindBLAS.cmake than the one
#  distributed with cmake
set(CBLAS_HEADER_NAME "cblas.h")
find_path(CBLAS_INCLUDE_DIR
  ${CBLAS_HEADER_NAME}
  PATHS
    /usr/include
    /usr/local/include
    /usr/include/atlas
    /usr/local/include/atlas
    /usr/local/opt/openblas/include # enables openblas-via-homebrew-on-mac
)
set(CBLAS_FOUND false)
if (NOT CBLAS_INCLUDE_DIR MATCHES "-NOTFOUND")
  set(CBLAS_FOUND true)
endif()

if (CBLAS_FOUND)
  message(STATUS "Found CBLAS header: ${CBLAS_INCLUDE_DIR}/${CBLAS_HEADER_NAME}")
  # XXX
  #  OpenBLAS seems to provide cblas symbols
  #  Atlas requires also linking against its provided libcblas for cblas symbols
  set(ATLAS_EXTRA_LIBS cblas) # XXX fragile
  set(OpenBLAS_EXTRA_LIBS)
  set(BLAS_VENDORS OpenBLAS ATLAS)

  # TODO
  # there are more vendors we could add here that support the cblas interface
  # there seem to be better FindBLAS.cmake scripts floating around that also find
  #  *_INCLUDE_DIR
  message(STATUS "Checking for available CBLAS implementations")
  foreach(BLAS_VENDOR ${BLAS_VENDORS})
    set(BLA_VENDOR ${BLAS_VENDOR})
    set(BLAS_FIND_QUIETLY true)
    find_package(BLAS)
    if (NOT BLAS_FOUND)
      message(STATUS " ${BLAS_VENDOR}: Missing")
    else()
      message(STATUS " ${BLAS_VENDOR}: Found")
      list(APPEND BLAS_VENDORS ${NAME})
    endif()
  endforeach()
endif()

add_subdirectory(src)

if (WITH_TESTS)
  add_subdirectory(tests)
endif()
add_subdirectory(benchmarks)
