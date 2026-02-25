# --------------- registrar (static-library) delegate

add_library(hannk_delegate STATIC
            hannk_delegate.cpp)
target_link_libraries(hannk_delegate PRIVATE
                      hannk_headers
                      hannk_log_tflite
                      interpreter_lower
                      tensorflow-lite::tensorflow-lite
                      Halide::Runtime)

# --------------- registrar (static-library) delegate

add_library(hannk_delegate_registrar STATIC
            hannk_delegate_provider.cpp)
target_link_libraries(hannk_delegate_registrar PRIVATE
                      hannk_headers
                      error_util
                      interpreter
                      hannk_delegate
                      hannk_log_tflite
                      tensorflow-lite::tensorflow-lite
                      Halide::Runtime)


# --------------- external (shared-library) delegate

# External delegates in TFLite are always .so (never .dylib);
# on Mac we must declare the library to be MODULE rather than SHARED to get this.
# Apparently on Linux they are basically the same, so, use it everywhere for now.
add_library(hannk_delegate_external MODULE
            hannk_delegate_adaptor.cpp)
target_link_libraries(hannk_delegate_external PRIVATE
                      hannk_headers
                      error_util
                      interpreter
                      hannk_delegate
                      hannk_log_tflite
                      tensorflow-lite::tensorflow-lite
                      Halide::Runtime)
if (APPLE)
    target_link_options(hannk_delegate_external PRIVATE "-Wl,-exported_symbols_list,${hannk_SOURCE_DIR}/delegate/exported_symbols.osx")
else ()
    target_link_options(hannk_delegate_external PRIVATE "-Wl,--version-script,${hannk_SOURCE_DIR}/delegate/exported_symbols.ldscript")
endif ()

# Give it the name that we already use elsewhere ("lib" and ".so" will be added)
set_target_properties(hannk_delegate_external PROPERTIES OUTPUT_NAME "HannkDelegate")
