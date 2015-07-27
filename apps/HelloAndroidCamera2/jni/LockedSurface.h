#ifndef LOCKED_SURFACE_H
#define LOCKED_SURFACE_H

#include <jni.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>

#include "YuvBufferT.h"

// Wraps an RAII pattern around locking an ANativeWindow.
class LockedSurface {
public:

    // Lock a Surface, returning a lock object, or nullptr if it failed.
    static LockedSurface *lock(JNIEnv *env, jobject surface);

    ~LockedSurface();

    const ANativeWindow_Buffer &buffer() const;

    // If buffer() is a compatible YUV format, returns a non-null YuvBufferT.
    // Otherwise, output.isNull() will be true.
    YuvBufferT yuvView() const;

private:

    LockedSurface() = default;

    ANativeWindow *window_;
    ANativeWindow_Buffer buffer_;

};

#endif // LOCKED_SURFACE_H