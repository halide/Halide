# Sigh, header-only libraries shouldn't be special
add_library(hallmark_ml_ops INTERFACE)
target_include_directories(hallmark_ml_ops INTERFACE
                           $<BUILD_INTERFACE:${hallmark_SOURCE_DIR}>)
