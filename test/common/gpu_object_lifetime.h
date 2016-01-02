#ifndef GPU_OBJECT_LIFETIME_H
#define GPU_OBJECT_LIFETIME_H

#include <stdio.h>
#include <string.h>

struct ObjectType {
    const char *created;
    const char *destroyed;
    bool is_global;
    int total_created;
    int live_count;

    ObjectType(const char *created, const char *destroyed, bool is_global = false) :
        created(created), destroyed(destroyed),
        is_global(is_global), total_created(0), live_count(0) {}
};

ObjectType object_types[] = {
    ObjectType("clCreateContext", "clReleaseContext", true),
    ObjectType("clCreateCommandQueue", "clReleaseCommandQueue", true),
    // This handles both "clCreateProgramWithSource" and
    // "clCreateProgramWithBinary".
    ObjectType("clCreateProgram", "clReleaseProgram"),
    ObjectType("clCreateBuffer", "clReleaseMemObject"),
    ObjectType("clCreateKernel", "clReleaseKernel"),

    ObjectType("cuCtxCreate", "cuCtxDestroy", true),
    ObjectType("cuModuleLoad", "cuModuleUnload"),
    ObjectType("cuMemAlloc", "cuMemFree"),

    ObjectType("Allocating: MTLCreateSystemDefaultDevice", "Releasing: MTLCreateSystemDefaultDevice", true),
    ObjectType("Allocating: new_command_queue", "Releasing: new_command_queue"),
    ObjectType("Allocating: new_library_with_source", "Releasing: new_library_with_source")
};

const int object_type_count = sizeof(object_types)/sizeof(object_types[0]);

// Parse a line of output from gpu_debug and update object counts.
static void record_gpu_debug(const char *str) {
    for (int i = 0; i < object_type_count; ++i) {
        if (strstr(str, object_types[i].created)) {
            object_types[i].total_created++;
            object_types[i].live_count++;
        }
        else if (strstr(str, object_types[i].destroyed)) {
            object_types[i].live_count--;
        }
    }
}

// Check that there are no live objects remaining, and we created at least one object.
static int validate_gpu_object_lifetime(bool allow_globals, bool allow_none, int max_globals) {
    int total = 0;
    for (int i = 0; i < object_type_count; i++) {
        if (object_types[i].live_count != 0 &&
            !(allow_globals && object_types[i].is_global)) {
            printf("Error! %d objects created by %s still live\n",
                   object_types[i].live_count, object_types[i].created);
            return -1;
        }
        if (object_types[i].is_global && object_types[i].total_created > max_globals) {
            printf("Error! %d global objects created by %s, max is %d\n",
                   object_types[i].total_created, object_types[i].created, max_globals);
            return -1;
        }

        total += object_types[i].total_created;
    }
    if (!allow_none && total == 0) {
        printf("Error! No objects created. Ensure gpu_debug is set, ");
        printf("and record_gpu_debug is called from halide_print.\n");
        return -1;
    }
    return 0;
}

#endif
