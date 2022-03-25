#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_spin_lock.h"

/* This provides an implementation of pthreads-like mutex and
 * condition variables with fast default case performance.  The code
 * is based on the "parking lot" design and specifically Amanieu
 * d'Antras' Rust implementation:
 *    https://github.com/Amanieu/parking_lot
 * and the one in WTF:
 *     https://webkit.org/blog/6161/locking-in-webkit/
 *
 * Neither of the above implementations were used directly largely for
 * dependency management. This implementation lacks a few features
 * relative to those two. Specifically timeouts are not
 * supported. Nor is optional fairness or deadlock detection.
 * This implementation should provide a faily standalone "one file"
 * fast synchronization layer on top of readily available system primitives.
 *
 * TODO: Implement pthread_once equivalent.
 * TODO: Add read/write lock and move SharedExclusiveSpinLock from tracing.cpp
 *        to this mechanism.
 * TODO: Add timeouts and optional fairness if needed.
 * TODO: Relying on condition variables has issues for old versions of Windows
 *       and likely has portability issues to some very bare bones embedded OSes.
 *       Doing an implementation using only semaphores or event counters should
 *       be doable.
 */

// Copied from tsan_interface.h
#ifndef TSAN_ANNOTATIONS
#define TSAN_ANNOTATIONS 0
#endif

#if TSAN_ANNOTATIONS
extern "C" {
const unsigned __tsan_mutex_linker_init = 1 << 0;
void __tsan_mutex_pre_lock(void *addr, unsigned flags);
void __tsan_mutex_post_lock(void *addr, unsigned flags, int recursion);
int __tsan_mutex_pre_unlock(void *addr, unsigned flags);
void __tsan_mutex_post_unlock(void *addr, unsigned flags);
void __tsan_mutex_pre_signal(void *addr, unsigned flags);
void __tsan_mutex_post_signal(void *addr, unsigned flags);
}
#endif

namespace Halide {
namespace Runtime {
namespace Internal {

namespace Synchronization {

namespace {

#if TSAN_ANNOTATIONS
ALWAYS_INLINE void if_tsan_pre_lock(void *mutex) {
    __tsan_mutex_pre_lock(mutex, __tsan_mutex_linker_init);
};
// TODO(zalman|dvyukov): Is 1 the right value for a non-recursive lock? pretty sure value is ignored.
ALWAYS_INLINE void if_tsan_post_lock(void *mutex) {
    __tsan_mutex_post_lock(mutex, __tsan_mutex_linker_init, 1);
}
// TODO(zalman|dvyukov): Is it safe to ignore return value here if locks are not recursive?
ALWAYS_INLINE void if_tsan_pre_unlock(void *mutex) {
    (void)__tsan_mutex_pre_unlock(mutex, __tsan_mutex_linker_init);
}
ALWAYS_INLINE void if_tsan_post_unlock(void *mutex) {
    __tsan_mutex_post_unlock(mutex, __tsan_mutex_linker_init);
}
ALWAYS_INLINE void if_tsan_pre_signal(void *cond) {
    __tsan_mutex_pre_signal(cond, 0);
}
ALWAYS_INLINE void if_tsan_post_signal(void *cond) {
    __tsan_mutex_post_signal(cond, 0);
}
#else
ALWAYS_INLINE void if_tsan_pre_lock(void *) {
}
ALWAYS_INLINE void if_tsan_post_lock(void *) {
}
ALWAYS_INLINE void if_tsan_pre_unlock(void *) {
}
ALWAYS_INLINE void if_tsan_post_unlock(void *) {
}
ALWAYS_INLINE void if_tsan_pre_signal(void *) {
}
ALWAYS_INLINE void if_tsan_post_signal(void *) {
}
#endif

#ifdef BITS_32
ALWAYS_INLINE uintptr_t atomic_and_fetch_release(uintptr_t *addr, uintptr_t val) {
    return __sync_and_and_fetch(addr, val);
}

template<typename T>
ALWAYS_INLINE T atomic_fetch_add_acquire_release(T *addr, T val) {
    return __sync_fetch_and_add(addr, val);
}

template<typename T>
ALWAYS_INLINE bool cas_strong_sequentially_consistent_helper(T *addr, T *expected, T *desired) {
    T oldval = *expected;
    T gotval = __sync_val_compare_and_swap(addr, oldval, *desired);
    *expected = gotval;
    return oldval == gotval;
}

ALWAYS_INLINE bool atomic_cas_strong_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

template<typename T>
ALWAYS_INLINE bool atomic_cas_weak_relacq_relaxed(T *addr, T *expected, T *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_relaxed_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_acquire_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE uintptr_t atomic_fetch_and_release(uintptr_t *addr, uintptr_t val) {
    return __sync_fetch_and_and(addr, val);
}

template<typename T>
ALWAYS_INLINE void atomic_load_relaxed(T *addr, T *val) {
    *val = *addr;
}

template<typename T>
ALWAYS_INLINE void atomic_load_acquire(T *addr, T *val) {
    __sync_synchronize();
    *val = *addr;
}

ALWAYS_INLINE uintptr_t atomic_or_fetch_relaxed(uintptr_t *addr, uintptr_t val) {
    return __sync_or_and_fetch(addr, val);
}

ALWAYS_INLINE void atomic_store_relaxed(uintptr_t *addr, uintptr_t *val) {
    *addr = *val;
}

template<typename T>
ALWAYS_INLINE void atomic_store_release(T *addr, T *val) {
    *addr = *val;
    __sync_synchronize();
}

ALWAYS_INLINE void atomic_thread_fence_acquire() {
    __sync_synchronize();
}

#else

ALWAYS_INLINE uintptr_t atomic_and_fetch_release(uintptr_t *addr, uintptr_t val) {
    return __atomic_and_fetch(addr, val, __ATOMIC_RELEASE);
}

template<typename T>
ALWAYS_INLINE T atomic_fetch_add_acquire_release(T *addr, T val) {
    return __atomic_fetch_add(addr, val, __ATOMIC_ACQ_REL);
}

ALWAYS_INLINE bool atomic_cas_strong_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE bool atomic_cas_weak_relacq_relaxed(T *addr, T *expected, T *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_relaxed_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_acquire_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

ALWAYS_INLINE uintptr_t atomic_fetch_and_release(uintptr_t *addr, uintptr_t val) {
    return __atomic_fetch_and(addr, val, __ATOMIC_RELEASE);
}

template<typename T>
ALWAYS_INLINE void atomic_load_relaxed(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_load_acquire(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_ACQUIRE);
}

ALWAYS_INLINE uintptr_t atomic_or_fetch_relaxed(uintptr_t *addr, uintptr_t val) {
    return __atomic_or_fetch(addr, val, __ATOMIC_RELAXED);
}

ALWAYS_INLINE void atomic_store_relaxed(uintptr_t *addr, uintptr_t *val) {
    __atomic_store(addr, val, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_store_release(T *addr, T *val) {
    __atomic_store(addr, val, __ATOMIC_RELEASE);
}

ALWAYS_INLINE void atomic_thread_fence_acquire() {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

#endif

}  // namespace

class spin_control {
    // Everyone says this should be 40. Have not measured it.
    int spin_count = 40;

public:
    ALWAYS_INLINE bool should_spin() {
        if (spin_count > 0) {
            spin_count--;
        }
        return spin_count > 0;
    }

    ALWAYS_INLINE void reset() {
        spin_count = 40;
    }
};

// Low order two bits are used for locking state,
static constexpr uint8_t lock_bit = 0x01;
static constexpr uint8_t queue_lock_bit = 0x02;
static constexpr uint8_t parked_bit = 0x02;

struct word_lock_queue_data {
    thread_parker parker;  // TODO: member or pointer?

    // This design is from the Rust parking lot implementation by Amanieu d'Antras.
    // Comment from original:
    //
    // Linked list of threads in the queue. The queue is split into two parts:
    // the processed part and the unprocessed part. When new nodes are added to
    // the list, they only have the next pointer set, and queue_tail is null.
    //
    // Nodes are processed with the queue lock held, which consists of setting
    // the prev pointer for each node and setting the queue_tail pointer on the
    // first processed node of the list.
    //
    // This setup allows nodes to be added to the queue without a lock, while
    // still allowing O(1) removal of nodes from the processed part of the list.
    // The only cost is the O(n) processing, but this only needs to be done
    // once for each node, and therefore isn't too expensive.

    word_lock_queue_data *next = nullptr;
    word_lock_queue_data *prev = nullptr;
    word_lock_queue_data *tail = nullptr;
};

class word_lock {
    uintptr_t state = 0;

    void lock_full();
    void unlock_full();

public:
    ALWAYS_INLINE void lock() {
        if_tsan_pre_lock(this);

        uintptr_t expected = 0;
        uintptr_t desired = lock_bit;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!atomic_cas_weak_acquire_relaxed(&state, &expected, &desired)) {
            lock_full();
        }

        if_tsan_post_lock(this);
    }

    ALWAYS_INLINE void unlock() {
        if_tsan_pre_unlock(this);

        uintptr_t val = atomic_fetch_and_release(&state, ~(uintptr_t)lock_bit);
        // If another thread is currently queueing, that thread will ensure
        // it acquires the lock or wakes a waiting thread.
        bool no_thread_queuing = (val & queue_lock_bit) == 0;
        // Only need to do a wakeup if there are threads waiting.
        bool some_queued = (val & ~(uintptr_t)(queue_lock_bit | lock_bit)) != 0;
        if (no_thread_queuing && some_queued) {
            unlock_full();
        }

        if_tsan_post_unlock(this);
    }
};

WEAK void word_lock::lock_full() {
    spin_control spinner;
    uintptr_t expected;
    atomic_load_relaxed(&state, &expected);

    while (true) {
        if (!(expected & lock_bit)) {
            uintptr_t desired = expected | lock_bit;

            if (atomic_cas_weak_acquire_relaxed(&state, &expected, &desired)) {
                return;
            }
            continue;
        }

        if (((expected & ~(uintptr_t)(queue_lock_bit | lock_bit)) != 0) && spinner.should_spin()) {
            halide_thread_yield();
            atomic_load_relaxed(&state, &expected);
            continue;
        }

        word_lock_queue_data node;

        node.parker.prepare_park();
        // TODO set up prelinkage parking state

        word_lock_queue_data *head = (word_lock_queue_data *)(expected & ~(uintptr_t)(queue_lock_bit | lock_bit));
        if (head == nullptr) {
            node.tail = &node;
            // constructor set node.prev = nullptr;
        } else {
            // Mark the tail as nullptr. The unlock routine will walk the list and wakeup
            // the thread at the end.
            // constructor set node.tail = nullptr;
            // constructor set node.prev = nullptr;
            node.next = head;
        }

        uintptr_t desired = ((uintptr_t)&node) | (expected & (queue_lock_bit | lock_bit));
        if (atomic_cas_weak_release_relaxed(&state, &expected, &desired)) {
            node.parker.park();
            spinner.reset();
            atomic_load_relaxed(&state, &expected);
        }
    }
}

WEAK void word_lock::unlock_full() {
    uintptr_t expected;
    atomic_load_relaxed(&state, &expected);

    while (true) {
        // If another thread is currently queueing, that thread will ensure
        // it acquires the lock or wakes a waiting thread.
        bool thread_queuing = (expected & queue_lock_bit);
        // Only need to do a wakeup if there are threads waiting.
        bool none_queued = (expected & ~(uintptr_t)(queue_lock_bit | lock_bit)) == 0;
        if (thread_queuing || none_queued) {
            return;
        }

        uintptr_t desired = expected | queue_lock_bit;
        if (atomic_cas_weak_acquire_relaxed(&state, &expected, &desired)) {
            break;
        }
    }

    while (true) {
        word_lock_queue_data *head = (word_lock_queue_data *)(expected & ~(uintptr_t)(queue_lock_bit | lock_bit));
        word_lock_queue_data *current = head;
        word_lock_queue_data *tail = current->tail;
        while (tail == nullptr) {
            word_lock_queue_data *next = current->next;
            halide_abort_if_false(nullptr, next != nullptr);
            next->prev = current;
            current = next;
            tail = current->tail;
        }
        head->tail = tail;

        // If the lock is now locked, unlock the queue and have the thread
        // that currently holds the lock do the wakeup
        if (expected & lock_bit) {
            uintptr_t desired = expected & ~(uintptr_t)queue_lock_bit;
            if (atomic_cas_weak_relacq_relaxed(&state, &expected, &desired)) {
                return;
            }
            atomic_thread_fence_acquire();
            continue;
        }

        word_lock_queue_data *new_tail = tail->prev;
        if (new_tail == nullptr) {
            bool continue_outer = false;
            while (!continue_outer) {
                uintptr_t desired = expected & lock_bit;
                if (atomic_cas_weak_relacq_relaxed(&state, &expected, &desired)) {
                    break;
                }
                if ((expected & ~(uintptr_t)(queue_lock_bit | lock_bit)) == 0) {
                    continue;
                } else {
                    atomic_thread_fence_acquire();
                    continue_outer = true;
                }
            }

            if (continue_outer) {
                continue;
            }
        } else {
            head->tail = new_tail;
            atomic_and_fetch_release(&state, ~(uintptr_t)queue_lock_bit);
        }

        // TODO: The reason there are three calls here is other things can happen between them.
        // Also it is not clear if unpark_start has to return the mutex/flag used by unpark
        // and unpark_finish due to memory lifetime reasons.
        tail->parker.unpark_start();
        tail->parker.unpark();
        tail->parker.unpark_finish();
        break;
    }
}

struct queue_data {
    thread_parker parker;  // TODO: member or pointer?

    uintptr_t sleep_address = 0;
    queue_data *next = nullptr;
    uintptr_t unpark_info = 0;
};

// Must be a power of two.
constexpr int LOAD_FACTOR = 4;

struct hash_bucket {
    word_lock mutex;

    queue_data *head = nullptr;  // Is this queue_data or thread_data?
    queue_data *tail = nullptr;  // Is this queue_data or thread_data?
};

constexpr int HASH_TABLE_SIZE = MAX_THREADS * LOAD_FACTOR;
struct hash_table {
    hash_bucket buckets[HASH_TABLE_SIZE];
};
WEAK hash_table table;

constexpr int HASH_TABLE_BITS = 10;
static_assert((1 << HASH_TABLE_BITS) >= MAX_THREADS * LOAD_FACTOR);

#if 0
WEAK void dump_hash() {
    int i = 0;
    for (auto &bucket : table.buckets) {
        queue_data *head = bucket.head;
        while (head != nullptr) {
            print(nullptr) << "Bucket index " << i << " addr " << (void *)head->sleep_address << "\n";
            head = head->next;
        }
        i++;
    }
}
#endif

static ALWAYS_INLINE uintptr_t addr_hash(uintptr_t addr) {
    // Fibonacci hashing. The golden ratio is 1.9E3779B97F4A7C15F39...
    // in hexadecimal.
    if (sizeof(uintptr_t) >= 8) {
        return (addr * (uintptr_t)0x9E3779B97F4A7C15) >> (64 - HASH_TABLE_BITS);
    } else {
        return (addr * (uintptr_t)0x9E3779B9) >> (32 - HASH_TABLE_BITS);
    }
}

WEAK hash_bucket &lock_bucket(uintptr_t addr) {
    uintptr_t hash = addr_hash(addr);

    halide_debug_assert(nullptr, hash < HASH_TABLE_SIZE);

    // TODO: if resizing is implemented, loop, etc.
    hash_bucket &bucket = table.buckets[hash];

    bucket.mutex.lock();

    return bucket;
}

struct bucket_pair {
    hash_bucket &from;
    hash_bucket &to;

    ALWAYS_INLINE bucket_pair(hash_bucket &from, hash_bucket &to)
        : from(from), to(to) {
    }
};

WEAK bucket_pair lock_bucket_pair(uintptr_t addr_from, uintptr_t addr_to) {
    // TODO: if resizing is implemented, loop, etc.
    uintptr_t hash_from = addr_hash(addr_from);
    uintptr_t hash_to = addr_hash(addr_to);

    halide_debug_assert(nullptr, hash_from < HASH_TABLE_SIZE);
    halide_debug_assert(nullptr, hash_to < HASH_TABLE_SIZE);

    // Lock the bucket with the smaller hash first in order
    // to prevent deadlock.
    if (hash_from == hash_to) {
        hash_bucket &first = table.buckets[hash_from];
        first.mutex.lock();
        return bucket_pair(first, first);
    } else if (hash_from < hash_to) {
        hash_bucket &first = table.buckets[hash_from];
        hash_bucket &second = table.buckets[hash_to];
        first.mutex.lock();
        second.mutex.lock();
        return bucket_pair(first, second);
    } else {
        hash_bucket &first = table.buckets[hash_to];
        hash_bucket &second = table.buckets[hash_from];
        first.mutex.lock();
        second.mutex.lock();
        return bucket_pair(second, first);
    }
}

WEAK void unlock_bucket_pair(bucket_pair &buckets) {
    // In the lock routine, the buckets are locked smaller hash index first.
    // Here we reverse this ordering by comparing the pointers. This works
    // since the pointers are obtained by indexing an array with the hash
    // values.
    if (&buckets.from == &buckets.to) {
        buckets.from.mutex.unlock();
    } else if (&buckets.from > &buckets.to) {
        buckets.from.mutex.unlock();
        buckets.to.mutex.unlock();
    } else {
        buckets.to.mutex.unlock();
        buckets.from.mutex.unlock();
    }
}

struct validate_action {
    bool unpark_one = false;
    uintptr_t invalid_unpark_info = 0;
};

struct parking_control {
    uintptr_t park(uintptr_t addr);
    uintptr_t unpark_one(uintptr_t addr);
    int unpark_requeue(uintptr_t addr_from, uintptr_t addr_to, uintptr_t unpark_info);

protected:
    virtual bool validate(validate_action &action) {
        return true;
    }
    virtual void before_sleep() {
        // nothing
    }
    virtual uintptr_t unpark(int unparked, bool more_waiters) {
        return 0;
    }
    virtual void requeue_callback(const validate_action &action, bool one_to_wake, bool some_requeued) {
        // nothing
    }
};

// TODO: Do we need a park_result thing here?
WEAK uintptr_t parking_control::park(uintptr_t addr) {
    queue_data queue_data;

    hash_bucket &bucket = lock_bucket(addr);

    validate_action action;
    if (!validate(action)) {
        bucket.mutex.unlock();
        return action.invalid_unpark_info;
    }

    queue_data.next = nullptr;
    queue_data.sleep_address = addr;
    queue_data.parker.prepare_park();
    if (bucket.head != nullptr) {
        bucket.tail->next = &queue_data;
    } else {
        bucket.head = &queue_data;
    }
    bucket.tail = &queue_data;
    bucket.mutex.unlock();

    before_sleep();

    queue_data.parker.park();

    return queue_data.unpark_info;

    // TODO: handling timeout.
}

WEAK uintptr_t parking_control::unpark_one(uintptr_t addr) {
    hash_bucket &bucket = lock_bucket(addr);

    queue_data **data_location = &bucket.head;
    queue_data *prev = nullptr;
    queue_data *data = *data_location;
    while (data != nullptr) {
        uintptr_t cur_addr;
        atomic_load_relaxed(&data->sleep_address, &cur_addr);
        if (cur_addr == addr) {
            queue_data *next = data->next;
            *data_location = next;

            bool more_waiters = false;

            if (bucket.tail == data) {
                bucket.tail = prev;
            } else {
                queue_data *data2 = next;
                while (data2 != nullptr && !more_waiters) {
                    uintptr_t cur_addr2;
                    atomic_load_relaxed(&data2->sleep_address, &cur_addr2);
                    more_waiters = (cur_addr2 == addr);
                    data2 = data2->next;
                }
            }

            data->unpark_info = unpark(1, more_waiters);

            data->parker.unpark_start();
            bucket.mutex.unlock();
            data->parker.unpark();
            data->parker.unpark_finish();

            // TODO: Figure out ret type.
            return more_waiters ? 1 : 0;
        } else {
            data_location = &data->next;
            prev = data;
            data = data->next;
        }
    }

    unpark(0, false);

    bucket.mutex.unlock();

    // TODO: decide if this is the right return value.
    return 0;
}

WEAK int parking_control::unpark_requeue(uintptr_t addr_from, uintptr_t addr_to, uintptr_t unpark_info) {
    bucket_pair buckets = lock_bucket_pair(addr_from, addr_to);

    validate_action action;
    if (!validate(action)) {
        unlock_bucket_pair(buckets);
        return 0;
    }

    queue_data **data_location = &buckets.from.head;
    queue_data *prev = nullptr;
    queue_data *data = *data_location;
    queue_data *requeue = nullptr;
    queue_data *requeue_tail = nullptr;
    queue_data *wakeup = nullptr;

    while (data != nullptr) {
        uintptr_t cur_addr;
        atomic_load_relaxed(&data->sleep_address, &cur_addr);

        queue_data *next = data->next;
        if (cur_addr == addr_from) {
            *data_location = next;

            if (buckets.from.tail == data) {
                buckets.from.tail = prev;
            }

            if (action.unpark_one && wakeup == nullptr) {
                wakeup = data;
            } else {
                if (requeue == nullptr) {
                    requeue = data;
                } else {
                    requeue_tail->next = data;
                }

                requeue_tail = data;
                atomic_store_relaxed(&data->sleep_address, &addr_to);
            }
            data = next;
            // TODO: prev ptr?
        } else {
            data_location = &data->next;
            prev = data;
            data = next;
        }
    }

    if (requeue != nullptr) {
        requeue_tail->next = nullptr;
        if (buckets.to.head == nullptr) {
            buckets.to.head = requeue;
        } else {
            buckets.to.tail->next = requeue;
        }
        buckets.to.tail = requeue_tail;
    }

    requeue_callback(action, wakeup != nullptr, requeue != nullptr);

    if (wakeup != nullptr) {
        wakeup->unpark_info = unpark_info;
        wakeup->parker.unpark_start();
        unlock_bucket_pair(buckets);
        wakeup->parker.unpark();
        wakeup->parker.unpark_finish();
    } else {
        unlock_bucket_pair(buckets);
    }

    return wakeup != nullptr && action.unpark_one;
}

struct mutex_parking_control final : public parking_control {
    uintptr_t *const lock_state;

    ALWAYS_INLINE mutex_parking_control(uintptr_t *lock_state)
        : lock_state(lock_state) {
    }

protected:
    bool validate(validate_action &action) final {
        uintptr_t result;
        atomic_load_relaxed(lock_state, &result);
        return result == (lock_bit | parked_bit);
    }

    uintptr_t unpark(int unparked, bool more_waiters) final {
        // TODO: consider handling fairness.
        uintptr_t return_state = more_waiters ? parked_bit : 0;
        atomic_store_release(lock_state, &return_state);
        return 0;
    }
};

class fast_mutex {
    uintptr_t state = 0;

    ALWAYS_INLINE void lock_full() {
        // Everyone says this should be 40. Have not measured it.
        spin_control spinner;
        uintptr_t expected;
        atomic_load_relaxed(&state, &expected);

        while (true) {
            if (!(expected & lock_bit)) {
                uintptr_t desired = expected | lock_bit;
                if (atomic_cas_weak_acquire_relaxed(&state, &expected, &desired)) {
                    return;
                }
                continue;
            }

            // Spin with spin count. Note that this occurs even if
            // threads are parked. We're prioritizing throughput over
            // fairness by letting sleeping threads lie.
            if (spinner.should_spin()) {
                halide_thread_yield();
                atomic_load_relaxed(&state, &expected);
                continue;
            }

            // Mark mutex as having parked threads if not already done.
            if ((expected & parked_bit) == 0) {
                uintptr_t desired = expected | parked_bit;
                if (!atomic_cas_weak_relaxed_relaxed(&state, &expected, &desired)) {
                    continue;
                }
            }

            // TODO: consider handling fairness, timeout
            mutex_parking_control control(&state);
            uintptr_t result = control.park((uintptr_t)this);
            if (result == (uintptr_t)this) {
                return;
            }

            spinner.reset();
            atomic_load_relaxed(&state, &expected);
        }
    }

    ALWAYS_INLINE void unlock_full() {
        uintptr_t expected = lock_bit;
        uintptr_t desired = 0;
        // Try for a fast release of the lock. Redundant with code in lock, but done
        // to make unlock_full a standalone unlock that can be called directly.
        if (atomic_cas_strong_release_relaxed(&state, &expected, &desired)) {
            return;
        }

        mutex_parking_control control(&state);
        control.unpark_one((uintptr_t)this);
    }

public:
    ALWAYS_INLINE void lock() {
        uintptr_t expected = 0;
        uintptr_t desired = lock_bit;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!atomic_cas_weak_acquire_relaxed(&state, &expected, &desired)) {
            lock_full();
        }
    }

    ALWAYS_INLINE void unlock() {
        uintptr_t expected = lock_bit;
        uintptr_t desired = 0;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!atomic_cas_weak_release_relaxed(&state, &expected, &desired)) {
            unlock_full();
        }
    }

    ALWAYS_INLINE bool make_parked_if_locked() {
        uintptr_t val;
        atomic_load_relaxed(&state, &val);
        while (true) {
            if (!(val & lock_bit)) {
                return false;
            }

            uintptr_t desired = val | parked_bit;
            if (atomic_cas_weak_relaxed_relaxed(&state, &val, &desired)) {
                return true;
            }
        }
    }

    ALWAYS_INLINE void make_parked() {
        atomic_or_fetch_relaxed(&state, parked_bit);
    }
};

struct signal_parking_control final : public parking_control {
    uintptr_t *const cond_state;
    fast_mutex *const mutex;

    ALWAYS_INLINE signal_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
        : cond_state(cond_state), mutex(mutex) {
    }

protected:
    uintptr_t unpark(int unparked, bool more_waiters) final {
        if (!more_waiters) {
            uintptr_t val = 0;
            atomic_store_relaxed(cond_state, &val);
        }

#if 0  // TODO: figure out why this was here.
        return (uintptr_t)mutex;
#else
        return 0;
#endif
    }
};

struct broadcast_parking_control final : public parking_control {
    uintptr_t *const cond_state;
    fast_mutex *const mutex;

    ALWAYS_INLINE broadcast_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
        : cond_state(cond_state), mutex(mutex) {
    }

protected:
    bool validate(validate_action &action) final {
        uintptr_t val;
        atomic_load_relaxed(cond_state, &val);
        // By the time this broadcast locked everything and was processed, the cond
        // has progressed to a new mutex, do nothing since any waiting threads have
        // to be waiting on what is effectively a different condition.
        if (val != (uintptr_t)mutex) {
            return false;
        }
        // Clear the cond's connection to the mutex as all waiting threads are going to reque onto the mutex.
        val = 0;
        atomic_store_relaxed(cond_state, &val);
        action.unpark_one = !mutex->make_parked_if_locked();
        return true;
    }

    void requeue_callback(const validate_action &action, bool one_to_wake, bool some_requeued) final {
        if (action.unpark_one && some_requeued) {
            mutex->make_parked();
        }
    }
};

struct wait_parking_control final : public parking_control {
    uintptr_t *const cond_state;
    fast_mutex *const mutex;

    ALWAYS_INLINE wait_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
        : cond_state(cond_state), mutex(mutex) {
    }

protected:
    bool validate(validate_action &action) final {
        uintptr_t val;
        atomic_load_relaxed(cond_state, &val);

        if (val == 0) {
            val = (uintptr_t)mutex;
            atomic_store_relaxed(cond_state, &val);
        } else if (val != (uintptr_t)mutex) {
            // TODO: signal error.
            action.invalid_unpark_info = (uintptr_t)mutex;
            return false;
        }

        return true;
    }

    void before_sleep() final {
        mutex->unlock();
    }

    uintptr_t unpark(int unparked, bool more_waiters) final {
        if (!more_waiters) {
            uintptr_t val = 0;
            atomic_store_relaxed(cond_state, &val);
        }
        return 0;
    }
};

class fast_cond {
    uintptr_t state = 0;

public:
    ALWAYS_INLINE void signal() {
        if_tsan_pre_signal(this);

        uintptr_t val;
        atomic_load_relaxed(&state, &val);
        if (val == 0) {
            if_tsan_post_signal(this);
            return;
        }
        signal_parking_control control(&state, (fast_mutex *)val);
        control.unpark_one((uintptr_t)this);
        if_tsan_post_signal(this);
    }

    ALWAYS_INLINE void broadcast() {
        if_tsan_pre_signal(this);
        uintptr_t val;
        atomic_load_relaxed(&state, &val);
        if (val == 0) {
            if_tsan_post_signal(this);
            return;
        }
        broadcast_parking_control control(&state, (fast_mutex *)val);
        control.unpark_requeue((uintptr_t)this, val, 0);
        if_tsan_post_signal(this);
    }

    ALWAYS_INLINE void wait(fast_mutex *mutex) {
        wait_parking_control control(&state, mutex);
        uintptr_t result = control.park((uintptr_t)this);
        if (result != (uintptr_t)mutex) {
            mutex->lock();
        } else {
            if_tsan_pre_lock(mutex);

            // TODO: this is debug only.
            uintptr_t val;
            atomic_load_relaxed((uintptr_t *)mutex, &val);
            halide_abort_if_false(nullptr, val & 0x1);

            if_tsan_post_lock(mutex);
        }
    }
};

}  // namespace Synchronization

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK void halide_mutex_lock(halide_mutex *mutex) {
    Halide::Runtime::Internal::Synchronization::fast_mutex *fast_mutex =
        (Halide::Runtime::Internal::Synchronization::fast_mutex *)mutex;
    fast_mutex->lock();
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
    Halide::Runtime::Internal::Synchronization::fast_mutex *fast_mutex =
        (Halide::Runtime::Internal::Synchronization::fast_mutex *)mutex;
    fast_mutex->unlock();
}

WEAK void halide_cond_broadcast(struct halide_cond *cond) {
    Halide::Runtime::Internal::Synchronization::fast_cond *fast_cond =
        (Halide::Runtime::Internal::Synchronization::fast_cond *)cond;
    fast_cond->broadcast();
}

WEAK void halide_cond_signal(struct halide_cond *cond) {
    Halide::Runtime::Internal::Synchronization::fast_cond *fast_cond =
        (Halide::Runtime::Internal::Synchronization::fast_cond *)cond;
    fast_cond->signal();
}

WEAK void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    Halide::Runtime::Internal::Synchronization::fast_cond *fast_cond =
        (Halide::Runtime::Internal::Synchronization::fast_cond *)cond;
    Halide::Runtime::Internal::Synchronization::fast_mutex *fast_mutex =
        (Halide::Runtime::Internal::Synchronization::fast_mutex *)mutex;
    fast_cond->wait(fast_mutex);
}

// Actual definition of the mutex array.
struct halide_mutex_array {
    struct halide_mutex *array;
};

WEAK halide_mutex_array *halide_mutex_array_create(int sz) {
    // TODO: If sz is huge, we should probably hash it down to something smaller
    // in the accessors below. Check for deadlocks before doing so.
    halide_mutex_array *array = (halide_mutex_array *)halide_malloc(
        nullptr, sizeof(halide_mutex_array));
    if (array == nullptr) {
        // Will result in a failed assertion and a call to halide_error.
        return nullptr;
    }
    array->array = (halide_mutex *)halide_malloc(
        nullptr, sz * sizeof(halide_mutex));
    if (array->array == nullptr) {
        halide_free(nullptr, array);
        // Will result in a failed assertion and a call to halide_error.
        return nullptr;
    }
    memset(array->array, 0, sz * sizeof(halide_mutex));
    return array;
}

WEAK void halide_mutex_array_destroy(void *user_context, void *array) {
    struct halide_mutex_array *arr_ptr = (struct halide_mutex_array *)array;
    halide_free(user_context, arr_ptr->array);
    halide_free(user_context, arr_ptr);
}

WEAK int halide_mutex_array_lock(struct halide_mutex_array *array, int entry) {
    halide_mutex_lock(&array->array[entry]);
    return 0;
}

WEAK int halide_mutex_array_unlock(struct halide_mutex_array *array, int entry) {
    halide_mutex_unlock(&array->array[entry]);
    return 0;
}
}
