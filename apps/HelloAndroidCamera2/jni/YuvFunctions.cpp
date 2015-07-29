#include "YuvFunctions.h"

#include <android/log.h>
#include "BufferTFunctions.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "YuvFunctions", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "YuvFunctions", __VA_ARGS__)

bool equalExtents(const YuvBufferT &a, const YuvBufferT &b) {
    return (equalExtents(a.luma(), b.luma()) &&
        equalExtents(a.chromaU(), b.chromaU()) &&
        equalExtents(a.chromaV(), b.chromaV()));
}

bool copy2D(const YuvBufferT &src, const YuvBufferT &dst) {
    return (copy2D(src.luma(), dst.luma()) &&
        copy2D(src.chromaU(), dst.chromaU()) &&
        copy2D(src.chromaV(), dst.chromaV()));
}
