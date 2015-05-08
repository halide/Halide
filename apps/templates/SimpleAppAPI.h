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

#endif  // _H_SimpleAppAPI
