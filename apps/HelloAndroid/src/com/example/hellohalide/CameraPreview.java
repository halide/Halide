package com.example.hellohalide;

import android.hardware.Camera;
import android.util.Log;
import java.io.IOException;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Surface;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.ImageFormat;

/** A basic Camera preview class */
public class CameraPreview extends SurfaceView
    implements SurfaceHolder.Callback, Camera.PreviewCallback {
    private static final String TAG = "CameraPreview";

    private Camera mCamera;
    private SurfaceView mFiltered;
    private byte[] previewData;

    // Link to native Halide code
    static {
        System.loadLibrary("native");
    }
    private static native void processFrame(byte[] src, int w, int h, Surface dst);

    public CameraPreview(Context context, SurfaceView filtered) {
        super(context);
        mFiltered = filtered;
        mFiltered.getHolder().setFormat(ImageFormat.YV12);

        previewData = null;

        // Install a SurfaceHolder.Callback so we get notified when the
        // underlying surface is created and destroyed.
        getHolder().addCallback(this);
    }

    public void onPreviewFrame(byte[] data, Camera camera) {
        if (camera != mCamera) {
            return;
        }
        if (mFiltered.getHolder().getSurface().isValid()) {
            Camera.Size s = camera.getParameters().getPreviewSize();
            processFrame(data, s.width, s.height, mFiltered.getHolder().getSurface());
        }

        // re-enqueue this buffer
        camera.addCallbackBuffer(data);
    }

    private void configureCamera(SurfaceHolder holder) {
        try {
            Camera.Parameters p = mCamera.getParameters();
            Camera.Size s = p.getPreviewSize();
            p.setPreviewFormat(ImageFormat.YV12);
            if (previewData == null) {
                previewData = new byte[(s.width * s.height * 3) / 2];
            }
            mCamera.addCallbackBuffer(previewData);
            mCamera.setParameters(p);
            mCamera.setPreviewCallbackWithBuffer(this);
            mCamera.setPreviewDisplay(holder);
            mCamera.startPreview();
        } catch (IOException e) {
            Log.d(TAG, "Error setting camera preview: " + e.getMessage());
        }
    }

    public void surfaceCreated(SurfaceHolder holder) {
        // The Surface has been created, now tell the camera where to draw the preview.
        if (mCamera != null) {
            configureCamera(holder);
        }
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        // empty. Take care of releasing the Camera preview in your activity.
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        // If your preview can change or rotate, take care of those events here.
        // Make sure to stop the preview before resizing or reformatting it.

        if (getHolder().getSurface() == null){
          // preview surface does not exist
          return;
        }

        // stop preview before making changes
        try {
            mCamera.stopPreview();
        } catch (Exception e){
          // ignore: tried to stop a non-existent preview
        }

        // set preview size and make any resize, rotate or
        // reformatting changes here

        // start preview with new settings
        if (mCamera != null) {
            configureCamera(holder);
        }
    }

    public void setCamera(Camera c) {
        if (mCamera != null) {
            mCamera.stopPreview();
        }
        mCamera = c;
        if (mCamera != null) {
            configureCamera(getHolder());
        }
    }
}
