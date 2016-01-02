#include "LockedSurface.h"

// Defined in http://developer.android.com/reference/android/graphics/ImageFormat.html
#define IMAGE_FORMAT_YV12 842094169

// Round x up to a multiple of mask.
// E.g., ALIGN(x, 16) means round x up to the nearest multiple of 16.
#define ALIGN(x, mask) (((x) + (mask)-1) & ~((mask)-1))

LockedSurface *LockedSurface::lock(JNIEnv *env, jobject surface) {
    LockedSurface *output = new LockedSurface;

    output->window_ = ANativeWindow_fromSurface(env, surface);
    if (int err = ANativeWindow_lock(output->window_, &(output->buffer_), NULL)) {
        ANativeWindow_release(output->window_);
        delete output;
        output = nullptr;
    }

    return output;
}

LockedSurface::~LockedSurface() {
    ANativeWindow_unlockAndPost(window_);
    ANativeWindow_release(window_);
    window_ = nullptr;
}

const ANativeWindow_Buffer &LockedSurface::buffer() const {
    return buffer_;
}

YuvBufferT LockedSurface::yuvView() const {
    if (buffer_.format != IMAGE_FORMAT_YV12) {
        return YuvBufferT();
    }
    // This is guaranteed by the YV12 format, see android.graphics.ImageFormat.
    uint8_t *lumaPtr = reinterpret_cast<uint8_t *>(buffer_.bits);
    uint32_t lumaRowStrideBytes = buffer_.stride;
    uint32_t lumaSizeBytes = lumaRowStrideBytes * buffer_.height;
    uint32_t chromaRowStrideBytes = ALIGN(buffer_.stride / 2, 16);
    // Size of one chroma plane.
    uint32_t chromaSizeBytes = chromaRowStrideBytes * buffer_.height / 2;
    // Yes, V is actually first.
    uint8_t *chromaVPtr = lumaPtr + lumaSizeBytes;
    uint8_t *chromaUPtr = lumaPtr + lumaSizeBytes + chromaSizeBytes;

    return YuvBufferT(lumaPtr,
        buffer_.width, buffer_.height,
        1 /* lumaElementStrideBytes */, lumaRowStrideBytes,
        chromaUPtr,
        buffer_.width / 2, buffer_.height / 2,
        1 /* chromaUElementStrideBytes */, chromaRowStrideBytes,
        chromaVPtr,
        buffer_.width / 2, buffer_.height / 2,
        1 /* chromaVElementStrideBytes */, chromaRowStrideBytes
    );
}
