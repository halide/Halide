#ifndef YUV_BUFFER_T_H
#define YUV_BUFFER_T_H

#include <assert.h>
#include <string.h>
#include "HalideRuntime.h"

class YuvBufferT {
public:

    enum class ChromaStorage {
        // UVUVUV... Interleaved U and V with element stride 2
        // UVUVUV... and arbitrary row stride.
        // U and V have the same extents and strides.
        kInterleavedUFirst,
        // VUVUVU... Interleaved V and U with element stride 2
        // VUVUVU... and arbitrary row stride.
        // U and V have the same extents and strides.
        kInterleavedVFirst,
        // U and V and stored in separate planes, with U first, followed
        // immediately by V. Element stride = 1, row stride = width.
        // U and V have the same extents and strides.
        kPlanarPackedUFirst,
        // V and U and stored in separate planes, with V first, followed
        // immediately by U. Element stride = 1, row stride = width.
        // U and V have the same extents and strides.
        kPlanarPackedVFirst,
        // U and V are stored in separate planes.
        // Element stride = 1, row stride = arbitrary.
        // U and V have the same extents and strides.
        kPlanarGeneric,
        // Some other arbitrary interleaving of chroma not easily classified.
        kOther
    };

    // Make a null YuvBufferT.
    YuvBufferT() = default;

    YuvBufferT(uint8_t *lumaPointer,
        int32_t lumaWidth, int32_t lumaHeight,
        int32_t lumaElementStrideBytes, int32_t lumaRowStrideBytes,
        uint8_t *chromaUPointer,
        int32_t chromaUWidth, int32_t chromaUHeight,
        int32_t chromaUElementStrideBytes, int32_t chromaURowStrideBytes,
        uint8_t *chromaVPointer,
        int32_t chromaVWidth, int32_t chromaVHeight,
        int32_t chromaVElementStrideBytes, int32_t chromaVRowStrideBytes);

    YuvBufferT(const YuvBufferT &copy) = default;

    bool isNull() const;

    const buffer_t &luma() const;

    const buffer_t &chromaU() const;

    const buffer_t &chromaV() const;

    ChromaStorage chromaStorage() const;

    // If chroma channels are interleaved, return an interleaved buffer_t with:
    // - The host pointer pointing to whichever chroma buffer is first in
    //   memory.
    // - Twice the width.
    // Otherwise, returns a buffer_t pointing to nullptr.
    const buffer_t &interleavedChromaView() const;

    // If chroma channels are planar and tightly packed (one directly follows
    // the other, with the same size and strides), then returns a buffer_t
    // with:
    // - The host pointer pointing to whichever chroma buffer is first in
    //   memory.
    // - Twice the height.
    // Otherwise, returns a buffer_t pointing to nullptr.
    const buffer_t &packedPlanarChromaView() const;

    // Rotate the buffer 180 degrees. Cheap. Just messes with the strides.
    void rotate180();

private:

    buffer_t luma_;
    buffer_t chromaU_;
    buffer_t chromaV_;

    ChromaStorage chromaStorage_;

    buffer_t interleavedChromaView_;
    buffer_t packedPlanarChromaView_;
};

#endif // YUV_BUFFER_T_H

