package com.example.helloandroidcamera2;

import android.graphics.ImageFormat;
import android.media.Image;
import android.media.Image.Plane;
import android.view.Surface;

import java.nio.ByteBuffer;

public class JNIUtils {
    private static final String TAG = "JNIUtils";

    // Load native Halide shared library.
    static {
        System.loadLibrary("native");
    }

    /**
     * Configures all buffers in {@link Surface} to the desired width and
     * height. Note that this only configures the geometry of the buffers, which
     * does <b>not</b> have to match that of the underlying window. The hardware
     * scaler will automatically scale the buffer to the window.
     */
    public static void configureSurface(Surface surface, int width, int height) {
        configureSurfaceNative(surface, width, height);
    }

    /**
     * Use native code to copy the contents of src to dst. src must have format YUV_420_888, dst
     * must be YV12 and have been configured with {@code configureSurface()}.
     *
     */
    public static boolean blit(Image src, Surface dst) {
        if (src.getFormat() != ImageFormat.YUV_420_888) {
            throw new IllegalArgumentException("src must have format YUV_420_888.");
        }
        Plane[] planes = src.getPlanes();
        if (planes[1].getPixelStride() != 2) {
            throw new IllegalArgumentException("src chroma must have a pixel stride of 2.");
        }
        return blit(src.getWidth(), src.getHeight(),
                planes[0].getBuffer(), planes[0].getRowStride(),
                planes[1].getBuffer(), planes[2].getBuffer(),
                planes[1].getPixelStride(), planes[1].getRowStride(),
                dst);
    }

    /**
     * Use native code to render edges detected in src into dst. src must have format YUV_420_888,
     * dst must be YV12 and have been configured with {@code configureSurface()}.
     */
    public static boolean edgeDetect(Image src, Surface dst) {
        if (src.getFormat() != ImageFormat.YUV_420_888) {
            throw new IllegalArgumentException("src must have format YUV_420_888.");
        }
        Plane[] planes = src.getPlanes();
        return edgeDetect(src.getWidth(), src.getHeight(), planes[0].getBuffer(),
                planes[0].getRowStride(), dst);
    }

    private static native void configureSurfaceNative(Surface surface, int width, int height);

    private static native boolean blit(int srcWidth, int srcHeight,
            ByteBuffer srcLuma, int srcLumaRowStride,
            ByteBuffer srcChromaU, ByteBuffer srcChromaV,
            int srcChromaUElementStride, int srcChromaURowStride,
            Surface dst);

    private static native boolean edgeDetect(int srcWidth, int srcHeight,
            ByteBuffer srcLuma, int srcRowStride, Surface dst);
}
