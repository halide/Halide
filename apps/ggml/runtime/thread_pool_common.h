#ifndef EXTENDED_DEBUG
#define EXTENDED_DEBUG 0
#endif

#if EXTENDED_DEBUG
#define log_message(stuff)                                                     \
    do {                                                                       \
        print(nullptr) << halide_current_thread_id() << ": " << stuff << "\n"; \
    } while (0)
#else
#define log_message(stuff) \
    do { /*nothing*/       \
    } while (0)
#endif

namespace Halide {
namespace Runtime {
namespace Internal {

// The process-wide keep-awake reference count; see
// halide_thread_pool_keep_awake. It lives outside work_queue so that
// halide_shutdown_thread_pool, which resets work_queue, doesn't drop
// references that are still held.
WEAK int keep_awake_count = 0;

// While the keep-awake count is held, how many times an idle thread polls for
// work (each poll calls halide_thread_yield) before going to sleep anyway. This
// is a backstop against leaked references and against taking cores from other
// thread pools for long periods. Waking a thread costs tens of microseconds, so
// once a thread has been idle for a few milliseconds, spinning any longer saves
// at most about 1% of the idle time. With a halide_thread_yield that costs
// about a microsecond (a sched_yield-style syscall), this is a few
// milliseconds.
constexpr int keep_awake_max_spins = 1 << 12;

ALWAYS_INLINE bool keep_awake_held() {
    int count;
    Synchronization::atomic_load_relaxed(&keep_awake_count, &count);
    return count > 0;
}

// Tell the CPU that this thread is busy-waiting.
ALWAYS_INLINE void spin_pause() {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

// A lock-free fast path for parallel for loops (halide_do_par_for) started
// while the pool is idle: no work is queued, and enough idle workers are
// polling for work in halide_cond_with_spinning::wait. Rather than take
// work_queue.mutex to queue the loop, and again to claim and to finish each
// iteration and to wait for the others, the owner publishes the loop here, and
// the polling workers join it and claim iterations without locks. One loop at
// a time can use the fast path; any other loop (nested inside one of its
// iterations, started on another thread meanwhile, or started while the pool
// is busy) takes the usual path, as do halide_do_parallel_tasks and everything
// with semaphores.
//
// The loop is split into a contiguous chunk of iterations per participant (the
// owner, and every worker that joins), so that each touches few cache lines
// that others write, and each worker gets the same iterations of loops of the
// same size, and so the same data, from one loop to the next. A participant
// claims iterations from its own chunk, then from the others' chunks, so that
// a late participant never holds up the loop. It counts the iterations it
// claimed and adds them to a shared total once, as it leaves; the owner returns
// when the total reaches the loop's size, rather than waiting for every helper
// to leave.
struct fast_par_for_t {
    // Polled by idle workers. state is odd while a loop is open for helpers
    // to join and even otherwise; the owner increments it to open the loop
    // and again to close it. busy is nonzero while a thread owns the fast
    // path. pollers counts the idle workers that may join a loop. workers
    // counts the workers started since the pool was last shut down, which
    // numbers them.
    alignas(128) uintptr_t state;
    int busy;
    int pollers;
    int workers;

    // Written only by the owner, before opening the loop, while no helper is
    // in it.
    alignas(128) halide_task_t fn;
    void *user_context;
    uint8_t *closure;
    int min, extent;
    int participants;

    // The number of iterations that helpers have claimed and finished (or
    // skipped, after one failed), and the first failure.
    alignas(128) int finished;
    int exit_status;

    // A cache line per participant: 0 is the owner's, and h + 1 is that of
    // the worker numbered h (see worker_thread_already_locked). in_use is set
    // while that worker is in a loop or checking whether it may join one.
    // Participant p's chunk is the iterations from next up to end (relative
    // to min); next is the next one to claim.
    struct alignas(128) slot_t {
        int in_use;
        int next, end;
    } slots[MAX_THREADS + 1];
};

WEAK fast_par_for_t fast_par_for = {};

// Run iteration i of the open fast-path loop, unless one has failed.
ALWAYS_INLINE void fast_par_for_run(int i) {
    int status;
    Synchronization::atomic_load_relaxed(&fast_par_for.exit_status, &status);
    if (status != halide_error_code_success) {
        return;
    }
    int result = halide_do_task(fast_par_for.user_context, fast_par_for.fn,
                                fast_par_for.min + i, fast_par_for.closure);
    if (result != halide_error_code_success) {
        int expected = halide_error_code_success;
        Synchronization::atomic_cas_strong_sequentially_consistent(&fast_par_for.exit_status, &expected, &result);
    }
}

// Claim and run iterations of the open fast-path loop as participant p until
// none are left to claim: first from p's own chunk, then from the others', in
// turn. Returns how many this thread claimed.
WEAK int fast_par_for_work(int p) {
    const int participants = fast_par_for.participants;
    int claimed = 0;
    for (int k = 0; k < participants; k++) {
        fast_par_for_t::slot_t &chunk = fast_par_for.slots[(p + k) % participants];
        while (true) {
            int i;
            Synchronization::atomic_load_relaxed(&chunk.next, &i);
            if (i < chunk.end) {
                i = Synchronization::atomic_fetch_add_acquire_release(&chunk.next, 1);
            }
            if (i >= chunk.end) {
                break;
            }
            fast_par_for_run(i);
            claimed++;
        }
    }
    return claimed;
}

// Called by the idle worker numbered h while it polls for work. If a fast-path
// loop other than *seen is open, join it and help. Returns whether this thread
// claimed any of its iterations.
WEAK bool fast_par_for_help(uintptr_t *seen, int h) {
    uintptr_t state;
    Synchronization::atomic_load_relaxed(&fast_par_for.state, &state);
    if (state == *seen) {
        return false;
    }
    *seen = state;
    if (!(state & 1)) {
        return false;
    }
    // Mark this thread as in the loop before checking that it's still open.
    // The loop closes before the next one is set up, and the owner of the
    // next one waits for every worker's in_use to clear before doing so, so
    // either it waits for this thread, or this thread sees that the loop is
    // closed and doesn't touch it.
    fast_par_for_t::slot_t &slot = fast_par_for.slots[h + 1];
    int one = 1;
    Synchronization::atomic_store_sequentially_consistent(&slot.in_use, &one);
    Synchronization::atomic_thread_fence_sequentially_consistent();
    uintptr_t current;
    Synchronization::atomic_load_acquire(&fast_par_for.state, &current);
    int claimed = 0;
    if (current == state) {
        claimed = fast_par_for_work(h + 1);
        if (claimed) {
            Synchronization::atomic_fetch_add_acquire_release(&fast_par_for.finished, claimed);
        }
    }
    int zero = 0;
    Synchronization::atomic_store_release(&slot.in_use, &zero);
    return claimed > 0;
}

// A condition variable, augmented with a bit of spinning on an atomic counter
// before going to sleep for real. This helps reduce overhead at the end of a
// parallel for loop when idle worker threads are waiting for other threads to
// finish so that the next parallel for loop can begin.
struct halide_cond_with_spinning {
    halide_cond cond;
    uintptr_t counter;

    // If kept_awake is non-null, then while the keep-awake count is held, this
    // waiter keeps spinning (up to keep_awake_max_spins) instead of going to
    // sleep. *kept_awake counts the waiters doing so, is protected by the
    // mutex, and is capped at max_kept_awake. If helper is the number of a
    // worker, rather than -1, this waiter also joins fast-path loops (see
    // fast_par_for_t) while it spins.
    void wait(halide_mutex *mutex, int *kept_awake = nullptr, int max_kept_awake = 0,
              int helper = -1) {
        // First spin for a bit, checking the counter for another thread to bump
        // it.
        uintptr_t initial;
        Synchronization::atomic_load_relaxed(&counter, &initial);
        const bool stay_awake =
            kept_awake && *kept_awake < max_kept_awake && keep_awake_held();
        if (stay_awake) {
            (*kept_awake)++;
        }
        halide_mutex_unlock(mutex);
        bool woken = false;
        uintptr_t fast_seen = 0;
        const bool may_help = helper >= 0;
        if (may_help) {
            Synchronization::atomic_fetch_add_acquire_release(&fast_par_for.pollers, 1);
        }
        for (int spin = 0;
             spin < 40 ||
             (stay_awake && spin < keep_awake_max_spins && keep_awake_held());
             spin++) {
            if (may_help && fast_par_for_help(&fast_seen, helper)) {
                // Found work, so start counting polls without work again.
                spin = 0;
            }
            halide_thread_yield();
            uintptr_t current;
            Synchronization::atomic_load_relaxed(&counter, &current);
            if (current != initial) {
                woken = true;
                break;
            }
        }
        if (may_help) {
            Synchronization::atomic_fetch_add_acquire_release(&fast_par_for.pollers, -1);
        }

        // Relock the mutex, and if we're done spinning without being woken,
        // prepare to sleep for real.
        halide_mutex_lock(mutex);
        if (stay_awake) {
            (*kept_awake)--;
        }
        if (woken) {
            return;
        }

        // Check one final time with the lock held. This guarantees we won't
        // miss an increment of the counter because it is only ever incremented
        // with the lock held.
        uintptr_t current;
        Synchronization::atomic_load_relaxed(&counter, &current);
        if (current != initial) {
            return;
        }

        halide_cond_wait(&cond, mutex);
    }

    void broadcast() {
        // Release any spinning waiters
        Synchronization::atomic_fetch_add_acquire_release(&counter, (uintptr_t)1);

        // Release any sleeping waiters
        halide_cond_broadcast(&cond);
    }

    // Note that this cond var variant doesn't have signal(), because it always
    // wakes all spinning waiters.
};

struct work {
    halide_parallel_task_t task;

    // If we come in to the task system via do_par_for we just have a
    // halide_task_t, not a halide_loop_task_t.
    halide_task_t task_fn;

    work *next_job;
    work *siblings;
    int sibling_count;
    work *parent_job;
    int threads_reserved;

    void *user_context;
    int active_workers;
    int exit_status;
    int next_semaphore;
    // which condition variable is the owner sleeping on. nullptr if it isn't sleeping.
    bool owner_is_sleeping;

    ALWAYS_INLINE bool make_runnable() {
        for (; next_semaphore < task.num_semaphores; next_semaphore++) {
            if (!halide_default_semaphore_try_acquire(task.semaphores[next_semaphore].semaphore,
                                                      task.semaphores[next_semaphore].count)) {
                // Note that we don't release the semaphores already
                // acquired. We never have two consumers contending
                // over the same semaphore, so it's not helpful to do
                // so.
                return false;
            }
        }
        // Future iterations of this task need to acquire the semaphores from scratch.
        next_semaphore = 0;
        return true;
    }

    ALWAYS_INLINE bool running() const {
        return task.extent || active_workers;
    }
};

// A thread that stalls on a job it owns may start tasks belonging to other
// parallel regions rather than idle, but doing so nests a new owned job inside
// the one it is already waiting on, and that stack frame can't unwind until the
// new job completes. Bounding how many jobs a thread may have stalled at once
// lets it start one more instance of an outer loop it is already inside, but
// not an unbounded number of them. Descending without stalling is already
// bounded, because it can only go one level deeper into the loop nest.
constexpr int max_stalled_jobs = 2;

// Which entry of work_queue.stalled_jobs belongs to the calling thread. Thread
// ids are dense on some platforms and multiples of four on others, so mix them
// before taking the low bits. Two threads may share an entry, which can only
// cause one of them to decline to start a job it could have run.
ALWAYS_INLINE int stalled_jobs_slot() {
    static_assert(MAX_THREADS <= 256 && (MAX_THREADS & (MAX_THREADS - 1)) == 0,
                  "MAX_THREADS must be a power of two no greater than 256.");
    const uint32_t id = (uint32_t)halide_current_thread_id();
    return (int)(((id * (uint32_t)2654435761) >> 24) & (MAX_THREADS - 1));
}

ALWAYS_INLINE int clamp_num_threads(int threads) {
    if (threads > MAX_THREADS) {
        return MAX_THREADS;
    } else if (threads < 1) {
        return 1;
    } else {
        return threads;
    }
}

WEAK int default_desired_num_threads() {
    char *threads_str = getenv("HL_NUM_THREADS");
    if (!threads_str) {
        // Legacy name for HL_NUM_THREADS
        threads_str = getenv("HL_NUMTHREADS");
    }
    return threads_str ?
               atoi(threads_str) :
               halide_host_cpu_count();
}

// The work queue and thread pool is weak, so one big work queue is shared by all halide functions
struct work_queue_t {
    // all fields are protected by this mutex.
    halide_mutex mutex;

    // The desired number threads doing work (HL_NUM_THREADS).
    int desired_threads_working;

    // All fields after this must be zero in the initial state. See assert_zeroed
    // Field serves both to mark the offset in struct and as layout padding.
    int zero_marker;

    // Singly linked list for job stack
    work *jobs;

    // The number threads created
    int threads_created;

    // Workers sleep on one of two condition variables, to make it
    // easier to wake up the right number if a small number of tasks
    // are enqueued. There are A-team workers and B-team workers. The
    // following variables track the current size and the desired size
    // of the A team.
    int a_team_size, target_a_team_size;

    // The condition variables that workers and owners sleep on. We
    // may want to wake them up independently. Any code that may
    // invalidate any of the reasons a worker or owner may have slept
    // must signal or broadcast the appropriate condition variable.
    halide_cond_with_spinning wake_a_team, wake_b_team, wake_owners;

    // A separate channel for workers that found a job they could run but
    // for an unavailable semaphore. This is distinct from the A/B teams,
    // which model idle capacity (no runnable work): a worker here is
    // blocked on an external event, not idle. Keeping it separate lets a
    // semaphore release wake exactly the workers waiting on a semaphore,
    // without a thundering herd of genuinely-idle workers waking only to
    // rescan and go back to sleep.
    halide_cond_with_spinning wake_from_semaphore;

    // The number of sleeping workers and owners. An over-estimate - a
    // waking-up thread may not have decremented this yet.
    int workers_sleeping, owners_sleeping;

    // The number of workers parked on wake_from_semaphore. A subset of
    // workers_sleeping (those threads are also counted there, so the A/B
    // team bookkeeping is undisturbed).
    int workers_parked_on_semaphore;

    // The number of idle A-team workers and of owners currently spinning
    // because the keep-awake count is held. Subsets of workers_sleeping and
    // owners_sleeping respectively.
    int workers_kept_awake, owners_kept_awake;

    // Keep track of threads so they can be joined at shutdown
    halide_thread *threads[MAX_THREADS];

    // Global flags indicating the threadpool should shut down, and
    // whether the thread pool has been initialized.
    bool shutdown, initialized;

    // The number of threads that are currently committed to possibly block
    // via outstanding jobs queued or being actively worked on. Used to limit
    // the number of iterations of parallel for loops that are invoked so as
    // to prevent deadlock due to oversubscription of threads.
    int threads_reserved;

    // For each thread, how many of the jobs it owns have stalled, indexed by
    // stalled_jobs_slot(). Only maintained for jobs that stall, because
    // finding the index can cost a syscall, and a stalled job has nothing
    // better to do.
    uint8_t stalled_jobs[MAX_THREADS];

    ALWAYS_INLINE bool running() const {
        return !shutdown;
    }

    // Used to check initial state is correct.
    ALWAYS_INLINE void assert_zeroed() const {
        // Assert that all fields except the mutex and desired threads count are zeroed.
        const char *bytes = ((const char *)&this->zero_marker);
        const char *limit = ((const char *)this) + sizeof(work_queue_t);
        while (bytes < limit && *bytes == 0) {
            bytes++;
        }
        halide_abort_if_false(nullptr, bytes == limit && "Logic error in thread pool work queue initialization.\n");
    }

    // Return the work queue to initial state. Must be called while locked
    // and queue will remain locked.
    ALWAYS_INLINE void reset() {
        // Ensure all fields except the mutex and desired hreads count are zeroed.
        char *bytes = ((char *)&this->zero_marker);
        char *limit = ((char *)this) + sizeof(work_queue_t);
        memset(bytes, 0, limit - bytes);
    }
};

WEAK work_queue_t work_queue = {};

#if EXTENDED_DEBUG

WEAK void print_job(work *job, const char *indent, const char *prefix = nullptr) {
    if (prefix == nullptr) {
        prefix = indent;
    }
    const char *name = job->task.name ? job->task.name : "<no name>";
    const char *parent_name = job->parent_job ? (job->parent_job->task.name ? job->parent_job->task.name : "<no name>") : "<no parent job>";
    log_message(prefix << name << "[" << job << "] serial: " << job->task.serial << " active_workers: " << job->active_workers << " min: " << job->task.min << " extent: " << job->task.extent << " siblings: " << job->siblings << " sibling count: " << job->sibling_count << " min_threads " << job->task.min_threads << " next_sempaphore: " << job->next_semaphore << " threads_reserved: " << job->threads_reserved << " parent_job: " << parent_name << "[" << job->parent_job << "]");
    for (int i = 0; i < job->task.num_semaphores; i++) {
        log_message(indent << "    semaphore " << (void *)job->task.semaphores[i].semaphore << " count " << job->task.semaphores[i].count << " val " << *(int *)job->task.semaphores[i].semaphore);
    }
}

WEAK void dump_job_state() {
    log_message("Dumping job state, jobs in queue:");
    work *job = work_queue.jobs;
    while (job != nullptr) {
        print_job(job, "    ");
        job = job->next_job;
    }
    log_message("Done dumping job state.");
}

#else

// clang-format off
#define print_job(job, indent, prefix)  do { /*nothing*/ } while (0)
#define dump_job_state()                do { /*nothing*/ } while (0)
// clang-format on

#endif

WEAK void worker_thread(void *);

WEAK void worker_thread_stall(work *owned_job) {
    work_queue.owners_sleeping++;
    owned_job->owner_is_sleeping = true;
    // An owner waiting on its own job is on the critical path of the
    // parallel loop it started, so it may stay awake while the keep-awake
    // count is held.
    work_queue.wake_owners.wait(&work_queue.mutex, &work_queue.owners_kept_awake, MAX_THREADS);
    owned_job->owner_is_sleeping = false;
    work_queue.owners_sleeping--;
}

WEAK void worker_thread_idle(int helper) {
    work_queue.workers_sleeping++;
    // While the keep-awake count is held, up to the desired number of threads
    // (less the calling thread) stay awake on the A team, even if the most
    // recent parallel loop didn't need them, so that a bigger loop that comes
    // next doesn't have to wake them.
    const int max_kept_awake = work_queue.desired_threads_working - 1;
    const bool stay_on_a_team =
        work_queue.workers_kept_awake < max_kept_awake && keep_awake_held();
    if (work_queue.a_team_size > work_queue.target_a_team_size && !stay_on_a_team) {
        // Transition to B team
        work_queue.a_team_size--;
        work_queue.wake_b_team.wait(&work_queue.mutex);
        work_queue.a_team_size++;
    } else {
        work_queue.wake_a_team.wait(&work_queue.mutex, &work_queue.workers_kept_awake,
                                    max_kept_awake, helper);
    }
    work_queue.workers_sleeping--;
}

// A worker that found runnable work blocked only on an unavailable
// semaphore. Unlike an idle worker, it must be woken by a semaphore
// release, so it waits on its own channel rather than the A/B teams.
WEAK void worker_thread_blocked_on_semaphore() {
    work_queue.workers_sleeping++;
    work_queue.workers_parked_on_semaphore++;
    work_queue.wake_from_semaphore.wait(&work_queue.mutex);
    work_queue.workers_parked_on_semaphore--;
    work_queue.workers_sleeping--;
}

WEAK void worker_thread_already_locked(work *owned_job) {
    // Set the first time this job stalls. Threads that don't own a job are
    // free to work on anything, so they never need it.
    int slot = -1;

    // A worker (a thread that owns no job) is numbered, so that it can help
    // with fast-path loops.
    const int helper = owned_job ? -1 : Synchronization::atomic_fetch_add_sequentially_consistent(&fast_par_for.workers, 1);

    while (owned_job ? owned_job->running() : !work_queue.shutdown) {
        work *job = work_queue.jobs;
        work **prev_ptr = &work_queue.jobs;

        // Did we pass over a job that we could otherwise run, but for an
        // unavailable semaphore? If so, a future semaphore release (not
        // just newly-enqueued work) can make us runnable.
        bool blocked_on_semaphore = false;

        if (owned_job) {
            if (owned_job->exit_status != halide_error_code_success) {
                if (owned_job->active_workers == 0) {
                    while (job != owned_job) {
                        prev_ptr = &job->next_job;
                        job = job->next_job;
                    }
                    *prev_ptr = job->next_job;
                    job->task.extent = 0;
                    continue;  // So loop exit is always in the same place.
                }
            } else if (owned_job->parent_job && owned_job->parent_job->exit_status != halide_error_code_success) {
                owned_job->exit_status = owned_job->parent_job->exit_status;
                // The wakeup can likely be only done under certain conditions, but it is only happening
                // in when an error has already occurred and it seems more important to ensure reliable
                // termination than to optimize this path.
                work_queue.wake_owners.broadcast();
                continue;
            }
        }

        dump_job_state();

        // Find a job to run, preferring things near the top of the stack.
        while (job) {
            print_job(job, "", "Considering job ");
            // Only schedule tasks with enough free worker threads
            // around to complete. They may get stolen later, but only
            // by tasks which can themselves use them to complete
            // work, so forward progress is made.
            bool enough_threads;

            work *parent_job = job->parent_job;

            int threads_available;
            if (parent_job == nullptr) {
                // The + 1 is because work_queue.threads_created does not include the main thread.
                threads_available = (work_queue.threads_created + 1) - work_queue.threads_reserved;
            } else {
                if (parent_job->active_workers == 0) {
                    threads_available = parent_job->task.min_threads - parent_job->threads_reserved;
                } else {
                    threads_available = parent_job->active_workers * parent_job->task.min_threads - parent_job->threads_reserved;
                }
            }
            enough_threads = threads_available >= job->task.min_threads;

            if (!enough_threads) {
                log_message("Not enough threads for job " << job->task.name << " available: " << threads_available << " min_threads: " << job->task.min_threads);
            }
            // Starting a job from another parallel region leaves the job we
            // own waiting on our stack, so only do it for jobs that can't
            // block, and only if we aren't already holding one that way.
            bool can_use_this_thread_stack =
                !owned_job || (job->siblings == owned_job->siblings);
            if (!can_use_this_thread_stack && job->task.min_threads == 0) {
                if (slot < 0) {
                    slot = stalled_jobs_slot();
                    work_queue.stalled_jobs[slot]++;
                }
                can_use_this_thread_stack =
                    work_queue.stalled_jobs[slot] < max_stalled_jobs;
            }
            if (!can_use_this_thread_stack) {
                log_message("Cannot run job " << job->task.name << " on this thread.");
            }
            bool can_add_worker = (!job->task.serial || (job->active_workers == 0));
            if (!can_add_worker) {
                log_message("Cannot add worker to job " << job->task.name);
            }

            if (enough_threads && can_use_this_thread_stack && can_add_worker) {
                if (job->make_runnable()) {
                    break;
                } else {
                    log_message("Cannot acquire semaphores for " << job->task.name);
                    blocked_on_semaphore = true;
                }
            }
            prev_ptr = &(job->next_job);
            job = job->next_job;
        }

        if (!job) {
            // There is no runnable job. Go to sleep.
            // The "stall" and "idle" function calls are not strictly necessary
            // and could be inlined here, but having symbols for these situations
            // is very informative when profiling.
            if (owned_job) {
                worker_thread_stall(owned_job);
            } else if (blocked_on_semaphore) {
                worker_thread_blocked_on_semaphore();
            } else {
                worker_thread_idle(helper);
            }
            continue;
        }

        log_message("Working on job " << job->task.name);

        // Increment the active_worker count so that other threads
        // are aware that this job is still in progress even
        // though there are no outstanding tasks for it.
        job->active_workers++;

        if (job->parent_job == nullptr) {
            work_queue.threads_reserved += job->task.min_threads;
            log_message("Reserved " << job->task.min_threads << " on work queue for " << job->task.name << " giving " << work_queue.threads_reserved << " of " << work_queue.threads_created + 1);
        } else {
            job->parent_job->threads_reserved += job->task.min_threads;
            log_message("Reserved " << job->task.min_threads << " on " << job->parent_job->task.name << " for " << job->task.name << " giving " << job->parent_job->threads_reserved << " of " << job->parent_job->task.min_threads);
        }

        int result = halide_error_code_success;

        if (job->task.serial) {
            // Remove it from the stack while we work on it
            *prev_ptr = job->next_job;

            // Release the lock and do the task.
            halide_mutex_unlock(&work_queue.mutex);
            int total_iters = 0;
            int iters = 1;
            while (result == halide_error_code_success) {
                // Claim as many iterations as possible
                while ((job->task.extent - total_iters) > iters &&
                       job->make_runnable()) {
                    iters++;
                }
                if (iters == 0) {
                    break;
                }

                // Do them
                result = halide_do_loop_task(job->user_context, job->task.fn,
                                             job->task.min + total_iters, iters,
                                             job->task.closure, job);
                total_iters += iters;
                iters = 0;
            }
            halide_mutex_lock(&work_queue.mutex);

            job->task.min += total_iters;
            job->task.extent -= total_iters;

            // Put it back on the job stack, if it hasn't failed.
            if (result != halide_error_code_success) {
                job->task.extent = 0;  // Force job to be finished.
            } else if (job->task.extent > 0) {
                job->next_job = work_queue.jobs;
                work_queue.jobs = job;
            }
        } else {
            // Claim a task from it.
            work myjob = *job;
            job->task.min++;
            job->task.extent--;

            // If there were no more tasks pending for this job, remove it
            // from the stack.
            if (job->task.extent == 0) {
                *prev_ptr = job->next_job;
            }

            // Release the lock and do the task.
            halide_mutex_unlock(&work_queue.mutex);
            if (myjob.task_fn) {
                result = halide_do_task(myjob.user_context, myjob.task_fn,
                                        myjob.task.min, myjob.task.closure);
            } else {
                result = halide_do_loop_task(myjob.user_context, myjob.task.fn,
                                             myjob.task.min, 1,
                                             myjob.task.closure, job);
            }
            halide_mutex_lock(&work_queue.mutex);
        }

        if (result != halide_error_code_success) {
            log_message("Saw thread pool saw error from task: " << (int)result);
        }

        bool wake_owners = false;

        // If this task failed, set the exit status on the job.
        if (result != halide_error_code_success) {
            job->exit_status = result;
            // Mark all siblings as also failed.
            for (int i = 0; i < job->sibling_count; i++) {
                log_message("Marking " << job->sibling_count << " siblings ");
                if (job->siblings[i].exit_status == halide_error_code_success) {
                    job->siblings[i].exit_status = result;
                    wake_owners |= (job->active_workers == 0 && job->siblings[i].owner_is_sleeping);
                }
                log_message("Done marking siblings.");
            }
        }

        if (job->parent_job == nullptr) {
            work_queue.threads_reserved -= job->task.min_threads;
            log_message("Returned " << job->task.min_threads << " to work queue for " << job->task.name << " giving " << work_queue.threads_reserved << " of " << work_queue.threads_created + 1);
        } else {
            job->parent_job->threads_reserved -= job->task.min_threads;
            log_message("Returned " << job->task.min_threads << " to " << job->parent_job->task.name << " for " << job->task.name << " giving " << job->parent_job->threads_reserved << " of " << job->parent_job->task.min_threads);
        }

        // We are no longer active on this job
        job->active_workers--;

        log_message("Done working on job " << job->task.name);

        // A serial job is put back unfinished when its semaphores run dry. The
        // owner may have gone to sleep while we held it (a semaphore release
        // then wakes the owner, which can't add itself to the job yet), and
        // we won't look at it again if we're an owner whose job is done, so
        // wake the owner to take it from here.
        if (wake_owners ||
            (job->active_workers == 0 && (job->task.extent == 0 || job->exit_status != halide_error_code_success || job->task.serial) && job->owner_is_sleeping)) {
            // The job is done or some owned job failed via sibling linkage. Wake up the owner.
            work_queue.wake_owners.broadcast();
        }
    }

    if (slot >= 0) {
        work_queue.stalled_jobs[slot]--;
    }
}

WEAK void worker_thread(void *arg) {
    halide_mutex_lock(&work_queue.mutex);
    worker_thread_already_locked((work *)arg);
    halide_mutex_unlock(&work_queue.mutex);
}

WEAK void enqueue_work_already_locked(int num_jobs, work *jobs, work *task_parent) {
    if (!work_queue.initialized) {
        work_queue.assert_zeroed();

        // Compute the desired number of threads to use. Other code
        // can also mess with this value, but only when the work queue
        // is locked.
        if (!work_queue.desired_threads_working) {
            work_queue.desired_threads_working = default_desired_num_threads();
        }
        work_queue.desired_threads_working = clamp_num_threads(work_queue.desired_threads_working);
        work_queue.initialized = true;
    }

    // Gather some information about the work.

    // Some tasks require a minimum number of threads to make forward
    // progress. Also assume the blocking tasks need to run concurrently.
    int min_threads = 0;

    // Count how many workers to wake. Start at -1 because this thread
    // will contribute.
    int workers_to_wake = -1;

    // Could stalled owners of other tasks conceivably help with one
    // of these jobs.
    bool stealable_jobs = false;

    bool job_has_acquires = false;
    bool job_may_block = false;
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].task.min_threads == 0) {
            stealable_jobs = true;
        } else {
            job_may_block = true;
            min_threads += jobs[i].task.min_threads;
        }
        if (jobs[i].task.num_semaphores != 0) {
            job_has_acquires = true;
        }

        if (jobs[i].task.serial) {
            workers_to_wake++;
        } else {
            workers_to_wake += jobs[i].task.extent;
        }
    }

    if (task_parent == nullptr) {
        // This is here because some top-level jobs may block, but are not accounted for
        // in any enclosing min_threads count. In order to handle extern stages and such
        // correctly, we likely need to make the total min_threads for an invocation of
        // a pipeline a property of the entire thing. This approach works because we use
        // the increased min_threads count to increase the size of the thread pool. It should
        // even be safe against reservation races because this is happening under the work
        // queue lock and that lock will be held into running the job. However that's many
        // lines of code from here to there and it is not guaranteed this will be the first
        // job run.
        if (job_has_acquires || job_may_block) {
            log_message("enqueue_work_already_locked adding one to min_threads.");
            min_threads += 1;
        }

        // Spawn more threads if necessary.
        while (work_queue.threads_created < MAX_THREADS &&
               ((work_queue.threads_created < work_queue.desired_threads_working - 1) ||
                (work_queue.threads_created + 1) - work_queue.threads_reserved < min_threads)) {
            // We might need to make some new threads, if work_queue.desired_threads_working has
            // increased, or if there aren't enough threads to complete this new task.
            work_queue.a_team_size++;
            work_queue.threads[work_queue.threads_created++] =
                halide_spawn_thread(worker_thread, nullptr);
        }
        log_message("enqueue_work_already_locked top level job " << jobs[0].task.name << " with min_threads " << min_threads << " work_queue.threads_created " << work_queue.threads_created << " work_queue.threads_reserved " << work_queue.threads_reserved);
        if (job_has_acquires || job_may_block) {
            work_queue.threads_reserved++;
        }
    } else {
        log_message("enqueue_work_already_locked job " << jobs[0].task.name << " with min_threads " << min_threads << " task_parent " << task_parent->task.name << " task_parent->task.min_threads " << task_parent->task.min_threads << " task_parent->threads_reserved " << task_parent->threads_reserved);
        halide_abort_if_false(nullptr, (min_threads <= ((task_parent->task.min_threads * task_parent->active_workers) -
                                                        task_parent->threads_reserved)) &&
                                           "Logic error: thread over commit.\n");
        if (job_has_acquires || job_may_block) {
            task_parent->threads_reserved++;
        }
    }

    // Push the jobs onto the stack.
    for (int i = num_jobs - 1; i >= 0; i--) {
        // We could bubble it downwards based on some heuristics, but
        // it's not strictly necessary to do so.
        jobs[i].next_job = work_queue.jobs;
        jobs[i].siblings = &jobs[0];
        jobs[i].sibling_count = num_jobs;
        jobs[i].threads_reserved = 0;
        work_queue.jobs = jobs + i;
    }

    bool nested_parallelism =
        work_queue.owners_sleeping ||
        (work_queue.workers_sleeping < work_queue.threads_created);

    // Wake up an appropriate number of threads
    if (nested_parallelism || workers_to_wake > work_queue.workers_sleeping) {
        // If there's nested parallelism going on, we just wake up
        // everyone. TODO: make this more precise.
        work_queue.target_a_team_size = work_queue.threads_created;
    } else {
        work_queue.target_a_team_size = workers_to_wake;
    }

    work_queue.wake_a_team.broadcast();
    if (work_queue.target_a_team_size > work_queue.a_team_size) {
        work_queue.wake_b_team.broadcast();
        if (stealable_jobs) {
            work_queue.wake_owners.broadcast();
        }
    }

    // Workers blocked on a semaphore wait on their own channel, so the
    // broadcasts above don't reach them. Wake them too: they may be able
    // to steal this newly-enqueued work.
    if (work_queue.workers_parked_on_semaphore) {
        work_queue.wake_from_semaphore.broadcast();
    }

    if (job_has_acquires || job_may_block) {
        if (task_parent != nullptr) {
            task_parent->threads_reserved--;
        } else {
            work_queue.threads_reserved--;
        }
    }
}

// Try to run a parallel for loop on the fast path (see fast_par_for_t).
// Returns false, having done nothing, if it can't; otherwise returns true and
// sets *exit_status.
WEAK bool fast_par_for_try(void *user_context, halide_task_t f, int min, int size,
                           uint8_t *closure, int *exit_status) {
    int threads;
    Synchronization::atomic_load_relaxed(&work_queue.desired_threads_working, &threads);
    if (threads <= 0 || size > 0x7fffffff - MAX_THREADS - 1) {
        // The pool hasn't been initialized, or the iteration counter could
        // overflow.
        return false;
    }
    const int participants = size < threads ? size : threads;
    if (participants == 1) {
        // There's nothing to share, so just run the loop here.
        for (int i = 0; i < size; i++) {
            int result = halide_do_task(user_context, f, min + i, closure);
            if (result != halide_error_code_success) {
                *exit_status = result;
                return true;
            }
        }
        *exit_status = halide_error_code_success;
        return true;
    }

    // Only use the fast path on an idle pool: with no queued work, and with
    // enough idle workers polling to run an iteration each.
    work *jobs;
    Synchronization::atomic_load_relaxed(&work_queue.jobs, &jobs);
    int pollers;
    Synchronization::atomic_load_relaxed(&fast_par_for.pollers, &pollers);
    if (jobs || pollers < participants - 1) {
        return false;
    }
    int expected = 0, desired = 1;
    if (!Synchronization::atomic_cas_strong_sequentially_consistent(&fast_par_for.busy, &expected, &desired)) {
        return false;
    }

    // Wait for any helpers still in the previous loop to leave it. They have
    // nothing left to claim there, so they are usually long gone.
    const int workers = Synchronization::atomic_fetch_add_sequentially_consistent(&fast_par_for.workers, 0);
    for (int h = 0; h < workers; h++) {
        for (int spin = 0;; spin++) {
            int in_use;
            Synchronization::atomic_load_acquire(&fast_par_for.slots[h + 1].in_use, &in_use);
            if (!in_use) {
                break;
            }
            if (spin < 4096) {
                spin_pause();
            } else {
                halide_thread_yield();
            }
        }
    }

    fast_par_for.fn = f;
    fast_par_for.user_context = user_context;
    fast_par_for.closure = closure;
    fast_par_for.min = min;
    fast_par_for.extent = size;
    fast_par_for.participants = participants;
    int zero = 0;
    for (int p = 0; p < participants; p++) {
        int next = (int)((int64_t)size * p / participants);
        int end = (int)((int64_t)size * (p + 1) / participants);
        Synchronization::atomic_store_relaxed(&fast_par_for.slots[p].next, &next);
        fast_par_for.slots[p].end = end;
    }
    Synchronization::atomic_store_relaxed(&fast_par_for.finished, &zero);
    Synchronization::atomic_store_relaxed(&fast_par_for.exit_status, &zero);

    // Open the loop, and work on it.
    uintptr_t state = fast_par_for.state + 1;
    Synchronization::atomic_store_release(&fast_par_for.state, &state);
    const int claimed = fast_par_for_work(0);

    // Every iteration has been claimed, so wait for the helpers to finish
    // theirs, then close the loop.
    for (int spin = 0;; spin++) {
        int finished;
        Synchronization::atomic_load_acquire(&fast_par_for.finished, &finished);
        if (finished + claimed == size) {
            break;
        }
        // Spin tightly for a few microseconds, which is as long as a short
        // iteration might take, then yield to anything else that wants this
        // core.
        if (spin < 4096) {
            spin_pause();
        } else {
            halide_thread_yield();
        }
    }
    state++;
    Synchronization::atomic_store_sequentially_consistent(&fast_par_for.state, &state);
    Synchronization::atomic_load_relaxed(&fast_par_for.exit_status, exit_status);
    Synchronization::atomic_store_release(&fast_par_for.busy, &zero);
    return true;
}

WEAK halide_do_task_t custom_do_task = halide_default_do_task;
WEAK halide_do_loop_task_t custom_do_loop_task = halide_default_do_loop_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_do_parallel_tasks_t custom_do_parallel_tasks = halide_default_do_parallel_tasks;
WEAK halide_semaphore_init_t custom_semaphore_init = halide_default_semaphore_init;
WEAK halide_semaphore_try_acquire_t custom_semaphore_try_acquire = halide_default_semaphore_try_acquire;
WEAK halide_semaphore_release_t custom_semaphore_release = halide_default_semaphore_release;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;

extern "C" {

namespace {
WEAK __attribute__((destructor)) void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}  // namespace

WEAK int halide_default_do_task(void *user_context, halide_task_t f, int idx,
                                uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK int halide_default_do_loop_task(void *user_context, halide_loop_task_t f,
                                     int min, int extent, uint8_t *closure,
                                     void *task_parent) {
    return f(user_context, min, extent, closure, task_parent);
}

WEAK int halide_default_do_par_for(void *user_context, halide_task_t f,
                                   int min, int size, uint8_t *closure) {
    if (size <= 0) {
        return halide_error_code_success;
    }

    int exit_status;
    if (fast_par_for_try(user_context, f, min, size, closure, &exit_status)) {
        return exit_status;
    }

    work job;
    job.task.fn = nullptr;
    job.task.min = min;
    job.task.extent = size;
    job.task.serial = false;
    job.task.semaphores = nullptr;
    job.task.num_semaphores = 0;
    job.task.closure = closure;
    job.task.min_threads = 0;
    job.task.name = nullptr;
    job.task_fn = f;
    job.user_context = user_context;
    job.exit_status = halide_error_code_success;
    job.active_workers = 0;
    job.next_semaphore = 0;
    job.owner_is_sleeping = false;
    job.siblings = &job;  // guarantees no other job points to the same siblings.
    job.sibling_count = 0;
    job.parent_job = nullptr;
    halide_mutex_lock(&work_queue.mutex);
    enqueue_work_already_locked(1, &job, nullptr);
    worker_thread_already_locked(&job);
    halide_mutex_unlock(&work_queue.mutex);
    return job.exit_status;
}

WEAK int halide_default_do_parallel_tasks(void *user_context, int num_tasks,
                                          struct halide_parallel_task_t *tasks,
                                          void *task_parent) {
    work *jobs = (work *)__builtin_alloca(sizeof(work) * num_tasks);

    for (int i = 0; i < num_tasks; i++) {
        if (tasks->extent <= 0) {
            // Skip extent zero jobs
            num_tasks--;
            continue;
        }
        jobs[i].task = *tasks++;
        jobs[i].task_fn = nullptr;
        jobs[i].user_context = user_context;
        jobs[i].exit_status = halide_error_code_success;
        jobs[i].active_workers = 0;
        jobs[i].next_semaphore = 0;
        jobs[i].owner_is_sleeping = false;
        jobs[i].parent_job = (work *)task_parent;
    }

    if (num_tasks == 0) {
        return halide_error_code_success;
    }

    halide_mutex_lock(&work_queue.mutex);
    enqueue_work_already_locked(num_tasks, jobs, (work *)task_parent);
    int exit_status = halide_error_code_success;
    for (int i = 0; i < num_tasks; i++) {
        // It doesn't matter what order we join the tasks in, because
        // we'll happily assist with siblings too.
        worker_thread_already_locked(jobs + i);
        if (jobs[i].exit_status != halide_error_code_success) {
            exit_status = jobs[i].exit_status;
        }
    }
    halide_mutex_unlock(&work_queue.mutex);
    return exit_status;
}

WEAK int halide_set_num_threads(int n) {
    if (n < 0) {
        halide_error(nullptr, "halide_set_num_threads: must be >= 0.");
    }
    // Don't make this an atomic swap - we don't want to be changing
    // the desired number of threads while another thread is in the
    // middle of a sequence of non-atomic operations.
    halide_mutex_lock(&work_queue.mutex);
    if (n == 0) {
        n = default_desired_num_threads();
    }
    int old = work_queue.desired_threads_working;
    work_queue.desired_threads_working = clamp_num_threads(n);
    halide_mutex_unlock(&work_queue.mutex);
    return old;
}

WEAK int halide_get_num_threads() {
    halide_mutex_lock(&work_queue.mutex);
    int n = work_queue.desired_threads_working;
    halide_mutex_unlock(&work_queue.mutex);
    return n;
}

WEAK int halide_thread_pool_keep_awake(bool keep_awake) {
    if (keep_awake) {
        int count = Synchronization::atomic_add_fetch_sequentially_consistent(&keep_awake_count, 1);
        if (count == 1) {
            // Wake idle workers on both teams, so that they spin from now on
            // rather than after the next parallel loop wakes them.
            halide_mutex_lock(&work_queue.mutex);
            if (work_queue.initialized && !work_queue.shutdown && work_queue.workers_sleeping) {
                work_queue.wake_a_team.broadcast();
                work_queue.wake_b_team.broadcast();
            }
            halide_mutex_unlock(&work_queue.mutex);
        }
        return count;
    }

    // Threads spinning because of the count notice it dropping to zero on
    // their next poll and go to sleep, so there's nothing to wake here.
    int count;
    Synchronization::atomic_load_relaxed(&keep_awake_count, &count);
    int desired;
    do {
        if (count <= 0) {
            halide_error(nullptr, "halide_thread_pool_keep_awake(false) called without a matching halide_thread_pool_keep_awake(true).\n");
            return halide_error_code_generic_error;
        }
        desired = count - 1;
    } while (!Synchronization::atomic_cas_weak_relacq_relaxed(&keep_awake_count, &count, &desired));
    return desired;
}

WEAK void halide_shutdown_thread_pool() {
    if (work_queue.initialized) {
        // Wake everyone up and tell them the party's over and it's time
        // to go home. This includes threads spinning because the
        // keep-awake count is held. The count itself is preserved, so the
        // threads of a restarted pool stay awake too.
        halide_mutex_lock(&work_queue.mutex);

        work_queue.shutdown = true;
        work_queue.wake_owners.broadcast();
        work_queue.wake_a_team.broadcast();
        work_queue.wake_b_team.broadcast();
        work_queue.wake_from_semaphore.broadcast();
        halide_mutex_unlock(&work_queue.mutex);

        // Wait until they leave
        for (int i = 0; i < work_queue.threads_created; i++) {
            halide_join_thread(work_queue.threads[i]);
        }

        // Tidy up
        work_queue.reset();
        int zero = 0;
        Synchronization::atomic_store_relaxed(&fast_par_for.workers, &zero);
    }
}

struct halide_semaphore_impl_t {
    int value;
};

WEAK int halide_default_semaphore_init(halide_semaphore_t *s, int n) {
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
    Halide::Runtime::Internal::Synchronization::atomic_store_release(&sem->value, &n);
    return n;
}

WEAK int halide_default_semaphore_release(halide_semaphore_t *s, int n) {
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
    int old_val = Halide::Runtime::Internal::Synchronization::atomic_fetch_add_acquire_release(&sem->value, n);
    if (n != 0) {
        halide_mutex_lock(&work_queue.mutex);
        if (work_queue.workers_parked_on_semaphore) {
            work_queue.wake_from_semaphore.broadcast();
        }
        if (work_queue.owners_sleeping) {
            work_queue.wake_owners.broadcast();
        }
        halide_mutex_unlock(&work_queue.mutex);
    }
    return old_val + n;
}

WEAK bool halide_default_semaphore_try_acquire(halide_semaphore_t *s, int n) {
    if (n == 0) {
        return true;
    }
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
    // Decrement and get new value
    int expected;
    int desired;
    Halide::Runtime::Internal::Synchronization::atomic_load_acquire(&sem->value, &expected);
    do {
        desired = expected - n;
    } while (desired >= 0 &&
             !Halide::Runtime::Internal::Synchronization::atomic_cas_weak_relacq_relaxed(&sem->value, &expected, &desired));
    return desired >= 0;
}

WEAK halide_do_task_t halide_set_custom_do_task(halide_do_task_t f) {
    halide_do_task_t result = custom_do_task;
    custom_do_task = f;
    return result;
}

WEAK halide_do_loop_task_t halide_set_custom_do_loop_task(halide_do_loop_task_t f) {
    halide_do_loop_task_t result = custom_do_loop_task;
    custom_do_loop_task = f;
    return result;
}

WEAK halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t f) {
    halide_do_par_for_t result = custom_do_par_for;
    custom_do_par_for = f;
    return result;
}

WEAK void halide_set_custom_parallel_runtime(
    halide_do_par_for_t do_par_for,
    halide_do_task_t do_task,
    halide_do_loop_task_t do_loop_task,
    halide_do_parallel_tasks_t do_parallel_tasks,
    halide_semaphore_init_t semaphore_init,
    halide_semaphore_try_acquire_t semaphore_try_acquire,
    halide_semaphore_release_t semaphore_release) {

    custom_do_par_for = do_par_for;
    custom_do_task = do_task;
    custom_do_loop_task = do_loop_task;
    custom_do_parallel_tasks = do_parallel_tasks;
    custom_semaphore_init = semaphore_init;
    custom_semaphore_try_acquire = semaphore_try_acquire;
    custom_semaphore_release = semaphore_release;
}

WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return (*custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
    return (*custom_do_par_for)(user_context, f, min, size, closure);
}

WEAK int halide_do_loop_task(void *user_context, halide_loop_task_t f,
                             int min, int size, uint8_t *closure, void *task_parent) {
    return custom_do_loop_task(user_context, f, min, size, closure, task_parent);
}

WEAK int halide_do_parallel_tasks(void *user_context, int num_tasks,
                                  struct halide_parallel_task_t *tasks,
                                  void *task_parent) {
    return custom_do_parallel_tasks(user_context, num_tasks, tasks, task_parent);
}

WEAK int halide_semaphore_init(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_init(sema, count);
}

WEAK int halide_semaphore_release(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_release(sema, count);
}

WEAK bool halide_semaphore_try_acquire(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_try_acquire(sema, count);
}
}
