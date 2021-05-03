add_library(error_util STATIC
            error_util.cpp)
target_include_directories(error_util PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)
target_link_libraries(error_util PRIVATE Halide::Runtime)

add_library(hannk_log_stderr STATIC
            hannk_log_stderr.cpp)
target_include_directories(hannk_log_stderr PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

add_library(hannk_log_tflite STATIC
            hannk_log_tflite.cpp)
target_link_libraries(hannk_log_tflite PRIVATE tensorflowlite_headers)
target_include_directories(hannk_log_tflite PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# Sigh, header-only libraries shouldn't be special
add_library(buffer_util INTERFACE)
target_include_directories(buffer_util INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

add_library(file_util INTERFACE)
target_include_directories(file_util INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

add_library(hannk_log_hdr INTERFACE)
target_include_directories(hannk_log_hdr INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
