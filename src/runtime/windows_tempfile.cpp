#include "HalideRuntime.h"

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

#define MAX_PATH 260

extern "C" {

extern uint32_t WIN32API GetTempPathA(uint32_t, char *);
extern uint32_t WIN32API GetTempFileNameA(const char *, const char *, uint32_t, char *);

WEAK int halide_create_temp_file(void *user_context, const char *prefix, const char *suffix,
                                 char *path_buf, size_t path_buf_size) {
    if (!prefix || !suffix || !path_buf) {
        return halide_error_code_internal_error;
    }
    // Windows implementations of mkstemp() try to create the file in the root
    // directory, which is... problematic.
    char tmp_dir_path[MAX_PATH];
    uint32_t ret = GetTempPathA(MAX_PATH, tmp_dir_path);
    if (ret != 0) {
        return halide_error_code_internal_error;
    }
    // GetTempFileName doesn't allow us to specify a custom suffix, so if
    // on is requested, fail.
    if (strlen(suffix) > 0) {
        return halide_error_code_internal_error;
    }
    // GetTempFileName doesn't accept a buffer length, so require
    // the input value to be at least MAX_PATH.
    if (path_buf_size < MAX_PATH) {
        return halide_error_code_internal_error;
    }
    // Note that GetTempFileName() actually creates the file.
    ret = GetTempFileNameA(tmp_dir_path, prefix, 0, path_buf);
    if (ret != 0) {
        return halide_error_code_internal_error;
    }
    return 0;
}

}  // extern "C"
