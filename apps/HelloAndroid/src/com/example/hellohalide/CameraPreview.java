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
public class CameraPreview extends SurfaceView implements SurfaceHolder.Callback, Camera.PreviewCallback {
    private static final String TAG = "CameraPreview";

    private SurfaceHolder mHolder;
    private Camera mCamera;
    private SurfaceView mFiltered;
    private byte[] previewData;
    private int[] filteredData;

    static {
        System.loadLibrary("native");
    }

    private static native void processFrame(byte[] src, Surface dst);

    public CameraPreview(Context context, Camera camera, SurfaceView filtered) {
        super(context);
        mCamera = camera;
        mFiltered = filtered;
        mFiltered.getHolder().setFormat(ImageFormat.YV12);

        previewData = new byte[640*480 + 320*240*2];

        // Install a SurfaceHolder.Callback so we get notified when the
        // underlying surface is created and destroyed.
        mHolder = getHolder();
        mHolder.addCallback(this);

    }

    public void onPreviewFrame(byte[] data, Camera camera) {
        if (mFiltered.getHolder().getSurface().isValid()) {
            processFrame(data, mFiltered.getHolder().getSurface());
            /*

            Canvas canvas = mFiltered.getHolder().lockCanvas();

            if (canvas != null) {
                //canvas.drawBitmap(filteredData, 0, 640, 0, 0, 640, 360, false, null);
                //canvas.drawBitmap(data, 0, 640, 0, 0, 640, 360, false, null);
                //mFiltered.getHolder().unlockCanvasAndPost(canvas);

            } else {
                Log.d(TAG, "canvas was null");
            }
            */
        }
        
        // re-enqueue this buffer
        mCamera.addCallbackBuffer(data);
    }

    public void surfaceCreated(SurfaceHolder holder) {
        // The Surface has been created, now tell the camera where to draw the preview.
        try {
            mCamera.addCallbackBuffer(previewData);
            Camera.Parameters p = mCamera.getParameters();
            p.setPreviewFormat(ImageFormat.YV12);
            mCamera.setParameters(p);
            mCamera.setPreviewCallbackWithBuffer(this);
            mCamera.setPreviewDisplay(holder);
            mCamera.startPreview();
        } catch (IOException e) {
            Log.d(TAG, "Error setting camera preview: " + e.getMessage());
        }
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        // empty. Take care of releasing the Camera preview in your activity.
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        // If your preview can change or rotate, take care of those events here.
        // Make sure to stop the preview before resizing or reformatting it.

        if (mHolder.getSurface() == null){
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
        try {
            mCamera.addCallbackBuffer(previewData);
            Camera.Parameters p = mCamera.getParameters();
            p.setPreviewFormat(ImageFormat.YV12);
            mCamera.setParameters(p);
            mCamera.setPreviewCallbackWithBuffer(this);
            mCamera.setPreviewDisplay(mHolder);
            mCamera.startPreview();

        } catch (Exception e){
            Log.d(TAG, "Error starting camera preview: " + e.getMessage());
        }
    }
}