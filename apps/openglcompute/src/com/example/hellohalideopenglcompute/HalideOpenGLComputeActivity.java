package com.example.hellohalideopenglcompute;

import android.app.Activity;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;

public class HalideOpenGLComputeActivity extends Activity {
    private static final String TAG = "HalideOpenGLComputeActivity";

    static {
        System.loadLibrary("oglc");
    }
    private static native void runTest();

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);
        Log.d(TAG, "Starting the test:");
        runTest();
        Log.d(TAG, "Done");
        finish();
   }
}
