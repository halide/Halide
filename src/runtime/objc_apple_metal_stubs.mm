#include "objc_apple_metal_stubs.h"

typedef size_t NSUInteger;

@protocol NSObject
-release;
@end
@interface NSObject
-release;
@end

@interface NSError;
@end

@interface NSString;
+(NSString *)alloc;
-(NSString *)initWithBytesNoCopy:(const char *)bytes length:(NSUInteger)length encoding:(NSUInteger)encoding freeWhenDone:(bool)freeWhenDone;
-release;
@end

extern "C" {
void NSLog(NSString *format, ...);
}

@protocol MTLComputePipelineState <NSObject>
@end

@protocol MTLBuffer
-(void *)contents;
@end

struct MTLSize {
    unsigned long width;
    unsigned long height;
    unsigned long depth;
};

@protocol MTLComputeCommandEncoder <NSObject>
- (void)endEncoding;
- (void)setComputePipelineState:(id <MTLComputePipelineState>)state;
- (void)setBuffer:(id <MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)dispatchThreadgroups:(MTLSize)threadgroupsPerGrid threadsPerThreadgroup:(MTLSize)threadsPerThreadgroup;
@end

@protocol MTLCommandBuffer <NSObject>
-(id <MTLComputeCommandEncoder>)computeCommandEncoder;
-commit;
-waitUntilCompleted;
@end

@protocol MTLCommandQueue <NSObject>
- (id <MTLCommandBuffer>)commandBuffer;
@end

@protocol MTLFunction <NSObject>
@end

@protocol MTLLibrary <NSObject>
- (id <MTLFunction>) newFunctionWithName:(NSString *)functionName;
@end

@interface MTLCompileOptions : NSObject
+(MTLCompileOptions *)alloc;
@property (readwrite, nonatomic) bool fastMathEnabled;
@end

@protocol MTLDevice <NSObject>
- (id <MTLCommandQueue>)newCommandQueue;
- (id <MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(NSUInteger /* MTLResourceOptions */)options;
- (id <MTLComputePipelineState>)newComputePipelineStateWithFunction:(id <MTLFunction>)computeFunction error:(/*__autoreleasing*/ NSError **)error;
- (id <MTLLibrary>)newLibraryWithSource: (NSString *)source_str options:(MTLCompileOptions *)options error: (NSError **)error_return;
@end

extern "C" {
extern id <MTLDevice> MTLCreateSystemDefaultDevice();
}

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

WEAK void release_metal_object(void *obj) {
    id <NSObject> obj_obj = (id)obj;
    [obj_obj release];
}

WEAK mtl_device *system_default_device() {
    return (mtl_device *)MTLCreateSystemDefaultDevice();
}

WEAK mtl_buffer *new_buffer(mtl_device *device, size_t length) {
    id <MTLDevice> device_obj = (id <MTLDevice>)device;
    return (mtl_buffer *)[device_obj newBufferWithLength: length options: 0 /* MTLResourceOptionCPUCacheModeDefault */];
}

WEAK mtl_command_queue *new_command_queue(mtl_device *device) {
    id <MTLDevice> device_obj = (id <MTLDevice>)device;
    return (mtl_command_queue *)[device_obj newCommandQueue];
}
    
WEAK mtl_command_buffer *new_command_buffer(mtl_command_queue *queue) {
    id <MTLCommandQueue> queue_obj = (id <MTLCommandQueue>)queue;
    return (mtl_command_buffer *)[queue_obj commandBuffer];
}

WEAK mtl_compute_command_encoder *new_compute_command_encoder(mtl_command_buffer *buffer) {
    id <MTLCommandBuffer> buffer_obj = (id <MTLCommandBuffer>)buffer;
    return (mtl_compute_command_encoder *)[buffer_obj computeCommandEncoder];
}

WEAK mtl_compute_pipeline_state *new_compute_pipeline_state_with_function(mtl_device *device, mtl_function *function) {
    NSError *error_return;
    id <MTLDevice> device_obj = (id <MTLDevice>)device;
    id <MTLFunction> function_obj = (id <MTLFunction>)function;
    // TODO: do something with error.
    mtl_compute_pipeline_state *result = (mtl_compute_pipeline_state *)[device_obj newComputePipelineStateWithFunction: function_obj error: &error_return];
    if (result == NULL) {
        NSLog(@"%@", error_return);     
    }
    return result;
}

WEAK void set_compute_pipeline_state(mtl_compute_command_encoder *encoder, mtl_compute_pipeline_state *pipeline_state) {
    id <MTLComputeCommandEncoder> encoder_obj = (id <MTLComputeCommandEncoder>)encoder;
    id <MTLComputePipelineState> pipeline_state_obj = (id <MTLComputePipelineState>)pipeline_state;
    [encoder_obj setComputePipelineState: pipeline_state_obj];
}

WEAK void end_encoding(mtl_compute_command_encoder *encoder) {
    id <MTLComputeCommandEncoder> encoder_obj = (id <MTLComputeCommandEncoder>)encoder;
    [encoder_obj endEncoding];
}

WEAK mtl_library *new_library_with_source(mtl_device *device, const char *source, size_t source_len) {
    id <MTLDevice> device_obj = (id <MTLDevice>)device;
    NSError *error_return;
    NSString *source_str = [[NSString alloc] initWithBytesNoCopy: source length: source_len encoding: 4 freeWhenDone: 0];

    MTLCompileOptions *options = [MTLCompileOptions alloc];
    options.fastMathEnabled = 1;

    // TODO: handle error.
    id <MTLLibrary> result = [device_obj newLibraryWithSource: source_str options: options error: &error_return];

    [options release];
    [source_str release];
    return (mtl_library *)result;
}

WEAK mtl_function *new_function_with_name(mtl_library *library, const char *name, size_t name_len) {
    id <MTLLibrary> library_obj = (id <MTLLibrary>)library;
    // TODO: Fix Objective-C stubs for real
    NSString *name_str = [[NSString alloc] initWithBytesNoCopy: name length: name_len encoding: 4 freeWhenDone: 0];

    id <MTLFunction> result = [library_obj newFunctionWithName: name_str];
    [name_str release];
    return (mtl_function *)result;
}

WEAK void set_input_buffer(mtl_compute_command_encoder *encoder, mtl_buffer *input_buffer, uint32_t index) {
    id <MTLComputeCommandEncoder> encoder_obj = (id <MTLComputeCommandEncoder>)encoder;
    id <MTLBuffer> buffer_obj = (id <MTLBuffer>)input_buffer;

    [encoder_obj setBuffer: buffer_obj offset: 0 atIndex: index];
}

WEAK void set_threadgroup_memory_length(mtl_compute_command_encoder *encoder, uint32_t index, uint32_t length) {
    id <MTLComputeCommandEncoder> encoder_obj = (id <MTLComputeCommandEncoder>)encoder;
    [encoder_obj setThreadgroupMemoryLength: length atIndex: index];
}

WEAK void dispatch_threadgroups(mtl_compute_command_encoder *encoder,
                                int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                                int32_t threads_x, int32_t threads_y, int32_t threads_z) {
    MTLSize threadgroupsPerGrid;
    threadgroupsPerGrid.width = blocks_x;
    threadgroupsPerGrid.height = blocks_y;
    threadgroupsPerGrid.depth = blocks_z;

    MTLSize threadsPerThreadgroup;
    threadsPerThreadgroup.width = threads_x;
    threadsPerThreadgroup.height = threads_y;
    threadsPerThreadgroup.depth = threads_z;

    id <MTLComputeCommandEncoder> encoder_obj = (id <MTLComputeCommandEncoder>)encoder;
    [encoder_obj dispatchThreadgroups: threadgroupsPerGrid threadsPerThreadgroup: threadsPerThreadgroup];
}

WEAK void commit_command_buffer(mtl_command_buffer *buffer) {
    id <MTLCommandBuffer> buffer_obj = (id <MTLCommandBuffer>)buffer;
    [buffer_obj commit];
}

WEAK void wait_until_completed(mtl_command_buffer *buffer) {
    id <MTLCommandBuffer> buffer_obj = (id <MTLCommandBuffer>)buffer;
    [buffer_obj waitUntilCompleted];
}

WEAK void *buffer_contents(mtl_buffer *buffer) {
    id <MTLBuffer> buffer_obj = (id <MTLBuffer>)buffer;
    return [buffer_obj contents];
}

}}}}

