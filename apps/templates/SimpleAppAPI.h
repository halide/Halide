#ifndef _H_SimpleAppAPI
#define _H_SimpleAppAPI

#include <stdarg.h>

#include <random>
#include <vector>

#include "HalideRuntime.h"

extern "C" {

// Note that in addition to the calls in this file, the standard Halide runtime
// calls halide_error() and halide_print() are captured in a platform
// specific manner by the standard Simple App code.

// This function outputs the buffer in text form in a platform specific manner.
int halide_buffer_print(const buffer_t* buffer);

// This function outputs the buffer as an image in a platform specific manner.
// For example, in a web based application the buffer contents might be
// displayed as a png image.
int halide_buffer_display(const buffer_t* buffer);

// These aren't "official" public runtime API, so please don't use them outside of this code.
int halide_start_clock(void *user_context);
int64_t halide_current_time_ns(void *user_context);

}  // extern "C"

// VarArg wrapper around halide_print()
void halide_printf(void* user_context, const char* fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  halide_print(user_context, buffer);
  va_end(args);
}

// VarArg wrapper around halide_error()
void halide_errorf(void* user_context, const char* fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  halide_error(user_context, buffer);
  va_end(args);
}

class SimpleTimer {
 public:
  SimpleTimer(void *user_context)
      : user_context_(user_context), time_net_(0), time_start_(0), running_(false) {
    halide_start_clock(user_context_);
  }
  void start() {
    if (!running_) {
      time_start_ = halide_current_time_ns(user_context_);
      running_ = true;
    }
  }
  void stop() {
    if (running_) {
      int64_t time_end = halide_current_time_ns(user_context_);
      time_net_ += (time_end - time_start_);
      time_start_ = 0;
      running_ = false;
    }
  }
  void reset() {
    time_net_ = 0;
    time_start_ = 0;
    running_ = false;
  }
  int64_t net_nsec() { return running_ ? 0 : time_net_; }
  double net_usec() { return running_ ? 0 : (double) time_net_ / 1e3; }
  double net_msec() { return running_ ? 0 : (double)time_net_ / 1e6; }
  double net_sec() { return running_ ? 0 : (double)time_net_ / 1e9; }
 private:
  void *user_context_;
  int64_t time_net_;
  int64_t time_start_;
  bool running_;
};

class ScopedTimer {
 public:
  ScopedTimer(void *user_context, const char *msg)
      : user_context_(user_context), timer_(user_context), msg_(msg) {
    timer_.start();
  }
  ~ScopedTimer() {
    timer_.stop();
    halide_printf(user_context_, "%s: %f usec", msg_, timer_.net_usec());
  }
 private:
  void *user_context_;
  SimpleTimer timer_;
  const char *msg_;
};


// Fill the buffer's host storage field with random data of the given type
// in the given range. Not very rigorous, but convenient for simple testing
// or profiling.
// If the buffer's elem_size doesn't match the size, or if host is null,
// return false.
template<typename T>
bool halide_randomize_buffer_host(int seed, T min, T max, buffer_t* buf) {
    if (sizeof(T) != buf->elem_size) {
        return false;
    }
    if (!buf->host) {
      return false;
    }
    std::mt19937 rnd(seed);
    T *p0 = reinterpret_cast<T*>(buf->host);
    for (int i0 = 0; i0 < std::max(1, buf->extent[0]); ++i0) {
        T *p1 = p0;
        for (int i1 = 0; i1 < std::max(1, buf->extent[1]); ++i1) {
            T *p2 = p1;
            for (int i2 = 0; i2 < std::max(1, buf->extent[2]); ++i2) {
                T *p3 = p2;
                for (int i3 = 0; i3 < std::max(1, buf->extent[3]); ++i3) {
                    *p3 = min + (T) (((double) rnd() / (double) 0xffffffff) * (max - min));
                    p3 += buf->stride[3];
                }
                p2 += buf->stride[2];
            }
            p1 += buf->stride[1];
        }
        p0 += buf->stride[0];
    }
    buf->host_dirty = true;
    return true;
}

#endif  // _H_SimpleAppAPI
