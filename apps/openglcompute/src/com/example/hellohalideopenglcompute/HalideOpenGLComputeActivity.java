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
        System.loadLibrary("oglc_two_kernels");
    }
    private static native void runTest();
    private static native void runTwoKernelsTest();

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);
        Log.d(TAG, "Starting the tests:");
        runTest();
        Log.d(TAG, "Done with first test");
        runTwoKernelsTest();
        Log.d(TAG, "Done");
        finish();
   }
}
