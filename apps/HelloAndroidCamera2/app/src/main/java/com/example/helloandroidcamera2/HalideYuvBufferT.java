package com.example.helloandroidcamera2;

import android.graphics.ImageFormat;
import android.media.Image;

import java.nio.ByteBuffer;

/**
 * A Java wrapper around a native Halide YuvBufferT.
 */
public class HalideYuvBufferT implements AutoCloseable {

    // Load native Halide shared library.
    static {
        System.loadLibrary("HelloAndroidCamera2");
    }

    private long mHandle;

    /**
     * Allocate a native YUV handle to an Image. It needs to be closed with close().
     */
    public static HalideYuvBufferT fromImage(Image image) {
        if (image.getFormat() != ImageFormat.YUV_420_888) {
            throw new IllegalArgumentException("src must have format YUV_420_888.");
        }
        Image.Plane[] planes = image.getPlanes();
        // Spec guarantees that planes[0] is luma and has pixel stride of 1.
        // It also guarantees that planes[1] and planes[2] have the same row and
        // pixel stride.
        long handle = AndroidBufferUtilities.allocNativeYuvBufferT(
                image.getWidth(),image.getHeight(),
                planes[0].getBuffer(), planes[0].getRowStride(), planes[1].getBuffer(),
                planes[2].getBuffer(), planes[1].getPixelStride(), planes[1].getRowStride());
        return new HalideYuvBufferT(handle);
    }

    public HalideYuvBufferT(long handle) {
        mHandle = handle;
    }

    public long handle() {
        return mHandle;
    }

    public void rotate180() {
        AndroidBufferUtilities.rotateNativeYuvBufferT180(mHandle);
    }

    @Override
    public void close() {
        if (mHandle != 0L) {
            AndroidBufferUtilities.freeNativeYuvBufferT(mHandle);
            mHandle = 0L;
        }
    }

    @Override
    protected void finalize() {
        close();
    }
}
