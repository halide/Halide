#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "thread_pool_lost_wakeup.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// Regression test for a lost wakeup in the thread pool.
//
// Thread A owns a group of two serial tasks that hand off to each other
// through semaphores. The main thread X runs a parallel loop whose last
// iteration is held up until X has stolen one of A's serial tasks, so that
// X's own loop finishes while X is running that task. When the task's
// semaphore runs dry, X puts it back and returns. If the release that makes
// it runnable again lands just before that, it wakes A while the task is
// still in use, and A goes back to sleep. Nothing else will wake A, so the
// group never finishes.

using namespace std::chrono;

namespace {

constexpr int kThreads = 4;
constexpr int kTrials = 2000;
constexpr int kHandoffs = 64;
constexpr auto kHangTimeout = seconds(10);

thread_local bool is_main_thread = false;
std::atomic<bool> main_thread_in_group{false};
std::atomic<bool> group_started{false};
std::atomic<bool> group_done{false};
std::atomic<int> holders_entered{0};
std::atomic<int> progress{0};

struct PingPong {
    halide_semaphore_t data, space;
    int value;
};

void note_task_running() {
    progress++;
    group_started = true;
    if (is_main_thread) {
        main_thread_in_group = true;
    }
}

int producer(void *, int min, int extent, uint8_t *closure, void *) {
    PingPong *p = (PingPong *)closure;
    note_task_running();
    for (int i = min; i < min + extent; i++) {
        p->value = i;
        halide_semaphore_release(&p->data, 1);
    }
    return 0;
}

int consumer(void *, int min, int extent, uint8_t *closure, void *) {
    PingPong *p = (PingPong *)closure;
    note_task_running();
    for (int i = min; i < min + extent; i++) {
        if (p->value != i) {
            printf("Consumer saw %d instead of %d\n", p->value, i);
            exit(1);
        }
        halide_semaphore_release(&p->space, 1);
    }
    return 0;
}

void run_group() {
    PingPong p;
    p.value = -1;
    halide_semaphore_init(&p.data, 0);
    halide_semaphore_init(&p.space, 1);
    halide_semaphore_acquire_t acquire_data = {&p.data, 1};
    halide_semaphore_acquire_t acquire_space = {&p.space, 1};

    halide_parallel_task_t tasks[2] = {};
    tasks[0].fn = consumer;
    tasks[0].closure = (uint8_t *)&p;
    tasks[0].name = "consumer";
    tasks[0].semaphores = &acquire_data;
    tasks[0].num_semaphores = 1;
    tasks[0].extent = kHandoffs;
    tasks[0].serial = true;
    tasks[1].fn = producer;
    tasks[1].closure = (uint8_t *)&p;
    tasks[1].name = "producer";
    tasks[1].semaphores = &acquire_space;
    tasks[1].num_semaphores = 1;
    tasks[1].extent = kHandoffs;
    tasks[1].serial = true;
    if (halide_do_parallel_tasks(nullptr, 2, tasks, nullptr) != halide_error_code_success) {
        printf("halide_do_parallel_tasks failed\n");
        exit(1);
    }
}

// The main thread's iteration waits briefly for a worker to take the other
// one, which then keeps the loop alive until the main thread is running one
// of the group's tasks (or gives up, if that doesn't happen soon).
int holder(void *, int, uint8_t *) {
    holders_entered++;
    auto start = steady_clock::now();
    if (is_main_thread) {
        while (holders_entered < 2 && steady_clock::now() - start < milliseconds(1)) {
            std::this_thread::yield();
        }
    } else {
        while (!main_thread_in_group && !group_done &&
               steady_clock::now() - start < milliseconds(5)) {
            std::this_thread::yield();
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<int> out(8);
    if (thread_pool_lost_wakeup(out) != 0) {
        printf("Pipeline failed\n");
        return 1;
    }

    is_main_thread = true;
    halide_set_num_threads(kThreads);

    for (int trial = 0; trial < kTrials; trial++) {
        main_thread_in_group = false;
        group_started = false;
        group_done = false;
        holders_entered = 0;

        std::thread owner([]() {
            run_group();
            group_done = true;
        });
        while (!group_started && !group_done) {
            std::this_thread::yield();
        }

        if (halide_do_par_for(nullptr, holder, 0, 2, nullptr) != halide_error_code_success) {
            printf("halide_do_par_for failed\n");
            return 1;
        }

        int last_progress = progress;
        auto last_change = steady_clock::now();
        while (!group_done) {
            std::this_thread::sleep_for(microseconds(50));
            if (progress != last_progress) {
                last_progress = progress;
                last_change = steady_clock::now();
            } else if (steady_clock::now() - last_change > kHangTimeout) {
                printf("Trial %d: the group's owner was never woken\n", trial);
                fflush(stdout);
                std::_Exit(1);
            }
        }
        owner.join();
    }

    printf("Success!\n");
    return 0;
}
