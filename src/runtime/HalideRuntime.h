#ifndef HALIDE_HALIDERUNTIME_H
#define HALIDE_HALIDERUNTIME_H

#ifndef COMPILING_HALIDE_RUNTIME
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif
#else
#include "runtime_internal.h"
#endif

#ifdef __cplusplus
// Forward declare type to allow naming typed handles.
// See Type.h for documentation.
template<typename T>
struct halide_handle_traits;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
// Note that (for MSVC) you should not use "inline" along with HALIDE_ALWAYS_INLINE;
// it is not necessary, and may produce warnings for some build configurations.
#define HALIDE_ALWAYS_INLINE __forceinline
#define HALIDE_NEVER_INLINE __declspec(noinline)
#else
// Note that (for Posixy compilers) you should always use "inline" along with HALIDE_ALWAYS_INLINE;
// otherwise some corner-case scenarios may erroneously report link errors.
#define HALIDE_ALWAYS_INLINE inline __attribute__((always_inline))
#define HALIDE_NEVER_INLINE __attribute__((noinline))
#endif

#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
// C++17 or later
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
// Clang/GCC
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif

/** \file
 *
 * This file declares the routines used by Halide internally in its
 * runtime. On platforms that support weak linking, these can be
 * replaced with user-defined versions by defining an extern "C"
 * function with the same name and signature.
 *
 * When doing Just In Time (JIT) compilation members of
 * some_pipeline_or_func.jit_handlers() must be replaced instead. The
 * corresponding methods are documented below.
 *
 * All of these functions take a "void *user_context" parameter as their
 * first argument; if the Halide kernel that calls back to any of these
 * functions has been compiled with the UserContext feature set on its Target,
 * then the value of that pointer passed from the code that calls the
 * Halide kernel is piped through to the function.
 *
 * Some of these are also useful to call when using the default
 * implementation. E.g. halide_shutdown_thread_pool.
 *
 * Note that even on platforms with weak linking, some linker setups
 * may not respect the override you provide. E.g. if the override is
 * in a shared library and the halide object files are linked directly
 * into the output, the builtin versions of the runtime functions will
 * be called. See your linker documentation for more details. On
 * Linux, LD_DYNAMIC_WEAK=1 may help.
 *
 */

// Forward-declare to suppress warnings if compiling as C.
struct halide_buffer_t;

/** Print a message to stderr. Main use is to support tracing
 * functionality, print, and print_when calls. Also called by the default
 * halide_error.  This function can be replaced in JITed code by using
 * halide_custom_print and providing an implementation of halide_print
 * in AOT code. See Func::set_custom_print.
 */
// @{
extern void halide_print(void *user_context, const char *);
extern void halide_default_print(void *user_context, const char *);
typedef void (*halide_print_t)(void *, const char *);
extern halide_print_t halide_set_custom_print(halide_print_t print);
// @}

/** Halide calls this function on runtime errors (for example bounds
 * checking failures). This function can be replaced in JITed code by
 * using Func::set_error_handler, or in AOT code by calling
 * halide_set_error_handler. In AOT code on platforms that support
 * weak linking (i.e. not Windows), you can also override it by simply
 * defining your own halide_error.
 */
// @{
extern void halide_error(void *user_context, const char *);
extern void halide_default_error(void *user_context, const char *);
typedef void (*halide_error_handler_t)(void *, const char *);
extern halide_error_handler_t halide_set_error_handler(halide_error_handler_t handler);
// @}

/** Cross-platform mutex. Must be initialized with zero and implementation
 * must treat zero as an unlocked mutex with no waiters, etc.
 */
struct halide_mutex {
    uintptr_t _private[1];
};

/** Cross platform condition variable. Must be initialized to 0. */
struct halide_cond {
    uintptr_t _private[1];
};

/** A basic set of mutex and condition variable functions, which call
 * platform specific code for mutual exclusion. Equivalent to posix
 * calls. */
//@{
extern void halide_mutex_lock(struct halide_mutex *mutex);
extern void halide_mutex_unlock(struct halide_mutex *mutex);
extern void halide_cond_signal(struct halide_cond *cond);
extern void halide_cond_broadcast(struct halide_cond *cond);
extern void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex);
//@}

/** Functions for constructing/destroying/locking/unlocking arrays of mutexes. */
struct halide_mutex_array;
//@{
extern struct halide_mutex_array *halide_mutex_array_create(int sz);
extern void halide_mutex_array_destroy(void *user_context, void *array);
extern int halide_mutex_array_lock(struct halide_mutex_array *array, int entry);
extern int halide_mutex_array_unlock(struct halide_mutex_array *array, int entry);
//@}

/** Define halide_do_par_for to replace the default thread pool
 * implementation. halide_shutdown_thread_pool can also be called to
 * release resources used by the default thread pool on platforms
 * where it makes sense. See Func::set_custom_do_task and
 * Func::set_custom_do_par_for. Should return zero if all the jobs
 * return zero, or an arbitrarily chosen return value from one of the
 * jobs otherwise.
 */
//@{
typedef int (*halide_task_t)(void *user_context, int task_number, uint8_t *closure);
extern int halide_do_par_for(void *user_context,
                             halide_task_t task,
                             int min, int size, uint8_t *closure);
extern void halide_shutdown_thread_pool();
//@}

/** Set a custom method for performing a parallel for loop. Returns
 * the old do_par_for handler. */
typedef int (*halide_do_par_for_t)(void *, halide_task_t, int, int, uint8_t *);
extern halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t do_par_for);

/** An opaque struct representing a semaphore. Used by the task system for async tasks. */
struct halide_semaphore_t {
    uint64_t _private[2];
};

/** A struct representing a semaphore and a number of items that must
 * be acquired from it. Used in halide_parallel_task_t below. */
struct halide_semaphore_acquire_t {
    struct halide_semaphore_t *semaphore;
    int count;
};
extern int halide_semaphore_init(struct halide_semaphore_t *, int n);
extern int halide_semaphore_release(struct halide_semaphore_t *, int n);
extern bool halide_semaphore_try_acquire(struct halide_semaphore_t *, int n);
typedef int (*halide_semaphore_init_t)(struct halide_semaphore_t *, int);
typedef int (*halide_semaphore_release_t)(struct halide_semaphore_t *, int);
typedef bool (*halide_semaphore_try_acquire_t)(struct halide_semaphore_t *, int);

/** A task representing a serial for loop evaluated over some range.
 * Note that task_parent is a pass through argument that should be
 * passed to any dependent taks that are invoked using halide_do_parallel_tasks
 * underneath this call. */
typedef int (*halide_loop_task_t)(void *user_context, int min, int extent,
                                  uint8_t *closure, void *task_parent);

/** A parallel task to be passed to halide_do_parallel_tasks. This
 * task may recursively call halide_do_parallel_tasks, and there may
 * be complex dependencies between seemingly unrelated tasks expressed
 * using semaphores. If you are using a custom task system, care must
 * be taken to avoid potential deadlock. This can be done by carefully
 * respecting the static metadata at the end of the task struct.*/
struct halide_parallel_task_t {
    // The function to call. It takes a user context, a min and
    // extent, a closure, and a task system pass through argument.
    halide_loop_task_t fn;

    // The closure to pass it
    uint8_t *closure;

    // The name of the function to be called. For debugging purposes only.
    const char *name;

    // An array of semaphores that must be acquired before the
    // function is called. Must be reacquired for every call made.
    struct halide_semaphore_acquire_t *semaphores;
    int num_semaphores;

    // The entire range the function should be called over. This range
    // may be sliced up and the function called multiple times.
    int min, extent;

    // A parallel task provides several pieces of metadata to prevent
    // unbounded resource usage or deadlock.

    // The first is the minimum number of execution contexts (call
    // stacks or threads) necessary for the function to run to
    // completion. This may be greater than one when there is nested
    // parallelism with internal producer-consumer relationships
    // (calling the function recursively spawns and blocks on parallel
    // sub-tasks that communicate with each other via semaphores). If
    // a parallel runtime calls the function when fewer than this many
    // threads are idle, it may need to create more threads to
    // complete the task, or else risk deadlock due to committing all
    // threads to tasks that cannot complete without more.
    //
    // FIXME: Note that extern stages are assumed to only require a
    // single thread to complete. If the extern stage is itself a
    // Halide pipeline, this may be an underestimate.
    int min_threads;

    // The calls to the function should be in serial order from min to min+extent-1, with only
    // one executing at a time. If false, any order is fine, and
    // concurrency is fine.
    bool serial;
};

/** Enqueue some number of the tasks described above and wait for them
 * to complete. While waiting, the calling threads assists with either
 * the tasks enqueued, or other non-blocking tasks in the task
 * system. Note that task_parent should be NULL for top-level calls
 * and the pass through argument if this call is being made from
 * another task. */
extern int halide_do_parallel_tasks(void *user_context, int num_tasks,
                                    struct halide_parallel_task_t *tasks,
                                    void *task_parent);

/** If you use the default do_par_for, you can still set a custom
 * handler to perform each individual task. Returns the old handler. */
//@{
typedef int (*halide_do_task_t)(void *, halide_task_t, int, uint8_t *);
extern halide_do_task_t halide_set_custom_do_task(halide_do_task_t do_task);
extern int halide_do_task(void *user_context, halide_task_t f, int idx,
                          uint8_t *closure);
//@}

/** The version of do_task called for loop tasks. By default calls the
 * loop task with the same arguments. */
// @{
typedef int (*halide_do_loop_task_t)(void *, halide_loop_task_t, int, int, uint8_t *, void *);
extern halide_do_loop_task_t halide_set_custom_do_loop_task(halide_do_loop_task_t do_task);
extern int halide_do_loop_task(void *user_context, halide_loop_task_t f, int min, int extent,
                               uint8_t *closure, void *task_parent);
//@}

/** Provide an entire custom tasking runtime via function
 * pointers. Note that do_task and semaphore_try_acquire are only ever
 * called by halide_default_do_par_for and
 * halide_default_do_parallel_tasks, so it's only necessary to provide
 * those if you are mixing in the default implementations of
 * do_par_for and do_parallel_tasks. */
// @{
typedef int (*halide_do_parallel_tasks_t)(void *, int, struct halide_parallel_task_t *,
                                          void *task_parent);
extern void halide_set_custom_parallel_runtime(
    halide_do_par_for_t,
    halide_do_task_t,
    halide_do_loop_task_t,
    halide_do_parallel_tasks_t,
    halide_semaphore_init_t,
    halide_semaphore_try_acquire_t,
    halide_semaphore_release_t);
// @}

/** The default versions of the parallel runtime functions. */
// @{
extern int halide_default_do_par_for(void *user_context,
                                     halide_task_t task,
                                     int min, int size, uint8_t *closure);
extern int halide_default_do_parallel_tasks(void *user_context,
                                            int num_tasks,
                                            struct halide_parallel_task_t *tasks,
                                            void *task_parent);
extern int halide_default_do_task(void *user_context, halide_task_t f, int idx,
                                  uint8_t *closure);
extern int halide_default_do_loop_task(void *user_context, halide_loop_task_t f,
                                       int min, int extent,
                                       uint8_t *closure, void *task_parent);
extern int halide_default_semaphore_init(struct halide_semaphore_t *, int n);
extern int halide_default_semaphore_release(struct halide_semaphore_t *, int n);
extern bool halide_default_semaphore_try_acquire(struct halide_semaphore_t *, int n);
// @}

struct halide_thread;

/** Spawn a thread. Returns a handle to the thread for the purposes of
 * joining it. The thread must be joined in order to clean up any
 * resources associated with it. */
extern struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure);

/** Join a thread. */
extern void halide_join_thread(struct halide_thread *);

/** Set the number of threads used by Halide's thread pool. Returns
 * the old number.
 *
 * n < 0  : error condition
 * n == 0 : use a reasonable system default (typically, number of cpus online).
 * n == 1 : use exactly one thread; this will always enforce serial execution
 * n > 1  : use a pool of exactly n threads.
 *
 * (Note that this is only guaranteed when using the default implementations
 * of halide_do_par_for(); custom implementations may completely ignore values
 * passed to halide_set_num_threads().)
 */
extern int halide_set_num_threads(int n);

/** Halide calls these functions to allocate and free memory. To
 * replace in AOT code, use the halide_set_custom_malloc and
 * halide_set_custom_free, or (on platforms that support weak
 * linking), simply define these functions yourself. In JIT-compiled
 * code use Func::set_custom_allocator.
 *
 * If you override them, and find yourself wanting to call the default
 * implementation from within your override, use
 * halide_default_malloc/free.
 *
 * Note that halide_malloc must return a pointer aligned to the
 * maximum meaningful alignment for the platform for the purpose of
 * vector loads and stores. The default implementation uses 32-byte
 * alignment, which is safe for arm and x86. Additionally, it must be
 * safe to read at least 8 bytes before the start and beyond the
 * end.
 */
//@{
extern void *halide_malloc(void *user_context, size_t x);
extern void halide_free(void *user_context, void *ptr);
extern void *halide_default_malloc(void *user_context, size_t x);
extern void halide_default_free(void *user_context, void *ptr);
typedef void *(*halide_malloc_t)(void *, size_t);
typedef void (*halide_free_t)(void *, void *);
extern halide_malloc_t halide_set_custom_malloc(halide_malloc_t user_malloc);
extern halide_free_t halide_set_custom_free(halide_free_t user_free);
//@}

/** Halide calls these functions to interact with the underlying
 * system runtime functions. To replace in AOT code on platforms that
 * support weak linking, define these functions yourself, or use
 * the halide_set_custom_load_library() and halide_set_custom_get_library_symbol()
 * functions. In JIT-compiled code, use JITSharedRuntime::set_default_handlers().
 *
 * halide_load_library and halide_get_library_symbol are equivalent to
 * dlopen and dlsym. halide_get_symbol(sym) is equivalent to
 * dlsym(RTLD_DEFAULT, sym).
 */
//@{
extern void *halide_get_symbol(const char *name);
extern void *halide_load_library(const char *name);
extern void *halide_get_library_symbol(void *lib, const char *name);
extern void *halide_default_get_symbol(const char *name);
extern void *halide_default_load_library(const char *name);
extern void *halide_default_get_library_symbol(void *lib, const char *name);
typedef void *(*halide_get_symbol_t)(const char *name);
typedef void *(*halide_load_library_t)(const char *name);
typedef void *(*halide_get_library_symbol_t)(void *lib, const char *name);
extern halide_get_symbol_t halide_set_custom_get_symbol(halide_get_symbol_t user_get_symbol);
extern halide_load_library_t halide_set_custom_load_library(halide_load_library_t user_load_library);
extern halide_get_library_symbol_t halide_set_custom_get_library_symbol(halide_get_library_symbol_t user_get_library_symbol);
//@}

/** Called when debug_to_file is used inside %Halide code.  See
 * Func::debug_to_file for how this is called
 *
 * Cannot be replaced in JITted code at present.
 */
extern int32_t halide_debug_to_file(void *user_context, const char *filename,
                                    int32_t type_code,
                                    struct halide_buffer_t *buf);

/** Types in the halide type system. They can be ints, unsigned ints,
 * or floats (of various bit-widths), or a handle (which is always 64-bits).
 * Note that the int/uint/float values do not imply a specific bit width
 * (the bit width is expected to be encoded in a separate value).
 */
typedef enum halide_type_code_t
#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    : uint8_t
#endif
{
    halide_type_int = 0,     ///< signed integers
    halide_type_uint = 1,    ///< unsigned integers
    halide_type_float = 2,   ///< IEEE floating point numbers
    halide_type_handle = 3,  ///< opaque pointer type (void *)
    halide_type_bfloat = 4,  ///< floating point numbers in the bfloat format
} halide_type_code_t;

// Note that while __attribute__ can go before or after the declaration,
// __declspec apparently is only allowed before.
#ifndef HALIDE_ATTRIBUTE_ALIGN
#ifdef _MSC_VER
#define HALIDE_ATTRIBUTE_ALIGN(x) __declspec(align(x))
#else
#define HALIDE_ATTRIBUTE_ALIGN(x) __attribute__((aligned(x)))
#endif
#endif

/** A runtime tag for a type in the halide type system. Can be ints,
 * unsigned ints, or floats of various bit-widths (the 'bits'
 * field). Can also be vectors of the same (by setting the 'lanes'
 * field to something larger than one). This struct should be
 * exactly 32-bits in size. */
struct halide_type_t {
    /** The basic type code: signed integer, unsigned integer, or floating point. */
#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    HALIDE_ATTRIBUTE_ALIGN(1)
    halide_type_code_t code;  // halide_type_code_t
#else
    HALIDE_ATTRIBUTE_ALIGN(1)
    uint8_t code;  // halide_type_code_t
#endif

    /** The number of bits of precision of a single scalar value of this type. */
    HALIDE_ATTRIBUTE_ALIGN(1)
    uint8_t bits;

    /** How many elements in a vector. This is 1 for scalar types. */
    HALIDE_ATTRIBUTE_ALIGN(2)
    uint16_t lanes;

#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    /** Construct a runtime representation of a Halide type from:
     * code: The fundamental type from an enum.
     * bits: The bit size of one element.
     * lanes: The number of vector elements in the type. */
    HALIDE_ALWAYS_INLINE constexpr halide_type_t(halide_type_code_t code, uint8_t bits, uint16_t lanes = 1)
        : code(code), bits(bits), lanes(lanes) {
    }

    /** Default constructor is required e.g. to declare halide_trace_event
     * instances. */
    HALIDE_ALWAYS_INLINE constexpr halide_type_t()
        : code((halide_type_code_t)0), bits(0), lanes(0) {
    }

    HALIDE_ALWAYS_INLINE constexpr halide_type_t with_lanes(uint16_t new_lanes) const {
        return halide_type_t((halide_type_code_t)code, bits, new_lanes);
    }

    HALIDE_ALWAYS_INLINE constexpr halide_type_t element_of() const {
        return with_lanes(1);
    }
    /** Compare two types for equality. */
    HALIDE_ALWAYS_INLINE constexpr bool operator==(const halide_type_t &other) const {
        return as_u32() == other.as_u32();
    }

    HALIDE_ALWAYS_INLINE constexpr bool operator!=(const halide_type_t &other) const {
        return !(*this == other);
    }

    HALIDE_ALWAYS_INLINE constexpr bool operator<(const halide_type_t &other) const {
        return as_u32() < other.as_u32();
    }

    /** Size in bytes for a single element, even if width is not 1, of this type. */
    HALIDE_ALWAYS_INLINE constexpr int bytes() const {
        return (bits + 7) / 8;
    }

    HALIDE_ALWAYS_INLINE constexpr uint32_t as_u32() const {
        // Note that this produces a result that is identical to memcpy'ing 'this'
        // into a u32 (on a little-endian machine, anyway), and at -O1 or greater
        // on Clang, the compiler knows this and optimizes this into a single 32-bit move.
        // (At -O0 it will look awful.)
        return static_cast<uint8_t>(code) |
               (static_cast<uint16_t>(bits) << 8) |
               (static_cast<uint32_t>(lanes) << 16);
    }
#endif
};

#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
static_assert(sizeof(halide_type_t) == sizeof(uint32_t), "size mismatch in halide_type_t");
#endif

enum halide_trace_event_code_t { halide_trace_load = 0,
                                 halide_trace_store = 1,
                                 halide_trace_begin_realization = 2,
                                 halide_trace_end_realization = 3,
                                 halide_trace_produce = 4,
                                 halide_trace_end_produce = 5,
                                 halide_trace_consume = 6,
                                 halide_trace_end_consume = 7,
                                 halide_trace_begin_pipeline = 8,
                                 halide_trace_end_pipeline = 9,
                                 halide_trace_tag = 10 };

struct halide_trace_event_t {
    /** The name of the Func or Pipeline that this event refers to */
    const char *func;

    /** If the event type is a load or a store, this points to the
     * value being loaded or stored. Use the type field to safely cast
     * this to a concrete pointer type and retrieve it. For other
     * events this is null. */
    void *value;

    /** For loads and stores, an array which contains the location
     * being accessed. For vector loads or stores it is an array of
     * vectors of coordinates (the vector dimension is innermost).
     *
     * For realization or production-related events, this will contain
     * the mins and extents of the region being accessed, in the order
     * min0, extent0, min1, extent1, ...
     *
     * For pipeline-related events, this will be null.
     */
    int32_t *coordinates;

    /** For halide_trace_tag, this points to a read-only null-terminated string
     * of arbitrary text. For all other events, this will be null.
     */
    const char *trace_tag;

    /** If the event type is a load or a store, this is the type of
     * the data. Otherwise, the value is meaningless. */
    struct halide_type_t type;

    /** The type of event */
    enum halide_trace_event_code_t event;

    /* The ID of the parent event (see below for an explanation of
     * event ancestry). */
    int32_t parent_id;

    /** If this was a load or store of a Tuple-valued Func, this is
     * which tuple element was accessed. */
    int32_t value_index;

    /** The length of the coordinates array */
    int32_t dimensions;
};

/** Called when Funcs are marked as trace_load, trace_store, or
 * trace_realization. See Func::set_custom_trace. The default
 * implementation either prints events via halide_print, or if
 * HL_TRACE_FILE is defined, dumps the trace to that file in a
 * sequence of trace packets. The header for a trace packet is defined
 * below. If the trace is going to be large, you may want to make the
 * file a named pipe, and then read from that pipe into gzip.
 *
 * halide_trace returns a unique ID which will be passed to future
 * events that "belong" to the earlier event as the parent id. The
 * ownership hierarchy looks like:
 *
 * begin_pipeline
 * +--trace_tag (if any)
 * +--trace_tag (if any)
 * ...
 * +--begin_realization
 * |  +--produce
 * |  |  +--load/store
 * |  |  +--end_produce
 * |  +--consume
 * |  |  +--load
 * |  |  +--end_consume
 * |  +--end_realization
 * +--end_pipeline
 *
 * Threading means that ownership cannot be inferred from the ordering
 * of events. There can be many active realizations of a given
 * function, or many active productions for a single
 * realization. Within a single production, the ordering of events is
 * meaningful.
 *
 * Note that all trace_tag events (if any) will occur just after the begin_pipeline
 * event, but before any begin_realization events. All trace_tags for a given Func
 * will be emitted in the order added.
 */
// @}
extern int32_t halide_trace(void *user_context, const struct halide_trace_event_t *event);
extern int32_t halide_default_trace(void *user_context, const struct halide_trace_event_t *event);
typedef int32_t (*halide_trace_t)(void *user_context, const struct halide_trace_event_t *);
extern halide_trace_t halide_set_custom_trace(halide_trace_t trace);
// @}

/** The header of a packet in a binary trace. All fields are 32-bit. */
struct halide_trace_packet_t {
    /** The total size of this packet in bytes. Always a multiple of
     * four. Equivalently, the number of bytes until the next
     * packet. */
    uint32_t size;

    /** The id of this packet (for the purpose of parent_id). */
    int32_t id;

    /** The remaining fields are equivalent to those in halide_trace_event_t */
    // @{
    struct halide_type_t type;
    enum halide_trace_event_code_t event;
    int32_t parent_id;
    int32_t value_index;
    int32_t dimensions;
    // @}

#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    /** Get the coordinates array, assuming this packet is laid out in
     * memory as it was written. The coordinates array comes
     * immediately after the packet header. */
    HALIDE_ALWAYS_INLINE const int *coordinates() const {
        return (const int *)(this + 1);
    }

    HALIDE_ALWAYS_INLINE int *coordinates() {
        return (int *)(this + 1);
    }

    /** Get the value, assuming this packet is laid out in memory as
     * it was written. The packet comes immediately after the coordinates
     * array. */
    HALIDE_ALWAYS_INLINE const void *value() const {
        return (const void *)(coordinates() + dimensions);
    }

    HALIDE_ALWAYS_INLINE void *value() {
        return (void *)(coordinates() + dimensions);
    }

    /** Get the func name, assuming this packet is laid out in memory
     * as it was written. It comes after the value. */
    HALIDE_ALWAYS_INLINE const char *func() const {
        return (const char *)value() + type.lanes * type.bytes();
    }

    HALIDE_ALWAYS_INLINE char *func() {
        return (char *)value() + type.lanes * type.bytes();
    }

    /** Get the trace_tag (if any), assuming this packet is laid out in memory
     * as it was written. It comes after the func name. If there is no trace_tag,
     * this will return a pointer to an empty string. */
    HALIDE_ALWAYS_INLINE const char *trace_tag() const {
        const char *f = func();
        // strlen may not be available here
        while (*f++) {
            // nothing
        }
        return f;
    }

    HALIDE_ALWAYS_INLINE char *trace_tag() {
        char *f = func();
        // strlen may not be available here
        while (*f++) {
            // nothing
        }
        return f;
    }
#endif
};

/** Set the file descriptor that Halide should write binary trace
 * events to. If called with 0 as the argument, Halide outputs trace
 * information to stdout in a human-readable format. If never called,
 * Halide checks the for existence of an environment variable called
 * HL_TRACE_FILE and opens that file. If HL_TRACE_FILE is not defined,
 * it outputs trace information to stdout in a human-readable
 * format. */
extern void halide_set_trace_file(int fd);

/** Halide calls this to retrieve the file descriptor to write binary
 * trace events to. The default implementation returns the value set
 * by halide_set_trace_file. Implement it yourself if you wish to use
 * a custom file descriptor per user_context. Return zero from your
 * implementation to tell Halide to print human-readable trace
 * information to stdout. */
extern int halide_get_trace_file(void *user_context);

/** If tracing is writing to a file. This call closes that file
 * (flushing the trace). Returns zero on success. */
extern int halide_shutdown_trace();

/** All Halide GPU or device backend implementations provide an
 * interface to be used with halide_device_malloc, etc. This is
 * accessed via the functions below.
 */

/** An opaque struct containing per-GPU API implementations of the
 * device functions. */
struct halide_device_interface_impl_t;

/** Each GPU API provides a halide_device_interface_t struct pointing
 * to the code that manages device allocations. You can access these
 * functions directly from the struct member function pointers, or by
 * calling the functions declared below. Note that the global
 * functions are not available when using Halide as a JIT compiler.
 * If you are using raw halide_buffer_t in that context you must use
 * the function pointers in the device_interface struct.
 *
 * The function pointers below are currently the same for every GPU
 * API; only the impl field varies. These top-level functions do the
 * bookkeeping that is common across all GPU APIs, and then dispatch
 * to more API-specific functions via another set of function pointers
 * hidden inside the impl field.
 */
struct halide_device_interface_t {
    int (*device_malloc)(void *user_context, struct halide_buffer_t *buf,
                         const struct halide_device_interface_t *device_interface);
    int (*device_free)(void *user_context, struct halide_buffer_t *buf);
    int (*device_sync)(void *user_context, struct halide_buffer_t *buf);
    void (*device_release)(void *user_context,
                           const struct halide_device_interface_t *device_interface);
    int (*copy_to_host)(void *user_context, struct halide_buffer_t *buf);
    int (*copy_to_device)(void *user_context, struct halide_buffer_t *buf,
                          const struct halide_device_interface_t *device_interface);
    int (*device_and_host_malloc)(void *user_context, struct halide_buffer_t *buf,
                                  const struct halide_device_interface_t *device_interface);
    int (*device_and_host_free)(void *user_context, struct halide_buffer_t *buf);
    int (*buffer_copy)(void *user_context, struct halide_buffer_t *src,
                       const struct halide_device_interface_t *dst_device_interface, struct halide_buffer_t *dst);
    int (*device_crop)(void *user_context, const struct halide_buffer_t *src,
                       struct halide_buffer_t *dst);
    int (*device_slice)(void *user_context, const struct halide_buffer_t *src,
                        int slice_dim, int slice_pos, struct halide_buffer_t *dst);
    int (*device_release_crop)(void *user_context, struct halide_buffer_t *buf);
    int (*wrap_native)(void *user_context, struct halide_buffer_t *buf, uint64_t handle,
                       const struct halide_device_interface_t *device_interface);
    int (*detach_native)(void *user_context, struct halide_buffer_t *buf);
    int (*compute_capability)(void *user_context, int *major, int *minor);
    const struct halide_device_interface_impl_t *impl;
};

/** Release all data associated with the given device interface, in
 * particular all resources (memory, texture, context handles)
 * allocated by Halide. Must be called explicitly when using AOT
 * compilation. This is *not* thread-safe with respect to actively
 * running Halide code. Ensure all pipelines are finished before
 * calling this. */
extern void halide_device_release(void *user_context,
                                  const struct halide_device_interface_t *device_interface);

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
extern int halide_copy_to_host(void *user_context, struct halide_buffer_t *buf);

/** Copy image data from host memory to device memory. This should not
 * be called directly; Halide handles copying to the device
 * automatically.  If interface is NULL and the buf has a non-zero dev
 * field, the device associated with the dev handle will be
 * used. Otherwise if the dev field is 0 and interface is NULL, an
 * error is returned. */
extern int halide_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                                 const struct halide_device_interface_t *device_interface);

/** Copy data from one buffer to another. The buffers may have
 * different shapes and sizes, but the destination buffer's shape must
 * be contained within the source buffer's shape. That is, for each
 * dimension, the min on the destination buffer must be greater than
 * or equal to the min on the source buffer, and min+extent on the
 * destination buffer must be less that or equal to min+extent on the
 * source buffer. The source data is pulled from either device or
 * host memory on the source, depending on the dirty flags. host is
 * preferred if both are valid. The dst_device_interface parameter
 * controls the destination memory space. NULL means host memory. */
extern int halide_buffer_copy(void *user_context, struct halide_buffer_t *src,
                              const struct halide_device_interface_t *dst_device_interface,
                              struct halide_buffer_t *dst);

/** Give the destination buffer a device allocation which is an alias
 * for the same coordinate range in the source buffer. Modifies the
 * device, device_interface, and the device_dirty flag only. Only
 * supported by some device APIs (others will return
 * halide_error_code_device_crop_unsupported). Call
 * halide_device_release_crop instead of halide_device_free to clean
 * up resources associated with the cropped view. Do not free the
 * device allocation on the source buffer while the destination buffer
 * still lives. Note that the two buffers do not share dirty flags, so
 * care must be taken to update them together as needed. Note that src
 * and dst are required to have the same number of dimensions.
 *
 * Note also that (in theory) device interfaces which support cropping may
 * still not support cropping a crop (instead, create a new crop of the parent
 * buffer); in practice, no known implementation has this limitation, although
 * it is possible that some future implementations may require it. */
extern int halide_device_crop(void *user_context,
                              const struct halide_buffer_t *src,
                              struct halide_buffer_t *dst);

/** Give the destination buffer a device allocation which is an alias
 * for a similar coordinate range in the source buffer, but with one dimension
 * sliced away in the dst. Modifies the device, device_interface, and the
 * device_dirty flag only. Only supported by some device APIs (others will return
 * halide_error_code_device_crop_unsupported). Call
 * halide_device_release_crop instead of halide_device_free to clean
 * up resources associated with the sliced view. Do not free the
 * device allocation on the source buffer while the destination buffer
 * still lives. Note that the two buffers do not share dirty flags, so
 * care must be taken to update them together as needed. Note that the dst buffer
 * must have exactly one fewer dimension than the src buffer, and that slice_dim
 * and slice_pos must be valid within src. */
extern int halide_device_slice(void *user_context,
                               const struct halide_buffer_t *src,
                               int slice_dim, int slice_pos,
                               struct halide_buffer_t *dst);

/** Release any resources associated with a cropped/sliced view of another
 * buffer. */
extern int halide_device_release_crop(void *user_context,
                                      struct halide_buffer_t *buf);

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
extern int halide_device_sync(void *user_context, struct halide_buffer_t *buf);

/** Allocate device memory to back a halide_buffer_t. */
extern int halide_device_malloc(void *user_context, struct halide_buffer_t *buf,
                                const struct halide_device_interface_t *device_interface);

/** Free device memory. */
extern int halide_device_free(void *user_context, struct halide_buffer_t *buf);

/** Wrap or detach a native device handle, setting the device field
 * and device_interface field as appropriate for the given GPU
 * API. The meaning of the opaque handle is specific to the device
 * interface, so if you know the device interface in use, call the
 * more specific functions in the runtime headers for your specific
 * device API instead (e.g. HalideRuntimeCuda.h). */
// @{
extern int halide_device_wrap_native(void *user_context,
                                     struct halide_buffer_t *buf,
                                     uint64_t handle,
                                     const struct halide_device_interface_t *device_interface);
extern int halide_device_detach_native(void *user_context, struct halide_buffer_t *buf);
// @}

/** Selects which gpu device to use. 0 is usually the display
 * device. If never called, Halide uses the environment variable
 * HL_GPU_DEVICE. If that variable is unset, Halide uses the last
 * device. Set this to -1 to use the last device. */
extern void halide_set_gpu_device(int n);

/** Halide calls this to get the desired halide gpu device
 * setting. Implement this yourself to use a different gpu device per
 * user_context. The default implementation returns the value set by
 * halide_set_gpu_device, or the environment variable
 * HL_GPU_DEVICE. */
extern int halide_get_gpu_device(void *user_context);

/** Set the soft maximum amount of memory, in bytes, that the LRU
 *  cache will use to memoize Func results.  This is not a strict
 *  maximum in that concurrency and simultaneous use of memoized
 *  reults larger than the cache size can both cause it to
 *  temporariliy be larger than the size specified here.
 */
extern void halide_memoization_cache_set_size(int64_t size);

/** Given a cache key for a memoized result, currently constructed
 *  from the Func name and top-level Func name plus the arguments of
 *  the computation, determine if the result is in the cache and
 *  return it if so. (The internals of the cache key should be
 *  considered opaque by this function.) If this routine returns true,
 *  it is a cache miss. Otherwise, it will return false and the
 *  buffers passed in will be filled, via copying, with memoized
 *  data. The last argument is a list if halide_buffer_t pointers which
 *  represents the outputs of the memoized Func. If the Func does not
 *  return a Tuple, there will only be one halide_buffer_t in the list. The
 *  tuple_count parameters determines the length of the list.
 *
 * The return values are:
 * -1: Signals an error.
 *  0: Success and cache hit.
 *  1: Success and cache miss.
 */
extern int halide_memoization_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size,
                                           struct halide_buffer_t *realized_bounds,
                                           int32_t tuple_count, struct halide_buffer_t **tuple_buffers);

/** Given a cache key for a memoized result, currently constructed
 *  from the Func name and top-level Func name plus the arguments of
 *  the computation, store the result in the cache for futre access by
 *  halide_memoization_cache_lookup. (The internals of the cache key
 *  should be considered opaque by this function.) Data is copied out
 *  from the inputs and inputs are unmodified. The last argument is a
 *  list if halide_buffer_t pointers which represents the outputs of the
 *  memoized Func. If the Func does not return a Tuple, there will
 *  only be one halide_buffer_t in the list. The tuple_count parameters
 *  determines the length of the list.
 *
 * If there is a memory allocation failure, the store does not store
 * the data into the cache.
 *
 * If has_eviction_key is true, the entry is marked with eviction_key to
 * allow removing the key with halide_memoization_cache_evict.
 */
extern int halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                          struct halide_buffer_t *realized_bounds,
                                          int32_t tuple_count,
                                          struct halide_buffer_t **tuple_buffers,
                                          bool has_eviction_key, uint64_t eviction_key);

/** Evict all cache entries that were tagged with the given
 *  eviction_key in the memoize scheduling directive.
 */
extern void halide_memoization_cache_evict(void *user_context, uint64_t eviction_key);

/** If halide_memoization_cache_lookup succeeds,
 * halide_memoization_cache_release must be called to signal the
 * storage is no longer being used by the caller. It will be passed
 * the host pointer of one the buffers returned by
 * halide_memoization_cache_lookup. That is
 * halide_memoization_cache_release will be called multiple times for
 * the case where halide_memoization_cache_lookup is handling multiple
 * buffers.  (This corresponds to memoizing a Tuple in Halide.) Note
 * that the host pointer must be sufficient to get to all information
 * the release operation needs. The default Halide cache impleemntation
 * accomplishes this by storing extra data before the start of the user
 * modifiable host storage.
 *
 * This call is like free and does not have a failure return.
 */
extern void halide_memoization_cache_release(void *user_context, void *host);

/** Free all memory and resources associated with the memoization cache.
 * Must be called at a time when no other threads are accessing the cache.
 */
extern void halide_memoization_cache_cleanup();

/** Verify that a given range of memory has been initialized; only used when Target::MSAN is enabled.
 *
 * The default implementation simply calls the LLVM-provided __msan_check_mem_is_initialized() function.
 *
 * The return value should always be zero.
 */
extern int halide_msan_check_memory_is_initialized(void *user_context, const void *ptr, uint64_t len, const char *name);

/** Verify that the data pointed to by the halide_buffer_t is initialized (but *not* the halide_buffer_t itself),
 * using halide_msan_check_memory_is_initialized() for checking.
 *
 * The default implementation takes pains to only check the active memory ranges
 * (skipping padding), and sorting into ranges to always check the smallest number of
 * ranges, in monotonically increasing memory order.
 *
 * Most client code should never need to replace the default implementation.
 *
 * The return value should always be zero.
 */
extern int halide_msan_check_buffer_is_initialized(void *user_context, struct halide_buffer_t *buffer, const char *buf_name);

/** Annotate that a given range of memory has been initialized;
 * only used when Target::MSAN is enabled.
 *
 * The default implementation simply calls the LLVM-provided __msan_unpoison() function.
 *
 * The return value should always be zero.
 */
extern int halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len);

/** Mark the data pointed to by the halide_buffer_t as initialized (but *not* the halide_buffer_t itself),
 * using halide_msan_annotate_memory_is_initialized() for marking.
 *
 * The default implementation takes pains to only mark the active memory ranges
 * (skipping padding), and sorting into ranges to always mark the smallest number of
 * ranges, in monotonically increasing memory order.
 *
 * Most client code should never need to replace the default implementation.
 *
 * The return value should always be zero.
 */
extern int halide_msan_annotate_buffer_is_initialized(void *user_context, struct halide_buffer_t *buffer);
extern void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *buffer);

/** The error codes that may be returned by a Halide pipeline. */
enum halide_error_code_t {
    /** There was no error. This is the value returned by Halide on success. */
    halide_error_code_success = 0,

    /** An uncategorized error occurred. Refer to the string passed to halide_error. */
    halide_error_code_generic_error = -1,

    /** A Func was given an explicit bound via Func::bound, but this
     * was not large enough to encompass the region that is used of
     * the Func by the rest of the pipeline. */
    halide_error_code_explicit_bounds_too_small = -2,

    /** The elem_size field of a halide_buffer_t does not match the size in
     * bytes of the type of that ImageParam. Probable type mismatch. */
    halide_error_code_bad_type = -3,

    /** A pipeline would access memory outside of the halide_buffer_t passed
     * in. */
    halide_error_code_access_out_of_bounds = -4,

    /** A halide_buffer_t was given that spans more than 2GB of memory. */
    halide_error_code_buffer_allocation_too_large = -5,

    /** A halide_buffer_t was given with extents that multiply to a number
     * greater than 2^31-1 */
    halide_error_code_buffer_extents_too_large = -6,

    /** Applying explicit constraints on the size of an input or
     * output buffer shrank the size of that buffer below what will be
     * accessed by the pipeline. */
    halide_error_code_constraints_make_required_region_smaller = -7,

    /** A constraint on a size or stride of an input or output buffer
     * was not met by the halide_buffer_t passed in. */
    halide_error_code_constraint_violated = -8,

    /** A scalar parameter passed in was smaller than its minimum
     * declared value. */
    halide_error_code_param_too_small = -9,

    /** A scalar parameter passed in was greater than its minimum
     * declared value. */
    halide_error_code_param_too_large = -10,

    /** A call to halide_malloc returned NULL. */
    halide_error_code_out_of_memory = -11,

    /** A halide_buffer_t pointer passed in was NULL. */
    halide_error_code_buffer_argument_is_null = -12,

    /** debug_to_file failed to open or write to the specified
     * file. */
    halide_error_code_debug_to_file_failed = -13,

    /** The Halide runtime encountered an error while trying to copy
     * from device to host. Turn on -debug in your target string to
     * see more details. */
    halide_error_code_copy_to_host_failed = -14,

    /** The Halide runtime encountered an error while trying to copy
     * from host to device. Turn on -debug in your target string to
     * see more details. */
    halide_error_code_copy_to_device_failed = -15,

    /** The Halide runtime encountered an error while trying to
     * allocate memory on device. Turn on -debug in your target string
     * to see more details. */
    halide_error_code_device_malloc_failed = -16,

    /** The Halide runtime encountered an error while trying to
     * synchronize with a device. Turn on -debug in your target string
     * to see more details. */
    halide_error_code_device_sync_failed = -17,

    /** The Halide runtime encountered an error while trying to free a
     * device allocation. Turn on -debug in your target string to see
     * more details. */
    halide_error_code_device_free_failed = -18,

    /** Buffer has a non-zero device but no device interface, which
     * violates a Halide invariant. */
    halide_error_code_no_device_interface = -19,

    /** An error occurred when attempting to initialize the Matlab
     * runtime. */
    halide_error_code_matlab_init_failed = -20,

    /** The type of an mxArray did not match the expected type. */
    halide_error_code_matlab_bad_param_type = -21,

    /** There is a bug in the Halide compiler. */
    halide_error_code_internal_error = -22,

    /** The Halide runtime encountered an error while trying to launch
     * a GPU kernel. Turn on -debug in your target string to see more
     * details. */
    halide_error_code_device_run_failed = -23,

    /** The Halide runtime encountered a host pointer that violated
     * the alignment set for it by way of a call to
     * set_host_alignment */
    halide_error_code_unaligned_host_ptr = -24,

    /** A fold_storage directive was used on a dimension that is not
     * accessed in a monotonically increasing or decreasing fashion. */
    halide_error_code_bad_fold = -25,

    /** A fold_storage directive was used with a fold factor that was
     * too small to store all the values of a producer needed by the
     * consumer. */
    halide_error_code_fold_factor_too_small = -26,

    /** User-specified require() expression was not satisfied. */
    halide_error_code_requirement_failed = -27,

    /** At least one of the buffer's extents are negative. */
    halide_error_code_buffer_extents_negative = -28,

    halide_error_code_unused_29 = -29,

    halide_error_code_unused_30 = -30,

    /** A specialize_fail() schedule branch was selected at runtime. */
    halide_error_code_specialize_fail = -31,

    /** The Halide runtime encountered an error while trying to wrap a
     * native device handle.  Turn on -debug in your target string to
     * see more details. */
    halide_error_code_device_wrap_native_failed = -32,

    /** The Halide runtime encountered an error while trying to detach
     * a native device handle.  Turn on -debug in your target string
     * to see more details. */
    halide_error_code_device_detach_native_failed = -33,

    /** The host field on an input or output was null, the device
     * field was not zero, and the pipeline tries to use the buffer on
     * the host. You may be passing a GPU-only buffer to a pipeline
     * which is scheduled to use it on the CPU. */
    halide_error_code_host_is_null = -34,

    /** A folded buffer was passed to an extern stage, but the region
     * touched wraps around the fold boundary. */
    halide_error_code_bad_extern_fold = -35,

    /** Buffer has a non-null device_interface but device is 0, which
     * violates a Halide invariant. */
    halide_error_code_device_interface_no_device = -36,

    /** Buffer has both host and device dirty bits set, which violates
     * a Halide invariant. */
    halide_error_code_host_and_device_dirty = -37,

    /** The halide_buffer_t * passed to a halide runtime routine is
     * nullptr and this is not allowed. */
    halide_error_code_buffer_is_null = -38,

    /** The Halide runtime encountered an error while trying to copy
     * from one buffer to another. Turn on -debug in your target
     * string to see more details. */
    halide_error_code_device_buffer_copy_failed = -39,

    /** Attempted to make cropped/sliced alias of a buffer with a device
     * field, but the device_interface does not support cropping. */
    halide_error_code_device_crop_unsupported = -40,

    /** Cropping/slicing a buffer failed for some other reason. Turn on -debug
     * in your target string. */
    halide_error_code_device_crop_failed = -41,

    /** An operation on a buffer required an allocation on a
     * particular device interface, but a device allocation already
     * existed on a different device interface. Free the old one
     * first. */
    halide_error_code_incompatible_device_interface = -42,

    /** The dimensions field of a halide_buffer_t does not match the dimensions of that ImageParam. */
    halide_error_code_bad_dimensions = -43,

    /** A buffer with the device_dirty flag set was passed to a
     * pipeline compiled with no device backends enabled, so it
     * doesn't know how to copy the data back from device memory to
     * host memory. Either call copy_to_host before calling the Halide
     * pipeline, or enable the appropriate device backend. */
    halide_error_code_device_dirty_with_no_device_support = -44,

    /** An explicit storage bound provided is too small to store
     * all the values produced by the function. */
    halide_error_code_storage_bound_too_small = -45,
};

/** Halide calls the functions below on various error conditions. The
 * default implementations construct an error message, call
 * halide_error, then return the matching error code above. On
 * platforms that support weak linking, you can override these to
 * catch the errors individually. */

/** A call into an extern stage for the purposes of bounds inference
 * failed. Returns the error code given by the extern stage. */
extern int halide_error_bounds_inference_call_failed(void *user_context, const char *extern_stage_name, int result);

/** A call to an extern stage failed. Returned the error code given by
 * the extern stage. */
extern int halide_error_extern_stage_failed(void *user_context, const char *extern_stage_name, int result);

/** Various other error conditions. See the enum above for a
 * description of each. */
// @{
extern int halide_error_explicit_bounds_too_small(void *user_context, const char *func_name, const char *var_name,
                                                  int min_bound, int max_bound, int min_required, int max_required);
extern int halide_error_bad_type(void *user_context, const char *func_name,
                                 uint32_t type_given, uint32_t correct_type);  // N.B. The last two args are the bit representation of a halide_type_t
extern int halide_error_bad_dimensions(void *user_context, const char *func_name,
                                       int32_t dimensions_given, int32_t correct_dimensions);
extern int halide_error_access_out_of_bounds(void *user_context, const char *func_name,
                                             int dimension, int min_touched, int max_touched,
                                             int min_valid, int max_valid);
extern int halide_error_buffer_allocation_too_large(void *user_context, const char *buffer_name,
                                                    uint64_t allocation_size, uint64_t max_size);
extern int halide_error_buffer_extents_negative(void *user_context, const char *buffer_name, int dimension, int extent);
extern int halide_error_buffer_extents_too_large(void *user_context, const char *buffer_name,
                                                 int64_t actual_size, int64_t max_size);
extern int halide_error_constraints_make_required_region_smaller(void *user_context, const char *buffer_name,
                                                                 int dimension,
                                                                 int constrained_min, int constrained_extent,
                                                                 int required_min, int required_extent);
extern int halide_error_constraint_violated(void *user_context, const char *var, int val,
                                            const char *constrained_var, int constrained_val);
extern int halide_error_param_too_small_i64(void *user_context, const char *param_name,
                                            int64_t val, int64_t min_val);
extern int halide_error_param_too_small_u64(void *user_context, const char *param_name,
                                            uint64_t val, uint64_t min_val);
extern int halide_error_param_too_small_f64(void *user_context, const char *param_name,
                                            double val, double min_val);
extern int halide_error_param_too_large_i64(void *user_context, const char *param_name,
                                            int64_t val, int64_t max_val);
extern int halide_error_param_too_large_u64(void *user_context, const char *param_name,
                                            uint64_t val, uint64_t max_val);
extern int halide_error_param_too_large_f64(void *user_context, const char *param_name,
                                            double val, double max_val);
extern int halide_error_out_of_memory(void *user_context);
extern int halide_error_buffer_argument_is_null(void *user_context, const char *buffer_name);
extern int halide_error_debug_to_file_failed(void *user_context, const char *func,
                                             const char *filename, int error_code);
extern int halide_error_unaligned_host_ptr(void *user_context, const char *func_name, int alignment);
extern int halide_error_host_is_null(void *user_context, const char *func_name);
extern int halide_error_bad_fold(void *user_context, const char *func_name, const char *var_name,
                                 const char *loop_name);
extern int halide_error_bad_extern_fold(void *user_context, const char *func_name,
                                        int dim, int min, int extent, int valid_min, int fold_factor);

extern int halide_error_fold_factor_too_small(void *user_context, const char *func_name, const char *var_name,
                                              int fold_factor, const char *loop_name, int required_extent);
extern int halide_error_requirement_failed(void *user_context, const char *condition, const char *message);
extern int halide_error_specialize_fail(void *user_context, const char *message);
extern int halide_error_no_device_interface(void *user_context);
extern int halide_error_device_interface_no_device(void *user_context);
extern int halide_error_host_and_device_dirty(void *user_context);
extern int halide_error_buffer_is_null(void *user_context, const char *routine);
extern int halide_error_device_dirty_with_no_device_support(void *user_context, const char *buffer_name);
extern int halide_error_storage_bound_too_small(void *user_context, const char *func_name, const char *var_name,
                                                int provided_size, int required_size);
extern int halide_error_device_crop_failed(void *user_context);
// @}

/** Optional features a compilation Target can have.
 * Be sure to keep this in sync with the Feature enum in Target.h and the implementation of
 * get_runtime_compatible_target in Target.cpp if you add a new feature.
 */
typedef enum halide_target_feature_t {
    halide_target_feature_jit = 0,          ///< Generate code that will run immediately inside the calling process.
    halide_target_feature_debug,            ///< Turn on debug info and output for runtime code.
    halide_target_feature_no_asserts,       ///< Disable all runtime checks, for slightly tighter code.
    halide_target_feature_no_bounds_query,  ///< Disable the bounds querying functionality.

    halide_target_feature_sse41,  ///< Use SSE 4.1 and earlier instructions. Only relevant on x86.
    halide_target_feature_avx,    ///< Use AVX 1 instructions. Only relevant on x86.
    halide_target_feature_avx2,   ///< Use AVX 2 instructions. Only relevant on x86.
    halide_target_feature_fma,    ///< Enable x86 FMA instruction
    halide_target_feature_fma4,   ///< Enable x86 (AMD) FMA4 instruction set
    halide_target_feature_f16c,   ///< Enable x86 16-bit float support

    halide_target_feature_armv7s,   ///< Generate code for ARMv7s. Only relevant for 32-bit ARM.
    halide_target_feature_no_neon,  ///< Avoid using NEON instructions. Only relevant for 32-bit ARM.

    halide_target_feature_vsx,              ///< Use VSX instructions. Only relevant on POWERPC.
    halide_target_feature_power_arch_2_07,  ///< Use POWER ISA 2.07 new instructions. Only relevant on POWERPC.

    halide_target_feature_cuda,               ///< Enable the CUDA runtime. Defaults to compute capability 2.0 (Fermi)
    halide_target_feature_cuda_capability30,  ///< Enable CUDA compute capability 3.0 (Kepler)
    halide_target_feature_cuda_capability32,  ///< Enable CUDA compute capability 3.2 (Tegra K1)
    halide_target_feature_cuda_capability35,  ///< Enable CUDA compute capability 3.5 (Kepler)
    halide_target_feature_cuda_capability50,  ///< Enable CUDA compute capability 5.0 (Maxwell)
    halide_target_feature_cuda_capability61,  ///< Enable CUDA compute capability 6.1 (Pascal)
    halide_target_feature_cuda_capability70,  ///< Enable CUDA compute capability 7.0 (Volta)
    halide_target_feature_cuda_capability75,  ///< Enable CUDA compute capability 7.5 (Turing)
    halide_target_feature_cuda_capability80,  ///< Enable CUDA compute capability 8.0 (Ampere)
    halide_target_feature_cuda_capability86,  ///< Enable CUDA compute capability 8.6 (Ampere)

    halide_target_feature_opencl,       ///< Enable the OpenCL runtime.
    halide_target_feature_cl_doubles,   ///< Enable double support on OpenCL targets
    halide_target_feature_cl_atomic64,  ///< Enable 64-bit atomics operations on OpenCL targets

    halide_target_feature_openglcompute,  ///< Enable OpenGL Compute runtime.

    halide_target_feature_user_context,  ///< Generated code takes a user_context pointer as first argument

    halide_target_feature_matlab,  ///< Generate a mexFunction compatible with Matlab mex libraries. See tools/mex_halide.m.

    halide_target_feature_profile,     ///< Launch a sampling profiler alongside the Halide pipeline that monitors and reports the runtime used by each Func
    halide_target_feature_no_runtime,  ///< Do not include a copy of the Halide runtime in any generated object file or assembly

    halide_target_feature_metal,  ///< Enable the (Apple) Metal runtime.

    halide_target_feature_c_plus_plus_mangling,  ///< Generate C++ mangled names for result function, et al

    halide_target_feature_large_buffers,  ///< Enable 64-bit buffer indexing to support buffers > 2GB. Ignored if bits != 64.

    halide_target_feature_hvx_128,                ///< Enable HVX 128 byte mode.
    halide_target_feature_hvx_v62,                ///< Enable Hexagon v62 architecture.
    halide_target_feature_fuzz_float_stores,      ///< On every floating point store, set the last bit of the mantissa to zero. Pipelines for which the output is very different with this feature enabled may also produce very different output on different processors.
    halide_target_feature_soft_float_abi,         ///< Enable soft float ABI. This only enables the soft float ABI calling convention, which does not necessarily use soft floats.
    halide_target_feature_msan,                   ///< Enable hooks for MSAN support.
    halide_target_feature_avx512,                 ///< Enable the base AVX512 subset supported by all AVX512 architectures. The specific feature sets are AVX-512F and AVX512-CD. See https://en.wikipedia.org/wiki/AVX-512 for a description of each AVX subset.
    halide_target_feature_avx512_knl,             ///< Enable the AVX512 features supported by Knight's Landing chips, such as the Xeon Phi x200. This includes the base AVX512 set, and also AVX512-CD and AVX512-ER.
    halide_target_feature_avx512_skylake,         ///< Enable the AVX512 features supported by Skylake Xeon server processors. This adds AVX512-VL, AVX512-BW, and AVX512-DQ to the base set. The main difference from the base AVX512 set is better support for small integer ops. Note that this does not include the Knight's Landing features. Note also that these features are not available on Skylake desktop and mobile processors.
    halide_target_feature_avx512_cannonlake,      ///< Enable the AVX512 features expected to be supported by future Cannonlake processors. This includes all of the Skylake features, plus AVX512-IFMA and AVX512-VBMI.
    halide_target_feature_avx512_sapphirerapids,  ///< Enable the AVX512 features supported by Sapphire Rapids processors. This include all of the Cannonlake features, plus AVX512-VNNI and AVX512-BF16.
    halide_target_feature_hvx_use_shared_object,  ///< Deprecated
    halide_target_feature_trace_loads,            ///< Trace all loads done by the pipeline. Equivalent to calling Func::trace_loads on every non-inlined Func.
    halide_target_feature_trace_stores,           ///< Trace all stores done by the pipeline. Equivalent to calling Func::trace_stores on every non-inlined Func.
    halide_target_feature_trace_realizations,     ///< Trace all realizations done by the pipeline. Equivalent to calling Func::trace_realizations on every non-inlined Func.
    halide_target_feature_trace_pipeline,         ///< Trace the pipeline.
    halide_target_feature_hvx_v65,                ///< Enable Hexagon v65 architecture.
    halide_target_feature_hvx_v66,                ///< Enable Hexagon v66 architecture.
    halide_target_feature_cl_half,                ///< Enable half support on OpenCL targets
    halide_target_feature_strict_float,           ///< Turn off all non-IEEE floating-point optimization. Currently applies only to LLVM targets.
    halide_target_feature_tsan,                   ///< Enable hooks for TSAN support.
    halide_target_feature_asan,                   ///< Enable hooks for ASAN support.
    halide_target_feature_d3d12compute,           ///< Enable Direct3D 12 Compute runtime.
    halide_target_feature_check_unsafe_promises,  ///< Insert assertions for promises.
    halide_target_feature_hexagon_dma,            ///< Enable Hexagon DMA buffers.
    halide_target_feature_embed_bitcode,          ///< Emulate clang -fembed-bitcode flag.
    halide_target_feature_enable_llvm_loop_opt,   ///< Enable loop vectorization + unrolling in LLVM. Overrides halide_target_feature_disable_llvm_loop_opt. (Ignored for non-LLVM targets.)
    halide_target_feature_disable_llvm_loop_opt,  ///< Disable loop vectorization + unrolling in LLVM. (Ignored for non-LLVM targets.)
    halide_target_feature_wasm_simd128,           ///< Enable +simd128 instructions for WebAssembly codegen.
    halide_target_feature_wasm_signext,           ///< Enable +sign-ext instructions for WebAssembly codegen.
    halide_target_feature_wasm_sat_float_to_int,  ///< Enable saturating (nontrapping) float-to-int instructions for WebAssembly codegen.
    halide_target_feature_wasm_threads,           ///< Enable use of threads in WebAssembly codegen. Requires the use of a wasm runtime that provides pthread-compatible wrappers (typically, Emscripten with the -pthreads flag). Unsupported under WASI.
    halide_target_feature_wasm_bulk_memory,       ///< Enable +bulk-memory instructions for WebAssembly codegen.
    halide_target_feature_sve,                    ///< Enable ARM Scalable Vector Extensions
    halide_target_feature_sve2,                   ///< Enable ARM Scalable Vector Extensions v2
    halide_target_feature_egl,                    ///< Force use of EGL support.
    halide_target_feature_arm_dot_prod,           ///< Enable ARMv8.2-a dotprod extension (i.e. udot and sdot instructions)
    halide_target_feature_arm_fp16,               ///< Enable ARMv8.2-a half-precision floating point data processing
    halide_llvm_large_code_model,                 ///< Use the LLVM large code model to compile
    halide_target_feature_rvv,                    ///< Enable RISCV "V" Vector Extension
    halide_target_feature_armv81a,                ///< Enable ARMv8.1-a instructions
    halide_target_feature_end                     ///< A sentinel. Every target is considered to have this feature, and setting this feature does nothing.
} halide_target_feature_t;

/** This function is called internally by Halide in some situations to determine
 * if the current execution environment can support the given set of
 * halide_target_feature_t flags. The implementation must do the following:
 *
 * -- If there are flags set in features that the function knows *cannot* be supported, return 0.
 * -- Otherwise, return 1.
 * -- Note that any flags set in features that the function doesn't know how to test should be ignored;
 * this implies that a return value of 1 means "not known to be bad" rather than "known to be good".
 *
 * In other words: a return value of 0 means "It is not safe to use code compiled with these features",
 * while a return value of 1 means "It is not obviously unsafe to use code compiled with these features".
 *
 * The default implementation simply calls halide_default_can_use_target_features.
 *
 * Note that `features` points to an array of `count` uint64_t; this array must contain enough
 * bits to represent all the currently known features. Any excess bits must be set to zero.
 */
// @{
extern int halide_can_use_target_features(int count, const uint64_t *features);
typedef int (*halide_can_use_target_features_t)(int count, const uint64_t *features);
extern halide_can_use_target_features_t halide_set_custom_can_use_target_features(halide_can_use_target_features_t);
// @}

/**
 * This is the default implementation of halide_can_use_target_features; it is provided
 * for convenience of user code that may wish to extend halide_can_use_target_features
 * but continue providing existing support, e.g.
 *
 *     int halide_can_use_target_features(int count, const uint64_t *features) {
 *          if (features[halide_target_somefeature >> 6] & (1LL << (halide_target_somefeature & 63))) {
 *              if (!can_use_somefeature()) {
 *                  return 0;
 *              }
 *          }
 *          return halide_default_can_use_target_features(count, features);
 *     }
 */
extern int halide_default_can_use_target_features(int count, const uint64_t *features);

typedef struct halide_dimension_t {
#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    int32_t min = 0, extent = 0, stride = 0;

    // Per-dimension flags. None are defined yet (This is reserved for future use).
    uint32_t flags = 0;

    HALIDE_ALWAYS_INLINE halide_dimension_t() = default;
    HALIDE_ALWAYS_INLINE halide_dimension_t(int32_t m, int32_t e, int32_t s, uint32_t f = 0)
        : min(m), extent(e), stride(s), flags(f) {
    }

    HALIDE_ALWAYS_INLINE bool operator==(const halide_dimension_t &other) const {
        return (min == other.min) &&
               (extent == other.extent) &&
               (stride == other.stride) &&
               (flags == other.flags);
    }

    HALIDE_ALWAYS_INLINE bool operator!=(const halide_dimension_t &other) const {
        return !(*this == other);
    }
#else
    int32_t min, extent, stride;

    // Per-dimension flags. None are defined yet (This is reserved for future use).
    uint32_t flags;
#endif
} halide_dimension_t;

#ifdef __cplusplus
}  // extern "C"
#endif

typedef enum { halide_buffer_flag_host_dirty = 1,
               halide_buffer_flag_device_dirty = 2 } halide_buffer_flags;

/**
 * The raw representation of an image passed around by generated
 * Halide code. It includes some stuff to track whether the image is
 * not actually in main memory, but instead on a device (like a
 * GPU). For a more convenient C++ wrapper, use Halide::Buffer<T>. */
typedef struct halide_buffer_t {
    /** A device-handle for e.g. GPU memory used to back this buffer. */
    uint64_t device;

    /** The interface used to interpret the above handle. */
    const struct halide_device_interface_t *device_interface;

    /** A pointer to the start of the data in main memory. In terms of
     * the Halide coordinate system, this is the address of the min
     * coordinates (defined below). */
    uint8_t *host;

    /** flags with various meanings. */
    uint64_t flags;

    /** The type of each buffer element. */
    struct halide_type_t type;

    /** The dimensionality of the buffer. */
    int32_t dimensions;

    /** The shape of the buffer. Halide does not own this array - you
     * must manage the memory for it yourself. */
    halide_dimension_t *dim;

    /** Pads the buffer up to a multiple of 8 bytes */
    void *padding;

#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)
    /** Convenience methods for accessing the flags */
    // @{
    HALIDE_ALWAYS_INLINE bool get_flag(halide_buffer_flags flag) const {
        return (flags & flag) != 0;
    }

    HALIDE_ALWAYS_INLINE void set_flag(halide_buffer_flags flag, bool value) {
        if (value) {
            flags |= flag;
        } else {
            flags &= ~uint64_t(flag);
        }
    }

    HALIDE_ALWAYS_INLINE bool host_dirty() const {
        return get_flag(halide_buffer_flag_host_dirty);
    }

    HALIDE_ALWAYS_INLINE bool device_dirty() const {
        return get_flag(halide_buffer_flag_device_dirty);
    }

    HALIDE_ALWAYS_INLINE void set_host_dirty(bool v = true) {
        set_flag(halide_buffer_flag_host_dirty, v);
    }

    HALIDE_ALWAYS_INLINE void set_device_dirty(bool v = true) {
        set_flag(halide_buffer_flag_device_dirty, v);
    }
    // @}

    /** The total number of elements this buffer represents. Equal to
     * the product of the extents */
    HALIDE_ALWAYS_INLINE size_t number_of_elements() const {
        size_t s = 1;
        for (int i = 0; i < dimensions; i++) {
            s *= dim[i].extent;
        }
        return s;
    }

    /** Offset to the element with the lowest address.
     * If all strides are positive, equal to zero.
     * Offset is in elements, not bytes.
     * Unlike begin(), this is ok to call on an unallocated buffer. */
    HALIDE_ALWAYS_INLINE ptrdiff_t begin_offset() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions; i++) {
            const int stride = dim[i].stride;
            if (stride < 0) {
                index += stride * (ptrdiff_t)(dim[i].extent - 1);
            }
        }
        return index;
    }

    /** An offset to one beyond the element with the highest address.
     * Offset is in elements, not bytes.
     * Unlike end(), this is ok to call on an unallocated buffer. */
    HALIDE_ALWAYS_INLINE ptrdiff_t end_offset() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions; i++) {
            const int stride = dim[i].stride;
            if (stride > 0) {
                index += stride * (ptrdiff_t)(dim[i].extent - 1);
            }
        }
        index += 1;
        return index;
    }

    /** A pointer to the element with the lowest address.
     * If all strides are positive, equal to the host pointer.
     * Illegal to call on an unallocated buffer. */
    HALIDE_ALWAYS_INLINE uint8_t *begin() const {
        return host + begin_offset() * type.bytes();
    }

    /** A pointer to one beyond the element with the highest address.
     * Illegal to call on an unallocated buffer. */
    HALIDE_ALWAYS_INLINE uint8_t *end() const {
        return host + end_offset() * type.bytes();
    }

    /** The total number of bytes spanned by the data in memory. */
    HALIDE_ALWAYS_INLINE size_t size_in_bytes() const {
        return (size_t)(end_offset() - begin_offset()) * type.bytes();
    }

    /** A pointer to the element at the given location. */
    HALIDE_ALWAYS_INLINE uint8_t *address_of(const int *pos) const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions; i++) {
            index += (ptrdiff_t)dim[i].stride * (pos[i] - dim[i].min);
        }
        return host + index * type.bytes();
    }

    /** Attempt to call device_sync for the buffer. If the buffer
     * has no device_interface (or no device_sync), this is a quiet no-op.
     * Calling this explicitly should rarely be necessary, except for profiling. */
    HALIDE_ALWAYS_INLINE int device_sync(void *ctx = nullptr) {
        if (device_interface && device_interface->device_sync) {
            return device_interface->device_sync(ctx, this);
        }
        return 0;
    }

    /** Check if an input buffer passed extern stage is a querying
     * bounds. Compared to doing the host pointer check directly,
     * this both adds clarity to code and will facilitate moving to
     * another representation for bounds query arguments. */
    HALIDE_ALWAYS_INLINE bool is_bounds_query() const {
        return host == nullptr && device == 0;
    }

#endif
} halide_buffer_t;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HALIDE_ATTRIBUTE_DEPRECATED
#ifdef HALIDE_ALLOW_DEPRECATED
#define HALIDE_ATTRIBUTE_DEPRECATED(x)
#else
#ifdef _MSC_VER
#define HALIDE_ATTRIBUTE_DEPRECATED(x) __declspec(deprecated(x))
#else
#define HALIDE_ATTRIBUTE_DEPRECATED(x) __attribute__((deprecated(x)))
#endif
#endif
#endif

/** halide_scalar_value_t is a simple union able to represent all the well-known
 * scalar values in a filter argument. Note that it isn't tagged with a type;
 * you must ensure you know the proper type before accessing. Most user
 * code will never need to create instances of this struct; its primary use
 * is to hold def/min/max values in a halide_filter_argument_t. (Note that
 * this is conceptually just a union; it's wrapped in a struct to ensure
 * that it doesn't get anonymized by LLVM.)
 */
struct halide_scalar_value_t {
    union {
        bool b;
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float f32;
        double f64;
        void *handle;
    } u;
#ifdef __cplusplus
    HALIDE_ALWAYS_INLINE halide_scalar_value_t() {
        u.u64 = 0;
    }
#endif
};

enum halide_argument_kind_t {
    halide_argument_kind_input_scalar = 0,
    halide_argument_kind_input_buffer = 1,
    halide_argument_kind_output_buffer = 2
};

/*
    These structs must be robust across different compilers and settings; when
    modifying them, strive for the following rules:

    1) All fields are explicitly sized. I.e. must use int32_t and not "int"
    2) All fields must land on an alignment boundary that is the same as their size
    3) Explicit padding is added to make that so
    4) The sizeof the struct is padded out to a multiple of the largest natural size thing in the struct
    5) don't forget that 32 and 64 bit pointers are different sizes
*/

/**
 * Obsolete version of halide_filter_argument_t; only present in
 * code that wrote halide_filter_metadata_t version 0.
 */
struct halide_filter_argument_t_v0 {
    const char *name;
    int32_t kind;
    int32_t dimensions;
    struct halide_type_t type;
    const struct halide_scalar_value_t *def, *min, *max;
};

/**
 * halide_filter_argument_t is essentially a plain-C-struct equivalent to
 * Halide::Argument; most user code will never need to create one.
 */
struct halide_filter_argument_t {
    const char *name;    // name of the argument; will never be null or empty.
    int32_t kind;        // actually halide_argument_kind_t
    int32_t dimensions;  // always zero for scalar arguments
    struct halide_type_t type;
    // These pointers should always be null for buffer arguments,
    // and *may* be null for scalar arguments. (A null value means
    // there is no def/min/max/estimate specified for this argument.)
    const struct halide_scalar_value_t *scalar_def, *scalar_min, *scalar_max, *scalar_estimate;
    // This pointer should always be null for scalar arguments,
    // and *may* be null for buffer arguments. If not null, it should always
    // point to an array of dimensions*2 pointers, which will be the (min, extent)
    // estimates for each dimension of the buffer. (Note that any of the pointers
    // may be null as well.)
    int64_t const *const *buffer_estimates;
};

struct halide_filter_metadata_t {
#ifdef __cplusplus
    static const int32_t VERSION = 1;
#endif

    /** version of this metadata; currently always 1. */
    int32_t version;

    /** The number of entries in the arguments field. This is always >= 1. */
    int32_t num_arguments;

    /** An array of the filters input and output arguments; this will never be
     * null. The order of arguments is not guaranteed (input and output arguments
     * may come in any order); however, it is guaranteed that all arguments
     * will have a unique name within a given filter. */
    const struct halide_filter_argument_t *arguments;

    /** The Target for which the filter was compiled. This is always
     * a canonical Target string (ie a product of Target::to_string). */
    const char *target;

    /** The function name of the filter. */
    const char *name;
};

/** halide_register_argv_and_metadata() is a **user-defined** function that
 * must be provided in order to use the registration.cc files produced
 * by Generators when the 'registration' output is requested. Each registration.cc
 * file provides a static initializer that calls this function with the given
 * filter's argv-call variant, its metadata, and (optionally) and additional
 * textual data that the build system chooses to tack on for its own purposes.
 * Note that this will be called at static-initializer time (i.e., before
 * main() is called), and in an unpredictable order. Note that extra_key_value_pairs
 * may be nullptr; if it's not null, it's expected to be a null-terminated list
 * of strings, with an even number of entries. */
void halide_register_argv_and_metadata(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    const char *const *extra_key_value_pairs);

/** The functions below here are relevant for pipelines compiled with
 * the -profile target flag, which runs a sampling profiler thread
 * alongside the pipeline. */

/** Per-Func state tracked by the sampling profiler. */
struct halide_profiler_func_stats {
    /** Total time taken evaluating this Func (in nanoseconds). */
    uint64_t time;

    /** The current memory allocation of this Func. */
    uint64_t memory_current;

    /** The peak memory allocation of this Func. */
    uint64_t memory_peak;

    /** The total memory allocation of this Func. */
    uint64_t memory_total;

    /** The peak stack allocation of this Func's threads. */
    uint64_t stack_peak;

    /** The average number of thread pool worker threads active while computing this Func. */
    uint64_t active_threads_numerator, active_threads_denominator;

    /** The name of this Func. A global constant string. */
    const char *name;

    /** The total number of memory allocation of this Func. */
    int num_allocs;
};

/** Per-pipeline state tracked by the sampling profiler. These exist
 * in a linked list. */
struct halide_profiler_pipeline_stats {
    /** Total time spent inside this pipeline (in nanoseconds) */
    uint64_t time;

    /** The current memory allocation of funcs in this pipeline. */
    uint64_t memory_current;

    /** The peak memory allocation of funcs in this pipeline. */
    uint64_t memory_peak;

    /** The total memory allocation of funcs in this pipeline. */
    uint64_t memory_total;

    /** The average number of thread pool worker threads doing useful
     * work while computing this pipeline. */
    uint64_t active_threads_numerator, active_threads_denominator;

    /** The name of this pipeline. A global constant string. */
    const char *name;

    /** An array containing states for each Func in this pipeline. */
    struct halide_profiler_func_stats *funcs;

    /** The next pipeline_stats pointer. It's a void * because types
     * in the Halide runtime may not currently be recursive. */
    void *next;

    /** The number of funcs in this pipeline. */
    int num_funcs;

    /** An internal base id used to identify the funcs in this pipeline. */
    int first_func_id;

    /** The number of times this pipeline has been run. */
    int runs;

    /** The total number of samples taken inside of this pipeline. */
    int samples;

    /** The total number of memory allocation of funcs in this pipeline. */
    int num_allocs;
};

/** The global state of the profiler. */

struct halide_profiler_state {
    /** Guards access to the fields below. If not locked, the sampling
     * profiler thread is free to modify things below (including
     * reordering the linked list of pipeline stats). */
    struct halide_mutex lock;

    /** The amount of time the profiler thread sleeps between samples
     * in milliseconds. Defaults to 1 */
    int sleep_time;

    /** An internal id used for bookkeeping. */
    int first_free_id;

    /** The id of the current running Func. Set by the pipeline, read
     * periodically by the profiler thread. */
    int current_func;

    /** The number of threads currently doing work. */
    int active_threads;

    /** A linked list of stats gathered for each pipeline. */
    struct halide_profiler_pipeline_stats *pipelines;

    /** Retrieve remote profiler state. Used so that the sampling
     * profiler can follow along with execution that occurs elsewhere,
     * e.g. on a DSP. If null, it reads from the int above instead. */
    void (*get_remote_profiler_state)(int *func, int *active_workers);

    /** Sampling thread reference to be joined at shutdown. */
    struct halide_thread *sampling_thread;
};

/** Profiler func ids with special meanings. */
enum {
    /// current_func takes on this value when not inside Halide code
    halide_profiler_outside_of_halide = -1,
    /// Set current_func to this value to tell the profiling thread to
    /// halt. It will start up again next time you run a pipeline with
    /// profiling enabled.
    halide_profiler_please_stop = -2
};

/** Get a pointer to the global profiler state for programmatic
 * inspection. Lock it before using to pause the profiler. */
extern struct halide_profiler_state *halide_profiler_get_state();

/** Get a pointer to the pipeline state associated with pipeline_name.
 * This function grabs the global profiler state's lock on entry. */
extern struct halide_profiler_pipeline_stats *halide_profiler_get_pipeline_state(const char *pipeline_name);

/** Reset profiler state cheaply. May leave threads running or some
 * memory allocated but all accumluated statistics are reset.
 * WARNING: Do NOT call this method while any halide pipeline is
 * running; halide_profiler_memory_allocate/free and
 * halide_profiler_stack_peak_update update the profiler pipeline's
 * state without grabbing the global profiler state's lock. */
extern void halide_profiler_reset();

/** Reset all profiler state.
 * WARNING: Do NOT call this method while any halide pipeline is
 * running; halide_profiler_memory_allocate/free and
 * halide_profiler_stack_peak_update update the profiler pipeline's
 * state without grabbing the global profiler state's lock. */
void halide_profiler_shutdown();

/** Print out timing statistics for everything run since the last
 * reset. Also happens at process exit. */
extern void halide_profiler_report(void *user_context);

/// \name "Float16" functions
/// These functions operate of bits (``uint16_t``) representing a half
/// precision floating point number (IEEE-754 2008 binary16).
//{@

/** Read bits representing a half precision floating point number and return
 *  the float that represents the same value */
extern float halide_float16_bits_to_float(uint16_t);

/** Read bits representing a half precision floating point number and return
 *  the double that represents the same value */
extern double halide_float16_bits_to_double(uint16_t);

// TODO: Conversion functions to half

//@}

// Allocating and freeing device memory is often very slow. The
// methods below give Halide's runtime permission to hold onto device
// memory to service future requests instead of returning it to the
// underlying device API. The API does not manage an allocation pool,
// all it does is provide access to a shared counter that acts as a
// limit on the unused memory not yet returned to the underlying
// device API. It makes callbacks to participants when memory needs to
// be released because the limit is about to be exceeded (either
// because the limit has been reduced, or because the memory owned by
// some participant becomes unused).

/** Tell Halide whether or not it is permitted to hold onto device
 * allocations to service future requests instead of returning them
 * eagerly to the underlying device API. Many device allocators are
 * quite slow, so it can be beneficial to set this to true. The
 * default value for now is false.
 *
 * Note that if enabled, the eviction policy is very simplistic. The
 * 32 most-recently used allocations are preserved, regardless of
 * their size. Additionally, if a call to cuMalloc results in an
 * out-of-memory error, the entire cache is flushed and the allocation
 * is retried. See https://github.com/halide/Halide/issues/4093
 *
 * If set to false, releases all unused device allocations back to the
 * underlying device APIs. For finer-grained control, see specific
 * methods in each device api runtime. */
extern int halide_reuse_device_allocations(void *user_context, bool);

/** Determines whether on device_free the memory is returned
 * immediately to the device API, or placed on a free list for future
 * use. Override and switch based on the user_context for
 * finer-grained control. By default just returns the value most
 * recently set by the method above. */
extern bool halide_can_reuse_device_allocations(void *user_context);

struct halide_device_allocation_pool {
    int (*release_unused)(void *user_context);
    struct halide_device_allocation_pool *next;
};

/** Register a callback to be informed when
 * halide_reuse_device_allocations(false) is called, and all unused
 * device allocations must be released. The object passed should have
 * global lifetime, and its next field will be clobbered. */
extern void halide_register_device_allocation_pool(struct halide_device_allocation_pool *);

#ifdef __cplusplus
}  // End extern "C"
#endif

#if (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)

namespace {

template<typename T>
struct check_is_pointer {
    static constexpr bool value = false;
};

template<typename T>
struct check_is_pointer<T *> {
    static constexpr bool value = true;
};

}  // namespace

/** Construct the halide equivalent of a C type */
template<typename T>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of() {
    // Create a compile-time error if T is not a pointer (without
    // using any includes - this code goes into the runtime).
    // (Note that we can't have uninitialized variables in constexpr functions,
    // even if those variables aren't used.)
    static_assert(check_is_pointer<T>::value, "Expected a pointer type here");
    return halide_type_t(halide_type_handle, 64);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<float>() {
    return halide_type_t(halide_type_float, 32);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<double>() {
    return halide_type_t(halide_type_float, 64);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<bool>() {
    return halide_type_t(halide_type_uint, 1);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<uint8_t>() {
    return halide_type_t(halide_type_uint, 8);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<uint16_t>() {
    return halide_type_t(halide_type_uint, 16);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<uint32_t>() {
    return halide_type_t(halide_type_uint, 32);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<uint64_t>() {
    return halide_type_t(halide_type_uint, 64);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<int8_t>() {
    return halide_type_t(halide_type_int, 8);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<int16_t>() {
    return halide_type_t(halide_type_int, 16);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<int32_t>() {
    return halide_type_t(halide_type_int, 32);
}

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<int64_t>() {
    return halide_type_t(halide_type_int, 64);
}

#endif  // (__cplusplus >= 201103L || _MSVC_LANG >= 201103L)

#endif  // HALIDE_HALIDERUNTIME_H
