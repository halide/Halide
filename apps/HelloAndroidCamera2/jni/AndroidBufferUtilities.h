#ifndef ANDROID_BUFFER_UTILITIES_H
#define ANDROID_BUFFER_UTILITIES_H

#include <jni.h>

extern "C" {

JNIEXPORT
jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_allocNativeYuvBufferT(
    JNIEnv *env, jobject, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes,
    jobject srcChromaUByteBuffer, jobject srcChromaVByteBuffer,
    jint srcChromaElementStrideBytes, jint srcChromaRowStrideBytes);

JNIEXPORT jboolean JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_freeNativeYuvBufferT(
    JNIEnv *env, jobject obj, jlong handle);

JNIEXPORT jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_lockSurface(
    JNIEnv *env, jobject obj, jobject surface);

JNIEXPORT jlong JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_allocNativeYuvBufferTFromSurfaceHandle(
    JNIEnv *env, jobject obj, jlong surfaceWrapperHandle);

JNIEXPORT jboolean JNICALL Java_com_example_helloandroidcamera2_AndroidBufferUtilities_unlockSurface(
    JNIEnv *env, jobject obj, jlong surfaceWrapperHandle);

} // extern "C"

#endif // ANDROID_BUFFER_UTILITIES_H
