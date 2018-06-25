#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

/** QuRT semaphore type.   */
typedef union {
    /** @cond */
  unsigned int raw[2] __attribute__((aligned(8)));
  struct {
    unsigned short val;        /**< */
    unsigned short n_waiting;  /**< */
        unsigned int reserved1;    /**< */
        unsigned int queue;       /**< */
        unsigned int reserved2;    /**< */
  }X; /** @endcond */
} qurt_sem_t;

extern void qurt_sem_init_val(qurt_sem_t *sem, unsigned short val);
extern int qurt_sem_down(qurt_sem_t *sem);
extern int qurt_sem_add(qurt_sem_t *sem, unsigned int amt);
static inline int qurt_sem_up(qurt_sem_t *sem) { return qurt_sem_add(sem,1); }
static inline unsigned short qurt_sem_get_val(qurt_sem_t *sem ){return sem->X.val;}
extern void qurt_sem_destroy(qurt_sem_t *sem);

extern void* HAP_request_VTCM (unsigned int size, unsigned int single_page_flag);
extern int HAP_release_VTCM (void* pVA);

typedef struct halide_vtcm_sync_t {
    qurt_mutex_t vtcm_slot_mutex;
    qurt_sem_t vtcm_slot_sem;
    bool avail[4];
} halide_vtcm_manager_t;

}
