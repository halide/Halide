halide_library(srgb_to_linear 
               SRCS srgb_to_linear_generator.cpp)
halide_library(linear_to_srgb 
               SRCS linear_to_srgb_generator.cpp)

halide_library(simple_blur 
               SRCS simple_blur_generator.cpp
               # For usage as a stub (via linear_blur), we don't need to specify
               # the type-and-dims explicitly, but for ahead-of-time compilation
               # (as used by run_linear_blur), the inputs must have
               # concrete type and dimensions. (The outputs must also have concrete
               # type-and-dimensions, too, but those can be inferred from the 
               # outputs as filled in by user code.)
               GENERATOR_ARGS input.type=float32 input.dim=3)

halide_library(linear_blur 
               SRCS linear_blur_generator.cpp
               GENERATOR_ARGS auto_schedule=true
               GENERATOR_DEPS srgb_to_linear.generator
                              linear_to_srgb.generator
                              simple_blur.generator
               # Emit the .stmt and .schedule files so we can 
               # examine how the auto-scheduled results look
               EXTRA_OUTPUTS stmt schedule)

add_executable(run_linear_blur run_linear_blur.cpp)
halide_use_image_io(run_linear_blur)

target_link_libraries(run_linear_blur PRIVATE linear_blur simple_blur)
