package com.example.hellohalide;

import android.hardware.Camera;
import android.util.Log;

public class FrameHandler implements Camera.PreviewCallback {
    private static final String TAG = "FrameHandler";

    public void onPreviewFrame(byte[] data, Camera camera) {
        Log.d(TAG, "Got a frame!");
    }
}