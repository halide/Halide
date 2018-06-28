#ifndef EXTENDED_DEBUG
#define EXTENDED_DEBUG 0
#endif

#if EXTENDED_DEBUG

// This code is currently setup for Linux debugging. Switch to using pthread_self on e.g. Mac OS X.
//extern "C" void * pthread_self();
extern "C" int syscall(int);

namespace {
int gettid() {
    return syscall(186);
}
}

#define log_message(stuff) print(NULL) << gettid() << ": " << stuff << "\n"
#else
#define log_message(stuff)
#endif

namespace Halide { namespace Runtime { namespace Internal {

struct work {
    halide_parallel_task_t task;

    // If we come in to the task system via do_par_for we just have a
    // halide_task_t, not a halide_loop_task_t.
    halide_task_t task_fn;

    work *next_job;
    int *parent;
    work *parent_job;
    int threads_reserved;
  
    void *user_context;
    int active_workers;
    int exit_status;
    int next_semaphore;
    // which condition variable is the owner sleeping on. NULL if it isn't sleeping.
    bool owner_is_sleeping;

    bool make_runnable() {
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

    bool running() {
        return task.extent || active_workers;
    }
};

#define MAX_THREADS 256

WEAK int clamp_num_threads(int threads) {
    if (threads > MAX_THREADS) {
        threads = MAX_THREADS;
    } else if (threads < 1) {
        threads = 1;
    }
    return threads;
}

WEAK int default_desired_num_threads() {
    int desired_num_threads = 0;
    char *threads_str = getenv("HL_NUM_THREADS");
    if (!threads_str) {
        // Legacy name for HL_NUM_THREADS
        threads_str = getenv("HL_NUMTHREADS");
    }
    if (threads_str) {
        desired_num_threads = atoi(threads_str);
    } else {
        desired_num_threads = halide_host_cpu_count();
    }
    return desired_num_threads;
}

// The work queue and thread pool is weak, so one big work queue is shared by all halide functions
struct work_queue_t {
    // all fields are protected by this mutex.
    halide_mutex mutex;

    // The desired number threads doing work (HL_NUM_THREADS).
    int desired_threads_working;

// This file currently contains code to dum the entire work queue state.
// Jobs actively being worked on are sometimes removed from the queue.
// This look aside list allows printing them for debug purposes.
// See dump_job_state.
// TODO(zvookin): Does this stay or does it go?
#ifndef WORK_QUEUE_DEBUG
#define WORK_QUEUE_DEBUG 0
#endif

#ifndef TLS_PARENT_LINK
// WORK_QUEUE_DEBUG requires the parent links.
#define TLS_PARENT_LINK WORK_QUEUE_DEBUG
#endif

#if TLS_PARENT_LINK
    struct working_job {
        work *job;
        working_job *next;
    };

    struct thread_state {
        thread_state *next;
        working_job *job_stack;
#if WORK_QUEUE_DEBUG
        int tid;
#endif
    } *thread_states;

    pthread_key_t job_link_key;
    bool job_link_key_inited;

    thread_state *get_thread_state() {
        if (!job_link_key_inited) {
            pthread_key_create(&job_link_key, free);
            job_link_key_inited = true;
        }
        thread_state *state = (thread_state *)pthread_getspecific(job_link_key);
        if (state == NULL) {
            state = (thread_state *)malloc(sizeof(thread_state));
            state->next = thread_states;
            thread_states = state;
            state->job_stack = NULL;
#if WORK_QUEUE_DEBUG
            state->tid = gettid();
#endif
            pthread_setspecific(job_link_key, state);
        }
        return state;
    }
#endif

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

// Currently this is necessary to make async code work without deadlocking.
// This is under an ifdef as the how is to remove the code by passing the
// parent in the task structure to the API, but this requires investigation
// and the algorithm needs to be vetted in the PR.
// TODO(zvookin): Either remove the code or change this comment.

#if TLS_PARENT_LINK
    int threads_reserved;
#endif

    bool running() const {
        return !shutdown;
    }

    // Used to check initial state is correct.
    void assert_zeroed() const {
        // Assert that all fields except the mutex and desired hreads count are zeroed.
        const char *bytes = ((const char *)&this->zero_marker);
        const char *limit = ((const char *)this) + sizeof(work_queue_t);
        while (bytes < limit && *bytes == 0) {
            bytes++;
        }
        halide_assert(NULL, bytes == limit && "Logic error in thread pool work queue initialization.\n");
    }

    // Return the work queue to initial state. Must be called while locked
    // and queue will remain locked.
    void reset() {
        // Ensure all fields except the mutex and desired hreads count are zeroed.
        char *bytes = ((char *)&this->zero_marker);
        char *limit = ((char *)this) + sizeof(work_queue_t);
        memset(bytes, 0, limit - bytes);
    }
};

WEAK work_queue_t work_queue = {};

WEAK void worker_thread(void *);

#if WORK_QUEUE_DEBUG

WEAK void print_job(work *job, const char *indent) {
    const char *name = job->task.name ? job->task.name : "<no name>";
    const char *parent_name = job->parent_job ? (job->parent_job->task.name ? job->parent_job->task.name : "<no name>") : "<no parent job>";
    log_message(indent << name << "[" << job << "] serial: " << job->task.serial << " active_workers: " << job->active_workers << " min: " << job->task.min << " extent: " << job->task.extent << " may_block: " << job->task.may_block << " parent key: " << job->parent << " min_threads " << job->task.min_threads << " next_sempaphore: " << job->next_semaphore << " threads_reserved: " << job->threads_reserved << " parent_job: " << parent_name << "[" << job->parent_job << "]");
    for (int i = 0; i < job->task.num_semaphores; i++) {
        log_message(indent << "    semaphore " << (void *)job->task.semaphores[i].semaphore << " count " << job->task.semaphores[i].count << " val " << *(int *)job->task.semaphores[i].semaphore);
    }
}

WEAK void dump_job_state() {
    log_message("Dumping job state, across threads:");
    for (work_queue_t::thread_state *t = work_queue.thread_states; t != NULL; t = t->next) {
        log_message("    Dumping jobs thread " << t->tid << " is working on, bottom of stack first:");
        work_queue_t::working_job *wjob = t->job_stack;
        while (wjob != NULL) {
            print_job(wjob->job, "        ");
            wjob = wjob->next;
        }
    }
    log_message("Dumping job state, jobs in queue:");
    work *job = work_queue.jobs;
    while (job != NULL) {
        print_job(job, "    ");
        job = job->next_job;
    }
    log_message("Done dumping job state.");
}

#endif

WEAK void worker_thread_already_locked(work *owned_job) {
#if TLS_PARENT_LINK
    work_queue_t::thread_state *state = work_queue.get_thread_state();
#endif
    while (owned_job ? owned_job->running() : !work_queue.shutdown) {
#if WORK_QUEUE_DEBUG        
        dump_job_state();
#endif

        // Find a job to run, prefering things near the top of the stack.
        work *job = work_queue.jobs;
        work **prev_ptr = &work_queue.jobs;
        while (job) {

#if EXTENDED_DEBUG && TLS_PARENT_LINK
            const char *name = job->task.name ? job->task.name : "<no name>";
            const char *owned_name = owned_job ? (owned_job->task.name ? owned_job->task.name : "<no name>") : "<no owned job>";
            auto owned_job_parent = owned_job ? owned_job->parent : 0;
#endif
            log_message("Considering job " << name << " extent: " << job->task.extent << " active_workers: " << job->active_workers << " serial: " << job->task.serial << " may_block: " << job->task.may_block << " current owned_job: " << owned_name << " job->parent: " << job->parent << " owned_job parent: " << owned_job_parent);
            // Only schedule tasks with enough free worker threads
            // around to complete. They may get stolen later, but only
            // by tasks which can themselves use them to complete
            // work, so forward progress is made.
            bool enough_threads;

#if TLS_PARENT_LINK
            work *parent_job = job->parent_job;

            int threads_available;
            if (parent_job == NULL) {
                threads_available = work_queue.threads_created - work_queue.threads_reserved;
                log_message("Top level job work_queue.threads_created: " << work_queue.threads_created << " work_queue.threads_reserved: " << work_queue.threads_reserved);
            } else {
                if (parent_job->active_workers == 0) {
                    threads_available = parent_job->task.min_threads - parent_job->threads_reserved;
                } else {
                    threads_available = parent_job->active_workers * parent_job->task.min_threads - parent_job->threads_reserved;
                }
                log_message("Sub task parent_job->active_workers: " << parent_job->active_workers << " parent_job->task.min_threads: " << parent_job->task.min_threads << " parent_job->threads_reserved: " << parent_job->threads_reserved);
            }
            enough_threads = threads_available >= job->task.min_threads;
#else
            int threads_that_could_assist = 1 + work_queue.workers_sleeping;
            if (!job->task.may_block) {
                threads_that_could_assist += work_queue.owners_sleeping;
            } else if (job->owner_is_sleeping) {
                threads_that_could_assist++;
            }
            enough_threads = job->task.min_threads <= threads_that_could_assist;
#endif
            bool may_try = ((!owned_job || (job->parent == owned_job->parent) || !job->task.may_block) &&
                            (!job->task.serial || (job->active_workers == 0)));
            if (may_try && enough_threads && job->make_runnable()) {
                break;
            }
            log_message(" Passing on " << job->task.name << " min_threads: " << job->task.min_threads << " " << enough_threads << " " << may_try);
            prev_ptr = &(job->next_job);
            job = job->next_job;
        }

        if (!job) {
            // There is no runnable job. Go to sleep.
            if (owned_job) {
                work_queue.owners_sleeping++;
                owned_job->owner_is_sleeping = true;
                log_message(" Owner sleeping");
                halide_cond_wait(&work_queue.wake_owners, &work_queue.mutex);
                owned_job->owner_is_sleeping = false;
                work_queue.owners_sleeping--;
            } else {
                work_queue.workers_sleeping++;
                if (work_queue.a_team_size > work_queue.target_a_team_size) {
                    // Transition to B team
                  log_message(" B team worker sleeping work_queue.a_team_size: " << work_queue.a_team_size << " work_queue.target_a_team_size: " << work_queue.target_a_team_size);
                    work_queue.a_team_size--;
                    halide_cond_wait(&work_queue.wake_b_team, &work_queue.mutex);
                    work_queue.a_team_size++;
                } else {
                  log_message(" A team worker sleeping");
                    halide_cond_wait(&work_queue.wake_a_team, &work_queue.mutex);
                }
                work_queue.workers_sleeping--;
            }
            continue;
        }

        log_message("Working on " << job->task.name);

        // Increment the active_worker count so that other threads
        // are aware that this job is still in progress even
        // though there are no outstanding tasks for it.
        job->active_workers++;

#if TLS_PARENT_LINK
        work_queue_t::working_job wjob;
        wjob.job = job;
        wjob.next = state->job_stack;
        state->job_stack = &wjob;
#endif

#if TLS_PARENT_LINK
#if EXTENDED_DEBUG
        const char *job_name = job->task.name ? job->task.name : "<no name>";
        const char *parent_job_name = (job->parent_job && job->parent_job->task.name) ? job->parent_job->task.name : "<no_name>";
#endif
        if (job->parent_job == NULL) {
            log_message("Reserving " << job->task.min_threads << " threads for " << job_name << " on work_queue.");
            work_queue.threads_reserved += job->task.min_threads;
        } else {
            log_message("Reserving " << job->task.min_threads << " threads for " << job_name << " on " << parent_job_name << ".");
            job->parent_job->threads_reserved += job->task.min_threads;
        }

        log_message("Setting parent job link to " << job);
#endif

        int result = 0;

        if (job->task.serial) {
            // Remove it from the stack while we work on it
            *prev_ptr = job->next_job;

            // Release the lock and do the task.
            halide_mutex_unlock(&work_queue.mutex);
            int total_iters = 0;
            int iters = 1;
            while (result == 0) {
                // Claim as many iterations as possible
                while ((job->task.extent - total_iters) > iters &&
                       job->make_runnable()) {
                    iters++;
                }
                if (iters == 0) break;

                // Do them
                result = halide_do_loop_task(job->user_context, job->task.fn,
                                             job->task.min + total_iters, iters, job->task.closure);
                total_iters += iters;
                iters = 0;
            }
            halide_mutex_lock(&work_queue.mutex);

            log_message("Did " << total_iters << " on " << job->task.name);
            job->task.min += total_iters;
            job->task.extent -= total_iters;

            // Put it back on the job stack
            if (job->task.extent > 0) {
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
                                             myjob.task.min, 1, myjob.task.closure);
            }
            halide_mutex_lock(&work_queue.mutex);
        }

        log_message("Finished working on " << job->task.name << " result: " << result);

        // If this task failed, set the exit status on the job.
        if (result) {
            job->exit_status = result;
        }

#if TLS_PARENT_LINK
        log_message("Resetting parent job link to " << wjob.next);
        state->job_stack = wjob.next;

        if (job->parent_job == NULL) {
            log_message("Releasing " << job->task.min_threads << " threads for " << job_name << " on work_queue.");
            work_queue.threads_reserved -= job->task.min_threads;
        } else {
            log_message("Releasing " << job->task.min_threads << " threads for " << job_name << " on " << parent_job_name << ".");
            job->parent_job->threads_reserved -= job->task.min_threads;
        }
#endif

        // We are no longer active on this job
        job->active_workers--;

        if (!job->running() && job->owner_is_sleeping) {
            // The job is done. Wake up the owner.
            halide_cond_broadcast(&work_queue.wake_owners);
        }
    }
}

WEAK void worker_thread(void *arg) {
    halide_mutex_lock(&work_queue.mutex);
    worker_thread_already_locked((work *)arg);
    halide_mutex_unlock(&work_queue.mutex);
}

WEAK void enqueue_work_already_locked(int num_jobs, work *jobs) {
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

    for (int i = 0; i < num_jobs; i++) {
        if (!jobs[i].task.may_block) {
          stealable_jobs = true;
        } else {
            min_threads += jobs[i].task.min_threads;
        }
        if (jobs[i].task.serial) {
            workers_to_wake++;
        } else {
            workers_to_wake += jobs[i].task.extent;
        }
        log_message(" Enqueueing " << jobs[i].task.name);
    }

    // Spawn more threads if necessary.
    while ((work_queue.threads_created < work_queue.desired_threads_working - 1) ||
           (work_queue.threads_created < min_threads - 1)) {
        // We might need to make some new threads, if work_queue.desired_threads_working has
        // increased, or if there aren't enough threads to complete this new task.
        work_queue.a_team_size++;
        work_queue.threads[work_queue.threads_created++] =
            halide_spawn_thread(worker_thread, NULL);
    }

    log_message(" Workers to wake: " << workers_to_wake);
    log_message(" min_threads for this task: " << min_threads);
    log_message(" Threads created: " << work_queue.threads_created);

    // Store a token on the stack so that we know which jobs we
    // own. We may work on any job we own, regardless of whether it
    // blocks. The value is unimportant - we only use the address.
    int parent_id = 0;

#if TLS_PARENT_LINK
    work_queue_t::thread_state *state = work_queue.get_thread_state();
    work *parent_job = (state->job_stack != NULL) ? state->job_stack->job : NULL;
#endif

    // Push the jobs onto the stack.
    for (int i = num_jobs - 1; i >= 0; i--) {
        // We could bubble it downwards based on some heuristics, but
        // it's not strictly necessary to do so.
        jobs[i].next_job = work_queue.jobs;
        jobs[i].parent = &parent_id;
#if TLS_PARENT_LINK
        log_message("Parenting job " << &jobs[i] << " with " << parent_job);
        jobs[i].parent_job = parent_job;
#endif
        jobs[i].threads_reserved = 0;
        work_queue.jobs = jobs + i;
    }

    bool nested_parallelism =
        work_queue.owners_sleeping ||
        (work_queue.workers_sleeping < work_queue.threads_created);

    log_message(" nested parallelism: " << nested_parallelism);

    // Wake up an appropriate number of threads
    if (nested_parallelism || workers_to_wake > work_queue.workers_sleeping) {
        // If there's nested parallelism going on, we just wake up
        // everyone. TODO: make this more precise.
        work_queue.target_a_team_size = work_queue.threads_created;
    } else {
        work_queue.target_a_team_size = workers_to_wake;
    }
    log_message(" A team size: " << work_queue.a_team_size);
    log_message(" Target A team size: " << work_queue.target_a_team_size);
// TODO(zvookin): test with this back in.
#if 1
    halide_cond_broadcast(&work_queue.wake_a_team);
    if (work_queue.target_a_team_size > work_queue.a_team_size) {
        halide_cond_broadcast(&work_queue.wake_b_team);
        if (stealable_jobs) {
            halide_cond_broadcast(&work_queue.wake_owners);
        }
    }
#else
    halide_cond_broadcast(&work_queue.wake_a_team);
    halide_cond_broadcast(&work_queue.wake_b_team);
    halide_cond_broadcast(&work_queue.wake_owners);
#endif    
}

WEAK halide_do_task_t custom_do_task = halide_default_do_task;
WEAK halide_do_loop_task_t custom_do_loop_task = halide_default_do_loop_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_do_parallel_tasks_t custom_do_parallel_tasks = halide_default_do_parallel_tasks;
WEAK halide_semaphore_init_t custom_semaphore_init = halide_default_semaphore_init;
WEAK halide_semaphore_try_acquire_t custom_semaphore_try_acquire = halide_default_semaphore_try_acquire;
WEAK halide_semaphore_release_t custom_semaphore_release = halide_default_semaphore_release;
 
}}}  // namespace Halide::Runtime::Internal

using namespace Halide::Runtime::Internal;

extern "C" {

namespace {
__attribute__((destructor))
WEAK void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}

WEAK int halide_default_do_task(void *user_context, halide_task_t f, int idx,
                                uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK int halide_default_do_loop_task(void *user_context, halide_loop_task_t f,
                                     int min, int extent, uint8_t *closure) {
    return f(user_context, min, extent, closure);
}

WEAK int halide_default_do_par_for(void *user_context, halide_task_t f,
                                   int min, int size, uint8_t *closure) {
    if (size <= 0) {
        return 0;
    }

    work job;
    job.task.fn = NULL;
    job.task.min = min;
    job.task.extent = size;
    job.task.may_block = false;
    job.task.serial = false;
    job.task.semaphores = NULL;
    job.task.num_semaphores = 0;
    job.task.closure = closure;
    job.task.min_threads = 1;
    job.task.name = NULL;
    job.task_fn = f;
    job.user_context = user_context;
    job.exit_status = 0;
    job.active_workers = 0;
    job.next_semaphore = 0;
    job.owner_is_sleeping = false;
#if TLS_PARENT_LINK
    work_queue_t::thread_state *state = work_queue.get_thread_state();
    job.parent_job = (state->job_stack != NULL) ? state->job_stack->job : NULL;
    log_message("Parenting job " << &job << " with " << job.parent);
#endif
    halide_mutex_lock(&work_queue.mutex);
    log_message("halide_default_do_par_for: Parenting job " << &job << " with " << job.parent);
    enqueue_work_already_locked(1, &job);
    worker_thread_already_locked(&job);
    log_message("halide_default_do_par_for: Destructing job " << &job << " with parent " << job.parent);
    halide_mutex_unlock(&work_queue.mutex);
    return job.exit_status;
}

WEAK int halide_default_do_parallel_tasks(void *user_context, int num_tasks,
                                          struct halide_parallel_task_t *tasks) {
    work *jobs = (work *)__builtin_alloca(sizeof(work) * num_tasks);

    for (int i = 0; i < num_tasks; i++) {
        if (tasks->extent <= 0) {
            // Skip extent zero jobs
            num_tasks--;
            continue;
        }
        jobs[i].task = *tasks++;
        jobs[i].task_fn = NULL;
        jobs[i].user_context = user_context;
        jobs[i].exit_status = 0;
        jobs[i].active_workers = 0;
        jobs[i].next_semaphore = 0;
        jobs[i].owner_is_sleeping = false;
    }

    if (num_tasks == 0) {
        return 0;
    }

    halide_mutex_lock(&work_queue.mutex);
    enqueue_work_already_locked(num_tasks, jobs);
    int exit_status = 0;
    for (int i = 0; i < num_tasks; i++) {
#if TLS_PARENT_LINK
      log_message(" Joining task " << jobs[i].task.name << " with parent " << jobs[i].parent_job);
#else
      log_message(" Joining task " << jobs[i].task.name);
#endif
        // It doesn't matter what order we join the tasks in, because
        // we'll happily assist with siblings too.
        worker_thread_already_locked(jobs + i);
        if (jobs[i].exit_status != 0) {
            exit_status = jobs[i].exit_status;
        }
    }
    halide_mutex_unlock(&work_queue.mutex);
    return exit_status;
}

WEAK int halide_set_num_threads(int n) {
    if (n < 0) {
        halide_error(NULL, "halide_set_num_threads: must be >= 0.");
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
    log_message("halide_default_semaphore_init " << s << " " << n);
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
#if 0
    sem->value = n;
#else    
    Halide::Runtime::Internal::Synchronization::atomic_store_release(&sem->value, &n);
#endif
    return n;
}

WEAK int halide_default_semaphore_release(halide_semaphore_t *s, int n) {
  log_message("halide_default_semaphore_release " << s << " " << n);
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
#if 0
    int new_val = __sync_add_and_fetch(&(sem->value), n);
    if (new_val == n) {
        // We may have just made a job runnable
        halide_cond_broadcast(&work_queue.wake_a_team);
        halide_cond_broadcast(&work_queue.wake_owners);
    }
    return new_val;
#else
    int old_val = Halide::Runtime::Internal::Synchronization::atomic_fetch_and_add_acquire_release(&sem->value, n);
    if (old_val == 0) {
      log_message("halide_default_semaphore_release sending wakeups " << s << " " << n);
        // We may have just made a job runnable
        halide_mutex_lock(&work_queue.mutex);
        halide_cond_broadcast(&work_queue.wake_a_team);
        //        halide_cond_broadcast(&work_queue.wake_b_team);
        halide_cond_broadcast(&work_queue.wake_owners);
        halide_mutex_unlock(&work_queue.mutex);
    }
    return old_val + n;
#endif
}

WEAK bool halide_default_semaphore_try_acquire(halide_semaphore_t *s, int n) {
  log_message("halide_default_semaphore_try_acquire " << s << " " << n);
    halide_semaphore_impl_t *sem = (halide_semaphore_impl_t *)s;
    // Decrement and get new value
#if 0
    int new_val = __sync_add_and_fetch(&(sem->value), -n);
    if (new_val < 0) {
        // Oops, increment and return failure
        __sync_add_and_fetch(&(sem->value), n);
        return false;
    }
    return true;
#else
    int expected;
    int desired;
    Halide::Runtime::Internal::Synchronization::atomic_load_acquire(&sem->value, &expected);
    do {
        desired = expected - n;
    } while (desired >= 0 &&
             !Halide::Runtime::Internal::Synchronization::atomic_cas_weak_relacq_relaxed(&sem->value, &expected, &desired));
    log_message("halide_default_semaphore_try_acquire result " << (desired >= 0) <<  " sema: " << s << " " << n);
    return desired >= 0;
#endif    
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
                             int min, int size, uint8_t *closure){
    return custom_do_loop_task(user_context, f, min, size, closure);
}

WEAK int halide_do_parallel_tasks(void *user_context, int num_tasks,
                                  struct halide_parallel_task_t *tasks) {
    return custom_do_parallel_tasks(user_context, num_tasks, tasks);
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
