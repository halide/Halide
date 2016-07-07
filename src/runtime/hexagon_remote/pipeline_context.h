#ifndef HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H
#define HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H

#include <HalideRuntime.h>

#include <qurt.h>

#include "HAP_power.h"
#include "HAP_perf.h"

// We can't control the stack size on the thread which receives our
// FastRPC calls. To work around this, we make our own thread, and
// forward the calls to that thread. That thread is managed by the
// following context class. This context also manages turning HVX
// power on and off as needed.
typedef int (*pipeline_argv_t)(void **);

class PipelineContext {
    void *stack, *watchdog_stack;
    qurt_thread_t thread, watchdog_thread;
    qurt_cond_t wakeup_thread, wakeup_caller;
    qurt_mutex_t work_mutex;

    // Shared state
    pipeline_argv_t function;
    void **args;
    int result;
    bool running;
    uint64_t last_did_work_time_us;
    bool hvx_powered;

    // Turn HVX power on if it isn't already on. Requires that the
    // work_mutex is held.
    int power_on_hvx_already_locked() {
        if (hvx_powered) return 0;

        HAP_power_request_t request;

        request.type = HAP_power_set_apptype;
        request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
        int retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_apptype) failed (%d)\n", retval);
            return -1;
        }

        request.type = HAP_power_set_HVX;
        request.hvx.power_up = TRUE;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_HVX) failed (%d)\n", retval);
            return -1;
        }

        request.type = HAP_power_set_mips_bw;
        request.mips_bw.set_mips = TRUE;
        request.mips_bw.mipsPerThread = 500;
        request.mips_bw.mipsTotal = 1000;
        request.mips_bw.set_bus_bw = TRUE;
        request.mips_bw.bwBytePerSec = static_cast<uint64_t>(12000) * 1000000;
        request.mips_bw.busbwUsagePercentage = 100;
        request.mips_bw.set_latency = TRUE;
        request.mips_bw.latency = 1;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_mips_bw) failed (%d)\n", retval);
            return -1;
        }

        hvx_powered = true;
        return 0;
    }

    // Turn HVX power off if it's on. Requires that the work_mutex is
    // held.
    void power_off_hvx_already_locked() {
        if (!hvx_powered) return;
        HAP_power_request(0, 0, -1);
        hvx_powered = false;
    }

    void thread_main() {
        qurt_mutex_lock(&work_mutex);
        while (running) {
            qurt_cond_wait(&wakeup_thread, &work_mutex);
            if (function) {
                power_on_hvx_already_locked();
                result = function(args);
                last_did_work_time_us = HAP_perf_get_time_us();
                function = NULL;
                qurt_cond_signal(&wakeup_caller);
            }
        }
        qurt_mutex_unlock(&work_mutex);
    }

    void thread_watchdog() {
        qurt_mutex_lock(&work_mutex);
        // Every 100ms, wake up and check if HVX is powered on but no
        // useful work has been done for 5 seconds. If so, turn off
        // HVX.
        const uint64_t poll_rate_us = 100 * 1000;
        const uint64_t timeout_us = 5 * 1000 * 1000;
        while (running) {
            if (hvx_powered) {
                uint64_t current_time = HAP_perf_get_time_us();
                if (current_time > last_did_work_time_us + timeout_us) {
                    power_off_hvx_already_locked();
                }
            }
            // Sleep for 100 ms. We could sleep for longer (5 seconds
            // minus time since last use), but we also need to wake up
            // periodically to check if we should exit.
            qurt_mutex_unlock(&work_mutex);
            qurt_timer_sleep(poll_rate_us);
            qurt_mutex_lock(&work_mutex);
        }
        qurt_mutex_unlock(&work_mutex);
    }

    static void redirect_main(void *data) {
        static_cast<PipelineContext *>(data)->thread_main();
    }

    static void redirect_watchdog(void *data) {
        static_cast<PipelineContext *>(data)->thread_watchdog();
    }

public:
    PipelineContext(int stack_alignment, int stack_size)
        : stack(NULL), function(NULL), args(NULL), running(true), hvx_powered(false) {
        qurt_mutex_init(&work_mutex);
        qurt_cond_init(&wakeup_thread);
        qurt_cond_init(&wakeup_caller);

        last_did_work_time_us = HAP_perf_get_time_us();

        // Allocate the stack for this thread.
        stack = memalign(stack_alignment, stack_size);

        qurt_thread_attr_t thread_attr;
        qurt_thread_attr_init(&thread_attr);
        qurt_thread_attr_set_stack_addr(&thread_attr, stack);
        qurt_thread_attr_set_stack_size(&thread_attr, stack_size);
        qurt_thread_attr_set_priority(&thread_attr, 100);
        qurt_thread_create(&thread, &thread_attr, redirect_main, this);

        // Also make a low-priority watchdog thread to periodically
        // wake up the worker thread, so that HVX can get powered off
        // when not in use. One page is sufficient stack for it.
        watchdog_stack = memalign(stack_alignment, 4096);
        qurt_thread_attr_t watchdog_thread_attr;
        qurt_thread_attr_init(&watchdog_thread_attr);
        qurt_thread_attr_set_stack_addr(&watchdog_thread_attr, watchdog_stack);
        qurt_thread_attr_set_stack_size(&watchdog_thread_attr, 4096);
        qurt_thread_attr_set_priority(&watchdog_thread_attr, 255);
        qurt_thread_create(&watchdog_thread, &watchdog_thread_attr, redirect_watchdog, this);
    }

    // Turn on power to the HVX units. Does nothing if it's already on.
    int power_on_hvx() {
        qurt_mutex_lock(&work_mutex);
        int ret = power_on_hvx_already_locked();
        qurt_mutex_unlock(&work_mutex);
        return ret;
    }

    // Turn off power to the HVX units. Does nothing if it's already off.
    void power_off_hvx() {
        qurt_mutex_lock(&work_mutex);
        power_off_hvx_already_locked();
        qurt_mutex_unlock(&work_mutex);
    }

    ~PipelineContext() {
        // Running a null function kills the thread.
        qurt_mutex_lock(&work_mutex);
        running = false;
        qurt_cond_signal(&wakeup_thread);
        qurt_mutex_unlock(&work_mutex);

        int status;
        qurt_thread_join(thread, &status);
        // This delays process exit by up to the poll interval of the
        // watchdog thread. Probably doesn't matter.
        qurt_thread_join(watchdog_thread, &status);

        qurt_cond_destroy(&wakeup_thread);
        qurt_cond_destroy(&wakeup_caller);
        qurt_mutex_destroy(&work_mutex);

        free(stack);
        free(watchdog_stack);
    }

    int run(pipeline_argv_t function, void **args) {
        // get a lock and set up work for the worker.
        qurt_mutex_lock(&work_mutex);
        this->function = function;
        this->args = args;
        // send a signal to the worker.
        qurt_cond_signal(&wakeup_thread);

        // Wait for the worker's signal that it is done.
        while (this->function != NULL) {
            qurt_cond_wait(&wakeup_caller, &work_mutex);
        }
        int result = this->result;
        qurt_mutex_unlock(&work_mutex);
        return result;
    }
};

#endif
