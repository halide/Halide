#include "HalideRuntime.h"
extern "C" {
extern void *memalign(size_t, size_t);

typedef unsigned int qurt_thread_t;
/*
   Macros for QuRT thread attributes.   
 */

#define QURT_HTHREAD_L1I_PREFETCH      0x1     /**< Enables hardware L1 instruction cache prefetching. */
#define QURT_HTHREAD_L1D_PREFETCH      0x2     /**< Enables hardware L1 data cache prefetching. */
#define QURT_HTHREAD_L2I_PREFETCH      0x4     /**< Enables hardware L2 instruction cache prefetching. */
#define QURT_HTHREAD_L2D_PREFETCH      0x8     /**< Enables hardware L2 data cache prefetching. */
#define QURT_HTHREAD_DCFETCH           0x10    /**< Enables DC fetch to the provided virtual address. 
                                                   DC fetch instructs the hardware that a data memory access is likely.
                                                   Instructions are dropped in the case of high bus utilization. */



#define QURT_THREAD_ATTR_NAME_MAXLEN            16  /**< */
#define QURT_THREAD_ATTR_TCB_PARTITION_RAM      0  /**< Creates threads in RAM/DDR. */
#define QURT_THREAD_ATTR_TCB_PARTITION_TCM      1  /**< Creates threads in TCM. */
#define QURT_THREAD_ATTR_TCB_PARTITION_DEFAULT  QURT_THREAD_ATTR_TCB_PARTITION_RAM  /**< Backward compatibility. */
#define QURT_THREAD_ATTR_PRIORITY_DEFAULT       256  /**< */
#define QURT_THREAD_ATTR_ASID_DEFAULT           0  /**< */
#define QURT_THREAD_ATTR_AFFINITY_DEFAULT      (-1)  /**< */
#define QURT_THREAD_ATTR_BUS_PRIO_DEFAULT       255  /**< */
#define QURT_THREAD_ATTR_TIMETEST_ID_DEFAULT   (-2)  /**< */
/** @} */ /* end_addtogroup thread_macros */

/** Thread attributes */
typedef struct _qurt_thread_attr {
    /** @cond */
    char name[QURT_THREAD_ATTR_NAME_MAXLEN]; /**< Thread name. */
    unsigned char tcb_partition;  /**< Should the thread TCB reside in RAM or
                                       on chip memory (i.e. TCM). */
    unsigned char  affinity;      /**< Hardware bitmask indicating the threads it
                                       can run on. */
    unsigned short priority;      /**< Thread priority. */
    unsigned char  asid;          /**< Address space ID. */
    unsigned char  bus_priority;  /**< internal bus priority. */
    unsigned short timetest_id;   /**< Timetest ID. */
    unsigned int   stack_size;    /**< Thread stack size. */
    void *stack_addr;             /**< Stack address base, the range of the stack is
                                       (stack_addr, stack_addr+stack_size-1). */
    /** @endcond */
} qurt_thread_attr_t;

/*=============================================================================
												FUNCTIONS
=============================================================================*/
/**@ingroup func_qurt_thread_attr_init
  Initializes the structure used to set the thread attributes when a thread is created.
  After an attribute structure is initialized, the individual attributes in the structure can be
  explicitly set using the thread attribute operations.

  The default attribute values set the by the initialize operation are the following: \n
  - Name -- Null string \n
  - Timetest ID -- QURT_THREAD_ATTR_TIMETEST_ID_DEFAULT \n
  - Priority -- QURT_THREAD_ATTR_PRIORITY_DEFAULT \n
  - Affinity -- QURT_THREAD_ATTR_AFFINITY_DEFAULT \n
  - Bus priority -- QURT_THREAD_ATTR_BUS_PRIO_DEFAULT \n
  - TCB partition -- QURT_THREAD_ATTR_TCB_PARTITION_DEFAULT
  - stack_size -- zero
  - stack_addr -- zero

  @datatypes
  #qurt_thread_attr_t
  
  @param[in,out] attr Thread attribute structure.

  @return
  None.

  @dependencies
  None.
*/
// extern void qurt_thread_attr_init(qurt_thread_attr_t *attr); //pdb remove
static inline void qurt_thread_attr_init (qurt_thread_attr_t *attr)
{

    attr->name[0] = 0;
    attr->tcb_partition = QURT_THREAD_ATTR_TCB_PARTITION_DEFAULT;
    attr->priority = QURT_THREAD_ATTR_PRIORITY_DEFAULT;
    attr->asid = QURT_THREAD_ATTR_ASID_DEFAULT;
    attr->affinity = QURT_THREAD_ATTR_AFFINITY_DEFAULT;
    attr->bus_priority = QURT_THREAD_ATTR_BUS_PRIO_DEFAULT;
    attr->timetest_id = QURT_THREAD_ATTR_TIMETEST_ID_DEFAULT;
    attr->stack_size = 0;
    attr->stack_addr = 0;
}

/**@ingroup func_qurt_thread_attr_set_stack_size
  @xreflabel{sec:set_stack_size}
  Sets the thread stack size attribute.\n
  Specifies the size of the memory area to be used for a thread's call stack.

  The thread stack address (Section @xref{sec:set_stack_addr}) and stack size specify the memory area used as a
  call stack for the thread. The user is responsible for allocating the memory area used for
  the stack.

  @datatypes
  #qurt_thread_attr_t

  @param[in,out] attr Thread attribute structure.
  @param[in] stack_size Size (in bytes) of the thread stack.

  @return
  None.

  @dependencies
  None.
*/
// extern void qurt_thread_attr_set_stack_size(qurt_thread_attr_t *attr, unsigned int stack_size); // pdb remove
static inline void qurt_thread_attr_set_stack_size (qurt_thread_attr_t *attr, unsigned int stack_size)
{
    attr->stack_size = stack_size;
}

/**@ingroup func_qurt_thread_attr_set_stack_addr
  @xreflabel{sec:set_stack_addr}
  Sets the thread stack address attribute. \n
  Specifies the base address of the memory area to be used for a thread's call stack.

  stack_addr must contain an address value that is 8-byte aligned.

  The thread stack address and stack size (Section @xref{sec:set_stack_size}) specify the memory area used as a
  call stack for the thread. \n
  @note1hang The user is responsible for allocating the memory area used for the thread
             stack. The memory area must be large enough to contain the stack that is
             created by the thread.

  @datatypes
  #qurt_thread_attr_t
  
  @param[in,out] attr Thread attribute structure.
  @param[in] stack_addr  8-byte aligned address of the thread stack.

  @return
  None.

  @dependencies
  None.
*/
static inline void qurt_thread_attr_set_stack_addr (qurt_thread_attr_t *attr, void *stack_addr)
{
    attr->stack_addr = stack_addr;
}

/**@ingroup func_qurt_thread_attr_set_priority
  Sets the thread priority to be assigned to a thread.
  Thread priorities are specified as numeric values in the range 1-255, with 1 representing
  the highest priority.

  @datatypes
  #qurt_thread_attr_t

  @param[in,out] attr Thread attribute structure.
  @param[in] priority Thread priority.

  @return
  None.

  @dependencies
  None.
*/
static inline void qurt_thread_attr_set_priority (qurt_thread_attr_t *attr, unsigned short priority)
{
    attr->priority = priority;
}

extern int qurt_thread_set_priority (qurt_thread_t threadid, unsigned short newprio);
extern int qurt_thread_create (qurt_thread_t *thread_id, qurt_thread_attr_t *attr, void (*entrypoint) (void *), void *arg);
/**@ingroup func_qurt_thread_join
   @xreflabel{sec:thread_join}
   Waits for a specified thread to finish.
   The specified thread should be another thread within the same process.
   The caller thread is suspended until the specified thread exits. When this happens the
   caller thread is awakened. \n
   @note1hang If the specified thread has already exited, this function returns immediately
              with the result value QURT_ENOTHREAD. \n
   @note1cont Two threads cannot call qurt_thread_join to wait for the same thread to finish.
              If this happens QuRT generates an exception (see Section @xref{sec:exceptionHandling}).
  
   @param[in]   tid     Thread identifier.
   @param[out]  status  Destination variable for thread exit status. Returns an application-defined 
                        value indicating the termination status of the specified thread. 
  
   @return  
   QURT_ENOTHREAD -- Thread has already exited. \n
   QURT_EOK -- Thread successfully joined with valid status value. 

   @dependencies
   None.
 */
extern int qurt_thread_join(unsigned int tid, int *status);

/** QuRT mutex type.

   Both non-recursive mutex lock/unlock and recursive
   mutex lock/unlock can be applied to this type.
 */
typedef union qurt_mutex_aligned8{
   /** @cond */
    struct {
        unsigned int holder;
        unsigned int count;
        unsigned int queue;
        unsigned int wait_count;
    };
    unsigned long long int raw;
    /** @endcond */
} qurt_mutex_t;


/** QuRT condition variable type.  */
typedef union {
    /** @cond */
    unsigned long long raw;
    struct {
        unsigned int count;
        unsigned int n_waiting;
        unsigned int queue;
        unsigned int reserved;
    }X;
    /** @endcond */
} qurt_cond_t;

extern void qurt_mutex_init(qurt_mutex_t *lock);
extern void qurt_mutex_destroy(qurt_mutex_t *lock);
extern void qurt_mutex_lock(qurt_mutex_t *lock);       /* blocking */
extern void qurt_mutex_unlock(qurt_mutex_t *lock); /* unlock */

extern void qurt_cond_init(qurt_cond_t *cond);
extern void qurt_cond_destroy(qurt_cond_t *cond);
extern void qurt_cond_broadcast(qurt_cond_t *cond);
extern void qurt_cond_wait(qurt_cond_t *cond, qurt_mutex_t *mutex);

typedef enum {
    QURT_HVX_MODE_64B = 0,      /**< HVX mode of 64 bytes */
    QURT_HVX_MODE_128B = 1      /**< HVX mode of 128 bytes */
} qurt_hvx_mode_t;

extern int qurt_hvx_lock(qurt_hvx_mode_t lock_mode);
extern int qurt_hvx_unlock(void);
extern int qurt_hvx_get_mode(void);
#define QURT_EOK                             0  /**< Operation successfully performed. */


extern qurt_thread_t qurt_thread_get_id (void);
} // extern C
struct halide_thread {
    qurt_thread_t val;
};

int halide_host_cpu_count() {
    // Assume a Snapdragon 820
    return 4;
}

namespace {
struct spawned_thread {
    void (*f)(void *);
    void *closure;
    void *stack;
    halide_thread handle;
};
void spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
}
}

#define STACK_SIZE 256*1024

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->stack = memalign(128, STACK_SIZE);
    memset(&t->handle, 0, sizeof(t->handle));
    qurt_thread_attr_t thread_attr;
    qurt_thread_attr_init(&thread_attr);
    qurt_thread_attr_set_stack_addr(&thread_attr, t->stack);
    qurt_thread_attr_set_stack_size(&thread_attr, STACK_SIZE);
    qurt_thread_attr_set_priority(&thread_attr, 255);
    qurt_thread_create(&t->handle.val, &thread_attr, &spawn_thread_helper, t);
    return (halide_thread *)t;
}

WEAK void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    int ret = 0;
    qurt_thread_join(t->handle.val, &ret);
    free(t->stack);
    free(t);
}

WEAK void halide_mutex_lock(halide_mutex *mutex) {
    qurt_mutex_lock((qurt_mutex_t *)mutex);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
    qurt_mutex_unlock((qurt_mutex_t *)mutex);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex) {
    qurt_mutex_destroy((qurt_mutex_t *)mutex);
    memset(mutex, 0, sizeof(halide_mutex));
}

// struct halide_cond {
//     uint64_t _private[8];
// };

WEAK void halide_cond_init(struct halide_cond *cond) {
    qurt_cond_init((qurt_cond_t *)cond);
}

WEAK void halide_cond_destroy(struct halide_cond *cond) {
    qurt_cond_destroy((qurt_cond_t *)cond);
}

WEAK void halide_cond_broadcast(struct halide_cond *cond) {
    qurt_cond_broadcast((qurt_cond_t *)cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    qurt_cond_wait((qurt_cond_t *)cond, (qurt_mutex_t *)mutex);
}

#include "thread_pool_common.h"

namespace {
// We wrap the closure passed to jobs with extra info we
// need. Currently just the hvx mode to use.
struct wrapped_closure {
    uint8_t *closure;
    int hvx_mode;
};
}

extern "C" {


// There are two locks at play: the thread pool lock and the hvx
// context lock. To ensure there's no way anything could ever
// deadlock, we never attempt to acquire one while holding the
// other.
    // void (*dtor_pdb)() = &halide_thread_pool_cleanup;
WEAK int halide_do_par_for(void *user_context,
                      halide_task_t task,
                      int min, int size, uint8_t *closure) {
    // Get the work queue mutex. We need to do a handful of hexagon-specific things.
    qurt_mutex_t *mutex = (qurt_mutex_t *)(&work_queue.mutex);
    if (!work_queue.initialized) {
        // The thread pool asssumes that a zero-initialized mutex can
        // be locked. Not true on hexagon, and there doesn't seem to
        // be an init_once mechanism either. In this shim binary, it's
        // safe to assume that the first call to halide_do_par_for is
        // done by the main thread, so there's no race condition on
        // initializing this mutex.
        qurt_mutex_init(mutex);
    }

    wrapped_closure c = {closure, qurt_hvx_get_mode()};

    // Set the desired number of threads based on the current HVX
    // mode.
    int old_num_threads =
        halide_set_num_threads((c.hvx_mode == QURT_HVX_MODE_128B) ? 2 : 2);
    // We're about to acquire the thread-pool lock, so we must drop
    // the hvx context lock, even though we'll likely reacquire it
    // immediately to do some work on this thread.
    if (c.hvx_mode != -1) {
        // The docs say that qurt_hvx_get_mode should return -1 when
        // "not available". However, it appears to actually return 0,
        // which is the value of QURT_HVX_MODE_64B!  This means that
        // if we enter a do_par_for with HVX unlocked, we will leave
        // it with HVX locked in 64B mode, which then never gets
        // unlocked (a major bug).

        // To avoid this, we need to know if we are actually locked in
        // 64B mode, or not locked. To do this, we can look at the
        // return value of qurt_hvx_unlock, which returns an error if
        // we weren't already locked.
        if (qurt_hvx_unlock() != QURT_EOK) {
            c.hvx_mode = -1;
        }
    }
    int ret = halide_default_do_par_for(user_context, task, min, size, (uint8_t *)&c);

    if (c.hvx_mode != -1) {
        qurt_hvx_lock((qurt_hvx_mode_t)c.hvx_mode);
    }

    // Set the desired number of threads back to what it was, in case
    // we're a 128 job and we were sharing the machine with a 64 job.
    halide_set_num_threads(old_num_threads);
    return ret;
}

WEAK int halide_do_task(void *user_context, halide_task_t f,
                   int idx, uint8_t *closure) {
    // Dig the appropriate hvx mode out of the wrapped closure and lock it.
    wrapped_closure *c = (wrapped_closure *)closure;
    // We don't own the thread-pool lock here, so we can safely
    // acquire the hvx context lock (if needed) to run some code.

    if (c->hvx_mode != -1) {
        qurt_hvx_lock((qurt_hvx_mode_t)c->hvx_mode);
        int ret = f(user_context, idx, c->closure);
        qurt_hvx_unlock();
        return ret;
    } else {
        return f(user_context, idx, c->closure);
    }
}
namespace {
__attribute__((destructor))
WEAK void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}
}
