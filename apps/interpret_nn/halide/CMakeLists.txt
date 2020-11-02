# TODO(srj): stale and unmaintained for now -- use Makefile instead

# # Common Halide code used in generators.
# add_library(common_halide STATIC common_halide.cpp)
# target_link_libraries(common_halide PUBLIC Halide::Halide)

# foreach (GENERATOR IN ITEMS add convolution depthwise_convolution)
#   add_executable(${GENERATOR}.generator ${GENERATOR}_generator.cpp)
#   target_link_libraries(${GENERATOR}.generator PRIVATE Halide::Generator common_halide)
# endforeach ()

# add_halide_library(Add4DUint8Uint8
#                    FROM add.generator
#                    GENERATOR Add
#                    FEATURES c_plus_plus_name_mangling
#                    NAMESPACE interpret_nn)

# add_halide_library(ConvolutionUint8
#                    FROM convolution.generator
#                    GENERATOR Convolution
#                    FEATURES c_plus_plus_name_mangling
#                    NAMESPACE interpret_nn)

# add_halide_library(DepthwiseConvolutionUint8
#                    FROM depthwise_convolution.generator
#                    GENERATOR DepthwiseConvolution
#                    FEATURES c_plus_plus_name_mangling
#                    NAMESPACE interpret_nn)

# add_library(ops_halide INTERFACE)
# target_link_libraries(ops_halide INTERFACE
#                       Add4DUint8Uint8
#                       ConvolutionUint8
#                       DepthwiseConvolutionUint8)