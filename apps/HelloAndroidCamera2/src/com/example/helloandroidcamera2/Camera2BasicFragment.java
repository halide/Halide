/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.example.helloandroidcamera2;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DialogFragment;
import android.app.Fragment;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.ImageFormat;
import android.graphics.PixelFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Message;
import android.util.Log;
import android.util.Size;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Toast;

import com.android.ex.camera2.blocking.BlockingCameraManager;
import com.android.ex.camera2.blocking.BlockingCameraManager.BlockingOpenException;
import com.android.ex.camera2.blocking.BlockingSessionCallback;
import com.android.ex.camera2.blocking.BlockingStateCallback;
import com.android.ex.camera2.exceptions.TimeoutRuntimeException;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

public class Camera2BasicFragment extends Fragment implements View.OnClickListener {

    /**
     * Tag for the {@link Log}.
     */
    private static final String TAG = "Camera2BasicFragment";

    private static final int STATE_TIMEOUT_MS = 5000;
    private static final int SESSION_WAIT_TIMEOUT_MS = 2500;

    private static final Size DESIRED_IMAGE_READER_SIZE = new Size(1920, 1440);
    private static final int IMAGE_READER_BUFFER_SIZE = 16;

    private final SurfaceHolder.Callback mSurfaceCallback
            = new SurfaceHolder.Callback() {

        @Override
        public void surfaceCreated(SurfaceHolder holder) {}

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width,
                int height) {
            mSurface = holder.getSurface();
            openCamera();
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {}

    };

    /**
     * ID of the current {@link CameraDevice}.
     */
    private String mCameraId;

    /**
     * Whether or not the camera is rotated 180 relative to the display.
     */
    private boolean mCameraRotated;

    /**
     * An {@link SurfaceView} and its associated {@link Surface} for camera
     * preview.
     */
    private AutoFitSurfaceView mSurfaceView;
    private Surface mSurface;

    /**
     * A {@link CameraCaptureSession } for camera preview.
     */
    private CameraCaptureSession mCaptureSession;

    /**
     * A reference to the opened {@link CameraDevice}.
     */
    private CameraDevice mCameraDevice;

    /**
     * The {@link android.util.Size} of camera preview.
     */
    private Size mPreviewSize;

    /**
     * {@link CameraDevice.StateCallback} is called when {@link CameraDevice} changes its state.
     */
    private BlockingStateCallback mDeviceCallback;

    private BlockingSessionCallback mSessionCallback;

    /**
     * An additional thread for running tasks that shouldn't block the UI.
     */
    private HandlerThread mBackgroundThread;

    /**
     * A {@link Handler} for running tasks in the background.
     */
    private Handler mBackgroundHandler;

    /**
     * An {@link ImageReader} that handles still image capture.
     */
    private ImageReader mImageReader;

    /**
     * Toggled by the button: whether we want to use the edge detector.
     */
    private boolean mUseEdgeDetector = false;

    /**
     * This a callback object for the {@link ImageReader}. "onImageAvailable" will be called when a
     * still image is ready to be saved.
     */
    private final ImageReader.OnImageAvailableListener mOnImageAvailableListener
            = new ImageReader.OnImageAvailableListener() {

        @Override
        public void onImageAvailable(ImageReader reader) {
            Log.d(TAG, "onImageAvailable");
            if (mCameraDevice != null) {
                Image image = reader.acquireLatestImage();
                if (image == null) {
                    return;
                }

                NativeSurfaceHandle dstSurface = NativeSurfaceHandle.lockSurface(mSurface);
                if (dstSurface != null) {
                    HalideYuvBufferT srcYuv = HalideYuvBufferT.fromImage(image);

                    if (srcYuv != null) {
                        if (mCameraRotated) {
                            srcYuv.rotate180();
                        }

                        HalideYuvBufferT dstYuv = dstSurface.allocNativeYuvBufferT();

                        if (mUseEdgeDetector) {
                            HalideFilters.edgeDetect(srcYuv, dstYuv);
                        } else {
                            HalideFilters.copy(srcYuv, dstYuv);
                        }

                        dstYuv.close();
                        srcYuv.close();
                    }
                    dstSurface.close();
                }
                image.close();
            }
        }

    };

    /**
     * {@link CaptureRequest.Builder} for the camera preview
     */
    private CaptureRequest.Builder mPreviewRequestBuilder;

    /**
     * {@link CaptureRequest} generated by {@link #mPreviewRequestBuilder}
     */
    private CaptureRequest mPreviewRequest;

    /**
     * A {@link CameraCaptureSession.CaptureCallback} that receives metadata about the
     * ongoing capture.
     */
    private CameraCaptureSession.CaptureCallback mCaptureCallback
            = new CameraCaptureSession.CaptureCallback() {

        @Override
        public void onCaptureProgressed(CameraCaptureSession session, CaptureRequest request,
                                        CaptureResult partialResult) {
            // Partial appears here, look at Image.getTimestamp() to find the
            // corresponding Image.
        }

        @Override
        public void onCaptureCompleted(CameraCaptureSession session, CaptureRequest request,
                                       TotalCaptureResult result) {
            // Metadata appears here, look at Image.getTimestamp() to find the
            // corresponding Image.
        }

    };

    public static Camera2BasicFragment newInstance() {
        Camera2BasicFragment fragment = new Camera2BasicFragment();
        fragment.setRetainInstance(true);
        return fragment;
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_camera2_basic, container, false);
    }

    @Override
    public void onViewCreated(final View view, Bundle savedInstanceState) {
        Log.d(TAG, "onViewCreated");
        mSurfaceView = (AutoFitSurfaceView) view.findViewById(R.id.surface_view);
        mSurfaceView.setAspectRatio(DESIRED_IMAGE_READER_SIZE.getWidth(),
                DESIRED_IMAGE_READER_SIZE.getHeight());
        view.findViewById(R.id.toggle).setOnClickListener(this);
    }

    @Override
    public void onResume() {
        Log.d(TAG, "onResume");
        super.onResume();
        startBackgroundThread();
        setupCameraOutputs();
        // Make the SurfaceView VISIBLE so that on resume, surfaceCreated() is called,
        // and on pause, surfaceDestroyed() is called.
        mSurfaceView.getHolder().setFormat(ImageFormat.YV12);
        mSurfaceView.getHolder().setFixedSize(mImageReader.getWidth(), mImageReader.getHeight());
        mSurfaceView.getHolder().addCallback(mSurfaceCallback);
        mSurfaceView.setVisibility(View.VISIBLE);
    }

    @Override
    public void onPause() {
        Log.d(TAG, "onPause()");
        try {
            if (mCaptureSession != null) {
                mCaptureSession.stopRepeating();
                mCaptureSession = null;
                mSessionCallback.getStateWaiter().waitForState(
                        BlockingSessionCallback.SESSION_READY, SESSION_WAIT_TIMEOUT_MS);
            }
        } catch (CameraAccessException e) {
            Log.e(TAG, "Could not stop repeating request.");
        } catch (TimeoutRuntimeException e) {
            Log.e(TAG, "Timed out waiting for camera to stop repeating.");
        }

        Log.d(TAG, "stopped repeating(), closing camera");
        if (mCameraDevice != null) {
            mCameraDevice.close();
            mCameraDevice = null;
        }

        try {
            mDeviceCallback.waitForState(BlockingStateCallback.STATE_CLOSED, STATE_TIMEOUT_MS);
            Log.d(TAG, "camera closed");
        } catch (TimeoutRuntimeException e) {
            Log.e(TAG, "Timed out waiting for camera to close.");
        }

        Log.d(TAG, "camera closed, closing ImageReader");
        if (mImageReader != null) {
            mImageReader.close();
            mImageReader = null;
        }

        stopBackgroundThread();

        // Make the SurfaceView GONE so that on resume, surfaceCreated() is called,
        // and on pause, surfaceDestroyed() is called.
        mSurfaceView.getHolder().removeCallback(mSurfaceCallback);
        mSurfaceView.setVisibility(View.GONE);

        super.onPause();
    }

    /**
     * Query the system for available cameras and configurations. If the configuration we want is
     * available, create an ImageReader of the right format and size, save the camera id, and
     * tell the UI what aspect ratio to use.
     */
    private void setupCameraOutputs() {
        Log.d(TAG, "setupCameraOutputs");
        Activity activity = getActivity();
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        try {
            for (String cameraId : manager.getCameraIdList()) {
                CameraCharacteristics characteristics
                        = manager.getCameraCharacteristics(cameraId);

                // We don't use a front facing camera in this sample.
                if (characteristics.get(CameraCharacteristics.LENS_FACING)
                        == CameraCharacteristics.LENS_FACING_FRONT) {
                    continue;
                }

                // Check if the sensor is rotated relative to what we expect.
                mCameraRotated = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION) >= 180;

                StreamConfigurationMap map = characteristics.get(
                        CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                // See if the camera supports a YUV resolution that we want and create an
                // ImageReader if it does.
                Size[] sizes = map.getOutputSizes(ImageFormat.YUV_420_888);
                Log.d(TAG, "Available YUV_420_888 sizes:");
                for (Size size : sizes) {
                    Log.d(TAG, size.toString());
                }

                Size optimalSize = chooseOptimalSize(sizes,
                        DESIRED_IMAGE_READER_SIZE.getWidth(),
                        DESIRED_IMAGE_READER_SIZE.getHeight(),
                        DESIRED_IMAGE_READER_SIZE /* aspect ratio */);
                if (optimalSize != null) {
                    Log.d(TAG, "Desired image size was: " + DESIRED_IMAGE_READER_SIZE
                            + " closest size was: " + optimalSize);
                    // Create an ImageReader to receive images at that resolution.
                    mImageReader = ImageReader.newInstance(optimalSize.getWidth(),
                            optimalSize.getHeight(), ImageFormat.YUV_420_888,
                            IMAGE_READER_BUFFER_SIZE);
                    mImageReader.setOnImageAvailableListener(
                            mOnImageAvailableListener, mBackgroundHandler);

                    // Save the camera id to open later.
                    mCameraId = cameraId;
                    return;
                } else {
                    Log.e(TAG, "Could not find suitable supported resolution.");
                    new ErrorDialog().show(getFragmentManager(), "dialog");
                }
            }
        } catch (Exception e) {
            new ErrorDialog().show(getFragmentManager(), "dialog");
        }
    }

    /**
     * Opens the camera specified by {@link Camera2BasicFragment#mCameraId}.
     */
    private void openCamera() {
        Activity activity = getActivity();
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        BlockingCameraManager blockingManager = new BlockingCameraManager(manager);
        try {
            mDeviceCallback = new BlockingStateCallback() {

                @Override
                public void onDisconnected(CameraDevice camera) {
                    camera.close();
                    mCameraDevice = null;
                }

                @Override
                public void onError(CameraDevice camera, int error) {
                    camera.close();
                    mCameraDevice = null;
                    Activity activity = getActivity();
                    if (activity != null) {
                        activity.finish();
                    }
                }
            };

            mCameraDevice = blockingManager.openCamera(mCameraId, mDeviceCallback,
                    mBackgroundHandler);
            createCameraPreviewSession();
        } catch (BlockingOpenException|TimeoutRuntimeException e) {
            showToast("Timed out opening camera.");
        } catch (CameraAccessException e) {
            showToast("Failed to open camera."); // failed immediately.
        }
    }

    /**
     * Starts a background thread and its {@link Handler}.
     */
    private void startBackgroundThread() {
        Log.d(TAG, "starting background thread");
        mBackgroundThread = new HandlerThread("CameraBackground");
        mBackgroundThread.start();
        mBackgroundHandler = new Handler(mBackgroundThread.getLooper());
    }

    /**
     * Stops the background thread and its {@link Handler}.
     */
    private void stopBackgroundThread() {
        mBackgroundThread.quitSafely();
        try {
            mBackgroundThread.join();
            mBackgroundThread = null;
            Log.d(TAG, "setting background handler to null");
            mBackgroundHandler = null;
            Log.d(TAG, "background handler is now null");
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    /**
     * Creates a new {@link CameraCaptureSession} for camera preview.
     */
    private void createCameraPreviewSession() {
        try {
            // This is the output Surface we need to start preview.
            Surface surface = mImageReader.getSurface();

            // We set up a CaptureRequest.Builder with the output Surface.
            mPreviewRequestBuilder
                    = mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            mPreviewRequestBuilder.addTarget(surface);

            mSessionCallback = new BlockingSessionCallback();

            // Here, we create a CameraCaptureSession for camera preview.
            Log.d(TAG, "creating capture session");
            mCameraDevice.createCaptureSession(Arrays.asList(surface), mSessionCallback,
                    mBackgroundHandler);
            try {
                Log.d(TAG, "waiting on session.");
                mCaptureSession = mSessionCallback.waitAndGetSession(SESSION_WAIT_TIMEOUT_MS);
                try {
                    mPreviewRequestBuilder.set(CaptureRequest.CONTROL_AF_MODE,
                        CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);

                    // Comment out the above and uncomment this to disable continuous autofocus and
                    // instead set it to a fixed value of 20 diopters. This should make the picture
                    // nice and blurry for denoised edge detection.
                    // mPreviewRequestBuilder.set(CaptureRequest.CONTROL_AF_MODE,
                    //         CaptureRequest.CONTROL_AF_MODE_OFF);
                    // mPreviewRequestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, 20.0f);

                    // Finally, we start displaying the camera preview.
                    mPreviewRequest = mPreviewRequestBuilder.build();
                    Log.d(TAG, "setting repeating request");

                    mCaptureSession.setRepeatingRequest(mPreviewRequest,
                            mCaptureCallback, mBackgroundHandler);
                } catch (CameraAccessException e) {
                    e.printStackTrace();
                }
            } catch (TimeoutRuntimeException e) {
                showToast("Failed to configure capture session.");
            }
        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onClick(View view) {
        switch (view.getId()) {
            case R.id.toggle: {
                mUseEdgeDetector = !mUseEdgeDetector;
                break;
            }
        }
    }

    /**
     * Compares two {@code Size}s based on their areas.
     */
    static class CompareSizesByArea implements Comparator<Size> {

        @Override
        public int compare(Size lhs, Size rhs) {
            // We cast here to ensure the multiplications won't overflow
            return Long.signum((long) lhs.getWidth() * lhs.getHeight() -
                    (long) rhs.getWidth() * rhs.getHeight());
        }

    }

    public static class ErrorDialog extends DialogFragment {

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            final Activity activity = getActivity();
            return new AlertDialog.Builder(activity)
                    .setMessage("This device doesn't support Camera2 API or doesn't have a"
                            + " supported image configuration.")
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialogInterface, int i) {
                            activity.finish();
                        }
                    })
                    .create();
        }

    }

    /**
     * A {@link Handler} for showing {@link Toast}s.
     */
    private Handler mMessageHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            Activity activity = getActivity();
            if (activity != null) {
                Toast.makeText(activity, (String) msg.obj, Toast.LENGTH_SHORT).show();
            }
        }
    };

    /**
     * Shows a {@link Toast} on the UI thread.
     *
     * @param text The message to show
     */
    private void showToast(String text) {
        // We show a Toast by sending request message to mMessageHandler. This makes sure that the
        // Toast is shown on the UI thread.
        Message message = Message.obtain();
        message.obj = text;
        mMessageHandler.sendMessage(message);
    }

    /**
     * Given {@code choices} of {@code Size}s supported by a camera, chooses the <b>largest</b>
     * one whose width and height are less than the desired ones, and whose aspect ratio matches
     * the specified value.
     *
     * @param choices     The list of sizes that the camera supports for the intended output class
     * @param width       The minimum desired width
     * @param height      The minimum desired height
     * @param aspectRatio The aspect ratio
     * @return The optimal {@code Size}, or null if none were big enough
     */
    private static Size chooseOptimalSize(Size[] choices, int width, int height, Size aspectRatio) {
        // Collect the supported resolutions that are at least as big as the desired size.
        List<Size> ok = new ArrayList<>();
        int w = aspectRatio.getWidth();
        int h = aspectRatio.getHeight();
        for (Size option : choices) {
            if (option.getHeight() == option.getWidth() * h / w &&
                    option.getWidth() <= width && option.getHeight() <= height) {
                ok.add(option);
            }
        }

        // Pick the biggest of those, assuming we found any.
        if (!ok.isEmpty()) {
            return Collections.max(ok, new CompareSizesByArea());
        } else {
            Log.e(TAG, "Couldn't find any suitable preview size");
            return null;
        }
    }
}
