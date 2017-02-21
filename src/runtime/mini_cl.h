/*******************************************************************************
 * Copyright (c) 2011 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and/or associated documentation files (the
 * "Materials"), to deal in the Materials without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Materials, and to
 * permit persons to whom the Materials are furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
 ******************************************************************************/

#ifndef __OPENCL_CL_H
#define __OPENCL_CL_H

#if defined(WINDOWS) && defined(BITS_32)
#define CL_API_CALL __stdcall
#define CL_CALLBACK __stdcall
#else
#define CL_API_CALL
#define CL_CALLBACK
#endif
#define CL_API_ENTRY
#define CL_API_SUFFIX__VERSION_1_0
#define CL_API_SUFFIX__VERSION_1_1
#define CL_API_SUFFIX__VERSION_1_2
#define CL_EXT_SUFFIX__VERSION_1_0_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_0_DEPRECATED
#define CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_1_DEPRECATED
#define CL_EXT_SUFFIX__VERSION_1_2_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_2_DEPRECATED
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef int64_t cl_long;
typedef uint64_t cl_ulong;

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

typedef struct _cl_platform_id *cl_platform_id;
typedef struct _cl_device_id *cl_device_id;
typedef struct _cl_context *cl_context;
typedef struct _cl_command_queue *cl_command_queue;
typedef struct _cl_mem *cl_mem;
typedef struct _cl_program *cl_program;
typedef struct _cl_kernel *cl_kernel;
typedef struct _cl_event *cl_event;
typedef struct _cl_sampler *cl_sampler;

typedef cl_uint cl_bool; /* WARNING!  Unlike cl_ types in cl_platform.h, cl_bool is not guaranteed to be the same size as the bool in kernels. */
typedef cl_ulong cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_uint cl_platform_info;
typedef cl_uint cl_device_info;
typedef cl_bitfield cl_device_fp_config;
typedef cl_uint cl_device_mem_cache_type;
typedef cl_uint cl_device_local_mem_type;
typedef cl_bitfield cl_device_exec_capabilities;
typedef cl_bitfield cl_command_queue_properties;
typedef intptr_t cl_device_partition_property;
typedef cl_bitfield cl_device_affinity_domain;

typedef intptr_t cl_context_properties;
typedef cl_uint cl_context_info;
typedef cl_uint cl_command_queue_info;
typedef cl_uint cl_channel_order;
typedef cl_uint cl_channel_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_uint cl_mem_object_type;
typedef cl_uint cl_mem_info;
typedef cl_bitfield cl_mem_migration_flags;
typedef cl_uint cl_image_info;
typedef cl_uint cl_buffer_create_type;
typedef cl_uint cl_addressing_mode;
typedef cl_uint cl_filter_mode;
typedef cl_uint cl_sampler_info;
typedef cl_bitfield cl_map_flags;
typedef cl_uint cl_program_info;
typedef cl_uint cl_program_build_info;
typedef cl_uint cl_program_binary_type;
typedef cl_int cl_build_status;
typedef cl_uint cl_kernel_info;
typedef cl_uint cl_kernel_arg_info;
typedef cl_uint cl_kernel_arg_address_qualifier;
typedef cl_uint cl_kernel_arg_access_qualifier;
typedef cl_bitfield cl_kernel_arg_type_qualifier;
typedef cl_uint cl_kernel_work_group_info;
typedef cl_uint cl_event_info;
typedef cl_uint cl_command_type;
typedef cl_uint cl_profiling_info;

typedef struct _cl_image_format {
    cl_channel_order image_channel_order;
    cl_channel_type image_channel_data_type;
} cl_image_format;

typedef struct _cl_image_desc {
    cl_mem_object_type image_type;
    size_t image_width;
    size_t image_height;
    size_t image_depth;
    size_t image_array_size;
    size_t image_row_pitch;
    size_t image_slice_pitch;
    cl_uint num_mip_levels;
    cl_uint num_samples;
    cl_mem buffer;
} cl_image_desc;

typedef struct _cl_buffer_region {
    size_t origin;
    size_t size;
} cl_buffer_region;

/******************************************************************************/

/* Error Codes */
#define CL_SUCCESS 0
#define CL_DEVICE_NOT_FOUND -1
#define CL_DEVICE_NOT_AVAILABLE -2
#define CL_COMPILER_NOT_AVAILABLE -3
#define CL_MEM_OBJECT_ALLOCATION_FAILURE -4
#define CL_OUT_OF_RESOURCES -5
#define CL_OUT_OF_HOST_MEMORY -6
#define CL_PROFILING_INFO_NOT_AVAILABLE -7
#define CL_MEM_COPY_OVERLAP -8
#define CL_IMAGE_FORMAT_MISMATCH -9
#define CL_IMAGE_FORMAT_NOT_SUPPORTED -10
#define CL_BUILD_PROGRAM_FAILURE -11
#define CL_MAP_FAILURE -12
#define CL_MISALIGNED_SUB_BUFFER_OFFSET -13
#define CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST -14
#define CL_COMPILE_PROGRAM_FAILURE -15
#define CL_LINKER_NOT_AVAILABLE -16
#define CL_LINK_PROGRAM_FAILURE -17
#define CL_DEVICE_PARTITION_FAILED -18
#define CL_KERNEL_ARG_INFO_NOT_AVAILABLE -19

#define CL_INVALID_VALUE -30
#define CL_INVALID_DEVICE_TYPE -31
#define CL_INVALID_PLATFORM -32
#define CL_INVALID_DEVICE -33
#define CL_INVALID_CONTEXT -34
#define CL_INVALID_QUEUE_PROPERTIES -35
#define CL_INVALID_COMMAND_QUEUE -36
#define CL_INVALID_HOST_PTR -37
#define CL_INVALID_MEM_OBJECT -38
#define CL_INVALID_IMAGE_FORMAT_DESCRIPTOR -39
#define CL_INVALID_IMAGE_SIZE -40
#define CL_INVALID_SAMPLER -41
#define CL_INVALID_BINARY -42
#define CL_INVALID_BUILD_OPTIONS -43
#define CL_INVALID_PROGRAM -44
#define CL_INVALID_PROGRAM_EXECUTABLE -45
#define CL_INVALID_KERNEL_NAME -46
#define CL_INVALID_KERNEL_DEFINITION -47
#define CL_INVALID_KERNEL -48
#define CL_INVALID_ARG_INDEX -49
#define CL_INVALID_ARG_VALUE -50
#define CL_INVALID_ARG_SIZE -51
#define CL_INVALID_KERNEL_ARGS -52
#define CL_INVALID_WORK_DIMENSION -53
#define CL_INVALID_WORK_GROUP_SIZE -54
#define CL_INVALID_WORK_ITEM_SIZE -55
#define CL_INVALID_GLOBAL_OFFSET -56
#define CL_INVALID_EVENT_WAIT_LIST -57
#define CL_INVALID_EVENT -58
#define CL_INVALID_OPERATION -59
#define CL_INVALID_GL_OBJECT -60
#define CL_INVALID_BUFFER_SIZE -61
#define CL_INVALID_MIP_LEVEL -62
#define CL_INVALID_GLOBAL_WORK_SIZE -63
#define CL_INVALID_PROPERTY -64
#define CL_INVALID_IMAGE_DESCRIPTOR -65
#define CL_INVALID_COMPILER_OPTIONS -66
#define CL_INVALID_LINKER_OPTIONS -67
#define CL_INVALID_DEVICE_PARTITION_COUNT -68

/* OpenCL Version */
#define CL_VERSION_1_0 1
#define CL_VERSION_1_1 1
#define CL_VERSION_1_2 1

/* cl_bool */
#define CL_FALSE 0
#define CL_TRUE 1
#define CL_BLOCKING CL_TRUE
#define CL_NON_BLOCKING CL_FALSE

/* cl_platform_info */
#define CL_PLATFORM_PROFILE 0x0900
#define CL_PLATFORM_VERSION 0x0901
#define CL_PLATFORM_NAME 0x0902
#define CL_PLATFORM_VENDOR 0x0903
#define CL_PLATFORM_EXTENSIONS 0x0904

/* cl_device_type - bitfield */
#define CL_DEVICE_TYPE_DEFAULT (1 << 0)
#define CL_DEVICE_TYPE_CPU (1 << 1)
#define CL_DEVICE_TYPE_GPU (1 << 2)
#define CL_DEVICE_TYPE_ACCELERATOR (1 << 3)
#define CL_DEVICE_TYPE_CUSTOM (1 << 4)
#define CL_DEVICE_TYPE_ALL 0xFFFFFFFF

/* cl_device_info */
#define CL_DEVICE_TYPE 0x1000
#define CL_DEVICE_VENDOR_ID 0x1001
#define CL_DEVICE_MAX_COMPUTE_UNITS 0x1002
#define CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS 0x1003
#define CL_DEVICE_MAX_WORK_GROUP_SIZE 0x1004
#define CL_DEVICE_MAX_WORK_ITEM_SIZES 0x1005
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR 0x1006
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT 0x1007
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT 0x1008
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG 0x1009
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT 0x100A
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE 0x100B
#define CL_DEVICE_MAX_CLOCK_FREQUENCY 0x100C
#define CL_DEVICE_ADDRESS_BITS 0x100D
#define CL_DEVICE_MAX_READ_IMAGE_ARGS 0x100E
#define CL_DEVICE_MAX_WRITE_IMAGE_ARGS 0x100F
#define CL_DEVICE_MAX_MEM_ALLOC_SIZE 0x1010
#define CL_DEVICE_IMAGE2D_MAX_WIDTH 0x1011
#define CL_DEVICE_IMAGE2D_MAX_HEIGHT 0x1012
#define CL_DEVICE_IMAGE3D_MAX_WIDTH 0x1013
#define CL_DEVICE_IMAGE3D_MAX_HEIGHT 0x1014
#define CL_DEVICE_IMAGE3D_MAX_DEPTH 0x1015
#define CL_DEVICE_IMAGE_SUPPORT 0x1016
#define CL_DEVICE_MAX_PARAMETER_SIZE 0x1017
#define CL_DEVICE_MAX_SAMPLERS 0x1018
#define CL_DEVICE_MEM_BASE_ADDR_ALIGN 0x1019
#define CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE 0x101A
#define CL_DEVICE_SINGLE_FP_CONFIG 0x101B
#define CL_DEVICE_GLOBAL_MEM_CACHE_TYPE 0x101C
#define CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE 0x101D
#define CL_DEVICE_GLOBAL_MEM_CACHE_SIZE 0x101E
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE 0x1020
#define CL_DEVICE_MAX_CONSTANT_ARGS 0x1021
#define CL_DEVICE_LOCAL_MEM_TYPE 0x1022
#define CL_DEVICE_LOCAL_MEM_SIZE 0x1023
#define CL_DEVICE_ERROR_CORRECTION_SUPPORT 0x1024
#define CL_DEVICE_PROFILING_TIMER_RESOLUTION 0x1025
#define CL_DEVICE_ENDIAN_LITTLE 0x1026
#define CL_DEVICE_AVAILABLE 0x1027
#define CL_DEVICE_COMPILER_AVAILABLE 0x1028
#define CL_DEVICE_EXECUTION_CAPABILITIES 0x1029
#define CL_DEVICE_QUEUE_PROPERTIES 0x102A
#define CL_DEVICE_NAME 0x102B
#define CL_DEVICE_VENDOR 0x102C
#define CL_DRIVER_VERSION 0x102D
#define CL_DEVICE_PROFILE 0x102E
#define CL_DEVICE_VERSION 0x102F
#define CL_DEVICE_EXTENSIONS 0x1030
#define CL_DEVICE_PLATFORM 0x1031
#define CL_DEVICE_DOUBLE_FP_CONFIG 0x1032
/* 0x1033 reserved for CL_DEVICE_HALF_FP_CONFIG */
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF 0x1034
#define CL_DEVICE_HOST_UNIFIED_MEMORY 0x1035
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR 0x1036
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT 0x1037
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_INT 0x1038
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG 0x1039
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT 0x103A
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE 0x103B
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF 0x103C
#define CL_DEVICE_OPENCL_C_VERSION 0x103D
#define CL_DEVICE_LINKER_AVAILABLE 0x103E
#define CL_DEVICE_BUILT_IN_KERNELS 0x103F
#define CL_DEVICE_IMAGE_MAX_BUFFER_SIZE 0x1040
#define CL_DEVICE_IMAGE_MAX_ARRAY_SIZE 0x1041
#define CL_DEVICE_PARENT_DEVICE 0x1042
#define CL_DEVICE_PARTITION_MAX_SUB_DEVICES 0x1043
#define CL_DEVICE_PARTITION_PROPERTIES 0x1044
#define CL_DEVICE_PARTITION_AFFINITY_DOMAIN 0x1045
#define CL_DEVICE_PARTITION_TYPE 0x1046
#define CL_DEVICE_REFERENCE_COUNT 0x1047
#define CL_DEVICE_PREFERRED_INTEROP_USER_SYNC 0x1048
#define CL_DEVICE_PRINTF_BUFFER_SIZE 0x1049

/* cl_device_fp_config - bitfield */
#define CL_FP_DENORM (1 << 0)
#define CL_FP_INF_NAN (1 << 1)
#define CL_FP_ROUND_TO_NEAREST (1 << 2)
#define CL_FP_ROUND_TO_ZERO (1 << 3)
#define CL_FP_ROUND_TO_INF (1 << 4)
#define CL_FP_FMA (1 << 5)
#define CL_FP_SOFT_FLOAT (1 << 6)
#define CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT (1 << 7)

/* cl_device_mem_cache_type */
#define CL_NONE 0x0
#define CL_READ_ONLY_CACHE 0x1
#define CL_READ_WRITE_CACHE 0x2

/* cl_device_local_mem_type */
#define CL_LOCAL 0x1
#define CL_GLOBAL 0x2

/* cl_device_exec_capabilities - bitfield */
#define CL_EXEC_KERNEL (1 << 0)
#define CL_EXEC_NATIVE_KERNEL (1 << 1)

/* cl_command_queue_properties - bitfield */
#define CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE (1 << 0)
#define CL_QUEUE_PROFILING_ENABLE (1 << 1)

/* cl_context_info  */
#define CL_CONTEXT_REFERENCE_COUNT 0x1080
#define CL_CONTEXT_DEVICES 0x1081
#define CL_CONTEXT_PROPERTIES 0x1082
#define CL_CONTEXT_NUM_DEVICES 0x1083

/* cl_context_properties */
#define CL_CONTEXT_PLATFORM 0x1084
#define CL_CONTEXT_INTEROP_USER_SYNC 0x1085

/* cl_device_partition_property */
#define CL_DEVICE_PARTITION_EQUALLY 0x1086
#define CL_DEVICE_PARTITION_BY_COUNTS 0x1087
#define CL_DEVICE_PARTITION_BY_COUNTS_LIST_END 0x0
#define CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN 0x1088

/* cl_device_affinity_domain */
#define CL_DEVICE_AFFINITY_DOMAIN_NUMA (1 << 0)
#define CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE (1 << 1)
#define CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE (1 << 2)
#define CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE (1 << 3)
#define CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE (1 << 4)
#define CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE (1 << 5)

/* cl_command_queue_info */
#define CL_QUEUE_CONTEXT 0x1090
#define CL_QUEUE_DEVICE 0x1091
#define CL_QUEUE_REFERENCE_COUNT 0x1092
#define CL_QUEUE_PROPERTIES 0x1093

/* cl_mem_flags - bitfield */
#define CL_MEM_READ_WRITE (1 << 0)
#define CL_MEM_WRITE_ONLY (1 << 1)
#define CL_MEM_READ_ONLY (1 << 2)
#define CL_MEM_USE_HOST_PTR (1 << 3)
#define CL_MEM_ALLOC_HOST_PTR (1 << 4)
#define CL_MEM_COPY_HOST_PTR (1 << 5)
// reserved                                         (1 << 6)
#define CL_MEM_HOST_WRITE_ONLY (1 << 7)
#define CL_MEM_HOST_READ_ONLY (1 << 8)
#define CL_MEM_HOST_NO_ACCESS (1 << 9)

/* cl_mem_migration_flags - bitfield */
#define CL_MIGRATE_MEM_OBJECT_HOST (1 << 0)
#define CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED (1 << 1)

/* cl_channel_order */
#define CL_R 0x10B0
#define CL_A 0x10B1
#define CL_RG 0x10B2
#define CL_RA 0x10B3
#define CL_RGB 0x10B4
#define CL_RGBA 0x10B5
#define CL_BGRA 0x10B6
#define CL_ARGB 0x10B7
#define CL_INTENSITY 0x10B8
#define CL_LUMINANCE 0x10B9
#define CL_Rx 0x10BA
#define CL_RGx 0x10BB
#define CL_RGBx 0x10BC
#define CL_DEPTH 0x10BD
#define CL_DEPTH_STENCIL 0x10BE

/* cl_channel_type */
#define CL_SNORM_INT8 0x10D0
#define CL_SNORM_INT16 0x10D1
#define CL_UNORM_INT8 0x10D2
#define CL_UNORM_INT16 0x10D3
#define CL_UNORM_SHORT_565 0x10D4
#define CL_UNORM_SHORT_555 0x10D5
#define CL_UNORM_INT_101010 0x10D6
#define CL_SIGNED_INT8 0x10D7
#define CL_SIGNED_INT16 0x10D8
#define CL_SIGNED_INT32 0x10D9
#define CL_UNSIGNED_INT8 0x10DA
#define CL_UNSIGNED_INT16 0x10DB
#define CL_UNSIGNED_INT32 0x10DC
#define CL_HALF_FLOAT 0x10DD
#define CL_FLOAT 0x10DE
#define CL_UNORM_INT24 0x10DF

/* cl_mem_object_type */
#define CL_MEM_OBJECT_BUFFER 0x10F0
#define CL_MEM_OBJECT_IMAGE2D 0x10F1
#define CL_MEM_OBJECT_IMAGE3D 0x10F2
#define CL_MEM_OBJECT_IMAGE2D_ARRAY 0x10F3
#define CL_MEM_OBJECT_IMAGE1D 0x10F4
#define CL_MEM_OBJECT_IMAGE1D_ARRAY 0x10F5
#define CL_MEM_OBJECT_IMAGE1D_BUFFER 0x10F6

/* cl_mem_info */
#define CL_MEM_TYPE 0x1100
#define CL_MEM_FLAGS 0x1101
#define CL_MEM_SIZE 0x1102
#define CL_MEM_HOST_PTR 0x1103
#define CL_MEM_MAP_COUNT 0x1104
#define CL_MEM_REFERENCE_COUNT 0x1105
#define CL_MEM_CONTEXT 0x1106
#define CL_MEM_ASSOCIATED_MEMOBJECT 0x1107
#define CL_MEM_OFFSET 0x1108

/* cl_image_info */
#define CL_IMAGE_FORMAT 0x1110
#define CL_IMAGE_ELEMENT_SIZE 0x1111
#define CL_IMAGE_ROW_PITCH 0x1112
#define CL_IMAGE_SLICE_PITCH 0x1113
#define CL_IMAGE_WIDTH 0x1114
#define CL_IMAGE_HEIGHT 0x1115
#define CL_IMAGE_DEPTH 0x1116
#define CL_IMAGE_ARRAY_SIZE 0x1117
#define CL_IMAGE_BUFFER 0x1118
#define CL_IMAGE_NUM_MIP_LEVELS 0x1119
#define CL_IMAGE_NUM_SAMPLES 0x111A

/* cl_addressing_mode */
#define CL_ADDRESS_NONE 0x1130
#define CL_ADDRESS_CLAMP_TO_EDGE 0x1131
#define CL_ADDRESS_CLAMP 0x1132
#define CL_ADDRESS_REPEAT 0x1133
#define CL_ADDRESS_MIRRORED_REPEAT 0x1134

/* cl_filter_mode */
#define CL_FILTER_NEAREST 0x1140
#define CL_FILTER_LINEAR 0x1141

/* cl_sampler_info */
#define CL_SAMPLER_REFERENCE_COUNT 0x1150
#define CL_SAMPLER_CONTEXT 0x1151
#define CL_SAMPLER_NORMALIZED_COORDS 0x1152
#define CL_SAMPLER_ADDRESSING_MODE 0x1153
#define CL_SAMPLER_FILTER_MODE 0x1154

/* cl_map_flags - bitfield */
#define CL_MAP_READ (1 << 0)
#define CL_MAP_WRITE (1 << 1)
#define CL_MAP_WRITE_INVALIDATE_REGION (1 << 2)

/* cl_program_info */
#define CL_PROGRAM_REFERENCE_COUNT 0x1160
#define CL_PROGRAM_CONTEXT 0x1161
#define CL_PROGRAM_NUM_DEVICES 0x1162
#define CL_PROGRAM_DEVICES 0x1163
#define CL_PROGRAM_SOURCE 0x1164
#define CL_PROGRAM_BINARY_SIZES 0x1165
#define CL_PROGRAM_BINARIES 0x1166
#define CL_PROGRAM_NUM_KERNELS 0x1167
#define CL_PROGRAM_KERNEL_NAMES 0x1168

/* cl_program_build_info */
#define CL_PROGRAM_BUILD_STATUS 0x1181
#define CL_PROGRAM_BUILD_OPTIONS 0x1182
#define CL_PROGRAM_BUILD_LOG 0x1183
#define CL_PROGRAM_BINARY_TYPE 0x1184

/* cl_program_binary_type */
#define CL_PROGRAM_BINARY_TYPE_NONE 0x0
#define CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT 0x1
#define CL_PROGRAM_BINARY_TYPE_LIBRARY 0x2
#define CL_PROGRAM_BINARY_TYPE_EXECUTABLE 0x4

/* cl_build_status */
#define CL_BUILD_SUCCESS 0
#define CL_BUILD_NONE -1
#define CL_BUILD_ERROR -2
#define CL_BUILD_IN_PROGRESS -3

/* cl_kernel_info */
#define CL_KERNEL_FUNCTION_NAME 0x1190
#define CL_KERNEL_NUM_ARGS 0x1191
#define CL_KERNEL_REFERENCE_COUNT 0x1192
#define CL_KERNEL_CONTEXT 0x1193
#define CL_KERNEL_PROGRAM 0x1194
#define CL_KERNEL_ATTRIBUTES 0x1195

/* cl_kernel_arg_info */
#define CL_KERNEL_ARG_ADDRESS_QUALIFIER 0x1196
#define CL_KERNEL_ARG_ACCESS_QUALIFIER 0x1197
#define CL_KERNEL_ARG_TYPE_NAME 0x1198
#define CL_KERNEL_ARG_TYPE_QUALIFIER 0x1199
#define CL_KERNEL_ARG_NAME 0x119A

/* cl_kernel_arg_address_qualifier */
#define CL_KERNEL_ARG_ADDRESS_GLOBAL 0x119B
#define CL_KERNEL_ARG_ADDRESS_LOCAL 0x119C
#define CL_KERNEL_ARG_ADDRESS_CONSTANT 0x119D
#define CL_KERNEL_ARG_ADDRESS_PRIVATE 0x119E

/* cl_kernel_arg_access_qualifier */
#define CL_KERNEL_ARG_ACCESS_READ_ONLY 0x11A0
#define CL_KERNEL_ARG_ACCESS_WRITE_ONLY 0x11A1
#define CL_KERNEL_ARG_ACCESS_READ_WRITE 0x11A2
#define CL_KERNEL_ARG_ACCESS_NONE 0x11A3

/* cl_kernel_arg_type_qualifer */
#define CL_KERNEL_ARG_TYPE_NONE 0
#define CL_KERNEL_ARG_TYPE_CONST (1 << 0)
#define CL_KERNEL_ARG_TYPE_RESTRICT (1 << 1)
#define CL_KERNEL_ARG_TYPE_VOLATILE (1 << 2)

/* cl_kernel_work_group_info */
#define CL_KERNEL_WORK_GROUP_SIZE 0x11B0
#define CL_KERNEL_COMPILE_WORK_GROUP_SIZE 0x11B1
#define CL_KERNEL_LOCAL_MEM_SIZE 0x11B2
#define CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE 0x11B3
#define CL_KERNEL_PRIVATE_MEM_SIZE 0x11B4
#define CL_KERNEL_GLOBAL_WORK_SIZE 0x11B5

/* cl_event_info  */
#define CL_EVENT_COMMAND_QUEUE 0x11D0
#define CL_EVENT_COMMAND_TYPE 0x11D1
#define CL_EVENT_REFERENCE_COUNT 0x11D2
#define CL_EVENT_COMMAND_EXECUTION_STATUS 0x11D3
#define CL_EVENT_CONTEXT 0x11D4

/* cl_command_type */
#define CL_COMMAND_NDRANGE_KERNEL 0x11F0
#define CL_COMMAND_TASK 0x11F1
#define CL_COMMAND_NATIVE_KERNEL 0x11F2
#define CL_COMMAND_READ_BUFFER 0x11F3
#define CL_COMMAND_WRITE_BUFFER 0x11F4
#define CL_COMMAND_COPY_BUFFER 0x11F5
#define CL_COMMAND_READ_IMAGE 0x11F6
#define CL_COMMAND_WRITE_IMAGE 0x11F7
#define CL_COMMAND_COPY_IMAGE 0x11F8
#define CL_COMMAND_COPY_IMAGE_TO_BUFFER 0x11F9
#define CL_COMMAND_COPY_BUFFER_TO_IMAGE 0x11FA
#define CL_COMMAND_MAP_BUFFER 0x11FB
#define CL_COMMAND_MAP_IMAGE 0x11FC
#define CL_COMMAND_UNMAP_MEM_OBJECT 0x11FD
#define CL_COMMAND_MARKER 0x11FE
#define CL_COMMAND_ACQUIRE_GL_OBJECTS 0x11FF
#define CL_COMMAND_RELEASE_GL_OBJECTS 0x1200
#define CL_COMMAND_READ_BUFFER_RECT 0x1201
#define CL_COMMAND_WRITE_BUFFER_RECT 0x1202
#define CL_COMMAND_COPY_BUFFER_RECT 0x1203
#define CL_COMMAND_USER 0x1204
#define CL_COMMAND_BARRIER 0x1205
#define CL_COMMAND_MIGRATE_MEM_OBJECTS 0x1206
#define CL_COMMAND_FILL_BUFFER 0x1207
#define CL_COMMAND_FILL_IMAGE 0x1208

/* command execution status */
#define CL_COMPLETE 0x0
#define CL_RUNNING 0x1
#define CL_SUBMITTED 0x2
#define CL_QUEUED 0x3

/* cl_buffer_create_type  */
#define CL_BUFFER_CREATE_TYPE_REGION 0x1220

/* cl_profiling_info  */
#define CL_PROFILING_COMMAND_QUEUED 0x1280
#define CL_PROFILING_COMMAND_SUBMIT 0x1281
#define CL_PROFILING_COMMAND_START 0x1282
#define CL_PROFILING_COMMAND_END 0x1283

#ifdef __cplusplus
}
#endif

#endif /* __OPENCL_CL_H */
