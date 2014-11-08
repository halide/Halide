halide_project(wavelet "apps" 
  wavelet.cpp
)

halide_project(wavelet_run "apps" 
  filter.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

ADD_CUSTOM_COMMAND(
    TARGET wavelet
    POST_BUILD
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && ${CMAKE_BINARY_DIR}/bin/wavelet
)

ADD_DEPENDENCIES(
    wavelet_run
    wavelet
)

target_link_libraries(
    wavelet_run
    ${CMAKE_CURRENT_BINARY_DIR}/daubechies_x.o
    ${CMAKE_CURRENT_BINARY_DIR}/haar_x.o
    ${CMAKE_CURRENT_BINARY_DIR}/inverse_daubechies_x.o
    ${CMAKE_CURRENT_BINARY_DIR}/inverse_haar_x.o
)
