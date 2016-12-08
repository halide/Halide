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

    luma_.host = lumaPointer;
    luma_.set_host_dirty(true);
    luma_.dim[0].extent = lumaWidth;
    luma_.dim[1].extent = lumaHeight;
    // Halide strides are in elements, but element size is 1.
    luma_.dim[0].stride = lumaElementStrideBytes;
    luma_.dim[1].stride = lumaRowStrideBytes;
    luma_.type = halide_type_of<uint8_t>();

    chromaU_.host = chromaUPointer;
    chromaU_.set_host_dirty(true);
    chromaU_.dim[0].extent = chromaUWidth;
    chromaU_.dim[1].extent = chromaUHeight;
    // Halide strides are in elements, but element size is 1.
    chromaU_.dim[0].stride = chromaUElementStrideBytes;
    chromaU_.dim[1].stride = chromaURowStrideBytes;
    chromaU_.type = halide_type_of<uint8_t>();

    chromaV_.host = chromaVPointer;
    chromaV_.set_host_dirty(true);
    chromaV_.dim[0].extent = chromaVWidth;
    chromaV_.dim[1].extent = chromaVHeight;
    // Halide strides are in elements, but element size is 1.
    chromaV_.dim[0].stride = chromaVElementStrideBytes;
    chromaV_.dim[1].stride = chromaVRowStrideBytes;
    chromaV_.type = halide_type_of<uint8_t>();

    // See if chroma is stored according to a well known format.
    chromaStorage_ = ChromaStorage::kOther;
    // U and V must have the same extents and strides.
    if (equalExtents(chromaU_, chromaV_) && equalStrides(chromaU_, chromaV_)) {
        // If strides are exactly 2, check if they are interleaved.
        if (chromaU_.dim[0].stride == 2 && chromaV_.dim[0].stride == 2) {
            if (chromaU_.host == chromaV_.host - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedUFirst;
            } else if (chromaV_.host == chromaU_.host - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedVFirst;
            }
        } else if (chromaU_.dim[0].stride == 1 && chromaV_.dim[0].stride == 1) {
            // If element stride is 1, then they're planar.
            // If there is no space at the end of each row, they might be packed.
            // Check if one directly follows the other.
            if (chromaU_.dim[0].extent == chromaU_.dim[1].stride &&
                chromaV_.dim[0].extent == chromaV_.dim[1].stride) {
                if (chromaU_.host + chromaU_.dim[1].stride * chromaU_.dim[1].extent == chromaV_.host) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedUFirst;
                } else if (chromaV_.host + chromaV_.dim[1].stride * chromaV_.dim[1].extent == chromaU_.host) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedVFirst;
                }
            } else {
                chromaStorage_ = ChromaStorage::kPlanarGeneric;
            }
        }
    }

    if (chromaStorage_ == ChromaStorage::kInterleavedUFirst ||
        chromaStorage_ == ChromaStorage::kInterleavedVFirst) {
        const halide_nd_buffer_t<2> &first =
            (chromaStorage_ == ChromaStorage::kInterleavedUFirst) ?
            chromaU_ :
            chromaV_;
        interleavedChromaView_.host = first.host;
        interleavedChromaView_.set_host_dirty(true);
        interleavedChromaView_.dim[0].extent = 2 * first.dim[0].extent;
        interleavedChromaView_.dim[1].extent = first.dim[1].extent;
        // Halide strides are in elements, but element size is 1.
        interleavedChromaView_.dim[0].stride = 1;
        interleavedChromaView_.dim[1].stride = first.dim[1].stride;
        interleavedChromaView_.type = halide_type_of<uint8_t>();
    }

    if (chromaStorage_ == ChromaStorage::kPlanarPackedUFirst ||
        chromaStorage_ == ChromaStorage::kPlanarPackedVFirst) {
        const halide_nd_buffer_t<2> &first =
            (chromaStorage_ == ChromaStorage::kPlanarPackedUFirst) ?
            chromaU_ :
            chromaV_;
        packedPlanarChromaView_.host = first.host;
        packedPlanarChromaView_.set_host_dirty(true);
        packedPlanarChromaView_.dim[0].extent = first.dim[0].extent;
        packedPlanarChromaView_.dim[1].extent = 2 * first.dim[1].extent;
        // Halide strides are in elements, but element size is 1.
        packedPlanarChromaView_.dim[0].stride = first.dim[0].stride;
        packedPlanarChromaView_.dim[1].stride = first.dim[1].stride;
        packedPlanarChromaView_.type = halide_type_of<uint8_t>();
    }
}

bool YuvBufferT::isNull() const {
    return isHostNull(luma_) || isHostNull(chromaU_) || isHostNull(chromaV_);
}

const halide_nd_buffer_t<2> &YuvBufferT::luma() const {
    return luma_;
}

const halide_nd_buffer_t<2> &YuvBufferT::chromaU() const {
    return chromaU_;
}

const halide_nd_buffer_t<2> &YuvBufferT::chromaV() const {
    return chromaV_;
}

YuvBufferT::ChromaStorage YuvBufferT::chromaStorage() const {
    return chromaStorage_;
}

const halide_nd_buffer_t<2> &YuvBufferT::interleavedChromaView() const {
    return interleavedChromaView_;
}

const halide_nd_buffer_t<2> &YuvBufferT::packedPlanarChromaView() const {
    return packedPlanarChromaView_;
}

namespace {
void rotateBuffer180(halide_nd_buffer_t<2> *buf) {
    buf->host += (buf->dim[0].extent - 1) * buf->dim[0].stride + (buf->dim[1].extent - 1) * buf->dim[1].stride;
    buf->dim[0].stride = -buf->dim[0].stride;
    buf->dim[1].stride = -buf->dim[1].stride;
}
};

void YuvBufferT::rotate180() {
    rotateBuffer180(&luma_);
    rotateBuffer180(&chromaU_);
    rotateBuffer180(&chromaV_);

    rotateBuffer180(&packedPlanarChromaView_);
    rotateBuffer180(&interleavedChromaView_);

    // Rotating the above two views effectively swaps U and V.
    switch(chromaStorage_) {
    case ChromaStorage::kPlanarPackedUFirst:
        chromaStorage_ = ChromaStorage::kPlanarPackedVFirst;
        break;
    case ChromaStorage::kPlanarPackedVFirst:
        chromaStorage_ = ChromaStorage::kPlanarPackedUFirst;
        break;
    case ChromaStorage::kInterleavedUFirst:
        chromaStorage_ = ChromaStorage::kInterleavedVFirst;
        break;
    case ChromaStorage::kInterleavedVFirst:
        chromaStorage_ = ChromaStorage::kInterleavedUFirst;
        break;
    default:
        // nothing
        break;
    };
}

