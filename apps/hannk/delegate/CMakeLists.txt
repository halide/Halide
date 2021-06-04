# --------------- registrar (static-library) delegate

add_library(hannk_delegate STATIC
            hannk_delegate.cpp)
target_link_libraries(hannk_delegate PRIVATE
                      tensorflowlite_headers
                      Halide::Runtime)
target_include_directories(hannk_delegate PRIVATE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

# --------------- registrar (static-library) delegate

add_library(hannk_delegate_registrar STATIC
            hannk_delegate_provider.cpp)
target_link_libraries(hannk_delegate_registrar PRIVATE
                      interpreter
                      error_util
                      hannk_delegate
                      hannk_log_tflite
                      tensorflowlite_headers
                      Halide::Runtime)
target_include_directories(hannk_delegate_registrar PRIVATE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)


# --------------- external (shared-library) delegate

# External delegates in TFLite are always .so (never .dylib);
# on Mac we must declare the library to be MODULE rather than SHARED to get this.
# Apparently on Linux they are basically the same, so, use it everywhere for now.
add_library(hannk_delegate_external MODULE
            hannk_delegate_adaptor.cpp)
target_link_libraries(hannk_delegate_external PRIVATE
                      interpreter
                      error_util
                      hannk_delegate
                      hannk_log_tflite
                      tensorflowlite_headers
                      Halide::Runtime)
target_include_directories(hannk_delegate_external PRIVATE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
if (APPLE)
    target_link_options(hannk_delegate_external PRIVATE "-Wl,-exported_symbols_list,${hannk_SOURCE_DIR}/delegate/exported_symbols.osx")
else ()
    target_link_options(hannk_delegate_external PRIVATE "-Wl,--version-script,${hannk_SOURCE_DIR}/delegate/exported_symbols.ldscript")
endif()

# Give it the name that we already use elsewhere ("lib" and ".so" will be added)
set_target_properties(hannk_delegate_external PROPERTIES OUTPUT_NAME "HannkDelegate")

