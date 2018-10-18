#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Qurt {

enum { QURT_EOK = 0 };

}}}} // namespace Halide::Runtime::Internal::Qurt

extern "C" {
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
/**
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

/**
  Sets the thread stack size attribute.\n
  Specifies the size of the memory area to be used for a thread's call stack.

  The thread stack address (Section \ref qurt_thread_attr_set_stack_addr ) and stack size specify the memory area used as a
  call stack for the thread. The user is responsible for allocating the memory area used for
  the stack.

  @datatypes
  #qurt_thread_attr_t

  @param[in,out] attr Thread attribute structure.
  @param[in] stack_size Size (in bytes) of the thread stack.

  @return
  None.
*/
// extern void qurt_thread_attr_set_stack_size(qurt_thread_attr_t *attr, unsigned int stack_size); // pdb remove
static inline void qurt_thread_attr_set_stack_size (qurt_thread_attr_t *attr, unsigned int stack_size)
{
    attr->stack_size = stack_size;
}

/**
  Sets the thread stack address attribute. \n
  Specifies the base address of the memory area to be used for a thread's call stack.

  stack_addr must contain an address value that is 8-byte aligned.

  The thread stack address and stack size (Section \ref qurt_thread_attr_set_stack_size ) specify the memory area used as a
  call stack for the thread. \n
  @note The user is responsible for allocating the memory area used for the thread
             stack. The memory area must be large enough to contain the stack that is
             created by the thread.

  @datatypes
  #qurt_thread_attr_t

  @param[in,out] attr Thread attribute structure.
  @param[in] stack_addr  8-byte aligned address of the thread stack.

  @return
  None.
*/
static inline void qurt_thread_attr_set_stack_addr (qurt_thread_attr_t *attr, void *stack_addr)
{
    attr->stack_addr = stack_addr;
}

/**
  Sets the thread priority to be assigned to a thread.
  Thread priorities are specified as numeric values in the range 1-255, with 1 representing
  the highest priority.

  @datatypes
  #qurt_thread_attr_t

  @param[in,out] attr Thread attribute structure.
  @param[in] priority Thread priority.

  @return
  None.
*/
static inline void qurt_thread_attr_set_priority (qurt_thread_attr_t *attr, unsigned short priority)
{
    attr->priority = priority;
}

extern int qurt_thread_set_priority (qurt_thread_t threadid, unsigned short newprio);
extern int qurt_thread_create (qurt_thread_t *thread_id, qurt_thread_attr_t *attr, void (*entrypoint) (void *), void *arg);
/**
   Waits for a specified thread to finish.
   The specified thread should be another thread within the same process.
   The caller thread is suspended until the specified thread exits. When this happens the
   caller thread is awakened. \n
   @note If the specified thread has already exited, this function returns immediately
              with the result value QURT_ENOTHREAD. \par
   @note Two threads cannot call qurt_thread_join to wait for the same thread to finish.
              If this happens QuRT generates an exception.

   @param[in]   tid     Thread identifier.
   @param[out]  status  Destination variable for thread exit status. Returns an application-defined
                        value indicating the termination status of the specified thread.

   @return
   QURT_ENOTHREAD -- Thread has already exited. \n
   QURT_EOK -- Thread successfully joined with valid status value.
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
extern void qurt_cond_signal(qurt_cond_t *cond);
extern void qurt_cond_wait(qurt_cond_t *cond, qurt_mutex_t *mutex);

typedef enum {
    QURT_HVX_MODE_64B = 0,      /**< HVX mode of 64 bytes */
    QURT_HVX_MODE_128B = 1      /**< HVX mode of 128 bytes */
} qurt_hvx_mode_t;

extern int qurt_hvx_lock(qurt_hvx_mode_t lock_mode);
extern int qurt_hvx_unlock(void);
extern int qurt_hvx_get_mode(void);

typedef unsigned int qurt_size_t;
typedef unsigned int qurt_mem_pool_t;

}
