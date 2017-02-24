#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <android/log.h>

namespace {

// Allocations that are intended to be shared with Hexagon can be
// shared without copying if they are contiguous in physical
// memory. Android's ION allocator gives us a mechanism with which we
// can allocate contiguous physical memory.
enum ion_heap_id {
    system_heap_id = 25,
};

enum ion_flags {
    ion_flag_cached = 1,
};

typedef int ion_user_handle_t;

struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    ion_user_handle_t handle;
};

struct ion_fd_data {
    ion_user_handle_t handle;
    int fd;
};

struct ion_handle_data {
    ion_user_handle_t handle;
};

#define ION_IOC_ALLOC _IOWR('I', 0, ion_allocation_data)
#define ION_IOC_FREE _IOWR('I', 1, ion_handle_data)
#define ION_IOC_MAP _IOWR('I', 2, ion_fd_data)

ion_user_handle_t ion_alloc(int ion_fd, size_t len, size_t align, unsigned int heap_id_mask, unsigned int flags) {
    ion_allocation_data alloc = {
        len,
        align,
        heap_id_mask,
        flags,
        0
    };
    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        return -1;
    }
    return alloc.handle;
}

int ion_map(int ion_fd, ion_user_handle_t handle) {
    ion_fd_data data = {
        handle,
        0
    };
    if (ioctl(ion_fd, ION_IOC_MAP, &data) < 0) {
        return -1;
    }
    return data.fd;
}

int ion_free(int ion_fd, ion_user_handle_t ion_handle) {
    if(ioctl(ion_fd, ION_IOC_FREE, &ion_handle) < 0) {
        return -1;
    }
    return 0;
}

// We need to be able to keep track of the size and some other
// information about ION allocations, so define a simple linked list
// of allocations we can traverse later.
struct allocation_record {
    allocation_record *next;
    ion_user_handle_t handle;
    int buf_fd;
    void *buf;
    size_t size;
};

// Make a dummy allocation so we don't need a special case for the
// head list node.
allocation_record allocations = { NULL, };
pthread_mutex_t allocations_mutex = PTHREAD_MUTEX_INITIALIZER;

int ion_fd = -1;

}  // namespace

extern "C" {

// If this symbol is defined in the stub library we are going to link
// to, we need to call this in order to actually get zero copy
// behavior from our buffers.
__attribute__((weak)) void remote_register_buf(void* buf, int size, int fd);

void halide_hexagon_host_malloc_init() {
    pthread_mutex_init(&allocations_mutex, NULL);
    ion_fd = open("/dev/ion", O_RDONLY, 0);
    if (ion_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "open('/dev/ion') failed");
    }
}

void halide_hexagon_host_malloc_deinit() {
    close(ion_fd);
    ion_fd = -1;
    pthread_mutex_destroy(&allocations_mutex);
}

void *halide_hexagon_host_malloc(size_t size) {
    const int heap_id = system_heap_id;
    const int ion_flags = ion_flag_cached;

    // Hexagon can only access a small number of mappings of these
    // sizes. We reduce the number of mappings required by aligning
    // large allocations to these sizes.
    static const size_t alignments[] = { 0x1000, 0x4000, 0x10000, 0x40000, 0x100000 };
    size_t alignment = alignments[0];

    // Align the size up to the minimum alignment.
    size = (size + alignment - 1) & ~(alignment - 1);

    if (heap_id != system_heap_id) {
        for (size_t i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
            if (size >= alignments[i]) {
                alignment = alignments[i];
            }
        }
    }

    ion_user_handle_t handle = ion_alloc(ion_fd, size, alignment, 1 << heap_id, ion_flags);
    if (handle < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "ion_alloc(%d, %d, %d, %d, %d) failed",
                            ion_fd, size, alignment, 1 << heap_id, ion_flags);
        return NULL;
    }

    // Map the ion handle to a file buffer.
    int buf_fd = ion_map(ion_fd, handle);
    if (buf_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "ion_map(%d, %d) failed", ion_fd, handle);
        ion_free(ion_fd, handle);
        return NULL;
    }

    // Map the file buffer to a pointer.
    void *buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    if (buf == MAP_FAILED) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "mmap(NULL, %d, PROT_READ | PROT_WRITE, MAP_SHARED, %d, 0) failed",
                            size, buf_fd);
        close(buf_fd);
        ion_free(ion_fd, handle);
        return NULL;
    }

    // Register the buffer, so we get zero copy.
    if (remote_register_buf) {
        remote_register_buf(buf, size, buf_fd);
    }

    // Build a record for this allocation.
    allocation_record *rec = (allocation_record *)malloc(sizeof(allocation_record));
    if (!rec) {
        __android_log_print(ANDROID_LOG_ERROR, "halide", "malloc failed");
        munmap(buf, size);
        close(buf_fd);
        ion_free(ion_fd, handle);
        return NULL;
    }

    rec->next = NULL;
    rec->handle = handle;
    rec->buf_fd = buf_fd;
    rec->buf = buf;
    rec->size = size;

    // Insert this record into the list of allocations. Insert it at
    // the front, since it's simpler, and most likely to be freed
    // next.
    pthread_mutex_lock(&allocations_mutex);
    rec->next = allocations.next;
    allocations.next = rec;
    pthread_mutex_unlock(&allocations_mutex);

    return buf;
}

void halide_hexagon_host_free(void *ptr) {
    if (!ptr) {
        return;
    }

    // Find the record for this allocation and remove it from the list.
    pthread_mutex_lock(&allocations_mutex);
    allocation_record *rec = &allocations;
    while (rec) {
        allocation_record *cur = rec;
        rec = rec->next;
        if (rec && rec->buf == ptr) {
            cur->next = rec->next;
            break;
        }
    }
    pthread_mutex_unlock(&allocations_mutex);
    if (!rec) {
        __android_log_print(ANDROID_LOG_WARN, "halide", "Allocation not found in allocation records");
        return;
    }

    // Unregister the buffer.
    if (remote_register_buf) {
        remote_register_buf(rec->buf, rec->size, -1);
    }

    // Unmap the memory
    munmap(rec->buf, rec->size);

    // free the ION allocation
    close(rec->buf_fd);
    if (ion_free(ion_fd, rec->handle) < 0) {
        __android_log_print(ANDROID_LOG_WARN, "halide", "ion_free(%d, %d) failed", ion_fd, rec->handle);
    }

    free(rec);
}

}  // extern "C"
