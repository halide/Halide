#ifndef DELEGATE_HALIDE_DELEGATE_H_
#define DELEGATE_HALIDE_DELEGATE_H_

#include "tensorflow/lite/c/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Use HalideDelegateOptionsDefault() for Default options.
struct TFL_CAPI_EXPORT HalideDelegateOptions {
    // The max number of threads to use in Halide.
    // 0 means use the default (typically, host-cpu-count).
    //
    // TODO: should we use TfLiteContext.recommended_num_threads instead?
    int num_threads;

#ifdef __cplusplus
    HalideDelegateOptions()
        : num_threads(1) {
    }
#endif
};

// Return a delegate that uses Halide interpret_nn for ops execution.
// Must outlive the interpreter.
TFL_CAPI_EXPORT
TfLiteDelegate *HalideDelegateCreate(const HalideDelegateOptions *options);

// Returns HalideDelegateOptions populated with default values.
TFL_CAPI_EXPORT
void HalideDelegateOptionsDefault(HalideDelegateOptions *options);

// Do any needed cleanup and delete 'delegate'.
TFL_CAPI_EXPORT
void HalideDelegateDelete(TfLiteDelegate *delegate);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // DELEGATE_HALIDE_DELEGATE_H_
