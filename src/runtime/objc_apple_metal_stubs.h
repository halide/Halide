#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

struct mtl_device;
struct mtl_buffer;
struct mtl_command_queue;
struct mtl_command_buffer;
struct mtl_compute_command_encoder;
struct mtl_compute_pipeline_state;
struct mtl_library;
struct mtl_function;
struct mtl_compile_options;

extern mtl_device *system_default_device();
extern void release_metal_object(void *obj);
extern mtl_buffer *new_buffer(mtl_device *device, size_t length);
extern mtl_command_queue *new_command_queue(mtl_device *device);
extern mtl_command_buffer *new_command_buffer(mtl_command_queue *queue);
extern mtl_compute_command_encoder *new_compute_command_encoder(mtl_command_buffer *buffer);
extern mtl_compute_pipeline_state *new_compute_pipeline_state_with_function(mtl_device *device, mtl_function *function);
extern void set_compute_pipeline_state(mtl_compute_command_encoder *encoder, mtl_compute_pipeline_state *pipeline_state);
extern void end_encoding(mtl_compute_command_encoder *encoder);
extern mtl_library *new_library_with_source(mtl_device *device, const char *source, size_t source_len);
extern mtl_function *new_function_with_name(mtl_library *library, const char *name, size_t name_len);
extern void set_input_buffer(mtl_compute_command_encoder *encoder, mtl_buffer *input_buffer, uint32_t index);
extern void set_threadgroup_memory_length(mtl_compute_command_encoder *encoder, uint32_t length, uint32_t index);
WEAK void dispatch_threadgroups(mtl_compute_command_encoder *encoder,
	                        int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
	                        int32_t threads_x, int32_t threads_y, int32_t threads_z);
extern void commit_command_buffer(mtl_command_buffer *buffer);
extern void wait_until_completed(mtl_command_buffer *buffer);
extern void *buffer_contents(mtl_buffer *buffer);

}}}}
