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
 * TODO: Change halide_mutex and halide_cond definition to one uinptr_t that
 *       can always be zero initialized.
 * TODO: Implement pthread_once equivalent.
 * TODO: Add read/write lock and move SharedExclusiveSpinLock from tracing.cpp
 *        to this mechanism.
 * TODO: Add timeouts and optional fairness if needed.
 * TODO: Relying on condition variables has issues for old versions of Windows
 *       and likely has portability issues to some very bare bones embedded OSes.
 *       Doing an implementation using only semaphores or event counters should
 *       be doable.
 */

namespace Halide { namespace Runtime { namespace Internal {

namespace Synchronization {

class spin_control {
    int spin_count{40}; // Everyone says this should be 40. Have not measured it.

public:
    bool should_spin() {
        if (spin_count > 0) {
            spin_count--;
        }
        return spin_count > 0;
    }

    void reset() {
        spin_count = 40;
    }
};

#if __cpluspluc >= 201103L
// Low order two bits are used for locking state,
static constexpr uint8_t lock_bit = 0x01;
static constexpr uint8_t queue_lock_bit = 0x02;
static constexpr uint8_t parked_bit = 0x02;
#else
#define lock_bit 0x01
#define queue_lock_bit 0x02
#define parked_bit 0x02
#endif

struct word_lock_queue_data {
    thread_parker parker; // TODO: member or pointer?

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

    word_lock_queue_data *next;
    word_lock_queue_data *prev;

    word_lock_queue_data *tail;

    word_lock_queue_data() : next(NULL), prev(NULL), tail(NULL) {
    }
};

class word_lock {
    uintptr_t state{0};

    void lock_full();
    void unlock_full();

public:
    __attribute__((always_inline)) void lock() {
        uintptr_t expected = 0;
        uintptr_t desired = lock_bit;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            lock_full();
        }
    }

    __attribute__((always_inline)) void unlock() {
        uintptr_t val = __atomic_fetch_and(&state, ~(uintptr_t)lock_bit, __ATOMIC_RELEASE);
        // If another thread is currently queueing, that thread will ensure
        // it acquires the lock or wakes a waiting thread.
        bool no_thread_queuing = (val & queue_lock_bit) == 0;
        // Only need to do a wakeup if there are threads waiting.
        bool some_queued = (val & ~(uintptr_t)(queue_lock_bit | lock_bit)) != 0;
        if (no_thread_queuing && some_queued) {
            unlock_full();
        }
    }
};

void word_lock::lock_full() {
    spin_control spinner;
    uintptr_t expected;
    __atomic_load(&state, &expected, __ATOMIC_RELAXED);

    while (true) {
        if (!(expected & lock_bit)) {
            uintptr_t desired = expected | lock_bit;

            if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return;
            }
            continue;
        }

        if (((expected & ~(uintptr_t)(queue_lock_bit | lock_bit)) != 0) && spinner.should_spin()) {
            halide_thread_yield();
            __atomic_load(&state, &expected, __ATOMIC_RELAXED);
            continue;
        }

        word_lock_queue_data node;

        node.parker.prepare_park();
        // TODO set up prelinkage parking state

        word_lock_queue_data *head = (word_lock_queue_data *)(expected & ~(uintptr_t)(queue_lock_bit | lock_bit));
        if (head == NULL) {
            node.tail = &node;
            // constructor set node.prev = NULL;
        } else {
            // Mark the tail as NULL. The unlock routine will walk the list and wakeup
            // the thread at the end.
            // constructor set node.tail = NULL;
            // constructor set node.prev = NULL;
            node.next = head;
        }

        uintptr_t desired = ((uintptr_t)&node) | (expected & (queue_lock_bit | lock_bit));
        if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            node.parker.park();
            spinner.reset();
            __atomic_load(&state, &expected, __ATOMIC_RELAXED);
        }
    }
}

void word_lock::unlock_full() {
    uintptr_t expected;
    __atomic_load(&state, &expected, __ATOMIC_RELAXED);

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
        if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            break;
        }
    }

    while (true) {
        word_lock_queue_data *head = (word_lock_queue_data *)(expected & ~(uintptr_t)(queue_lock_bit | lock_bit));
        word_lock_queue_data *current = head;
        word_lock_queue_data *tail = current->tail;
        int times_through = 0;
        while (tail == NULL) {
            word_lock_queue_data *next = current->next;
            if (next == NULL) {
                abort();
            }
            next->prev = current;
            current = next;
            tail = current->tail;
            times_through++;
        }
        head->tail = tail;

        // If the lock is now locked, unlock the queue and have the thread
        // that currently holds the lock do the wakeup
        if (expected & lock_bit) {
            uintptr_t desired = expected & ~(uintptr_t)queue_lock_bit;
            if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                return;
            }
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            continue;
        }

        word_lock_queue_data *new_tail = tail->prev;
        if (new_tail == NULL) {
            bool continue_outer = false;
            while (!continue_outer) {
                uintptr_t desired = expected & lock_bit;
                if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                    break;
                }
                if ((expected & ~(uintptr_t)(queue_lock_bit | lock_bit)) == 0) {
                    continue;
                } else {
                    __atomic_thread_fence(__ATOMIC_ACQUIRE);
                    continue_outer = true;
                }
            }

            if (continue_outer) {
                continue;
            }
        } else {
            head->tail = new_tail;
            __atomic_and_fetch(&state, ~(uintptr_t)queue_lock_bit, __ATOMIC_RELEASE);
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
    thread_parker parker; // TODO: member or pointer?

    uintptr_t sleep_address;

    queue_data *next;

    uintptr_t unpark_info;

    queue_data() : sleep_address(0), next(NULL), unpark_info(0) {
    }
};

// Must be a power of two.
#define LOAD_FACTOR 4

struct hash_bucket {
    word_lock mutex;
    
    queue_data *head{0}; // Is this queue_data or thread_data?
    queue_data *tail{0}; // Is this queue_data or thread_data?
};

WEAK struct hash_table {
    hash_bucket buckets[MAX_THREADS * LOAD_FACTOR];
    uint32_t num_bits{10}; // prolly a constant here.
} table;

inline void check_hash(uintptr_t hash) {
    halide_assert(NULL, hash < sizeof(table.buckets)/sizeof(table.buckets[0]));
}

#if 0
WEAK void dump_hash() {
    int i = 0;
    for (auto &bucket : table.buckets) {
        queue_data *head = bucket.head;
        while (head != NULL) {
            print(NULL) << "Bucket index " << i << " addr " << (void *)head->sleep_address << "\n";
            head = head->next;
        }
        i++;
    }
}
#endif

static inline uintptr_t addr_hash(uintptr_t addr, uint32_t bits) {
    // Fibonacci hashing. The golden ratio is 1.9E3779B97F4A7C15F39...
    // in hexadecimal.
    if (sizeof(uintptr_t) >= 8) {
        return (addr * (uintptr_t)0x9E3779B97F4A7C15) >> (64 - bits);
    } else {
        return (addr * (uintptr_t)0x9E3779B9) >> (32 - bits);
    }
}

hash_bucket &lock_bucket(uintptr_t addr) {
    uintptr_t hash = addr_hash(addr, table.num_bits);

    check_hash(hash);

    // TODO: if resizing is implemented, loop, etc.
    hash_bucket &bucket = table.buckets[hash];

    bucket.mutex.lock();

    return bucket;
}

struct bucket_pair {
    hash_bucket &from;
    hash_bucket &to;
};

bucket_pair lock_bucket_pair(uintptr_t addr_from, uintptr_t addr_to) {
    // TODO: if resizing is implemented, loop, etc.
    uintptr_t hash_from = addr_hash(addr_from, table.num_bits);
    uintptr_t hash_to = addr_hash(addr_to, table.num_bits);

    check_hash(hash_from);
    check_hash(hash_to);

    // Lock the bucket with the smaller hash first in order
    // to prevent deadlock.
    if (hash_from == hash_to) {
        hash_bucket &first = table.buckets[hash_from];
        first.mutex.lock();
        return { first, first };
    } else if (hash_from < hash_to) {
        hash_bucket &first = table.buckets[hash_from];
        hash_bucket &second = table.buckets[hash_to];
        first.mutex.lock();
        second.mutex.lock();
        return { first, second };
    } else {
        hash_bucket &first = table.buckets[hash_to];
        hash_bucket &second = table.buckets[hash_from];
        first.mutex.lock();
        second.mutex.lock();
        return { second, first };
    }
}

void unlock_bucket_pair(bucket_pair &buckets) {
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
    bool unpark_one{false};
    uintptr_t invalid_unpark_info{0};
};

bool parking_control_validate(void *control, validate_action &action) { return true; };
void parking_control_before_sleep(void *control) { };
uintptr_t parking_control_unpark(void *control, int unparked, bool more_waiters) { return 0; };
void parking_control_requeue_callback(void *control, const validate_action &action, bool one_to_wake, bool some_requeued) { };

struct parking_control {
    bool (*validate)(void *control, validate_action &action);
    void (*before_sleep)(void *control);
    uintptr_t (*unpark)(void *control, int unparked, bool more_waiters);
    void (*requeue_callback)(void *control, const validate_action &action, bool one_to_wake, bool some_requeued);

    parking_control() : validate(parking_control_validate), before_sleep(parking_control_before_sleep),
        unpark(parking_control_unpark), requeue_callback(parking_control_requeue_callback) {
    }
};

// TODO: Do we need a park_result thing here?
uintptr_t park(uintptr_t addr, parking_control &control) {
    queue_data queue_data;

    hash_bucket &bucket = lock_bucket(addr);

    validate_action action;
    if (!control.validate(&control, action)) {
        bucket.mutex.unlock();
        return action.invalid_unpark_info;
    }

    queue_data.next = NULL;
    queue_data.sleep_address = addr;
    queue_data.parker.prepare_park();
    if (bucket.head != NULL) {
        bucket.tail->next = &queue_data;
    } else {
        bucket.head = &queue_data;
    }
    bucket.tail = &queue_data;
    bucket.mutex.unlock();

    control.before_sleep(&control);

    queue_data.parker.park();

    return queue_data.unpark_info;

    // TODO: handling timeout.
}

uintptr_t unpark_one(uintptr_t addr, parking_control &control) {
    hash_bucket &bucket = lock_bucket(addr);

    queue_data **data_location = &bucket.head;
    queue_data *prev = NULL;
    queue_data *data = *data_location;
    while (data != NULL) {
        uintptr_t cur_addr;
        __atomic_load(&data->sleep_address, &cur_addr, __ATOMIC_RELAXED);
        if (cur_addr == addr) {
            queue_data *next = data->next;
            *data_location = next;

            bool more_waiters = false;

            if (bucket.tail == data) {
                bucket.tail = prev;
            } else {
                queue_data *data2 = next;
                while (data2 != NULL && !more_waiters) {
                    uintptr_t cur_addr2;
                    __atomic_load(&data2->sleep_address, &cur_addr2, __ATOMIC_RELAXED);
                    more_waiters = (cur_addr2 == addr);
                    data2 = data2->next;
                }
            }

            data->unpark_info = control.unpark(&control, 1, more_waiters);

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

    control.unpark(&control, 0, false);

    bucket.mutex.unlock();

    // TODO: decide if this is the right return value.
    return 0;
}

uintptr_t unpark_all(uintptr_t addr, uintptr_t unpark_info) {
    hash_bucket &bucket = lock_bucket(addr);

    queue_data **data_location = &bucket.head;
    queue_data *prev = NULL;
    queue_data *data = *data_location;
    size_t waiters = 0;
    queue_data *temp_list_storage[16];
    queue_data **temp_list = &temp_list_storage[0];
    size_t max_waiters = sizeof(temp_list_storage)/sizeof(temp_list_storage[0]);

    while (data != NULL) {
        uintptr_t cur_addr;
        __atomic_load(&data->sleep_address, &cur_addr, __ATOMIC_RELAXED);

        queue_data *next = data->next;
        if (cur_addr == addr) {
            *data_location = next;

            if (bucket.tail == data) {
                bucket.tail = prev;
            }

            if (waiters == max_waiters) {
              queue_data **temp = temp_list;
              temp_list = (queue_data **)malloc(sizeof(queue_data *) * max_waiters * 2);
              for (size_t i = 0; i < max_waiters; i++) {
                  temp_list[i] = temp[i];
              }
              max_waiters *= 2;
              if (temp != &temp_list_storage[0]) {
                  free(temp);
              }
            }

            data->unpark_info = unpark_info;

            temp_list[waiters++] = data;
            data->parker.unpark_start();

            data = next;
        } else {
          *data_location = data->next;
          prev = data;
          data = next;
        }
    }

    bucket.mutex.unlock();

    for (size_t i = 0; i < waiters; i++) {
        temp_list[i]->parker.unpark();
    }

    // TODO: decide if this really needs to be two loops.
    for (size_t i = 0; i < waiters; i++) {
        temp_list[i]->parker.unpark_finish();
    }

    if (temp_list != &temp_list_storage[0]) {
        free(temp_list);
    }

    return waiters;
}

int unpark_requeue(uintptr_t addr_from, uintptr_t addr_to, parking_control &control, uintptr_t unpark_info) {
    bucket_pair buckets = lock_bucket_pair(addr_from, addr_to);

    validate_action action;
    if (!control.validate(&control, action)) {
        unlock_bucket_pair(buckets);
        return 0;
    }

    queue_data **data_location = &buckets.from.head;
    queue_data *prev = NULL;
    queue_data *data = *data_location;
    queue_data *requeue = NULL;
    queue_data *requeue_tail = NULL;
    queue_data *wakeup = NULL;

    while (data != NULL) {
        uintptr_t cur_addr;
        __atomic_load(&data->sleep_address, &cur_addr, __ATOMIC_RELAXED);

        queue_data *next = data->next;
        if (cur_addr == addr_from) {
            *data_location = next;

            if (buckets.from.tail == data) {
                buckets.from.tail = prev;
            }

            if (action.unpark_one && wakeup == NULL) {
                wakeup = data;
            } else {
                if (requeue == NULL) {
                    requeue = data;
                } else {
                    requeue_tail->next = data;
                }

                requeue_tail = data;
                __atomic_store(&data->sleep_address, &addr_to, __ATOMIC_RELAXED);
            }
            data = next;
            // TODO: prev ptr?
        } else {
            data_location = &data->next;
            prev = data;
            data = next;
        }
    }

    if (requeue != NULL) {
        requeue_tail->next = NULL;
        if (buckets.to.head == NULL) {
            buckets.to.head = requeue;
        } else {
            buckets.to.tail->next = requeue;
        }
        buckets.to.tail = requeue_tail;
    }

    control.requeue_callback(&control, action, wakeup != NULL, requeue != NULL);

    if (wakeup != NULL) {
        wakeup->unpark_info = unpark_info;
        wakeup->parker.unpark_start();
        unlock_bucket_pair(buckets);
        wakeup->parker.unpark();
        wakeup->parker.unpark_finish();
    } else {
        unlock_bucket_pair(buckets);
    }

    return wakeup != NULL && action.unpark_one;
}

struct mutex_parking_control : parking_control {
    uintptr_t *lock_state;

    mutex_parking_control(uintptr_t *lock_state);
};

// Only used in parking -- lock_full.
bool mutex_parking_control_validate(void *control, validate_action &action) {
    mutex_parking_control *mutex_control = (mutex_parking_control *)control;

    uintptr_t result;
    __atomic_load(mutex_control->lock_state, &result, __ATOMIC_RELAXED);
    return result == (lock_bit | parked_bit);
}

// Only used in unparking -- unlock_full.
uintptr_t mutex_parking_control_unpark(void *control, int unparked, bool more_waiters) {
    mutex_parking_control *mutex_control = (mutex_parking_control *)control;

    // TODO: consider handling fairness.
    uintptr_t return_state = more_waiters ? parked_bit : 0;
    __atomic_store(mutex_control->lock_state, &return_state, __ATOMIC_RELEASE);

    return 0;
}

mutex_parking_control::mutex_parking_control(uintptr_t *lock_state)
    : lock_state(lock_state) {
    validate = mutex_parking_control_validate;
    unpark = mutex_parking_control_unpark;
}

class fast_mutex {
    uintptr_t state;

    __attribute__((always_inline)) void lock_full() {
        // Everyone says this should be 40. Have not measured it.
        spin_control spinner;
        uintptr_t expected;
        __atomic_load(&state, &expected, __ATOMIC_RELAXED);

        while (true) {
            if (!(expected & lock_bit)) {
                uintptr_t desired = expected | lock_bit;
                if (__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                    return;
                }
                continue;
            }

            // If no one is parked, spin with spin count.
            if ((expected & parked_bit) == 0 && spinner.should_spin()) {
                halide_thread_yield();
                __atomic_load(&state, &expected, __ATOMIC_RELAXED);
                continue;
            }
            
            // Mark mutex as having parked threads if not already done.
            if ((expected & parked_bit) == 0) {
                uintptr_t desired = expected | parked_bit;
                if (!__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                    continue;
                }
            }

            // TODO: consider handling fairness, timeout
            mutex_parking_control control(&state);
            uintptr_t result = park((uintptr_t)this, control);
            if (result == (uintptr_t)this) {
                return;
            }

            spinner.reset();
            __atomic_load(&state, &expected, __ATOMIC_RELAXED);
        }
    }

    __attribute__((always_inline)) void unlock_full() {
        uintptr_t expected = lock_bit;
        uintptr_t desired = 0;
        // Try for a fast release of the lock. Redundant with code in lock, but done
        // to make unlock_full a standalone unlock that can be called directly.
        if (__atomic_compare_exchange(&state, &expected, &desired, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            return;
        }

        mutex_parking_control control(&state);
        unpark_one((uintptr_t)this, control);
    }

public:

    __attribute__((always_inline)) void lock() {
        uintptr_t expected = 0;
        uintptr_t desired = lock_bit;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            lock_full();
        }
    }

    __attribute__((always_inline)) void unlock() {
        uintptr_t expected = lock_bit;
        uintptr_t desired = 0;
        // Try for a fast grab of the lock bit. If this does not work, call the full adaptive looping code.
        if (!__atomic_compare_exchange(&state, &expected, &desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            unlock_full();
        }
    }

    bool make_parked_if_locked() {
        uintptr_t val;
        __atomic_load(&state, &val, __ATOMIC_RELAXED);
        while (true) {
            if (!(val & lock_bit)) {
                return false;
            }

            uintptr_t desired = val | parked_bit;
            if (__atomic_compare_exchange(&state, &val, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                return true;
            }
        }
    }

    void make_parked() {
        __atomic_or_fetch(&state, parked_bit, __ATOMIC_RELAXED);
    }
};

struct signal_parking_control : parking_control {
    uintptr_t *cond_state;
    fast_mutex *mutex;

    signal_parking_control(uintptr_t *cond_state, fast_mutex *mutex);
};

uintptr_t signal_parking_control_unpark(void *control, int unparked, bool more_waiters) {
    signal_parking_control *signal_control = (signal_parking_control *)control;

    if (!more_waiters) {
        uintptr_t val = 0;
        __atomic_store(signal_control->cond_state, &val, __ATOMIC_RELAXED);
    }

#if 0 // TODO: figure out why this was here.
    return (uintptr_t)signal_control->mutex;
#else
    return 0;
#endif
}

signal_parking_control::signal_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
    : cond_state(cond_state), mutex(mutex) {
    unpark = signal_parking_control_unpark;
}

struct broadcast_parking_control : parking_control {
    uintptr_t *cond_state;
    fast_mutex *mutex;

    broadcast_parking_control(uintptr_t *cond_state, fast_mutex *mutex);
};

bool broadcast_parking_control_validate(void *control, validate_action &action) {
    broadcast_parking_control *broadcast_control = (broadcast_parking_control *)control;

    uintptr_t val;
    __atomic_load(broadcast_control->cond_state, &val, __ATOMIC_RELAXED);
    // By the time this broadcast locked everything and was processed, the cond
    // has progressed to a new mutex, do nothing since any waiting threads have
    // to be waiting on what is effectively a different condition.
    if (val != (uintptr_t)broadcast_control->mutex) {
        return false;
    }
    // Clear the cond's connection to the mutex as all waiting threads are going to reque onto the mutex.
    val = 0;
    __atomic_store(broadcast_control->cond_state, &val, __ATOMIC_RELAXED);

    action.unpark_one = !broadcast_control->mutex->make_parked_if_locked();

    return true;
}

void broadcast_parking_control_requeue_callback(void *control, const validate_action &action, bool one_to_wake, bool some_requeued) {
    broadcast_parking_control *broadcast_control = (broadcast_parking_control *)control;

    if (action.unpark_one && some_requeued) {
        broadcast_control->mutex->make_parked();
    }
}

broadcast_parking_control::broadcast_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
    : cond_state(cond_state), mutex(mutex) {
    validate = broadcast_parking_control_validate;
    requeue_callback = broadcast_parking_control_requeue_callback;
}

struct wait_parking_control : parking_control {
    uintptr_t *cond_state;
    fast_mutex *mutex;

    wait_parking_control(uintptr_t *cond_state, fast_mutex *mutex);
};

bool wait_parking_control_validate(void *control, validate_action &action) {
    wait_parking_control *wait_control = (wait_parking_control *)control;

    uintptr_t val;
    __atomic_load(wait_control->cond_state, &val, __ATOMIC_RELAXED);

    if (val == 0) {
        val = (uintptr_t)wait_control->mutex;
        __atomic_store(wait_control->cond_state, &val, __ATOMIC_RELAXED);
    } else if (val != (uintptr_t)wait_control->mutex) {
        // TODO: signal error.
        action.invalid_unpark_info = (uintptr_t)wait_control->mutex;
        return false;
    }

    return true;
}

void wait_parking_control_before_sleep(void *control) {
    wait_parking_control *wait_control = (wait_parking_control *)control;

    wait_control->mutex->unlock();
}

uintptr_t wait_parking_control_unpark(void *control, int unparked, bool more_waiters) {
    wait_parking_control *wait_control = (wait_parking_control *)control;

    if (!more_waiters) {
        uintptr_t val = 0;
        __atomic_store(wait_control->cond_state, &val, __ATOMIC_RELAXED);
    }
    return 0;
}

wait_parking_control::wait_parking_control(uintptr_t *cond_state, fast_mutex *mutex)
    : cond_state(cond_state), mutex(mutex) {
    validate = wait_parking_control_validate;
    before_sleep = wait_parking_control_before_sleep;
    unpark = wait_parking_control_unpark;
}

class fast_cond {
    uintptr_t state{0};

 public:

    __attribute__((always_inline)) void signal() {
        uintptr_t val;
        __atomic_load(&state, &val, __ATOMIC_RELAXED);
        if (val == 0) {
            return;
        }
        signal_parking_control control(&state, (fast_mutex *)val);
        unpark_one((uintptr_t)this, control);
    }

    __attribute__((always_inline)) void broadcast() {
        uintptr_t val;
        __atomic_load(&state, &val, __ATOMIC_RELAXED);
        if (val == 0) {
            return;
        }
        broadcast_parking_control control(&state, (fast_mutex *)val);
        unpark_requeue((uintptr_t)this, val, control, 0);
    }

    __attribute__((always_inline)) void wait(fast_mutex *mutex) {
        wait_parking_control control(&state, mutex);
        uintptr_t result = park((uintptr_t)this, control);
        if (result != (uintptr_t)mutex) {
            mutex->lock();
        } else { // TODO: this is debug only.
            uintptr_t val;
            __atomic_load((uintptr_t *)mutex, &val, __ATOMIC_RELAXED);
            halide_assert(NULL, val & 0x1);
        }
    }
};

}

}}}

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

}
