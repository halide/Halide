#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/types.h>

#include "bin/src/halide_hexagon_remote.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

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

void ion_free(int ion_fd, ion_user_handle_t ion_handle) {
    if(ioctl(ion_fd, ION_IOC_FREE, &ion_handle) < 0) {
    }
}

struct allocation_record {
    allocation_record *next;
    int ion_fd;
    ion_user_handle_t handle;
    int buf_fd;
    void *buf;
    size_t size;
};

// Make a dummy allocation so we don't need a special case for the head node.
allocation_record allocations = { NULL, };
pthread_mutex_t allocations_mutex;

extern "C" {

__attribute__((weak)) void remote_register_buf(void* buf, int size, int fd);

void halide_hexagon_host_malloc_init() {
    pthread_mutex_init(&allocations_mutex, NULL);
}

void halide_hexagon_host_malloc_deinit() {
    pthread_mutex_destroy(&allocations_mutex);
}

void *halide_hexagon_host_malloc(size_t size) {
    int ion_fd = open("/dev/ion", O_RDONLY, 0);
    if (ion_fd < 0) {
        return NULL;
    }

    const int heap_id = 25;  // system heap
    const int ion_flags = 1;  // cached

    // Align the size up to pages.
    size = (size + 0x1000 - 1) & ~(0x1000 - 1);

    // Hexagon can only access a small number of mappings of these
    // sizes. We reduce the number of mappings required by aligning
    // large allocations to these sizes.
    static const size_t alignments[] = { 0x1000, 0x4000, 0x10000, 0x40000, 0x100000 };
    size_t alignment = alignments[0];
    if (heap_id != 25) {
        for (size_t i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
            if (size >= alignments[i]) {
                alignment = alignments[i];
            }
        }
    }

    ion_user_handle_t handle = ion_alloc(ion_fd, size, alignment, 1 << heap_id, ion_flags);
    if (handle < 0) {
        close(ion_fd);
        return NULL;
    }

    // Map the ion handle to a file buffer.
    int buf_fd = ion_map(ion_fd, handle);
    if (buf_fd < 0) {
        ion_free(ion_fd, handle);
        close(ion_fd);
        return NULL;
    }

    // Map the file buffer to a pointer.
    void *buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    if (buf == MAP_FAILED) {
        ion_free(ion_fd, handle);
        close(ion_fd);
        return NULL;
    }

    // Register the buffer, so we get zero copy.
    if (remote_register_buf) {
        remote_register_buf(buf, size, buf_fd);
    }

    // Build a record for this allocation.
    allocation_record *rec = (allocation_record *)malloc(sizeof(allocation_record));
    if (!rec) {
        munmap(buf, size);
        ion_free(ion_fd, handle);
        close(ion_fd);
        return NULL;
    }

    rec->next = NULL;
    rec->ion_fd = ion_fd;
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
    while (rec->next) {
        if (rec->next->buf == ptr) {
            allocation_record *before_rec = rec;
            rec = before_rec->next;
            before_rec->next = before_rec->next->next;
            break;
        } else {
            rec = rec->next;
        }
    }
    pthread_mutex_unlock(&allocations_mutex);
    if (!rec) {
        // Bad to get here... freeing junk memory?
        return;
    }

    // Unregister the buffer.
    if (remote_register_buf) {
        remote_register_buf(rec->buf, rec->size, -1);
    }

    // Unmap the memory
    munmap(rec->buf, rec->size);

    // free the ION allocation
    ion_free(rec->ion_fd, rec->handle);

    // close the ion handle
    close(rec->ion_fd);

    free(rec);
}

// This is a shim for calling v2 from v1.
handle_t halide_hexagon_remote_get_symbol(handle_t module_ptr,
                                          const char* name, int nameLen) {
    handle_t sym = 0;
    int result = halide_hexagon_remote_get_symbol_v2(module_ptr, name, nameLen, &sym);
    return result == 0 ? sym : 0;
}

}  // extern "C"
