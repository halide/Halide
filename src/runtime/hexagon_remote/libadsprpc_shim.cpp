#include <dlfcn.h>
#include <stdint.h>
#include <android/log.h>

// This is a shim to look like we linked to libadsprpc.so, but without
// actually linking to it. The android loader seems to have problems
// with loading libhalide_hexagon_host.so unless we do this.

#define WEAK __attribute__((weak))

namespace {

void *libadsprpc = dlopen("libadsprpc.so", RTLD_LAZY | RTLD_LOCAL);

template <typename T>
T get_libadsprpc_symbol(const char *sym) {
    if (!libadsprpc) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "Failed to load libadsprpc.so");
        return NULL;
    }
    T ret = (T)dlsym(libadsprpc, sym);
    if (!ret) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "Failed to get libadsprpc.so symbol %s", sym);
    }
    return ret;
}

typedef uint32_t remote_handle;
typedef uint64_t remote_handle64;

typedef struct {
    void *ptr;
    size_t size;
} remote_buf;

typedef union {
    remote_buf buf;
    remote_handle handle;
    remote_handle64 handle64;
} remote_arg;

// Due to some ridiculous Android dlopen behavior, we need to dlopen
// and dlsym the symbols from libadsprpc.so ourselves.
typedef int (*remote_handle_open_fn)(const char *, remote_handle *);
typedef int (*remote_handle64_open_fn)(const char *, remote_handle64 *);
typedef int (*remote_handle_invoke_fn)(remote_handle, uint32_t, remote_arg *pra);
typedef int (*remote_handle64_invoke_fn)(remote_handle64, uint32_t, remote_arg *pra);
typedef int (*remote_handle_close_fn)(remote_handle);
typedef int (*remote_handle64_close_fn)(remote_handle64);
typedef int (*remote_mmap_fn)(int, uint32_t, uint32_t, int, uint32_t*);
typedef int (*remote_munmap_fn)(uint32_t, int);
typedef void (*remote_register_buf_fn)(void *, int, int);
typedef int (*remote_set_mode_fn)(uint32_t);

remote_handle_open_fn handle_open_fn = get_libadsprpc_symbol<remote_handle_open_fn>("remote_handle_open");
remote_handle64_open_fn handle64_open_fn = get_libadsprpc_symbol<remote_handle64_open_fn>("remote_handle64_open");
remote_handle_invoke_fn handle_invoke_fn = get_libadsprpc_symbol<remote_handle_invoke_fn>("remote_handle_invoke");
remote_handle64_invoke_fn handle64_invoke_fn = get_libadsprpc_symbol<remote_handle64_invoke_fn>("remote_handle64_invoke");
remote_handle_close_fn handle_close_fn = get_libadsprpc_symbol<remote_handle_close_fn>("remote_handle_close");
remote_handle64_close_fn handle64_close_fn = get_libadsprpc_symbol<remote_handle64_close_fn>("remote_handle64_close");
remote_mmap_fn mmap_fn = get_libadsprpc_symbol<remote_mmap_fn>("remote_mmap");
remote_munmap_fn munmap_fn = get_libadsprpc_symbol<remote_munmap_fn>("remote_munmap");
remote_register_buf_fn register_buf_fn = get_libadsprpc_symbol<remote_register_buf_fn>("remote_register_buf");
remote_set_mode_fn set_mode_fn = get_libadsprpc_symbol<remote_set_mode_fn>("remote_set_mode");

}  // namespace

extern "C" {

// Define these symbols as weak in case the implementation of
// halide_load_library doesn't load the symbols locally. If they are
// replaced by those in libadsprpc.so, that's fine, that's what we're
// trying to do anyways.
WEAK int remote_handle_open(const char *name, remote_handle *h) {
    return handle_open_fn(name, h);
}

WEAK int remote_handle64_open(const char *name, remote_handle64 *h) {
    return handle64_open_fn(name, h);
}

WEAK int remote_handle_invoke(remote_handle h, uint32_t scalars, remote_arg *args) {
    return handle_invoke_fn(h, scalars, args);
}

WEAK int remote_handle64_invoke(remote_handle64 h, uint32_t scalars, remote_arg *args) {
    return handle64_invoke_fn(h, scalars, args);
}

WEAK int remote_handle_close(remote_handle h) {
    return handle_close_fn(h);
}

WEAK int remote_handle64_close(remote_handle64 h) {
    return handle64_close_fn(h);
}

WEAK int remote_mmap(int fd, uint32_t flags, uint32_t addr, int size, uint32_t *result) {
    return mmap_fn(fd, flags, addr, size, result);
}

WEAK int remote_munmap(uint32_t addr, int size) {
    return munmap_fn(addr, size);
}

WEAK void remote_register_buf(void *buf, int size, int fd) {
    // This symbol may not be present.
    if (register_buf_fn) {
        register_buf_fn(buf, size, fd);
    }
}

WEAK int remote_set_mode(uint32_t mode) {
    return set_mode_fn(mode);
}

}  // extern "C"
