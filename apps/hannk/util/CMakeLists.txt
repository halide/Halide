add_library(error_util STATIC
            error_util.cpp)
target_sources(error_util PUBLIC FILE_SET HEADERS FILES error_util.h)
target_link_libraries(error_util PUBLIC hannk_headers
                      PRIVATE Halide::Runtime)


add_library(hannk_log_stderr STATIC EXCLUDE_FROM_ALL
            hannk_log_stderr.cpp)
target_link_libraries(hannk_log_stderr PUBLIC
                      hannk_headers
                      "$<$<PLATFORM_ID:Android>:log>")


add_library(hannk_log_tflite STATIC EXCLUDE_FROM_ALL
            hannk_log_tflite.cpp)
target_link_libraries(hannk_log_tflite
                      PRIVATE tensorflow-lite::tensorflow-lite
                      PUBLIC hannk_headers)

add_library(buffer_util INTERFACE)
target_sources(buffer_util INTERFACE FILE_SET HEADERS FILES buffer_util.h)
target_link_libraries(buffer_util INTERFACE hannk_headers)

add_library(file_util INTERFACE)
target_sources(file_util INTERFACE FILE_SET HEADERS FILES file_util.h)
target_link_libraries(file_util INTERFACE hannk_headers)

add_library(small_vector INTERFACE)
target_sources(small_vector INTERFACE FILE_SET HEADERS FILES small_vector.h)
target_link_libraries(small_vector INTERFACE hannk_headers)

add_library(hannk_log_hdr INTERFACE)
target_sources(hannk_log_hdr INTERFACE FILE_SET HEADERS FILES hannk_log_hdr.h)
target_link_libraries(hannk_log_hdr INTERFACE hannk_headers)

add_library(model_runner STATIC
            model_runner.cpp)
target_compile_definitions(model_runner PRIVATE
                           TFLITE_VERSION_MAJOR=${TFLITE_VERSION_MAJOR}
                           TFLITE_VERSION_MINOR=${TFLITE_VERSION_MINOR}
                           TFLITE_VERSION_PATCH=${TFLITE_VERSION_PATCH})
target_link_libraries(model_runner PRIVATE
                      hannk_headers
                      interpreter
                      error_util
                      file_util
                      tflite_parser
                      Halide::Tools  # for halide_benchmark.h
                      Halide::Runtime)
if (HANNK_BUILD_TFLITE)
    target_link_libraries(model_runner PRIVATE
                          tensorflow-lite::tensorflow-lite
                          hannk_delegate)
endif ()
