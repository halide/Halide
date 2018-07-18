#include "runtime_internal.h"
#include "HalideRuntimeQurt.h"
#include "printer.h"
#include "mini_qurt.h"
#include "mini_qurt_vtcm.h"

using namespace Halide::Runtime::Internal::Qurt;

extern "C" {

extern void hap_printf(const char *fmt, ...);

const int max_threads = 4;

WEAK void* halide_request_vtcm(void *user_context, int size, int page) {
    return HAP_request_VTCM(size, page);
}

WEAK void halide_release_vtcm(void *user_context, void *addr) {
    HAP_release_VTCM(addr);
}

WEAK void* halide_vtcm_manager_init(void *user_context) {
    halide_vtcm_manager_t* sync = (halide_vtcm_manager_t *) malloc(sizeof(halide_vtcm_manager_t));
    qurt_mutex_init(&sync->vtcm_slot_mutex);
    qurt_sem_init_val(&sync->vtcm_slot_sem, max_threads);
    for (int i = 0; i < max_threads; i++) {
        sync->avail[i] = true;
    }
    return (void *)sync;
}

WEAK void halide_vtcm_manager_destroy(void *user_context, void* vtcm_manager) {
    halide_vtcm_manager_t* sync = (halide_vtcm_manager_t *) vtcm_manager;
    qurt_mutex_destroy(&sync->vtcm_slot_mutex);
    qurt_sem_destroy(&sync->vtcm_slot_sem);
    free(vtcm_manager);
}

WEAK int halide_get_vtcm_slot(void *user_context, void* vtcm_manager) {
    halide_vtcm_manager_t* sync = (halide_vtcm_manager_t *) vtcm_manager;
    qurt_sem_down(&sync->vtcm_slot_sem);
    qurt_mutex_lock(&sync->vtcm_slot_mutex);
    for (int i = 0; i < max_threads; i++) {
        if (sync->avail[i]) {
            sync->avail[i] = false;
            qurt_mutex_unlock(&sync->vtcm_slot_mutex);
            return i;
        }
    }
    qurt_mutex_unlock(&sync->vtcm_slot_mutex);
    return -1;
}

WEAK int halide_free_vtcm_slot(void *user_context, void* vtcm_manager, int slot) {
    halide_vtcm_manager_t* sync = (halide_vtcm_manager_t *) vtcm_manager;
    qurt_mutex_lock(&sync->vtcm_slot_mutex);
    sync->avail[slot] = true;
    qurt_mutex_unlock(&sync->vtcm_slot_mutex);
    qurt_sem_up(&sync->vtcm_slot_sem);
    return 0;
}

__attribute__((always_inline))
WEAK int halide_scatter_release(void *ptr, int offset) {
    char* store_at = (char *)ptr + offset;
    __asm__ __volatile__ ("vmem(%0 + #0):scatter_release\n;": "+m" (*store_at) : : );
    return 0;
}

}
