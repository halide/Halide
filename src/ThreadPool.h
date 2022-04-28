#ifndef HALIDE_THREAD_POOL_H
#define HALIDE_THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#ifdef _MSC_VER
#else
#include <unistd.h>
#endif

/** \file
 * Define a simple thread pool utility that is modeled on the api of
 * std::async(); since implementation details of std::async
 * can vary considerably, with no control over thread spawning, this class
 * allows us to use the same model but with precise control over thread usage.
 *
 * A ThreadPool is created with a specific number of threads, which will never
 * vary over the life of the ThreadPool. (If created without a specific number
 * of threads, it will attempt to use threads == number-of-cores.)
 *
 * Each async request will go into a queue, and will be serviced by the next
 * available thread from the pool.
 *
 * The ThreadPool's dtor will block until all currently-executing tasks
 * to finish (but won't schedule any more).
 *
 * Note that this is a fairly simpleminded ThreadPool, meant for tasks
 * that are fairly coarse (e.g. different tasks in a test); it is specifically
 * *not* intended to be the underlying implementation for Halide runtime threads
 */
namespace Halide {
namespace Internal {

template<typename T>
class ThreadPool {
    struct Job {
        std::function<T()> func;
        std::promise<T> result;

        void run_unlocked(std::unique_lock<std::mutex> &unique_lock);
    };

    // all fields are protected by this mutex.
    std::mutex mutex;

    // Queue of Jobs.
    std::queue<Job> jobs;

    // Broadcast whenever items are added to the Job queue.
    std::condition_variable wakeup_threads;

    // Keep track of threads so they can be joined at shutdown
    std::vector<std::thread> threads;

    // True if the pool is shutting down.
    bool shutting_down{false};

    void worker_thread() {
        std::unique_lock<std::mutex> unique_lock(mutex);
        while (!shutting_down) {
            if (jobs.empty()) {
                // There are no jobs pending. Wait until more jobs are enqueued.
                wakeup_threads.wait(unique_lock);
            } else {
                // Grab the next job.
                Job cur_job = std::move(jobs.front());
                jobs.pop();
                cur_job.run_unlocked(unique_lock);
            }
        }
    }

public:
    static size_t num_processors_online() {
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        return num_cores ? atoi(num_cores) : 8;
#else
        return sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }

    // Default to number of available cores if not specified otherwise
    ThreadPool(size_t desired_num_threads = num_processors_online()) {
        // This file doesn't depend on anything else in libHalide, so
        // we'll use assert, not internal_assert.
        assert(desired_num_threads > 0);

        std::lock_guard<std::mutex> lock(mutex);

        // Create all the threads.
        for (size_t i = 0; i < desired_num_threads; ++i) {
            threads.emplace_back([this] { worker_thread(); });
        }
    }

    ~ThreadPool() {
        // Wake everyone up and tell them the party's over and it's time to go home
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutting_down = true;
            wakeup_threads.notify_all();
        }

        // Wait until they leave
        for (auto &t : threads) {
            t.join();
        }
    }

    template<typename Func, typename... Args>
    std::future<T> async(Func func, Args... args) {
        std::lock_guard<std::mutex> lock(mutex);

        Job job;
        // Don't use std::forward here: we never want args passed by reference,
        // since they will be accessed from an arbitrary thread.
        //
        // Some versions of GCC won't allow capturing variadic arguments in a lambda;
        //
        //     job.func = [func, args...]() -> T { return func(args...); };  // Nope, sorry
        //
        // fortunately, we can use std::bind() to accomplish the same thing.
        job.func = std::bind(func, args...);
        jobs.emplace(std::move(job));
        std::future<T> result = jobs.back().result.get_future();

        // Wake up our threads.
        wakeup_threads.notify_all();

        return result;
    }
};

template<typename T>
inline void ThreadPool<T>::Job::run_unlocked(std::unique_lock<std::mutex> &unique_lock) {
    unique_lock.unlock();
    T r = func();
    unique_lock.lock();
    result.set_value(std::move(r));
}

template<>
inline void ThreadPool<void>::Job::run_unlocked(std::unique_lock<std::mutex> &unique_lock) {
    unique_lock.unlock();
    func();
    unique_lock.lock();
    result.set_value();
}

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_THREAD_POOL_H
