#include "YuvBufferT.h"

#include "BufferTFunctions.h"

#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "YuvBufferT", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "YuvBufferT", __VA_ARGS__)

YuvBufferT::YuvBufferT(uint8_t *lumaPointer,
    int32_t lumaWidth, int32_t lumaHeight,
    int32_t lumaElementStrideBytes, int32_t lumaRowStrideBytes,
    uint8_t *chromaUPointer,
    int32_t chromaUWidth, int32_t chromaUHeight,
    int32_t chromaUElementStrideBytes, int32_t chromaURowStrideBytes,
    uint8_t *chromaVPointer,
    int32_t chromaVWidth, int32_t chromaVHeight,
    int32_t chromaVElementStrideBytes, int32_t chromaVRowStrideBytes) {
    assert(lumaPointer != nullptr);
    assert(chromaUPointer != nullptr);
    assert(chromaVPointer != nullptr);

    luma_ = { 0 };
    luma_.host = lumaPointer;
    luma_.host_dirty = true;
    luma_.extent[0] = lumaWidth;
    luma_.extent[1] = lumaHeight;
    luma_.extent[2] = 0;
    luma_.extent[3] = 0;
    // Halide strides are in elements, but element size is 1.
    luma_.stride[0] = lumaElementStrideBytes;
    luma_.stride[1] = lumaRowStrideBytes;
    luma_.min[0] = 0;
    luma_.min[1] = 0;
    luma_.elem_size = 1;

    chromaU_ = { 0 };
    chromaU_.host = chromaUPointer;
    chromaU_.host_dirty = true;
    chromaU_.extent[0] = chromaUWidth;
    chromaU_.extent[1] = chromaUHeight;
    chromaU_.extent[2] = 0;
    chromaU_.extent[3] = 0;
    // Halide strides are in elements, but element size is 1.
    chromaU_.stride[0] = chromaUElementStrideBytes;
    chromaU_.stride[1] = chromaURowStrideBytes;
    chromaU_.min[0] = 0;
    chromaU_.min[1] = 0;
    chromaU_.elem_size = 1;

    chromaV_ = { 0 };
    chromaV_.host = chromaVPointer;
    chromaV_.host_dirty = true;
    chromaV_.extent[0] = chromaVWidth;
    chromaV_.extent[1] = chromaVHeight;
    chromaV_.extent[2] = 0;
    chromaV_.extent[3] = 0;
    // Halide strides are in elements, but element size is 1.
    chromaV_.stride[0] = chromaVElementStrideBytes;
    chromaV_.stride[1] = chromaVRowStrideBytes;
    chromaV_.min[0] = 0;
    chromaV_.min[1] = 0;
    chromaV_.elem_size = 1;

    // See if chroma is stored according to a well known format.
    chromaStorage_ = ChromaStorage::kOther;
    // U and V must have the same extents and strides.
    if (equalExtents(chromaU_, chromaV_) && equalStrides(chromaU_, chromaV_)) {
        // If strides are exactly 2, check if they are interleaved.
        if (chromaU_.stride[0] == 2 && chromaV_.stride[0] == 2) {
            if (chromaU_.host == chromaV_.host - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedUFirst;
            } else if (chromaV_.host == chromaU_.host - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedVFirst;
            }
        } else if (chromaU_.stride[0] == 1 && chromaV_.stride[0] == 1) {
            // If element stride is 1, then they're planar.
            // If there is no space at the end of each row, they might be packed.
            // Check if one directly follows the other.
            if (chromaU_.extent[0] == chromaU_.stride[1] &&
                chromaV_.extent[0] == chromaV_.stride[1]) {
                if (chromaU_.host + chromaU_.stride[1] * chromaU_.extent[1] == chromaV_.host) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedUFirst;
                } else if (chromaV_.host + chromaV_.stride[1] * chromaV_.extent[1] == chromaU_.host) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedVFirst;
                }
            } else {
                chromaStorage_ = ChromaStorage::kPlanarGeneric;
            }
        }
    }

    interleavedChromaView_ = { 0 };
    if (chromaStorage_ == ChromaStorage::kInterleavedUFirst ||
        chromaStorage_ == ChromaStorage::kInterleavedVFirst) {
        const buffer_t &first =
            (chromaStorage_ == ChromaStorage::kInterleavedUFirst) ?
            chromaU_ :
            chromaV_;
        interleavedChromaView_.host = first.host;
        interleavedChromaView_.host_dirty = true;
        interleavedChromaView_.extent[0] = 2 * first.extent[0];
        interleavedChromaView_.extent[1] = first.extent[1];
        // Halide strides are in elements, but element size is 1.
        interleavedChromaView_.stride[0] = 1;
        interleavedChromaView_.stride[1] = first.stride[1];
        interleavedChromaView_.elem_size = 1;
    }

    packedPlanarChromaView_ = { 0 };
    if (chromaStorage_ == ChromaStorage::kPlanarPackedUFirst ||
        chromaStorage_ == ChromaStorage::kPlanarPackedVFirst) {
        const buffer_t &first =
            (chromaStorage_ == ChromaStorage::kPlanarPackedUFirst) ?
            chromaU_ :
            chromaV_;
        packedPlanarChromaView_.host = first.host;
        packedPlanarChromaView_.host_dirty = true;
        packedPlanarChromaView_.extent[0] = first.extent[0];
        packedPlanarChromaView_.extent[1] = 2 * first.extent[1];
        // Halide strides are in elements, but element size is 1.
        packedPlanarChromaView_.stride[0] = first.stride[0];
        packedPlanarChromaView_.stride[1] = first.stride[1];
        packedPlanarChromaView_.elem_size = 1;
    }
}

bool YuvBufferT::isNull() const {
    return isHostNull(luma_) || isHostNull(chromaU_) || isHostNull(chromaV_);
}

const buffer_t &YuvBufferT::luma() const {
    return luma_;
}

const buffer_t &YuvBufferT::chromaU() const {
    return chromaU_;
}

const buffer_t &YuvBufferT::chromaV() const {
    return chromaV_;
}

YuvBufferT::ChromaStorage YuvBufferT::chromaStorage() const {
    return chromaStorage_;
}

const buffer_t &YuvBufferT::interleavedChromaView() const {
    return interleavedChromaView_;
}

const buffer_t &YuvBufferT::packedPlanarChromaView() const {
    return packedPlanarChromaView_;
}