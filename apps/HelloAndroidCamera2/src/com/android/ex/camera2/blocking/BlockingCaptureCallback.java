/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.ex.camera2.blocking;

import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CaptureFailure;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.util.Log;

import com.android.ex.camera2.utils.StateChangeListener;
import com.android.ex.camera2.utils.StateWaiter;

/**
 * A camera capture listener that implements blocking operations on state changes for a
 * particular capture request.
 *
 * <p>Provides a waiter that can be used to block until the next unobserved state of the
 * requested type arrives.</p>
 *
 * <p>Pass-through all StateListener changes to the proxy.</p>
 *
 * @see #getStateWaiter
 */
public class BlockingCaptureCallback extends CameraCaptureSession.CaptureCallback {

    /**
     * {@link #onCaptureStarted} has been called.
     */
    public static final int CAPTURE_STARTED = 0;

    /**
     * {@link #onCaptureProgressed} has been
     * called.
     */
    public static final int CAPTURE_PROGRESSED = 1;

    /**
     * {@link #onCaptureCompleted} has
     * been called.
     */
    public static final int CAPTURE_COMPLETED = 2;

    /**
     * {@link #onCaptureFailed} has been
     * called.
     */
    public static final int CAPTURE_FAILED = 3;

    /**
     * {@link #onCaptureSequenceCompleted} has been called.
     */
    public static final int CAPTURE_SEQUENCE_COMPLETED = 4;

    /**
     * {@link #onCaptureSequenceAborted} has been called.
     */
    public static final int CAPTURE_SEQUENCE_ABORTED = 5;

    private static final String[] sStateNames = {
            "CAPTURE_STARTED",
            "CAPTURE_PROGRESSED",
            "CAPTURE_COMPLETED",
            "CAPTURE_FAILED",
            "CAPTURE_SEQUENCE_COMPLETED",
            "CAPTURE_SEQUENCE_ABORTED"
    };

    private static final String TAG = "BlockingCaptureCallback";
    private static final boolean VERBOSE = Log.isLoggable(TAG, Log.VERBOSE);

    private final CameraCaptureSession.CaptureCallback mProxy;

    private final StateWaiter mStateWaiter = new StateWaiter(sStateNames);
    private final StateChangeListener mStateChangeListener = mStateWaiter.getListener();

    /**
     * Create a blocking capture listener without forwarding the capture listener invocations
     * to another capture listener.
     */
    public BlockingCaptureCallback() {
        mProxy = null;
    }

    /**
     * Create a blocking capture listener; forward original listener invocations
     * into {@code listener}.
     *
     * @param listener a non-{@code null} listener to forward invocations into
     *
     * @throws NullPointerException if {@code listener} was {@code null}
     */
    public BlockingCaptureCallback(CameraCaptureSession.CaptureCallback listener) {
        if (listener == null) {
            throw new NullPointerException("listener must not be null");
        }
        mProxy = listener;
    }

    /**
     * Acquire the state waiter; can be used to block until a set of state transitions have
     * been reached.
     *
     * <p>Only one thread should wait at a time.</p>
     */
    public StateWaiter getStateWaiter() {
        return mStateWaiter;
    }

    @Override
    public void onCaptureStarted(CameraCaptureSession session, CaptureRequest request,
                                 long timestamp, long frameNumber) {
        if (mProxy != null) mProxy.onCaptureStarted(session, request, timestamp, frameNumber);
        mStateChangeListener.onStateChanged(CAPTURE_STARTED);
    }

    @Override
    public void onCaptureProgressed(CameraCaptureSession session, CaptureRequest request,
                                    CaptureResult partialResult) {
        if (mProxy != null) mProxy.onCaptureProgressed(session, request, partialResult);
        mStateChangeListener.onStateChanged(CAPTURE_PROGRESSED);
    }

    @Override
    public void onCaptureCompleted(CameraCaptureSession session, CaptureRequest request,
                                   TotalCaptureResult result) {
        if (mProxy != null) mProxy.onCaptureCompleted(session, request, result);
        mStateChangeListener.onStateChanged(CAPTURE_COMPLETED);
    }

    @Override
    public void onCaptureFailed(CameraCaptureSession session, CaptureRequest request,
                                CaptureFailure failure) {
        if (mProxy != null) mProxy.onCaptureFailed(session, request, failure);
        mStateChangeListener.onStateChanged(CAPTURE_FAILED);
    }

    @Override
    public void onCaptureSequenceCompleted(CameraCaptureSession session, int sequenceId,
                                           long frameNumber) {
        if (mProxy != null) mProxy.onCaptureSequenceCompleted(session, sequenceId, frameNumber);
        mStateChangeListener.onStateChanged(CAPTURE_SEQUENCE_COMPLETED);
    }

    @Override
    public void onCaptureSequenceAborted(CameraCaptureSession session, int sequenceId) {
        if (mProxy != null) mProxy.onCaptureSequenceAborted(session, sequenceId);
        mStateChangeListener.onStateChanged(CAPTURE_SEQUENCE_ABORTED);
    }
}
