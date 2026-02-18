
# --------------------------- Generate flatbuffer files
set(tflite_schema_source "${CMAKE_CURRENT_SOURCE_DIR}/tflite_schema.fbs")
set(tflite_generated_header "${CMAKE_CURRENT_BINARY_DIR}/tflite_schema_generated.h")
add_custom_command(
    OUTPUT "${tflite_generated_header}"
    COMMAND flatbuffers::flatc --cpp --cpp-std C++17 --no-union-value-namespacing --keep-prefix -o "${CMAKE_CURRENT_BINARY_DIR}" "${tflite_schema_source}"
    DEPENDS "${fb_def}"
    VERBATIM
)
add_custom_target(generate_tflite_schema_header DEPENDS "${tflite_generated_header}")
set_source_files_properties("${tflite_generated_header}" PROPERTIES GENERATED TRUE)

# --------------------------- Generate protobuf files
add_library(proto_objects OBJECT llm_params.proto transformer_params.proto)
target_link_libraries(proto_objects PUBLIC protobuf::libprotobuf)
target_include_directories(proto_objects PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")

protobuf_generate(
    TARGET proto_objects
    PROTOC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")


# --------------------------- Ordinary code
add_library(hallmark_contrib
            llm_params.cc
            sampler.cc
            weights_loader.cc)
add_dependencies(hallmark_contrib
                 generate_tflite_schema_header)
target_include_directories(hallmark_contrib INTERFACE
                           $<BUILD_INTERFACE:${hallmark_SOURCE_DIR}>)
target_include_directories(hallmark_contrib PRIVATE
                           $<BUILD_INTERFACE:${hallmark_SOURCE_DIR}>
                           $<BUILD_INTERFACE:${hallmark_BINARY_DIR}>)
target_link_libraries(hallmark_contrib
                      PRIVATE
                      absl::status
                      Halide::Runtime
                      hallmark_flatbuffers
                      proto_objects)
