package com.example.helloandroidcamera2;

import android.graphics.ImageFormat;
import android.media.Image;
import android.media.Image.Plane;
import android.view.Surface;

import java.nio.ByteBuffer;

public class AndroidBufferUtilities {
    private static final String TAG = "AndroidBufferUtilities";

    // Load native Halide shared library.
    static {
        System.loadLibrary("HelloAndroidCamera2");
    }

    /**
     * Allocate a native Halide YuvBufferT wrapping luma and chroma ByteBuffers. The returned handle
     * needs to be deallocated with freeNativeYuvBufferT().
     * @return The handle, or 0L on failure.
     */
    public static native long allocNativeYuvBufferT(int width, int height, ByteBuffer luma,
                                                    int lumaRowStride, ByteBuffer chromaU,
                                                    ByteBuffer chromaV,
                                                    int chromaElementStride,
                                                    int chromaRowStride);

    /**
     * Deallocate a native Halide YuvBufferT.
     * @return false if handle is 0L.
     */
    public static native boolean freeNativeYuvBufferT(long handle);

    /**
     * Rotate a native Halide YuvBufferT by 180 degrees. Cheap (just
     * messes with the strides, doesn't actually move pixels around.
     * @return false if handle is 0L.
     */
    public static native boolean rotateNativeYuvBufferT180(long handle);

    /**
     * Lock a Surface, returning a native handle. It needs to be unlocked with unlockSurface().
     * @return The handle, or 0L if the surface was invalid.
     */
    public static native long lockSurface(Surface surface);

    /**
     * Obtain a native Halide YuvBufferT handle from a handle to a locked surface. It needs to be
     * deallocated with freeNativeYuvBufferT().
     */
    public static native long allocNativeYuvBufferTFromSurfaceHandle(long surfaceHandle);

    /**
     * Unlock a locked native Surface handle.
     * @return false if handle is 0L.
     */
    public static native boolean unlockSurface(long handle);
}
