#ifndef _H_SimpleAppAPI
#define _H_SimpleAppAPI

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

}  // extern "C"


// This set of macros may be used to placing timing code around a halide
// function to be benchmarked.
#ifdef __APPLE__
#define PROFILE_SHADER 1

// In a program that calls the halide function repeatedly, e.g. once per frame
// set the number of iterations to compute statistics over.
#define PROFILE_ITERATIONS 1
#ifdef PROFILE_SHADER
#import <mach/mach_time.h>

#define PROFILE_BEGIN(msg) \
{\
  uint64_t begin_time = mach_absolute_time();

// This macro is used to stop timing a section of code. If PROFILE_ITERATIONS
// have occurred, the average, min, and max time will be printed to the console
#define PROFILE_END(msg,w,h) \
  uint64_t end_time = mach_absolute_time();\
\
  static mach_timebase_info_data_t info = { 0, 0 };\
  if (!info.denom) {\
    mach_timebase_info(&info);\
  }\
  static float totalDuration = 0;\
  static float minDuration = 1000.0f;\
  static float maxDuration = 0.0f;\
  static int iterations = 0;\
\
  float duration = ((end_time-begin_time)*info.numer)/info.denom * 1e-9;\
  totalDuration += duration;\
  minDuration = (duration < minDuration) ? duration : minDuration;\
  maxDuration = (duration > maxDuration) ? duration : maxDuration;\
\
  if (!(++iterations % PROFILE_ITERATIONS)) {\
\
    const size_t pixelsPerFrame = (w) * (h);\
    float Mpixelpers = (((float)pixelsPerFrame*(float)PROFILE_ITERATIONS)/1e6) / totalDuration;\
    float avgDuration = totalDuration / (float)PROFILE_ITERATIONS;\
\
    NSLog(@"H>> %s %3.3f Mpixels/sec %3.3f avg sec %3.3f min %3.3f max (%d iterations)\n", \
      #msg, Mpixelpers, avgDuration, minDuration, maxDuration, \
      PROFILE_ITERATIONS);\
\
    totalDuration = 0;\
    minDuration = 1000.0f;\
    maxDuration = 0.0f;\
    iterations = 0;\
  }\
}
#else
#define PROFILE_BEGIN(msg)
#define PROFILE_END(msg)
#endif
#endif

#endif  // _H_SimpleAppAPI
