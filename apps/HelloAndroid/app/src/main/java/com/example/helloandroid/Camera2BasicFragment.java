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

package com.example.helloandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.graphics.ImageFormat;
import android.os.Bundle;
import android.util.Log;
import android.util.Size;
import android.view.LayoutInflater;
import android.view.OrientationEventListener;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;

import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.resolutionselector.AspectRatioStrategy;
import androidx.camera.core.resolutionselector.ResolutionSelector;
import androidx.camera.core.resolutionselector.ResolutionStrategy;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.core.content.ContextCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import androidx.fragment.app.DialogFragment;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.LifecycleOwner;

import com.google.common.util.concurrent.ListenableFuture;

import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Camera2BasicFragment extends Fragment implements View.OnClickListener {

    /**
     * Tag for the {@link Log}.
     */
    private static final String TAG = "Camera2BasicFragment";

    private static final Size DESIRED_ANALYSIS_SIZE = new Size(1920, 1080);

    private final SurfaceHolder.Callback mSurfaceCallback = new SurfaceHolder.Callback() {

        @Override
        public void surfaceCreated(SurfaceHolder holder) {}

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            mSurface = holder.getSurface();
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            mSurface = null;
        }

    };

    /**
     * The on-screen {@link AutoFitSurfaceView} and its {@link Surface}. CameraX never draws to
     * this directly -- it's the destination we lock and write Halide's output into.
     */
    private AutoFitSurfaceView mSurfaceView;
    private Surface mSurface;

    /**
     * Toggled by the button: whether we want to use the edge detector.
     */
    private boolean mUseEdgeDetector = false;

    /**
     * Runs {@link #analyzeFrame} off the main thread.
     */
    private ExecutorService mAnalysisExecutor;

    /**
     * The camera feed fills the whole screen regardless of how the phone is held (the app is
     * locked to landscape), so this just keeps the toggle button visually upright by
     * counter-rotating it to match the device's physical orientation. {@link OrientationEventListener}
     * reports orientation relative to the device's natural (usually portrait) orientation, so we
     * subtract the fixed landscape rotation the Activity is locked to before snapping to a
     * multiple of 90 degrees.
     */
    private View mToggleButton;
    private OrientationEventListener mOrientationEventListener;
    private int mBaseSurfaceRotationDegrees;
    // The last physical device rotation we reacted to (a multiple of 90), used to ignore jitter
    // that doesn't cross a quarter-turn boundary.
    private int mDeviceRotation = -1;
    // The button's current View rotation, kept *unwrapped* (not reduced mod 360) so that
    // animate().rotation() always takes the short way round to the next quarter turn instead of
    // spinning most of the way around.
    private float mButtonRotation = 0f;

    public static Camera2BasicFragment newInstance() {
        return new Camera2BasicFragment();
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
        // Hidden until bindCameraUseCases() below knows the resolution CameraX picked, and can
        // size this view (and the Surface it holds) to match.
        mSurfaceView.setVisibility(View.GONE);
        mToggleButton = view.findViewById(R.id.toggle);
        mToggleButton.setOnClickListener(this);

        // Draw the camera feed edge-to-edge behind the system bars and hide them, so the preview
        // truly fills the screen and there's no status/nav bar to occlude the toggle button. The
        // bars reappear transiently on an edge swipe.
        Window window = requireActivity().getWindow();
        WindowCompat.setDecorFitsSystemWindows(window, false);
        WindowInsetsControllerCompat insetsController =
                WindowCompat.getInsetsController(window, window.getDecorView());
        insetsController.hide(WindowInsetsCompat.Type.systemBars());
        insetsController.setSystemBarsBehavior(
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);

        mAnalysisExecutor = Executors.newSingleThreadExecutor();

        mBaseSurfaceRotationDegrees =
                surfaceRotationDegrees(requireActivity().getWindowManager().getDefaultDisplay()
                        .getRotation());
        mOrientationEventListener = new OrientationEventListener(requireContext()) {
            @Override
            public void onOrientationChanged(int orientation) {
                if (orientation == ORIENTATION_UNKNOWN) {
                    return;
                }
                // Snap the device's physical rotation (clockwise from its natural orientation) to
                // the nearest quarter turn, ignoring jitter that doesn't cross a boundary.
                int deviceRotation = Math.round(orientation / 90f) * 90 % 360;
                if (deviceRotation == mDeviceRotation) {
                    return;
                }
                mDeviceRotation = deviceRotation;
                // The Activity is landscape-locked, so the window itself never rotates -- it's
                // drawn at a fixed offset (mBaseSurfaceRotationDegrees) from the device's natural
                // orientation. Counter-rotate the button by the device's physical rotation, backing
                // out that fixed window offset, so it stays upright against gravity in every hold.
                int target = (360 - mBaseSurfaceRotationDegrees - deviceRotation) % 360;
                // Move to whichever multiple-of-360 representative of `target` is nearest the
                // button's current (unwrapped) rotation, so the animation is always a <=90-degree
                // turn rather than a near-full-circle spin.
                float delta = (((target - mButtonRotation) % 360) + 360) % 360;
                if (delta > 180f) {
                    delta -= 360f;
                }
                mButtonRotation += delta;
                mToggleButton.animate().rotation(mButtonRotation).setDuration(200).start();
            }
        };
        mOrientationEventListener.enable();

        // Capture the view's LifecycleOwner now, while it's valid: if the future below resolves
        // after this fragment's view has been destroyed, CameraX safely no-ops when handed an
        // already-destroyed LifecycleOwner, but getViewLifecycleOwner() itself would throw once
        // the view is gone.
        final LifecycleOwner lifecycleOwner = getViewLifecycleOwner();
        final ListenableFuture<ProcessCameraProvider> cameraProviderFuture =
                ProcessCameraProvider.getInstance(requireContext());
        cameraProviderFuture.addListener(() -> {
            try {
                bindCameraUseCases(cameraProviderFuture.get(), lifecycleOwner);
            } catch (ExecutionException | InterruptedException e) {
                Log.e(TAG, "Failed to obtain a camera provider.", e);
                new ErrorDialog().show(getParentFragmentManager(), "dialog");
            }
        }, ContextCompat.getMainExecutor(requireContext()));
    }

    /**
     * Binds an {@link ImageAnalysis} use case that hands us YUV frames for Halide to process,
     * then wires up {@link #mSurfaceView} (already {@code match_parent}) to display the result.
     */
    private void bindCameraUseCases(ProcessCameraProvider cameraProvider,
                                    LifecycleOwner lifecycleOwner) {
        ResolutionSelector resolutionSelector = new ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
                .setResolutionStrategy(new ResolutionStrategy(DESIRED_ANALYSIS_SIZE,
                        ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER))
                .build();

        ImageAnalysis imageAnalysis = new ImageAnalysis.Builder()
                .setResolutionSelector(resolutionSelector)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
                // Halide processing can't always keep up with the sensor; drop stale frames
                // instead of queueing them, same as the old acquireLatestImage()-based pipeline.
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build();
        imageAnalysis.setAnalyzer(mAnalysisExecutor, this::analyzeFrame);

        try {
            cameraProvider.bindToLifecycle(
                    lifecycleOwner, CameraSelector.DEFAULT_BACK_CAMERA, imageAnalysis);
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Could not bind to a suitable camera.", e);
            new ErrorDialog().show(getParentFragmentManager(), "dialog");
            return;
        }

        Size size = imageAnalysis.getResolutionInfo().getResolution();
        Log.d(TAG, "CameraX selected analysis size: " + size);
        // Deliberately not AutoFitSurfaceView.setAspectRatio(): the view fills the screen
        // (fragment_camera2_basic.xml), and setFixedSize()'s buffer/display decoupling below
        // (see the README's ANativeWindow CAVEAT) lets the system scale the analysis-resolution
        // buffer to fit, at the cost of a small stretch if the screen's aspect ratio doesn't
        // exactly match the 16:9 analysis stream.
        SurfaceHolder holder = mSurfaceView.getHolder();
        holder.setFormat(ImageFormat.YV12);
        holder.setFixedSize(size.getWidth(), size.getHeight());
        holder.addCallback(mSurfaceCallback);
        mSurfaceView.setVisibility(View.VISIBLE);
    }

    /**
     * Runs on {@link #mAnalysisExecutor} for every camera frame: processes it with the selected
     * Halide filter and writes the result into {@link #mSurface}.
     */
    private void analyzeFrame(ImageProxy imageProxy) {
        try {
            if (mSurface == null) {
                return;
            }

            NativeSurfaceHandle dstSurface = NativeSurfaceHandle.lockSurface(mSurface);
            if (dstSurface == null) {
                return;
            }

            try {
                HalideYuvBufferT srcYuv = HalideYuvBufferT.fromImage(imageProxy.getImage());
                try {
                    if (imageProxy.getImageInfo().getRotationDegrees() >= 180) {
                        srcYuv.rotate180();
                    }

                    HalideYuvBufferT dstYuv = dstSurface.allocNativeYuvBufferT();
                    if (mUseEdgeDetector) {
                        HalideFilters.edgeDetect(srcYuv, dstYuv);
                    } else {
                        HalideFilters.copy(srcYuv, dstYuv);
                    }
                    dstYuv.close();
                } finally {
                    srcYuv.close();
                }
            } finally {
                dstSurface.close();
            }
        } finally {
            imageProxy.close();
        }
    }

    @Override
    public void onDestroyView() {
        Log.d(TAG, "onDestroyView");
        mSurfaceView.getHolder().removeCallback(mSurfaceCallback);
        mOrientationEventListener.disable();
        mAnalysisExecutor.shutdown();
        super.onDestroyView();
    }

    private static int surfaceRotationDegrees(int surfaceRotation) {
        switch (surfaceRotation) {
            case Surface.ROTATION_90:
                return 90;
            case Surface.ROTATION_180:
                return 180;
            case Surface.ROTATION_270:
                return 270;
            default:
                return 0;
        }
    }

    @Override
    public void onClick(View view) {
        if (view.getId() == R.id.toggle) {
            mUseEdgeDetector = !mUseEdgeDetector;
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
}
