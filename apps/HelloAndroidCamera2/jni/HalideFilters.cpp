#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "native", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "native", __VA_ARGS__)

#include "AndroidBufferUtilities.h"
#include "deinterleave.h"
#include "edge_detect.h"
#include "HalideRuntime.h"
#include "YuvBufferT.h"

#define DEBUG 1

// Extern functions from the Halide runtime that are not exposed in
// HalideRuntime.h.
extern "C" int halide_host_cpu_count();
extern "C" int64_t halide_current_time_ns();

// Override Halide's print to use LOGD and also print the time.
extern "C" void halide_print(void *, const char *msg) {
    static int64_t t0 = halide_current_time_ns();
    int64_t t1 = halide_current_time_ns();
    LOGD("%d: %s\n", (int)(t1 - t0)/1000000, msg);
    t0 = t1;
}

extern "C" {

bool checkEqualExtents(YuvBufferT *src, YuvBufferT *dst) {
    if (src->luma().width()  != dst->luma().width() ||
        src->luma().height() != dst->luma().height() ||
        src->chromaU().width()  != dst->chromaU().width() ||
        src->chromaU().height() != dst->chromaU().height() ||
        src->chromaV().width()  != dst->chromaV().width() ||
        src->chromaV().height() != dst->chromaV().height()) {

        LOGE("failed: src and dst extents must be equal.\n\t"
            "src extents: luma: %d, %d, chromaU: %d, %d, chromaV: %d, %d.\n\t"
            "dst extents: luma: %d, %d, chromaU: %d, %d, chromaV: %d, %d.",
            src->luma().width(), src->luma().height(),
            src->chromaU().width(), src->chromaU().height(),
            src->chromaV().width(), src->chromaV().height(),
            dst->luma().width(), dst->luma().height(),
            dst->chromaU().width(), dst->chromaU().height(),
            dst->chromaV().width(), dst->chromaV().height());
        return false;
    } else {
        return true;
    }
}

JNIEXPORT bool JNICALL Java_com_example_helloandroidcamera2_HalideFilters_copyHalide(
    JNIEnv *env, jobject obj, jlong srcYuvBufferTHandle, jlong dstYuvBufferTHandle) {
    if (srcYuvBufferTHandle == 0L || dstYuvBufferTHandle == 0L ) {
        LOGE("copyHalide failed: src and dst must not be null");
        return false;
    }

    YuvBufferT *src = reinterpret_cast<YuvBufferT *>(srcYuvBufferTHandle);
    YuvBufferT *dst = reinterpret_cast<YuvBufferT *>(dstYuvBufferTHandle);

    if (!checkEqualExtents(src, dst)) {
        return false;
    }

    YuvBufferT::ChromaStorage srcChromaStorage = src->chromaStorage();
    YuvBufferT::ChromaStorage dstChromaStorage = dst->chromaStorage();

    bool succeeded;
    int halideErrorCode;

    // Use Halide deinterleave if the source chroma is interleaved and destination chroma is planar.
    // Other, fall back to slow copy.
    if ((srcChromaStorage == YuvBufferT::ChromaStorage::kInterleavedUFirst ||
         srcChromaStorage == YuvBufferT::ChromaStorage::kInterleavedVFirst) &&
         (dstChromaStorage == YuvBufferT::ChromaStorage::kPlanarPackedUFirst ||
          dstChromaStorage == YuvBufferT::ChromaStorage::kPlanarPackedVFirst ||
          dstChromaStorage == YuvBufferT::ChromaStorage::kPlanarGeneric)) {

        // Copy the luma channel directly.
        dst->luma().copy_from(src->luma());

        // Use Halide to deinterleave the chroma channels.
        auto srcInterleavedChroma = src->interleavedChromaView();
        auto dstPlanarChromaU = dst->chromaU();
        auto dstPlanarChromaV = dst->chromaV();
        if (srcChromaStorage == YuvBufferT::ChromaStorage::kInterleavedUFirst) {
            halideErrorCode = deinterleave(srcInterleavedChroma,
                                           dstPlanarChromaU,
                                           dstPlanarChromaV);
        } else {
            halideErrorCode = deinterleave(srcInterleavedChroma,
                                           dstPlanarChromaV,
                                           dstPlanarChromaU);
        }
        succeeded = (halideErrorCode != halide_error_code_success);
        if (halideErrorCode != halide_error_code_success) {
            LOGE("deinterleave failed with error code: %d", halideErrorCode);
        }
    } else {
        (*dst).copy_from(*src);
    }

    return succeeded;
}

JNIEXPORT bool JNICALL Java_com_example_helloandroidcamera2_HalideFilters_edgeDetectHalide(
    JNIEnv *env, jobject obj, jlong srcYuvBufferTHandle, jlong dstYuvBufferTHandle) {
    if (srcYuvBufferTHandle == 0L || dstYuvBufferTHandle == 0L ) {
        LOGE("edgeDetectHalide failed: src and dst must not be null");
        return false;
    }

    YuvBufferT *src = reinterpret_cast<YuvBufferT *>(srcYuvBufferTHandle);
    YuvBufferT *dst = reinterpret_cast<YuvBufferT *>(dstYuvBufferTHandle);

    if (!checkEqualExtents(src, dst)) {
        return false;
    }

    static bool first_call = true;
    static unsigned counter = 0;
    static unsigned times[16];
    if (first_call) {
        LOGD("According to Halide, host system has %d cpus\n",
             halide_host_cpu_count());
        first_call = false;
        for (int t = 0; t < 16; t++) {
            times[t] = 0;
        }
    }

    // Set chrominance to 128 to appear grayscale.
    dst->fillUV(128, 128);

    auto srcLuma = src->luma();
    auto dstLuma = dst->luma();
    int64_t t1 = halide_current_time_ns();
    int err = edge_detect(srcLuma, dstLuma);
    if (err != halide_error_code_success) {
        LOGE("edge_detect failed with error code: %d", err);
    }

    int64_t t2 = halide_current_time_ns();
    unsigned elapsed_us = (t2 - t1) / 1000;

    times[counter & 15] = elapsed_us;
    counter++;
    unsigned min = times[0];
    for (int i = 1; i < 16; i++) {
        if (times[i] < min) {
            min = times[i];
        }
    }
    LOGD("Time taken: %d us (minimum: %d us)", elapsed_us, min);

    return (err != halide_error_code_success);
}

} // extern "C"
