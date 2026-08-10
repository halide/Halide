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
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewGroup;

import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.resolutionselector.AspectRatioStrategy;
import androidx.camera.core.resolutionselector.ResolutionSelector;
import androidx.camera.core.resolutionselector.ResolutionStrategy;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.core.content.ContextCompat;
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

    private static final Size DESIRED_ANALYSIS_SIZE = new Size(1920, 1440);

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
        view.findViewById(R.id.toggle).setOnClickListener(this);

        mAnalysisExecutor = Executors.newSingleThreadExecutor();

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
     * then sizes {@link #mSurfaceView} to match whatever resolution CameraX settled on.
     */
    private void bindCameraUseCases(ProcessCameraProvider cameraProvider,
                                    LifecycleOwner lifecycleOwner) {
        ResolutionSelector resolutionSelector = new ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
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
        mSurfaceView.setAspectRatio(size.getWidth(), size.getHeight());
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
        mAnalysisExecutor.shutdown();
        super.onDestroyView();
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
