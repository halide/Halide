#ifndef HANNK_DELEGATE_H
#define HANNK_DELEGATE_H

#if !HANNK_BUILD_TFLITE_DELEGATE
#error "This file should not be included when HANNK_BUILD_TFLITE_DELEGATE=0"
#endif

#include "tensorflow/lite/c/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Use HannkDelegateOptionsDefault() for Default options.
struct TFL_CAPI_EXPORT HannkDelegateOptions {
    // Verbosity to use.
    // 0 means "only bare minimum TFKERNEL logs, etc"
    // 1 means "also do HLOG(INFO)"
    // higher numbers may produce additional output
    int verbosity;

#ifdef __cplusplus
    HannkDelegateOptions()
        : verbosity(0) {
    }
#endif
};

// Return a delegate that uses hannk for ops execution.
// Must outlive the interpreter.
TFL_CAPI_EXPORT
TfLiteDelegate *HannkDelegateCreate(const HannkDelegateOptions *options);

// Returns HannkDelegateOptions populated with default values.
TFL_CAPI_EXPORT
void HannkDelegateOptionsDefault(HannkDelegateOptions *options);

// Do any needed cleanup and delete 'delegate'.
TFL_CAPI_EXPORT
void HannkDelegateDelete(TfLiteDelegate *delegate);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // HANNK_DELEGATE_H
