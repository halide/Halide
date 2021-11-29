add_library(error_util STATIC
            error_util.cpp)
target_include_directories(error_util PUBLIC
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(error_util PRIVATE Halide::Runtime)

add_library(hannk_log_stderr STATIC EXCLUDE_FROM_ALL
            hannk_log_stderr.cpp)
target_include_directories(hannk_log_stderr PUBLIC
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(hannk_log_stderr PUBLIC "$<$<PLATFORM_ID:Android>:log>")

add_library(hannk_log_tflite STATIC EXCLUDE_FROM_ALL
            hannk_log_tflite.cpp)
target_link_libraries(hannk_log_tflite PRIVATE tensorflowlite_headers)
target_include_directories(hannk_log_tflite PUBLIC
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

# Sigh, header-only libraries shouldn't be special
add_library(buffer_util INTERFACE)
target_include_directories(buffer_util INTERFACE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

add_library(file_util INTERFACE)
target_include_directories(file_util INTERFACE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

add_library(small_vector INTERFACE)
target_include_directories(small_vector INTERFACE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

add_library(hannk_log_hdr INTERFACE)
target_include_directories(hannk_log_hdr INTERFACE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

add_library(model_runner STATIC
            model_runner.cpp)
target_include_directories(model_runner PUBLIC
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_compile_definitions(model_runner PRIVATE
                           -DTFLITE_VERSION_MAJOR=${TFLITE_VERSION_MAJOR}
                           -DTFLITE_VERSION_MINOR=${TFLITE_VERSION_MINOR}
                           -DTFLITE_VERSION_PATCH=${TFLITE_VERSION_PATCH})
target_link_libraries(model_runner PRIVATE
                      interpreter
                      error_util
                      file_util
                      tflite_parser
                      Halide::Tools  # for halide_benchmark.h
                      Halide::Runtime
                      tensorflowlite_headers)

if (HANNK_BUILD_TFLITE)
    target_link_libraries(model_runner PRIVATE
                          tensorflowlite_c
                          hannk_delegate)
endif ()
