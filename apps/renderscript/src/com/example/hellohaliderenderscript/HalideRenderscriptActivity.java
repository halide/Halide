package com.example.hellohaliderenderscript;

import android.app.Activity;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;

public class HalideRenderscriptActivity extends Activity {
    private static final String TAG = "HalideRenderscriptActivity";

    static {
        System.loadLibrary("rstest");
    }
    private static native void runTest(String cacheDir);

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);
        Log.d(TAG, "Starting the test:");
        Log.i(TAG, "Data dir: " + getApplicationInfo().dataDir);
        runTest(getApplicationInfo().dataDir + "/cache");
        Log.d(TAG, "Done");
        finish();
   }
}
