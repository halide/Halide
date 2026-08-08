package com.example.hellohalide;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.hardware.Camera;
import android.os.Bundle;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class CameraActivity extends Activity {
    private static final String TAG = "CameraActivity";
    private static final int REQUEST_CAMERA_PERMISSION = 1;

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
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED) {
            openCamera();
        } else {
            ActivityCompat.requestPermissions(
                    this, new String[] {Manifest.permission.CAMERA}, REQUEST_CAMERA_PERMISSION);
        }
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == REQUEST_CAMERA_PERMISSION
                && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            openCamera();
        }
    }

    private void openCamera() {
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
