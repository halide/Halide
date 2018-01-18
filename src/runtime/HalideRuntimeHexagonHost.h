#ifndef HALIDE_HALIDERUNTIMEHEXAGONHOST_H
#define HALIDE_HALIDERUNTIMEHEXAGONHOST_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide Hexagon host-side runtime.
 */

#define HALIDE_RUNTIME_HEXAGON

typedef int halide_hexagon_handle_t;

extern const struct halide_device_interface_t *halide_hexagon_device_interface();

/** Check if the Hexagon runtime (libhalide_hexagon_host.so) is
 * available. If it is not, pipelines using Hexagon will fail. */
extern bool halide_is_hexagon_available(void *user_context);

/** The device handle for Hexagon is simply a pointer and size, stored
 * in the dev field of the buffer_t. If the buffer is allocated in a
 * particular way (ion_alloc), the buffer will be shared with Hexagon
 * (not copied). The device field of the buffer_t must be NULL when this
 * routine is called. This call can fail due to running out of memory
 * or being passed an invalid device handle. The device and host
 * dirty bits are left unmodified. */
extern int halide_hexagon_wrap_device_handle(void *user_context, struct halide_buffer_t *buf,
                                             void *ptr, uint64_t size);

/** Disconnect this halide_buffer_t from the device handle it was
 * previously wrapped around. Should only be called for a
 * halide_buffer_t that halide_hexagon_wrap_device_handle was
 * previously called on. Frees any storage associated with the binding
 * of the halide_buffer_t and the device handle, but does not free the
 * device handle. The device field of the halide_buffer_t will be NULL
 * on return. */
extern int halide_hexagon_detach_device_handle(void *user_context, struct halide_buffer_t *buf);

/** Return the underlying device handle for a buffer_t. If there is
 * no device memory (dev field is NULL), this returns 0. */
extern void *halide_hexagon_get_device_handle(void *user_context, struct halide_buffer_t *buf);
extern uint64_t halide_hexagon_get_device_size(void *user_context, struct halide_buffer_t *buf);

/** Power HVX on and off. Calling a Halide pipeline will do this
 * automatically on each pipeline invocation; however, it costs a
 * small but possibly significant amount of time for short running
 * pipelines. To avoid this cost, HVX can be powered on prior to
 * running several pipelines, and powered off afterwards. If HVX is
 * powered on, subsequent calls to power HVX on will be cheap. */
// @{
extern int halide_hexagon_power_hvx_on(void *user_context);
extern int halide_hexagon_power_hvx_off(void *user_context);
extern void halide_hexagon_power_hvx_off_as_destructor(void *user_context, void * /* obj */);
// @}

/** Power modes for Hexagon. */
typedef enum halide_hexagon_power_mode_t {
    halide_hexagon_power_low     = 0,
    halide_hexagon_power_nominal = 1,
    halide_hexagon_power_turbo   = 2,
    halide_hexagon_power_default = 3, /// Resets power to its default state.

    // These are deprecated.
    halide_hvx_power_low     = halide_hexagon_power_low,
    halide_hvx_power_nominal = halide_hexagon_power_nominal,
    halide_hvx_power_turbo   = halide_hexagon_power_turbo,
    halide_hvx_power_default = halide_hexagon_power_default,
} halide_hexagon_power_mode_t;

/** More detailed power settings to control Hexagon.
 * @param set_mips - Set to TRUE to requst MIPS
 * @param mipsPerThread - mips requested per thread, to establish a minimal clock frequency per HW thread
 * @param mipsTotal - Total mips requested, to establish total number of MIPS required across all HW threads
 * @param set_bus_bw - Set to TRUE to request bus_bw
 * @param bwMeagabytesPerSec - Max bus BW requested (megabytes per second)
 * @param busbwUsagePercentage - Percentage of time during which bwBytesPerSec BW is required from the bus (0..100)
 * @param set_latency - Set to TRUE to set latency
 * @param latency - maximum hardware wakeup latency in microseconds.  The
 *                  higher the value the deeper state of sleep
 *                  that can be entered but the longer it may
 *                  take to awaken. Only values > 0 are supported (1 microsecond is the smallest valid value)
 */
typedef struct {
    bool set_mips;
    unsigned int mipsPerThread;
    unsigned int mipsTotal;
    bool set_bus_bw;
    unsigned int bwMegabytesPerSec;
    unsigned short busbwUsagePercentage;
    bool set_latency;
    int latency;
} halide_hexagon_power_t;

// This is deprecated.
typedef halide_hexagon_power_t halide_hvx_power_perf_t;

/** Set a performance target for Hexagon. Hexagon applications can
 * vote for the performance levels they want, which may or may not be
 * respected by Hexagon. Applications should be careful not to leave
 * Hexagon in a high power state for too long. These functions can
 * significantly increase standby power consumption. Use
 * halide_hexagon_power_default to reset performance to the default
 * power state. */
// @{
extern int halide_hexagon_set_performance_mode(void *user_context, halide_hexagon_power_mode_t mode);
extern int halide_hexagon_set_performance(void *user_context, halide_hexagon_power_t *perf);
// @}

/** These are forward declared here to allow clients to override the
 *  Halide Hexagon runtime. Do not call them. */
// @{
extern int halide_hexagon_initialize_kernels(void *user_context,
                                             void **module_ptr,
                                             const uint8_t *code, uint64_t code_size,
                                             const uint8_t *runtime, uint64_t runtime_size);
extern int halide_hexagon_run(void *user_context,
                              void *module_ptr,
                              const char *name,
                              halide_hexagon_handle_t *function,
                              uint64_t arg_sizes[],
                              void *args[],
                              int arg_flags[]);
extern int halide_hexagon_device_release(void* user_context);
// @}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEHEXAGONHOST_H
