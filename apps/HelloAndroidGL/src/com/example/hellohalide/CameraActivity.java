package com.example.hellohalide;

import android.app.Activity;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;

public class CameraActivity extends Activity {
    private static final String TAG = "CameraActivity";

    private Camera camera;
    private CameraPreview preview;
    private SurfaceView filtered;

    public static Camera getCameraInstance() {
        Camera c = null;
        try {
            c = Camera.open();
        } catch (Exception e) {
            Log.d(TAG, "Could not open camera");
        }
        return c;
    }

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);

        setContentView(R.layout.main);

        // Create a canvas for drawing stuff on
        filtered = new SurfaceView(this);

        // Create our Preview view and set it as the content of our activity.
        preview = new CameraPreview(this, filtered);

        FrameLayout layout = (FrameLayout) findViewById(R.id.camera_preview);
        layout.addView(preview);
        layout.addView(filtered);
        filtered.setZOrderOnTop(true);
    }

    @Override
    public void onResume() {
        super.onResume();
        camera = getCameraInstance();
        preview.setCamera(camera);
    }

    @Override
    public void onPause() {
        super.onPause();
        if (camera != null) {
            preview.setCamera(null);
            camera.release();
            camera = null;
        }
    }
}