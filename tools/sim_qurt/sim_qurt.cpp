#include "hexagon_standalone.h"

extern "C" {

int qurt_hvx_lock(int mode) {
    SIM_ACQUIRE_HVX;
    if (mode == 0) {
        SIM_CLEAR_HVX_DOUBLE_MODE;
    } else {
        SIM_SET_HVX_DOUBLE_MODE;
    }
    return 0;
}

int qurt_hvx_unlock() {
    SIM_RELEASE_HVX;
    return 0;
}

void qurt_sem_init_val(void *ptr, int val) {}
void qurt_sem_destroy(void *ptr) {}
int qurt_sem_down(void *ptr) {return 0;}
int qurt_sem_up(void *ptr) {return 0;}
int qurt_sem_add(void *ptr, int val) {return 0;}
void qurt_mutex_init(void *ptr) {}
void qurt_mutex_destroy(void *ptr) {}
void qurt_mutex_lock(void *ptr) {}
void qurt_mutex_unlock(void *ptr) {}

void* HAP_request_VTCM(unsigned int size, unsigned int single_page_flag) {
    return (void *)0xd8200000;
}

int HAP_release_VTCM(void *pVA) {
    return 0;
}

}  // extern "C"
