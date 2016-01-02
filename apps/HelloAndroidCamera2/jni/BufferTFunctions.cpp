#include "BufferTFunctions.h"

#include <algorithm>
#include <string.h>

#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "BufferTFunctions", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BufferTFunctions", __VA_ARGS__)

bool isHostNull(const buffer_t &buf) {
    return (buf.host == nullptr);
}

bool equalExtents(const buffer_t &a, const buffer_t &b) {
    return (a.extent[0] == b.extent[0] &&
        a.extent[1] == b.extent[1] &&
        a.extent[2] == b.extent[2] &&
        a.extent[3] == b.extent[3]);
}

bool equalStrides(const buffer_t &a, const buffer_t &b) {
    return (a.stride[0] == b.stride[0] &&
        a.stride[1] == b.stride[1] &&
        a.stride[2] == b.stride[2] &&
        a.stride[3] == b.stride[3]);
}

bool copy2D(const buffer_t &src, const buffer_t &dst) {
    if (!equalExtents(src, dst)) {
        return false;
    }
    if (src.elem_size != dst.elem_size) {
        return false;
    }

    if (src.stride[0] == 1 && src.stride[1] == src.extent[0] &&
        dst.stride[0] == 1 && dst.stride[1] == dst.extent[0]) {
        // Copy all at once.
        memcpy(dst.host, src.host, src.extent[1] * src.stride[1] * src.elem_size);
    } else if (src.stride[0] == 1 && dst.stride[0] == 1) {
        // memcpy row by row.
        int32_t minRowStrideBytes = std::min(src.stride[1], dst.stride[1]) * src.elem_size;
        for (int32_t y = 0; y < src.extent[1]; ++y) {
            const uint8_t *srcRow = src.host + y * src.stride[1] * src.elem_size;
            uint8_t *dstRow = dst.host + y * dst.stride[1] * dst.elem_size;
            memcpy(dstRow, srcRow, minRowStrideBytes);
        }
    } else {
        // Copy element by element.
        int32_t srcElementStrideBytes = src.stride[0] * src.elem_size;
        int32_t dstElementStrideBytes = dst.stride[0] * dst.elem_size;
        int32_t srcRowStrideBytes = src.stride[1] * src.elem_size;
        int32_t dstRowStrideBytes = dst.stride[1] * dst.elem_size;

        // Just do array assignment if elements are small enough.
        // It's slightly ridiculous that we have all these versions, but oh well.
        if (src.elem_size == 1) {
            for (int32_t y = 0; y < src.extent[1]; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.extent[0]; ++x) {
                    dstRow[x * dstElementStrideBytes] = srcRow[x * srcElementStrideBytes];
                }
            }
        } else if (src.elem_size == 2) {
            for (int32_t y = 0; y < src.extent[1]; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.extent[0]; ++x) {
                    const uint16_t *srcElement = reinterpret_cast<const uint16_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint16_t *dstElement = reinterpret_cast<uint16_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else if (src.elem_size == 4) {
            for (int32_t y = 0; y < src.extent[1]; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.extent[0]; ++x) {
                    const uint32_t *srcElement = reinterpret_cast<const uint32_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint32_t *dstElement = reinterpret_cast<uint32_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else if (src.elem_size == 8) {
            for (int32_t y = 0; y < src.extent[1]; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.extent[0]; ++x) {
                    const uint64_t *srcElement = reinterpret_cast<const uint64_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint64_t *dstElement = reinterpret_cast<uint64_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else {
            // Otherwise, use memcpy.
            for (int32_t y = 0; y < src.extent[1]; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.extent[0]; ++x) {
                    const uint8_t *srcElementPtr = srcRow + x * srcElementStrideBytes;
                    uint8_t *dstElementPtr = dstRow + x * dstElementStrideBytes;
                    memcpy(dstElementPtr, srcElementPtr, src.elem_size);
                }
            }
        }
    }
    return true;
}

bool fill2D(const buffer_t &buffer, uint8_t value) {
    if (buffer.host == nullptr) {
        return false;
    }
    if (buffer.elem_size != 1) {
        return false;
    }

    if (buffer.stride[0] == 1 &&
        buffer.stride[1] == buffer.extent[0]) {
        // Data is totally packed, memset all at once.
        memset(buffer.host, value, buffer.extent[1] * buffer.stride[1]);
    } else if (buffer.stride[0] == 1) {
        // Each row is packed, use memset one row at a time.
        for (int y = 0; y < buffer.extent[1]; ++y) {
            uint8_t *rowPointer = buffer.host + y * buffer.stride[1];
            memset(rowPointer, value, buffer.stride[1]);
        }
    } else {
        // Set each element one at a time.
        for (int y = 0; y < buffer.extent[1]; ++y) {
            uint8_t *rowPointer = buffer.host + y * buffer.stride[1];
            for (int x = 0; x < buffer.extent[0]; ++x) {
                rowPointer[x * buffer.stride[0]] = value;
            }
        }
    }

    return true;
}