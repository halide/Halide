#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "deinterleave.h"
#include "edge_detect.h"
#include "HalideRuntime.h"
#include "HalideRuntimeOpenCL.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "halide_native", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "halide_native", __VA_ARGS__)

#define DEBUG 1
#define IMAGE_FORMAT_YV12 842094169

// Round x up to a multiple of mask.
// E.g., ALIGN(x, 16) means round x up to the nearest multiple of 16.
#define ALIGN(x, mask) (((x) + (mask)-1) & ~((mask)-1))

extern "C" void halide_set_error_handler(int (*handler)(void *user_context, const char *));
extern "C" int halide_host_cpu_count();
extern "C" int halide_start_clock(void *user_context);
extern "C" int64_t halide_current_time_ns();
extern "C" int halide_copy_to_host(void *, buffer_t *);

int handler(void * /* user_context */, const char *msg) {
    LOGE("%s", msg);
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_helloandroidcamera2_JNIUtils_configureSurfaceNative(
    JNIEnv *env, jobject obj, jobject surface, int width, int height) {
    LOGD("[configureSurfaceNative] desired width = %d, height = %d", width,
         height);

    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    ANativeWindow_acquire(win);

#if 0
    ANativeWindow_Buffer buf;
    if (int err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGE("ANativeWindow_lock failed with error code %d\n", err);
        ANativeWindow_release(win);
        return;
    }
    LOGD("[configureSurfaceNative] locked buffer original width = %d,"
            "height = %d, row_stride_in_pixels = %d, format = %d",
            buf.width, buf.height, buf.stride, buf.format);
    ANativeWindow_unlockAndPost(win);
#endif

    LOGD("[configureSurfaceNative] Resetting buffer format.");
    ANativeWindow_setBuffersGeometry(win, width, height, 0 /*format unchanged*/);

    ANativeWindow_release(win);
}
}

bool checkBufferFormatsMatch(int srcWidth, int srcHeight, int srcRowStrideBytes,
                             const ANativeWindow_Buffer *buf) {
    return (srcWidth == buf->width && srcHeight == buf->height &&
            srcRowStrideBytes == buf->stride);
}

// The source buffers must be YUV_420_888:
//   chroma_width = luma_width / 2
//   chroma_height = luma_height / 2
//   luma pixel stride is guaranteed to be 1.
//   both chroma planes have the same pixel stride and row stride.
//
//   Since we de-interleave the source, we further require that the chroma
//   planes be interleaved (the pointers differ by 1, it does not matter which
//   is first, chroma pixel stride must be 2).
//
// The destination Surface must be YV12.
//
// The src and dst buffers have the same width, height, and row stride.
extern "C" {
JNIEXPORT bool JNICALL Java_com_example_helloandroidcamera2_JNIUtils_blit(
    JNIEnv *env, jobject obj, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes,
    jobject srcChromaUByteBuffer, jobject srcChromaVByteBuffer,
    jint srcChromaElementStrideBytes, jint srcChromaRowStrideBytes,
    jobject dstSurface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, dstSurface);
    ANativeWindow_acquire(win);

    ANativeWindow_Buffer buf;
    if (int err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGE("ANativeWindow_lock failed with error code %d\n", err);
        ANativeWindow_release(win);
        return false;
    }

    if (buf.format != IMAGE_FORMAT_YV12) {
        LOGE("ANativeWindow buffer locked but its format was not YV12.");
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    if (!checkBufferFormatsMatch(srcWidth, srcHeight, srcLumaRowStrideBytes,
                                 &buf)) {
        LOGE("ANativeWindow buffer locked but its size was %d x %d, stride = %d"
             " expected %d x %d, stride = %d",
             buf.width, buf.height,
             buf.stride, srcWidth, srcHeight, srcLumaRowStrideBytes);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    uint8_t *srcLumaPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcLumaByteBuffer));
    uint8_t *srcChromaUPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaUByteBuffer));
    uint8_t *srcChromaVPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaVByteBuffer));
    if (srcLumaPtr == nullptr || srcChromaUPtr == nullptr ||
        srcChromaVPtr == nullptr) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    // Check that the chroma channels are interleaved.
    // Our Halide kernel "directly deinterleaves" UVUVUVUV --> UUUU, VVVV
    // to handle VUVUVUVU, just swap the destination pointers.
    uint8_t *srcChromaUVInterleavedPtr;
    bool swapDstUV;
    if (srcChromaVPtr - srcChromaUPtr == 1) {
        srcChromaUVInterleavedPtr = srcChromaUPtr;  // UVUVUV...
        swapDstUV = false;
    } else if (srcChromaUPtr - srcChromaVPtr == 1) {
        srcChromaUVInterleavedPtr = srcChromaVPtr;  // VUVUVU...
        swapDstUV = true;
    } else {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    int32_t srcChromaWidth = srcWidth / 2;
    int32_t srcChromaHeight = srcHeight / 2;

    // This is guaranteed by the YV12 format, see android.graphics.ImageFormat.
    uint8_t *dstLumaPtr = reinterpret_cast<uint8_t *>(buf.bits);
    uint32_t dstLumaSizeBytes = buf.stride * buf.height;
    uint32_t dstChromaRowStrideBytes = ALIGN(buf.stride / 2, 16);
    // Size of one chroma plane.
    uint32_t dstChromaSizeBytes = dstChromaRowStrideBytes * buf.height / 2;
    // Yes, V is actually first.
    uint8_t *dstChromaVPtr = dstLumaPtr + dstLumaSizeBytes;
    uint8_t *dstChromaUPtr = dstLumaPtr + dstLumaSizeBytes + dstChromaSizeBytes;

    // Make these static so that we can reuse device allocations across frames.
    static buffer_t srcBuf = { 0 };
    static buffer_t dstBuf0 = { 0 };
    static buffer_t dstBuf1 = { 0 };

    srcBuf.host = srcChromaUVInterleavedPtr;
    srcBuf.host_dirty = true;
    srcBuf.extent[0] = 2 * srcChromaWidth;  // src is interleaved.
    srcBuf.extent[1] = srcChromaHeight;
    srcBuf.extent[2] = 0;
    srcBuf.extent[3] = 0;
    srcBuf.stride[0] = 1;
    srcBuf.stride[1] = 2 * srcChromaWidth;
    srcBuf.min[0] = 0;
    srcBuf.min[1] = 0;
    srcBuf.elem_size = 1;

    dstBuf0.host = swapDstUV ? dstChromaVPtr : dstChromaUPtr;
    dstBuf0.extent[0] = srcChromaWidth;  // src and dst width and height match.
    dstBuf0.extent[1] = srcChromaHeight;
    dstBuf0.extent[2] = 0;
    dstBuf0.extent[3] = 0;
    dstBuf0.stride[0] = 1;
    dstBuf0.stride[1] = srcChromaWidth;
    dstBuf0.min[0] = 0;
    dstBuf0.min[1] = 0;
    dstBuf0.elem_size = 1;

    dstBuf1.host = swapDstUV ? dstChromaUPtr : dstChromaVPtr;
    dstBuf1.extent[0] = srcChromaWidth;  // src and dst width and height match.
    dstBuf1.extent[1] = srcChromaHeight;
    dstBuf1.extent[2] = 0;
    dstBuf1.extent[3] = 0;
    dstBuf1.stride[0] = 1;
    dstBuf1.stride[1] = srcChromaWidth;
    dstBuf1.min[0] = 0;
    dstBuf1.min[1] = 0;
    dstBuf1.elem_size = 1;

    // Copy over the luma channel as is.
    // Can use a single memcpy since strides match.
    memcpy(dstLumaPtr, srcLumaPtr, dstLumaSizeBytes);
    // Use Halide to deinterleave the chroma channels.
    deinterleave(&srcBuf, &dstBuf0, &dstBuf1);

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    return true;
}
}

// src luma must have an element stride of 1.
extern "C" {
JNIEXPORT bool JNICALL Java_com_example_helloandroidcamera2_JNIUtils_edgeDetect(
    JNIEnv *env, jobject obj, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes, jobject dstSurface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, dstSurface);
    ANativeWindow_acquire(win);

    ANativeWindow_Buffer buf;
    if (int err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGE("ANativeWindow_lock failed with error code %d\n", err);
        ANativeWindow_release(win);
        return false;
    }

    if (buf.format != IMAGE_FORMAT_YV12) {
        LOGE("ANativeWindow buffer locked but its format was not YV12.");
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    if (!checkBufferFormatsMatch(srcWidth, srcHeight, srcLumaRowStrideBytes,
                                 &buf)) {
        LOGE("ANativeWindow buffer locked but its size was %d x %d, stride = %d"
             " expected %d x %d, stride = %d",
             buf.width, buf.height,
             buf.stride, srcWidth, srcHeight, srcLumaRowStrideBytes);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    uint8_t *srcLumaPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcLumaByteBuffer));
    if (srcLumaPtr == NULL) {
        return false;
    }

    uint8_t *dstLumaPtr = reinterpret_cast<uint8_t *>(buf.bits);
    if (dstLumaPtr == NULL) {
        return false;
    }

    uint32_t dstLumaSizeBytes = buf.stride * buf.height;
    uint32_t dstChromaRowStrideBytes = ALIGN(buf.stride / 2, 16);
    // Size of one chroma plane.
    uint32_t dstChromaSizeBytes = dstChromaRowStrideBytes * buf.height / 2;
    uint8_t *dstChromaVPtr = dstLumaPtr + dstLumaSizeBytes;
    uint8_t *dstChromaUPtr = dstLumaPtr + dstLumaSizeBytes + dstChromaSizeBytes;

// If we're using opencl, use the gpu backend for it.
#if COMPILING_FOR_OPENCL
    halide_opencl_set_device_type("gpu");
#endif

    // Make these static so that we can reuse device allocations across frames.
    static buffer_t srcBuf = { 0 };
    static buffer_t dstBuf = { 0 };

    srcBuf.host = srcLumaPtr;
    srcBuf.host_dirty = true;
    srcBuf.extent[0] = srcWidth;
    srcBuf.extent[1] = srcHeight;
    srcBuf.extent[2] = 0;
    srcBuf.extent[3] = 0;
    srcBuf.stride[0] = 1;
    srcBuf.stride[1] = srcLumaRowStrideBytes;
    srcBuf.min[0] = 0;
    srcBuf.min[1] = 0;
    srcBuf.elem_size = 1;

    dstBuf.host = dstLumaPtr;
    dstBuf.extent[0] = buf.width;  // src and dst width/height actually match.
    dstBuf.extent[1] = buf.height;
    dstBuf.extent[2] = 0;
    dstBuf.extent[3] = 0;
    dstBuf.stride[0] = 1;
    dstBuf.stride[1] = buf.stride;  // src and dst strides actually match.
    dstBuf.min[0] = 0;
    dstBuf.min[1] = 0;
    dstBuf.elem_size = 1;

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
    // The dst chroma is guaranteed to be tightly packed since it's YV12.
    memset(dstChromaVPtr, 128, dstChromaSizeBytes * 2);

    int64_t t1 = halide_current_time_ns();
    edge_detect(&srcBuf, &dstBuf);

    if (dstBuf.dev) {
        halide_copy_to_host(NULL, &dstBuf);
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

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    return true;
}
}
