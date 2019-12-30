// Note that this header intentionally does not use include
// guards. The intended usage of this file is to define the meaning of
// the CL_FN macro, and then include this file, sometimes repeatedly
// within the same compilation unit.

#ifndef CL_FN
#define CL_FN(ret, fn, args)
#endif

/* Platform API */
CL_FN(cl_int,
      clGetPlatformIDs, (cl_uint /* num_entries */, cl_platform_id * /* platforms */, cl_uint * /* num_platforms */));

CL_FN(cl_int,
      clGetPlatformInfo, (cl_platform_id /* platform */, cl_platform_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

/* Device APIs */
CL_FN(cl_int,
      clGetDeviceIDs, (cl_platform_id /* platform */, cl_device_type /* device_type */, cl_uint /* num_entries */, cl_device_id * /* devices */, cl_uint * /* num_devices */));

CL_FN(cl_int,
      clGetDeviceInfo, (cl_device_id /* device */, cl_device_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

#ifdef HAVE_OPENCL_12
CL_FN(cl_int,
      clCreateSubDevices, (cl_device_id /* in_device */, const cl_device_partition_property * /* properties */, cl_uint /* num_devices */, cl_device_id * /* out_devices */, cl_uint * /* num_devices_ret */));

CL_FN(cl_int,
      clRetainDevice, (cl_device_id /* device */));

CL_FN(cl_int,
      clReleaseDevice, (cl_device_id /* device */));
#endif

/* Context APIs  */
CL_FN(cl_context,
      clCreateContext, (const cl_context_properties * /* properties */, cl_uint /* num_devices */, const cl_device_id * /* devices */, void(CL_CALLBACK * /* pfn_notify */)(const char *, const void *, size_t, void *), void * /* user_data */, cl_int * /* errcode_ret */));

CL_FN(cl_context,
      clCreateContextFromType, (const cl_context_properties * /* properties */, cl_device_type /* device_type */, void(CL_CALLBACK * /* pfn_notify*/)(const char *, const void *, size_t, void *), void * /* user_data */, cl_int * /* errcode_ret */));

CL_FN(cl_int,
      clRetainContext, (cl_context /* context */));

CL_FN(cl_int,
      clReleaseContext, (cl_context /* context */));

CL_FN(cl_int,
      clGetContextInfo, (cl_context /* context */, cl_context_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

/* Command Queue APIs */
CL_FN(cl_command_queue,
      clCreateCommandQueue, (cl_context /* context */, cl_device_id /* device */, cl_command_queue_properties /* properties */, cl_int * /* errcode_ret */));

CL_FN(cl_int,
      clRetainCommandQueue, (cl_command_queue /* command_queue */));

CL_FN(cl_int,
      clReleaseCommandQueue, (cl_command_queue /* command_queue */));

CL_FN(cl_int,
      clGetCommandQueueInfo, (cl_command_queue /* command_queue */, cl_command_queue_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

/* Memory Object APIs */
CL_FN(cl_mem,
      clCreateBuffer, (cl_context /* context */, cl_mem_flags /* flags */, size_t /* size */, void * /* host_ptr */, cl_int * /* errcode_ret */));

CL_FN(cl_mem,
      clCreateSubBuffer, (cl_mem /* buffer */, cl_mem_flags /* flags */, cl_buffer_create_type /* buffer_create_type */, const void * /* buffer_create_info */, cl_int * /* errcode_ret */));

#ifdef HAVE_OPENCL_12
CL_FN(cl_mem,
      clCreateImage, (cl_context /* context */, cl_mem_flags /* flags */, const cl_image_format * /* image_format */, const cl_image_desc * /* image_desc */, void * /* host_ptr */, cl_int * /* errcode_ret */));
#endif

CL_FN(cl_int,
      clRetainMemObject, (cl_mem /* memobj */));

CL_FN(cl_int,
      clReleaseMemObject, (cl_mem /* memobj */));

CL_FN(cl_int,
      clGetSupportedImageFormats, (cl_context /* context */, cl_mem_flags /* flags */, cl_mem_object_type /* image_type */, cl_uint /* num_entries */, cl_image_format * /* image_formats */, cl_uint * /* num_image_formats */));

CL_FN(cl_int,
      clGetMemObjectInfo, (cl_mem /* memobj */, cl_mem_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

CL_FN(cl_int,
      clGetImageInfo, (cl_mem /* image */, cl_image_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

CL_FN(cl_int,
      clSetMemObjectDestructorCallback, (cl_mem /* memobj */, void(CL_CALLBACK * /*pfn_notify*/)(cl_mem /* memobj */, void * /*user_data*/), void * /*user_data */));

/* Program Object APIs  */
CL_FN(cl_program,
      clCreateProgramWithSource, (cl_context /* context */, cl_uint /* count */, const char ** /* strings */, const size_t * /* lengths */, cl_int * /* errcode_ret */));
CL_FN(cl_int,
      clRetainProgram, (cl_program /* program */));

CL_FN(cl_int,
      clReleaseProgram, (cl_program /* program */));

CL_FN(cl_int,
      clBuildProgram, (cl_program /* program */, cl_uint /* num_devices */, const cl_device_id * /* device_list */, const char * /* options */, void(CL_CALLBACK * /* pfn_notify */)(cl_program /* program */, void * /* user_data */), void * /* user_data */));

CL_FN(cl_int,
      clGetProgramBuildInfo, (cl_program /* program */, cl_device_id /* device */, cl_program_build_info /* param_name */, size_t /* param_value_size */, void * /* param_value */, size_t * /* param_value_size_ret */));

/* Kernel Object APIs */
CL_FN(cl_kernel,
      clCreateKernel, (cl_program /* program */, const char * /* kernel_name */, cl_int * /* errcode_ret */));

CL_FN(cl_int,
      clRetainKernel, (cl_kernel /* kernel */));

CL_FN(cl_int,
      clReleaseKernel, (cl_kernel /* kernel */));

CL_FN(cl_int,
      clSetKernelArg, (cl_kernel /* kernel */, cl_uint /* arg_index */, size_t /* arg_size */, const void * /* arg_value */));

/* Flush and Finish APIs */
CL_FN(cl_int,
      clFlush, (cl_command_queue /* command_queue */));

CL_FN(cl_int,
      clFinish, (cl_command_queue /* command_queue */));

/* Enqueued Commands APIs */
CL_FN(cl_int,
      clEnqueueReadBuffer, (cl_command_queue /* command_queue */, cl_mem /* buffer */, cl_bool /* blocking_read */, size_t /* offset */, size_t /* size */, void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueReadBufferRect, (cl_command_queue /* command_queue */, cl_mem /* buffer */, cl_bool /* blocking_read */, const size_t * /* buffer_offset */, const size_t * /* host_offset */, const size_t * /* region */, size_t /* buffer_row_pitch */, size_t /* buffer_slice_pitch */, size_t /* host_row_pitch */, size_t /* host_slice_pitch */, void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueWriteBuffer, (cl_command_queue /* command_queue */, cl_mem /* buffer */, cl_bool /* blocking_write */, size_t /* offset */, size_t /* size */, const void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueWriteBufferRect, (cl_command_queue /* command_queue */, cl_mem /* buffer */, cl_bool /* blocking_write */, const size_t * /* buffer_offset */, const size_t * /* host_offset */, const size_t * /* region */, size_t /* buffer_row_pitch */, size_t /* buffer_slice_pitch */, size_t /* host_row_pitch */, size_t /* host_slice_pitch */, const void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueCopyBuffer, (cl_command_queue /* command_queue */, cl_mem /* src_buffer */, cl_mem /* dst_buffer */, size_t /* src_offset */, size_t /* dst_offset */, size_t /* cb */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueReadImage, (cl_command_queue /* command_queue */, cl_mem /* image */, cl_bool /* blocking_read */, const size_t * /* origin[3] */, const size_t * /* region[3] */, size_t /* row_pitch */, size_t /* slice_pitch */, void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueWriteImage, (cl_command_queue /* command_queue */, cl_mem /* image */, cl_bool /* blocking_write */, const size_t * /* origin[3] */, const size_t * /* region[3] */, size_t /* input_row_pitch */, size_t /* input_slice_pitch */, const void * /* ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(void *,
      clEnqueueMapBuffer, (cl_command_queue /* command_queue */, cl_mem /* buffer */, cl_bool /* blocking_map */, cl_map_flags /* map_flags */, size_t /* offset */, size_t /* size */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */, cl_int * /* errcode_ret */));

CL_FN(void *,
      clEnqueueMapImage, (cl_command_queue /* command_queue */, cl_mem /* image */, cl_bool /* blocking_map */, cl_map_flags /* map_flags */, const size_t * /* origin[3] */, const size_t * /* region[3] */, size_t * /* image_row_pitch */, size_t * /* image_slice_pitch */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */, cl_int * /* errcode_ret */));

CL_FN(cl_int,
      clEnqueueUnmapMemObject, (cl_command_queue /* command_queue */, cl_mem /* memobj */, void * /* mapped_ptr */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

CL_FN(cl_int,
      clEnqueueNDRangeKernel, (cl_command_queue /* command_queue */, cl_kernel /* kernel */, cl_uint /* work_dim */, const size_t * /* global_work_offset */, const size_t * /* global_work_size */, const size_t * /* local_work_size */, cl_uint /* num_events_in_wait_list */, const cl_event * /* event_wait_list */, cl_event * /* event */));

#undef CL_FN
