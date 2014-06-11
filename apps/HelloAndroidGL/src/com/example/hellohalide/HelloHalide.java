package com.example.hellohalide;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;
import android.view.Surface;
import android.graphics.Bitmap;
import android.graphics.Canvas;

public class HelloHalide extends Activity {
    private static final String TAG = "HelloHalide";

    private Bitmap input, bitmap;

    // Link to native Halide code
    static {
        System.loadLibrary("native");
    }
    private static native void processFrame(Bitmap src, Bitmap dst);

    class MyView extends android.view.View {
        public MyView(Context context) {
            super(context);
        }

        protected void onDraw(Canvas canvas) {
            processFrame(input, bitmap);
            canvas.drawBitmap(bitmap, 0, 0, null);

            // Swap front and backbuffer
            Bitmap tmp = input;
            input = bitmap;
            bitmap = tmp;

            invalidate();
        }
    }

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);

        setContentView(new MyView(this));

        bitmap = Bitmap.createBitmap(640, 400, Bitmap.Config.ARGB_8888);
        input = Bitmap.createBitmap(640, 400, Bitmap.Config.ARGB_8888);
    }

    @Override
    public void onResume() {
        super.onResume();
    }

    @Override
    public void onPause() {
        super.onPause();
    }
}
