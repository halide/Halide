package com.halide.arm;

import android.app.Activity;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;

public class TestActivity extends Activity {
    private static final String TAG = "TestActivity";

    static {
        System.loadLibrary("native");
    }
    private static native boolean runTest8bit();
    private static native boolean runTest16bit();
    private static native boolean runTest32bit();

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);
        Log.d(TAG, "Starting the test...");
        if (runTest8bit()) {
            Log.i(TAG, "8-bit Pass");
        } else {
            Log.i(TAG, "8-bit Failed");
        }
        if (runTest16bit()) {
            Log.i(TAG, "16-bit Pass");
        } else {
            Log.i(TAG, "16-bit Failed");
        }
        if (runTest32bit()) {
            Log.i(TAG, "32-bit Pass");
        } else {
            Log.i(TAG, "32-bit Failed");
        }
   }
}