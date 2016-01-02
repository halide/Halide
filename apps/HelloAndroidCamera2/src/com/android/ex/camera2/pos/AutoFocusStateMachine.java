/*
 * Copyright 2013 The Android Open Source Project
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
package com.android.ex.camera2.pos;

import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CaptureResult.Key;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.util.Log;

import com.android.ex.camera2.utils.SysTrace;

/**
 * Manage the auto focus state machine for CameraDevice.
 *
 * <p>Requests are created only when the AF needs to be manipulated from the user,
 * but automatic camera-caused AF state changes are broadcasted from any new result.</p>
 */
public class AutoFocusStateMachine {

    /**
     * Observe state AF state transitions triggered by
     * {@link AutoFocusStateMachine#onCaptureCompleted onCaptureCompleted}.
     */
    public interface AutoFocusStateListener {
        /**
         * The camera is currently focused (either active or passive).
         *
         * @param locked True if the lens has been locked from moving, false otherwise.
         */
        void onAutoFocusSuccess(CaptureResult result, boolean locked);

        /**
         * The camera is currently not focused (either active or passive).
         *
         * @param locked False if the AF is still scanning, true if needs a restart.
         */
        void onAutoFocusFail(CaptureResult result, boolean locked);

        /**
         * The camera is currently scanning (either active or passive)
         * and has not yet converged.
         *
         * <p>This is not called for results where the AF either succeeds or fails.</p>
         */
        void onAutoFocusScan(CaptureResult result);

        /**
         * The camera is currently not doing anything with the autofocus.
         *
         * <p>Autofocus could be off, or this could be an intermediate state transition as
         * scanning restarts.</p>
         */
        void onAutoFocusInactive(CaptureResult result);
    }

    private static final String TAG = "AutoFocusStateMachine";
    private static final boolean DEBUG_LOGGING = Log.isLoggable(TAG, Log.DEBUG);
    private static final boolean VERBOSE_LOGGING = Log.isLoggable(TAG, Log.VERBOSE);
    private static final int AF_UNINITIALIZED = -1;

    private final AutoFocusStateListener mListener;
    private int mLastAfState = AF_UNINITIALIZED;
    private int mLastAfMode = AF_UNINITIALIZED;
    private int mCurrentAfMode = AF_UNINITIALIZED;
    private int mCurrentAfTrigger = AF_UNINITIALIZED;

    private int mCurrentAfCookie = AF_UNINITIALIZED;
    private String mCurrentAfTrace = "";
    private int mLastAfCookie = 0;

    public AutoFocusStateMachine(AutoFocusStateListener listener) {
        if (listener == null) {
            throw new IllegalArgumentException("listener should not be null");
        }
        mListener = listener;
    }

    /**
     * Invoke every time we get a new CaptureResult via
     * {@link CameraDevice.CaptureCallback#onCaptureCompleted}.
     *
     * <p>This function is responsible for dispatching updates via the
     * {@link AutoFocusStateListener} so without calling this on a regular basis, no
     * AF changes will be observed.</p>
     *
     * @param result CaptureResult
     */
    public synchronized void onCaptureCompleted(CaptureResult result) {

        /**
         * Work-around for b/11269834
         * Although these should never-ever happen, harden for ship
         */
        if (result == null) {
            Log.w(TAG, "onCaptureCompleted - missing result, skipping AF update");
            return;
        }

        Key<Integer> keyAfState = CaptureResult.CONTROL_AF_STATE;
        if (keyAfState == null) {
            Log.e(TAG, "onCaptureCompleted - missing android.control.afState key, " +
                    "skipping AF update");
            return;
        }

        Key<Integer> keyAfMode = CaptureResult.CONTROL_AF_MODE;
        if (keyAfMode == null) {
            Log.e(TAG, "onCaptureCompleted - missing android.control.afMode key, " +
                    "skipping AF update");
            return;
        }

        Integer afState = result.get(CaptureResult.CONTROL_AF_STATE);
        Integer afMode = result.get(CaptureResult.CONTROL_AF_MODE);

        /**
         * Work-around for b/11238865
         * This is a HAL bug as these fields should be there always.
         */
        if (afState == null) {
            Log.w(TAG, "onCaptureCompleted - missing android.control.afState !");
            return;
        } else if (afMode == null) {
            Log.w(TAG, "onCaptureCompleted - missing android.control.afMode !");
            return;
        }

        if (DEBUG_LOGGING) Log.d(TAG, "onCaptureCompleted - new AF mode = " + afMode +
                " new AF state = " + afState);

        if (mLastAfState == afState && afMode == mLastAfMode) {
            // Same AF state as last time, nothing else needs to be done.
            return;
        }

        if (VERBOSE_LOGGING) Log.v(TAG, "onCaptureCompleted - new AF mode = " + afMode +
                " new AF state = " + afState);

        mLastAfState = afState;
        mLastAfMode = afMode;

        switch (afState) {
            case CaptureResult.CONTROL_AF_STATE_FOCUSED_LOCKED:
                mListener.onAutoFocusSuccess(result, /*locked*/true);
                endTraceAsync();
                break;
            case CaptureResult.CONTROL_AF_STATE_NOT_FOCUSED_LOCKED:
                mListener.onAutoFocusFail(result, /*locked*/true);
                endTraceAsync();
                break;
            case CaptureResult.CONTROL_AF_STATE_PASSIVE_FOCUSED:
                mListener.onAutoFocusSuccess(result, /*locked*/false);
                break;
            case CaptureResult.CONTROL_AF_STATE_PASSIVE_UNFOCUSED:
                mListener.onAutoFocusFail(result, /*locked*/false);
                break;
            case CaptureResult.CONTROL_AF_STATE_ACTIVE_SCAN:
                mListener.onAutoFocusScan(result);
                break;
            case CaptureResult.CONTROL_AF_STATE_PASSIVE_SCAN:
                mListener.onAutoFocusScan(result);
                break;
            case CaptureResult.CONTROL_AF_STATE_INACTIVE:
                mListener.onAutoFocusInactive(result);
                break;
        }
    }

    /**
     * Reset the current AF state.
     *
     * <p>
     * When dropping capture results (by not invoking {@link #onCaptureCompleted} when a new
     * {@link CaptureResult} is available), call this function to reset the state. Otherwise
     * the next time a new state is observed this class may incorrectly consider it as the same
     * state as before, and not issue any callbacks by {@link AutoFocusStateListener}.
     * </p>
     */
    public synchronized void resetState() {
        if (VERBOSE_LOGGING) Log.v(TAG, "resetState - last state was " + mLastAfState);

        mLastAfState = AF_UNINITIALIZED;
    }

    /**
     * Lock the lens from moving. Typically used before taking a picture.
     *
     * <p>After calling this function, submit the new requestBuilder as a separate capture.
     * Do not submit it as a repeating request or the AF lock will be repeated every time.</p>
     *
     * <p>Create a new repeating request from repeatingBuilder and set that as the updated
     * repeating request.</p>
     *
     * <p>If the lock succeeds, {@link AutoFocusStateListener#onAutoFocusSuccess} with
     * {@code locked == true} will be invoked. If the lock fails,
     * {@link AutoFocusStateListener#onAutoFocusFail} with {@code scanning == false} will be
     * invoked.</p>
     *
     * @param repeatingBuilder Builder for a repeating request.
     * @param requestBuilder Builder for a non-repeating request.
     *
     */
    public synchronized void lockAutoFocus(CaptureRequest.Builder repeatingBuilder,
            CaptureRequest.Builder requestBuilder) {

        if (VERBOSE_LOGGING) Log.v(TAG, "lockAutoFocus");

        if (mCurrentAfMode == AF_UNINITIALIZED) {
            throw new IllegalStateException("AF mode was not enabled");
        }

        beginTraceAsync("AFSM_lockAutoFocus");

        mCurrentAfTrigger = CaptureRequest.CONTROL_AF_TRIGGER_START;

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);
        requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_IDLE);
        requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_START);
    }

    /**
     * Unlock the lens, allowing it to move again. Typically used after taking a picture.
     *
     * <p>After calling this function, submit the new requestBuilder as a separate capture.
     * Do not submit it as a repeating request or the AF lock will be repeated every time.</p>
     *
     * <p>Create a new repeating request from repeatingBuilder and set that as the updated
     * repeating request.</p>
     *
     * <p>Once the unlock takes effect, {@link AutoFocusStateListener#onAutoFocusInactive} is
     * invoked, and after that the effects depend on which mode you were in:
     * <ul>
     * <li>Passive - Scanning restarts with {@link AutoFocusStateListener#onAutoFocusScan}</li>
     * <li>Active - The lens goes back to a default position (no callbacks)</li>
     * </ul>
     * </p>
     *
     * @param repeatingBuilder Builder for a repeating request.
     * @param requestBuilder Builder for a non-repeating request.
     *
     */
    public synchronized void unlockAutoFocus(CaptureRequest.Builder repeatingBuilder,
            CaptureRequest.Builder requestBuilder) {

        if (VERBOSE_LOGGING) Log.v(TAG, "unlockAutoFocus");

        if (mCurrentAfMode == AF_UNINITIALIZED) {
            throw new IllegalStateException("AF mode was not enabled");
        }

        mCurrentAfTrigger = CaptureRequest.CONTROL_AF_TRIGGER_CANCEL;

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);
        requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_IDLE);
        requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_CANCEL);
    }

    /**
     * Enable active auto focus, immediately triggering a converging scan.
     *
     * <p>This is typically only used when locking the passive AF has failed.</p>
     *
     * <p>Once active AF scanning starts, {@link AutoFocusStateListener#onAutoFocusScan} will be
     * invoked.</p>
     *
     * <p>If the active scan succeeds, {@link AutoFocusStateListener#onAutoFocusSuccess} with
     * {@code locked == true} will be invoked. If the active scan fails,
     * {@link AutoFocusStateListener#onAutoFocusFail} with {@code scanning == false} will be
     * invoked.</p>
     *
     * <p>After calling this function, submit the new requestBuilder as a separate capture.
     * Do not submit it as a repeating request or the AF trigger will be repeated every time.</p>
     *
     * <p>Create a new repeating request from repeatingBuilder and set that as the updated
     * repeating request.</p>
     *
     * @param repeatingBuilder Builder for a repeating request.
     * @param requestBuilder Builder for a non-repeating request.
     *
     * @param repeatingBuilder Builder for a repeating request.
     */
    public synchronized void setActiveAutoFocus(CaptureRequest.Builder repeatingBuilder,
            CaptureRequest.Builder requestBuilder) {
        if (VERBOSE_LOGGING) Log.v(TAG, "setActiveAutoFocus");

        beginTraceAsync("AFSM_setActiveAutoFocus");

        mCurrentAfMode = CaptureRequest.CONTROL_AF_MODE_AUTO;

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);
        requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_IDLE);
        requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_TRIGGER_START);
    }

    /**
     * Enable passive autofocus, immediately triggering a non-converging scan.
     *
     * <p>While passive autofocus is enabled, use {@link #lockAutoFocus} to lock
     * the lens before taking a picture. Once a picture is taken, use {@link #unlockAutoFocus}
     * to let the lens go back into passive scanning.</p>
     *
     * <p>Once passive AF scanning starts, {@link AutoFocusStateListener#onAutoFocusScan} will be
     * invoked.</p>
     *
     * @param repeatingBuilder Builder for a repeating request.
     * @param picture True for still capture AF, false for video AF.
     */
    public synchronized void setPassiveAutoFocus(boolean picture,
            CaptureRequest.Builder repeatingBuilder) {
        if (VERBOSE_LOGGING) Log.v(TAG, "setPassiveAutoFocus - picture " + picture);

        if (picture) {
            mCurrentAfMode = CaptureResult.CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        } else {
            mCurrentAfMode = CaptureResult.CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        }

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);
    }

    private synchronized void beginTraceAsync(String sectionName) {
        if (mCurrentAfCookie != AF_UNINITIALIZED) {
            // Terminate any currently active async sections before beginning another section
            SysTrace.endSectionAsync(mCurrentAfTrace, mCurrentAfCookie);
        }

        mLastAfCookie++;
        mCurrentAfCookie = mLastAfCookie;
        mCurrentAfTrace = sectionName;

        SysTrace.beginSectionAsync(sectionName, mCurrentAfCookie);
    }

    private synchronized void endTraceAsync() {
        if (mCurrentAfCookie == AF_UNINITIALIZED) {
            Log.w(TAG, "endTraceAsync - no current trace active");
            return;
        }

        SysTrace.endSectionAsync(mCurrentAfTrace, mCurrentAfCookie);
        mCurrentAfCookie = AF_UNINITIALIZED;
    }

    /**
     * Update the repeating request with current focus mode.
     *
     * <p>This is typically used when a new repeating request is created to update preview with
     * new metadata (i.e. crop region). The current auto focus mode needs to be carried over for
     * correct auto focus behavior.<p>
     *
     * @param repeatingBuilder Builder for a repeating request.
     */
    public synchronized void updateCaptureRequest(CaptureRequest.Builder repeatingBuilder) {
        if (repeatingBuilder == null) {
            throw new IllegalArgumentException("repeatingBuilder shouldn't be null");
        }

        if (mCurrentAfMode == AF_UNINITIALIZED) {
            throw new IllegalStateException("AF mode was not enabled");
        }

        repeatingBuilder.set(CaptureRequest.CONTROL_AF_MODE, mCurrentAfMode);
    }
}
