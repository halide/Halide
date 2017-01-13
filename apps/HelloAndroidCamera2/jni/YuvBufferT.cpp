#include "YuvBufferT.h"

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

    halide_dimension_t lumaShape[] = {
        {0, lumaWidth, lumaElementStrideBytes},
        {0, lumaHeight, lumaRowStrideBytes}
    };
    luma_ = Halide::Runtime::Buffer<uint8_t>(lumaPointer, 2, lumaShape);

    halide_dimension_t chromaUShape[] = {
        {0, chromaUWidth, chromaUElementStrideBytes},
        {0, chromaUHeight, chromaURowStrideBytes}
    };
    chromaU_ = Halide::Runtime::Buffer<uint8_t>(chromaUPointer, 2, chromaUShape);

    halide_dimension_t chromaVShape[] = {
        {0, chromaVWidth, chromaVElementStrideBytes},
        {0, chromaVHeight, chromaVRowStrideBytes}
    };
    chromaV_ = Halide::Runtime::Buffer<uint8_t>(chromaVPointer, 2, chromaVShape);

    // See if chroma is stored according to a well known format.
    chromaStorage_ = ChromaStorage::kOther;
    // U and V must have the same extents and strides.
    if (chromaU_.width() == chromaV_.width() &&
        chromaU_.height() == chromaV_.height() &&
        chromaU_.dim(0).stride() == chromaV_.dim(0).stride() &&
        chromaU_.dim(1).stride() == chromaV_.dim(1).stride()) {
        // If strides are exactly 2, check if they are interleaved.
        if (chromaU_.dim(0).stride() == 2 &&
            chromaV_.dim(0).stride() == 2) {
            if (chromaU_.data() == chromaV_.data() - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedUFirst;
            } else if (chromaV_.data() == chromaU_.data() - 1) {
                chromaStorage_ = ChromaStorage::kInterleavedVFirst;
            }
        } else if (chromaU_.dim(0).stride() == 1 &&
                   chromaV_.dim(0).stride() == 1) {
            // If element stride is 1, then they're planar.
            // If there is no space at the end of each row, they might be packed.
            // Check if one directly follows the other.
            if (chromaU_.width() == chromaU_.dim(1).stride() &&
                chromaV_.width() == chromaV_.dim(1).stride()) {
                if (&chromaU_(0, chromaU_.height()) == &chromaV_(0, 0)) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedUFirst;
                } else if (&chromaV_(0, chromaU_.height()) == &chromaU_(0, 0)) {
                    chromaStorage_ = ChromaStorage::kPlanarPackedVFirst;
                }
            } else {
                chromaStorage_ = ChromaStorage::kPlanarGeneric;
            }
        }
    }

    interleavedChromaView_ = { 0 };
    if (chromaStorage_ == ChromaStorage::kInterleavedUFirst) {
        halide_dimension_t chromaShape[] = {
            {0, chromaUWidth*2, 1},
            {0, chromaUHeight, chromaURowStrideBytes}
        };
        interleavedChromaView_ = Halide::Runtime::Buffer<uint8_t>(chromaUPointer, 2, chromaShape);
    } else if (chromaStorage_ == ChromaStorage::kInterleavedVFirst) {
        halide_dimension_t chromaShape[] = {
            {0, chromaVWidth*2, 1},
            {0, chromaVHeight, chromaVRowStrideBytes}
        };
        interleavedChromaView_ = Halide::Runtime::Buffer<uint8_t>(chromaVPointer, 2, chromaShape);
    } else if (chromaStorage_ == ChromaStorage::kPlanarPackedUFirst) {
        packedPlanarChromaView_ = chromaU_;
        packedPlanarChromaView_.crop(1, 0, chromaUHeight*2);
    } else if (chromaStorage_ == ChromaStorage::kPlanarPackedVFirst) {
        packedPlanarChromaView_ = chromaV_;
        packedPlanarChromaView_.crop(1, 0, chromaVHeight*2);
    }

    interleavedChromaView_.set_host_dirty();
    packedPlanarChromaView_.set_host_dirty();
    chromaU_.set_host_dirty();
    chromaV_.set_host_dirty();
    luma_.set_host_dirty();
}

bool YuvBufferT::isNull() const {
    return luma_.data() == nullptr;
}

Halide::Runtime::Buffer<uint8_t> YuvBufferT::luma() const {
    return luma_;
}

Halide::Runtime::Buffer<uint8_t> YuvBufferT::chromaU() const {
    return chromaU_;
}

Halide::Runtime::Buffer<uint8_t> YuvBufferT::chromaV() const {
    return chromaV_;
}

YuvBufferT::ChromaStorage YuvBufferT::chromaStorage() const {
    return chromaStorage_;
}

Halide::Runtime::Buffer<uint8_t> YuvBufferT::interleavedChromaView() const {
    return interleavedChromaView_;
}

Halide::Runtime::Buffer<uint8_t> YuvBufferT::packedPlanarChromaView() const {
    return packedPlanarChromaView_;
}

void YuvBufferT::copy_from(const YuvBufferT &other) {
    luma_.copy_from(other.luma_);
    if (interleavedChromaView_.data() &&
        other.interleavedChromaView_.data()) {
        interleavedChromaView_.copy_from(other.interleavedChromaView_);
    } else if (packedPlanarChromaView_.data() &&
               other.packedPlanarChromaView_.data()) {
        packedPlanarChromaView_.copy_from(other.packedPlanarChromaView_);
    } else {
        chromaU_.copy_from(other.chromaU_);
        chromaV_.copy_from(other.chromaV_);
    }
}

void YuvBufferT::fill(uint8_t y, uint8_t u, uint8_t v) {
    luma_.fill(y);
    fillUV(u, v);
}

void YuvBufferT::fillUV(uint8_t u, uint8_t v) {
    if (interleavedChromaView_.data() && u == v) {
        interleavedChromaView_.fill(u);
    } else if (packedPlanarChromaView_.data() && u == v) {
        packedPlanarChromaView_.fill(v);
    } else {
        chromaU_.fill(u);
        chromaV_.fill(v);
    }
}

namespace {
Halide::Runtime::Buffer<uint8_t> rotateBuffer180(Halide::Runtime::Buffer<uint8_t> buf) {
    if (buf.data() == nullptr) return buf;
    halide_dimension_t shape[] = {
        {0, buf.dim(0).extent(), -buf.dim(0).stride()},
        {0, buf.dim(1).extent(), -buf.dim(1).stride()},
    };
    return Halide::Runtime::Buffer<uint8_t>(&buf(buf.width()-1, buf.height()-1), 2, shape);
}
};

void YuvBufferT::rotate180() {
    luma_    = rotateBuffer180(luma_);
    chromaU_ = rotateBuffer180(chromaU_);
    chromaV_ = rotateBuffer180(chromaV_);
    packedPlanarChromaView_ = rotateBuffer180(packedPlanarChromaView_);
    interleavedChromaView_ = rotateBuffer180(interleavedChromaView_);

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
