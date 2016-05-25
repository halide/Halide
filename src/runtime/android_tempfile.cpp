#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

extern int mkstemps(char *, int);

// Note that the Android implementation is identical to the Posix version, except that
// the root is /data/local/tmp rather than /tmp
WEAK int halide_create_temp_file(void *user_context, const char *prefix, const char *suffix,
                                 char *path_buf, size_t path_buf_size) {
    if (!prefix || !suffix || !path_buf) {
        return halide_error_code_internal_error;
    }
    const char *kTmp = "/data/local/tmp/";
    const char *kWild = "XXXXXX";
    const size_t needed = strlen(kTmp) + strlen(prefix) + strlen(kWild) + strlen(suffix) + 1;
    if (path_buf_size < needed) {
        return halide_error_code_internal_error;
    }
    char *dst = path_buf;
    char *end = path_buf + path_buf_size - 1;
    dst = halide_string_to_string(dst, end, kTmp);
    dst = halide_string_to_string(dst, end, prefix);
    dst = halide_string_to_string(dst, end, kWild);
    dst = halide_string_to_string(dst, end, suffix);
    *dst = 0;
    int fd = mkstemps(path_buf, strlen(suffix));
    if (fd == -1) {
        return halide_error_code_internal_error;
    }
    close(fd);
    return 0;
}

}  // extern "C"
