#define EXTENDED_DEBUG 0

#if EXTENDED_DEBUG
// This code is currently setup for Linux debugging. Switch to using pthread_self on e.g. Mac OS X.
extern "C" int syscall(int);

namespace {
int gettid() {
#ifdef BITS_32
    return syscall(224);
#else
    return syscall(186);
#endif
}
}  // namespace

// clang-format off
#define log_message(stuff) do { print(nullptr) << gettid() << ": " << stuff << "\n"; } while (0)
// clang-format on

#else

// clang-format off
#define log_message(stuff) do { /*nothing*/ } while (0)
// clang-format on

#endif

namespace Halide {
namespace Runtime {
namespace Internal {

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
    halide_cond wake_a_team, wake_b_team, wake_owners;

    // The number of sleeping workers and owners. An over-estimate - a
    // waking-up thread may not have decremented this yet.
    int workers_sleeping, owners_sleeping;

    // Keep track of threads so they can be joined at shutdown
    halide_thread *threads[MAX_THREADS];

    // Global flags indicating the threadpool should shut down, and
    // whether the thread pool has been initialized.
    bool shutdown, initialized;

    // The number of threads that are currently commited to possibly block
    // via outstanding jobs queued or being actively worked on. Used to limit
    // the number of iterations of parallel for loops that are invoked so as
    // to prevent deadlock due to oversubscription of threads.
    int threads_reserved;

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

WEAK void worker_thread_already_locked(work *owned_job) {
    int spin_count = 0;
    const int max_spin_count = 40;

    while (owned_job ? owned_job->running() : !work_queue.shutdown) {
        work *job = work_queue.jobs;
        work **prev_ptr = &work_queue.jobs;

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
                // in when an error has already occured and it seems more important to ensure reliable
                // termination than to optimize this path.
                halide_cond_broadcast(&work_queue.wake_owners);
                continue;
            }
        }

        dump_job_state();

        // Find a job to run, prefering things near the top of the stack.
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
            bool can_use_this_thread_stack = !owned_job || (job->siblings == owned_job->siblings) || job->task.min_threads == 0;
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
                }
            }
            prev_ptr = &(job->next_job);
            job = job->next_job;
        }

        if (!job) {
            // There is no runnable job. Go to sleep.
            if (owned_job) {
                if (spin_count++ < max_spin_count) {
                    // Give the workers a chance to finish up before sleeping
                    halide_mutex_unlock(&work_queue.mutex);
                    halide_thread_yield();
                    halide_mutex_lock(&work_queue.mutex);
                } else {
                    work_queue.owners_sleeping++;
                    owned_job->owner_is_sleeping = true;
                    halide_cond_wait(&work_queue.wake_owners, &work_queue.mutex);
                    owned_job->owner_is_sleeping = false;
                    work_queue.owners_sleeping--;
                }
            } else {
                work_queue.workers_sleeping++;
                if (work_queue.a_team_size > work_queue.target_a_team_size) {
                    // Transition to B team
                    work_queue.a_team_size--;
                    halide_cond_wait(&work_queue.wake_b_team, &work_queue.mutex);
                    work_queue.a_team_size++;
                } else if (spin_count++ < max_spin_count) {
                    // Spin waiting for new work
                    halide_mutex_unlock(&work_queue.mutex);
                    halide_thread_yield();
                    halide_mutex_lock(&work_queue.mutex);
                } else {
                    halide_cond_wait(&work_queue.wake_a_team, &work_queue.mutex);
                }
                work_queue.workers_sleeping--;
            }
            continue;
        } else {
            spin_count = 0;
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

        if (wake_owners ||
            (job->active_workers == 0 && (job->task.extent == 0 || job->exit_status != halide_error_code_success) && job->owner_is_sleeping)) {
            // The job is done or some owned job failed via sibling linkage. Wake up the owner.
            halide_cond_broadcast(&work_queue.wake_owners);
        }
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

    halide_cond_broadcast(&work_queue.wake_a_team);
    if (work_queue.target_a_team_size > work_queue.a_team_size) {
        halide_cond_broadcast(&work_queue.wake_b_team);
        if (stealable_jobs) {
            halide_cond_broadcast(&work_queue.wake_owners);
        }
    }

    if (job_has_acquires || job_may_block) {
        if (task_parent != nullptr) {
            task_parent->threads_reserved--;
        } else {
            work_queue.threads_reserved--;
        }
    }
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

WEAK void halide_shutdown_thread_pool() {
    if (work_queue.initialized) {
        // Wake everyone up and tell them the party's over and it's time
        // to go home
        halide_mutex_lock(&work_queue.mutex);

        work_queue.shutdown = true;
        halide_cond_broadcast(&work_queue.wake_owners);
        halide_cond_broadcast(&work_queue.wake_a_team);
        halide_cond_broadcast(&work_queue.wake_b_team);
        halide_mutex_unlock(&work_queue.mutex);

        // Wait until they leave
        for (int i = 0; i < work_queue.threads_created; i++) {
            halide_join_thread(work_queue.threads[i]);
        }

        // Tidy up
        work_queue.reset();
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
    // TODO(abadams|zvookin): Is this correct if an acquire can be for say count of 2 and the releases are 1 each?
    if (old_val == 0 && n != 0) {  // Don't wake if nothing released.
        // We may have just made a job runnable
        halide_mutex_lock(&work_queue.mutex);
        halide_cond_broadcast(&work_queue.wake_a_team);
        halide_cond_broadcast(&work_queue.wake_owners);
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
