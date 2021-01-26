#ifndef DELEGATE_HALIDE_DELEGATE_H_
#define DELEGATE_HALIDE_DELEGATE_H_

#include "tensorflow/lite/c/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Use HalideDelegateOptionsDefault() for Default options.
struct TFL_CAPI_EXPORT HalideDelegateOptions {
    // Verbosity to use.
    // 0 means "only bare minimum TFKERNEL logs, etc"
    // 1 means "also do LOG(INFO)"
    // higher numbers may produce additional output
    int verbosity;

#ifdef __cplusplus
    HalideDelegateOptions()
        : verbosity(1) {
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
