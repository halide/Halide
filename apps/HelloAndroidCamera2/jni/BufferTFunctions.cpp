#include "BufferTFunctions.h"

#include <algorithm>
#include <string.h>

#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "BufferTFunctions", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BufferTFunctions", __VA_ARGS__)

bool isHostNull(const halide_buffer_t &buf) {
    return (buf.host == nullptr);
}

bool equalExtents(const halide_buffer_t &a, const halide_buffer_t &b) {
    if (a.dimensions != b.dimensions) return false;
    for (int i = 0; i < a.dimensions; i++) {
        if (a.dim[i].extent != b.dim[i].extent) return false;
    }
    return true;
}

bool equalStrides(const halide_buffer_t &a, const halide_buffer_t &b) {
    if (a.dimensions != b.dimensions) return false;
    for (int i = 0; i < a.dimensions; i++) {
        if (a.dim[i].stride != b.dim[i].stride) return false;
    }
    return true;
}

bool copy2D(const halide_buffer_t &src, const halide_buffer_t &dst) {
    if (!equalExtents(src, dst)) {
        return false;
    }
    if (src.type != dst.type) {
        return false;
    }

    if (src.dimensions != 2) {
        return false;
    }

    if (src.dim[0].stride == 1 && src.dim[1].stride == src.dim[0].extent &&
        dst.dim[0].stride == 1 && dst.dim[1].stride == dst.dim[0].extent) {
        // Copy all at once.
        memcpy(dst.host, src.host, src.dim[1].extent * src.dim[1].stride * src.type.bytes());
    } else if (src.dim[0].stride == 1 && dst.dim[0].stride == 1) {
        // memcpy row by row.
        int32_t minRowStrideBytes = std::min(src.dim[1].stride, dst.dim[1].stride) * src.type.bytes();
        for (int32_t y = 0; y < src.dim[1].extent; ++y) {
            const uint8_t *srcRow = src.host + y * src.dim[1].stride * src.type.bytes();
            uint8_t *dstRow = dst.host + y * dst.dim[1].stride * dst.type.bytes();
            memcpy(dstRow, srcRow, minRowStrideBytes);
        }
    } else {
        // Copy element by element.
        int32_t srcElementStrideBytes = src.dim[0].stride * src.type.bytes();
        int32_t dstElementStrideBytes = dst.dim[0].stride * dst.type.bytes();
        int32_t srcRowStrideBytes = src.dim[1].stride * src.type.bytes();
        int32_t dstRowStrideBytes = dst.dim[1].stride * dst.type.bytes();

        // Just do array assignment if elements are small enough.
        // It's slightly ridiculous that we have all these versions, but oh well.
        if (src.type.bytes() == 1) {
            for (int32_t y = 0; y < src.dim[1].extent; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.dim[0].extent; ++x) {
                    dstRow[x * dstElementStrideBytes] = srcRow[x * srcElementStrideBytes];
                }
            }
        } else if (src.type.bytes() == 2) {
            for (int32_t y = 0; y < src.dim[1].extent; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.dim[0].extent; ++x) {
                    const uint16_t *srcElement = reinterpret_cast<const uint16_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint16_t *dstElement = reinterpret_cast<uint16_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else if (src.type.bytes() == 4) {
            for (int32_t y = 0; y < src.dim[1].extent; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.dim[0].extent; ++x) {
                    const uint32_t *srcElement = reinterpret_cast<const uint32_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint32_t *dstElement = reinterpret_cast<uint32_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else if (src.type.bytes() == 8) {
            for (int32_t y = 0; y < src.dim[1].extent; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.dim[0].extent; ++x) {
                    const uint64_t *srcElement = reinterpret_cast<const uint64_t *>(
                        srcRow + x * srcElementStrideBytes);
                    uint64_t *dstElement = reinterpret_cast<uint64_t *>(
                        dstRow + x * dstElementStrideBytes);
                    *dstElement = *srcElement;
                }
            }
        } else {
            // Otherwise, use memcpy.
            for (int32_t y = 0; y < src.dim[1].extent; ++y) {
                const uint8_t *srcRow = src.host + y * srcRowStrideBytes;
                uint8_t *dstRow = dst.host + y * dstRowStrideBytes;
                for (int32_t x = 0; x < src.dim[0].extent; ++x) {
                    const uint8_t *srcElementPtr = srcRow + x * srcElementStrideBytes;
                    uint8_t *dstElementPtr = dstRow + x * dstElementStrideBytes;
                    memcpy(dstElementPtr, srcElementPtr, src.type.bytes());
                }
            }
        }
    }
    return true;
}

bool fill2D(const halide_buffer_t &buffer, uint8_t value) {
    if (buffer.host == nullptr) {
        return false;
    }
    if (buffer.type.bytes() != 1) {
        return false;
    }

    if (buffer.dim[0].stride == 1 &&
        buffer.dim[1].stride == buffer.dim[0].extent) {
        // Data is totally packed, memset all at once.
        memset(buffer.host, value, buffer.dim[1].extent * buffer.dim[1].stride);
    } else if (buffer.dim[0].stride == 1) {
        // Each row is packed, use memset one row at a time.
        for (int y = 0; y < buffer.dim[1].extent; ++y) {
            uint8_t *rowPointer = buffer.host + y * buffer.dim[1].stride;
            memset(rowPointer, value, buffer.dim[1].stride);
        }
    } else {
        // Set each element one at a time.
        for (int y = 0; y < buffer.dim[1].extent; ++y) {
            uint8_t *rowPointer = buffer.host + y * buffer.dim[1].stride;
            for (int x = 0; x < buffer.dim[0].extent; ++x) {
                rowPointer[x * buffer.dim[0].stride] = value;
            }
        }
    }

    return true;
}
