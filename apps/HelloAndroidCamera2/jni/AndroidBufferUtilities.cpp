#include "AndroidBufferUtilities.h"

#include <stdint.h>

#include <android/log.h>

#include "LockedSurface.h"
#include "YuvBufferT.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "AndroidBufferUtilities", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AndroidBufferUtilities", __VA_ARGS__)

extern "C" {

JNIEXPORT
jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_allocNativeYuvBufferT(
    JNIEnv *env, jobject, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes,
    jobject srcChromaUByteBuffer, jobject srcChromaVByteBuffer,
    jint srcChromaElementStrideBytes, jint srcChromaRowStrideBytes) {
    uint8_t *srcLumaPtr = reinterpret_cast<uint8_t *>(
            env->GetDirectBufferAddress(srcLumaByteBuffer));
    uint8_t *srcChromaUPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaUByteBuffer));
    uint8_t *srcChromaVPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaVByteBuffer));
    if (srcLumaPtr == nullptr || srcChromaUPtr == nullptr ||
        srcChromaVPtr == nullptr) {
        return 0L;
    }

    YuvBufferT *buffer = new YuvBufferT(srcLumaPtr, srcWidth, srcHeight,
        1 /* srcLumaElementStrideBytes */, srcLumaRowStrideBytes,
        srcChromaUPtr, srcWidth / 2, srcHeight / 2,
        srcChromaElementStrideBytes, srcChromaRowStrideBytes,
        srcChromaVPtr, srcWidth / 2, srcHeight / 2,
        srcChromaElementStrideBytes, srcChromaRowStrideBytes);
    return reinterpret_cast<jlong>(buffer);
}

JNIEXPORT jboolean JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_freeNativeYuvBufferT(
    JNIEnv *env, jobject obj, jlong handle) {
    if (handle == 0L) {
        return false;
    }
    YuvBufferT *yuvBufferT = reinterpret_cast<YuvBufferT *>(handle);
    delete yuvBufferT;
    return true;
}

JNIEXPORT
jboolean JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_rotateNativeYuvBufferT180(
    JNIEnv *env, jobject obj, jlong handle) {
    if (handle == 0L) {
        return false;
    }
    YuvBufferT *yuvBufferT = reinterpret_cast<YuvBufferT *>(handle);
    yuvBufferT->rotate180();
    return true;
}

JNIEXPORT jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_lockSurface(
    JNIEnv *env, jobject obj, jobject surface) {
    return reinterpret_cast<jlong>(LockedSurface::lock(env, surface));
}

JNIEXPORT jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_allocNativeYuvBufferTFromSurfaceHandle(
    JNIEnv *env, jobject obj, jlong lockedSurfaceHandle) {
    if (lockedSurfaceHandle == 0L) {
        return 0L;
    }
    LockedSurface *ls = reinterpret_cast<LockedSurface *>(lockedSurfaceHandle);
    YuvBufferT tmp = ls->yuvView();
    if (tmp.isNull()) {
        return 0L;
    }
    YuvBufferT *yuvBufferT = new YuvBufferT(tmp);
    return reinterpret_cast<jlong>(yuvBufferT);
}

JNIEXPORT jboolean JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_unlockSurface(
    JNIEnv *env, jobject obj, jlong lockedSurfaceHandle) {
    if (lockedSurfaceHandle == 0L) {
        return false;
    }
    LockedSurface *ls = reinterpret_cast<LockedSurface *>(lockedSurfaceHandle);
    delete ls;
    return true;
}

} // extern "C"
