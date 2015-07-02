package com.example.helloandroidcamera2;

import android.view.Surface;

public class NativeSurfaceHandle implements AutoCloseable {

    private long mHandle;

    /**
     * Lock a Surface that's been properly configured with {@link SurfaceHolder.setFixedSize()} and
     * {@link SurfaceHolder.setFormat()}. The format must be YV12.
     *
     * @return A native handle to a native view of the locked surface. It needs to be closed with
     * {@link close()}.
     */
    public static NativeSurfaceHandle lockSurface(Surface surface) {
        long handle = AndroidBufferUtilities.lockSurface(surface);
        if (handle == 0L) {
            return null;
        }
        return new NativeSurfaceHandle(handle);
    }
    public NativeSurfaceHandle(long handle) {
        mHandle = handle;
    }

    public long handle() {
        return mHandle;
    }

    /**
     * Obtain a HalideYuvBufferT from a handle to a locked surface. It needs to be deallocated
     * with close().
     */
    public HalideYuvBufferT allocNativeYuvBufferT() {
        if (mHandle == 0L){
            throw new IllegalStateException("Surface already unlocked.");
        }
        long yuvHandle = AndroidBufferUtilities.allocNativeYuvBufferTFromSurfaceHandle(mHandle);
        return new HalideYuvBufferT(yuvHandle);
    }

    @Override
    public void close() {
        if (mHandle != 0L) {
            AndroidBufferUtilities.unlockSurface(mHandle);
            mHandle = 0L;
        }
    }

    @Override
    protected void finalize() {
        close();
    }
}
