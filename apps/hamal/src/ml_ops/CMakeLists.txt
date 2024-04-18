# Sigh, header-only libraries shouldn't be special
add_library(hamal_ml_ops INTERFACE)
target_include_directories(hamal_ml_ops INTERFACE
                           $<BUILD_INTERFACE:${hamal_SOURCE_DIR}>)
