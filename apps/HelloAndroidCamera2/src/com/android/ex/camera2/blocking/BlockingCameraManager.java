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
package com.android.ex.camera2.blocking;

import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.os.ConditionVariable;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.android.ex.camera2.exceptions.TimeoutRuntimeException;

import java.util.Objects;

/**
 * Expose {@link CameraManager} functionality with blocking functions.
 *
 * <p>Safe to use at the same time as the regular CameraManager, so this does not
 * duplicate any functionality that is already blocking.</p>
 *
 * <p>Be careful when using this from UI thread! This function will typically block
 * for about 500ms when successful, and as long as {@value #OPEN_TIME_OUT_MS}ms when timing out.</p>
 */
public class BlockingCameraManager {

    private static final String TAG = "BlockingCameraManager";
    private static final boolean VERBOSE = Log.isLoggable(TAG, Log.VERBOSE);

    private static final int OPEN_TIME_OUT_MS = 2000; // ms time out for openCamera

    /**
     * Exception thrown by {@link #openCamera} if the open fails asynchronously.
     */
    public static class BlockingOpenException extends Exception {
        /**
         * Suppress Eclipse warning
         */
        private static final long serialVersionUID = 12397123891238912L;

        public static final int ERROR_DISCONNECTED = 0; // Does not clash with ERROR_...

        private final int mError;

        public boolean wasDisconnected() {
            return mError == ERROR_DISCONNECTED;
        }

        public boolean wasError() {
            return mError != ERROR_DISCONNECTED;
        }

        /**
         * Returns the error code {@link ERROR_DISCONNECTED} if disconnected, or one of
         * {@code CameraDevice.StateCallback#ERROR_*} if there was another error.
         *
         * @return int Disconnect/error code
         */
        public int getCode() {
            return mError;
        }

        /**
         * Thrown when camera device enters error state during open, or if
         * it disconnects.
         *
         * @param errorCode
         * @param message
         *
         * @see {@link CameraDevice.StateCallback#ERROR_CAMERA_DEVICE}
         */
        public BlockingOpenException(int errorCode, String message) {
            super(message);
            mError = errorCode;
        }
    }

    private final CameraManager mManager;

    /**
     * Create a new blocking camera manager.
     *
     * @param manager
     *            CameraManager returned by
     *            {@code Context.getSystemService(Context.CAMERA_SERVICE)}
     */
    public BlockingCameraManager(CameraManager manager) {
        if (manager == null) {
            throw new IllegalArgumentException("manager must not be null");
        }
        mManager = manager;
    }

    /**
     * Open the camera, blocking it until it succeeds or fails.
     *
     * <p>Note that the Handler provided must not be null. Furthermore, if there is a handler,
     * its Looper must not be the current thread's Looper. Otherwise we'd never receive
     * the callbacks from the CameraDevice since this function would prevent them from being
     * processed.</p>
     *
     * <p>Throws {@link CameraAccessException} for the same reason {@link CameraManager#openCamera}
     * does.</p>
     *
     * <p>Throws {@link BlockingOpenException} when the open fails asynchronously (due to
     * {@link CameraDevice.StateCallback#onDisconnected(CameraDevice)} or
     * ({@link CameraDevice.StateCallback#onError(CameraDevice)}.</p>
     *
     * <p>Throws {@link TimeoutRuntimeException} if opening times out. This is usually
     * highly unrecoverable, and all future calls to opening that camera will fail since the
     * service will think it's busy. This class will do its best to clean up eventually.</p>
     *
     * @param cameraId
     *            Id of the camera
     * @param listener
     *            Listener to the camera. onOpened, onDisconnected, onError need not be implemented.
     * @param handler
     *            Handler which to run the listener on. Must not be null.
     *
     * @return CameraDevice
     *
     * @throws IllegalArgumentException
     *            If the handler is null, or if the handler's looper is current.
     * @throws CameraAccessException
     *            If open fails immediately.
     * @throws BlockingOpenException
     *            If open fails after blocking for some amount of time.
     * @throws TimeoutRuntimeException
     *            If opening times out. Typically unrecoverable.
     */
    public CameraDevice openCamera(String cameraId, CameraDevice.StateCallback listener,
            Handler handler) throws CameraAccessException, BlockingOpenException {

        if (handler == null) {
            throw new IllegalArgumentException("handler must not be null");
        } else if (handler.getLooper() == Looper.myLooper()) {
            throw new IllegalArgumentException("handler's looper must not be the current looper");
        }

        return (new OpenListener(mManager, cameraId, listener, handler)).blockUntilOpen();
    }

    private static void assertEquals(Object a, Object b) {
        if (!Objects.equals(a, b)) {
            throw new AssertionError("Expected " + a + ", but got " + b);
        }
    }

    /**
     * Block until CameraManager#openCamera finishes with onOpened/onError/onDisconnected
     *
     * <p>Pass-through all StateCallback changes to the proxy.</p>
     *
     * <p>Time out after {@link #OPEN_TIME_OUT_MS} and unblock. Clean up camera if it arrives
     * later.</p>
     */
    private class OpenListener extends CameraDevice.StateCallback {
        private static final int ERROR_UNINITIALIZED = -1;

        private final String mCameraId;

        private final CameraDevice.StateCallback mProxy;

        private final Object mLock = new Object();
        private final ConditionVariable mDeviceReady = new ConditionVariable();

        private CameraDevice mDevice = null;
        private boolean mSuccess = false;
        private int mError = ERROR_UNINITIALIZED;
        private boolean mDisconnected = false;

        private boolean mNoReply = true; // Start with no reply until proven otherwise
        private boolean mTimedOut = false;

        OpenListener(CameraManager manager, String cameraId,
                CameraDevice.StateCallback listener, Handler handler)
                throws CameraAccessException {
            mCameraId = cameraId;
            mProxy = listener;
            manager.openCamera(cameraId, this, handler);
        }

        // Freebie check to make sure we aren't calling functions multiple times.
        // We should still test the state interactions in a separate more thorough test.
        private void assertInitialState() {
            assertEquals(null, mDevice);
            assertEquals(false, mDisconnected);
            assertEquals(ERROR_UNINITIALIZED, mError);
            assertEquals(false, mSuccess);
        }

        @Override
        public void onOpened(CameraDevice camera) {
            if (VERBOSE) {
                Log.v(TAG, "onOpened: camera " + ((camera != null) ? camera.getId() : "null"));
            }

            synchronized (mLock) {
                assertInitialState();
                mNoReply = false;
                mSuccess = true;
                mDevice = camera;
                mDeviceReady.open();

                if (mTimedOut && camera != null) {
                    camera.close();
                    return;
                }
            }

            if (mProxy != null) mProxy.onOpened(camera);
        }

        @Override
        public void onDisconnected(CameraDevice camera) {
            if (VERBOSE) {
                Log.v(TAG, "onDisconnected: camera "
                        + ((camera != null) ? camera.getId() : "null"));
            }

            synchronized (mLock) {
                assertInitialState();
                mNoReply = false;
                mDisconnected = true;
                mDevice = camera;
                mDeviceReady.open();

                if (mTimedOut && camera != null) {
                    camera.close();
                    return;
                }
            }

            if (mProxy != null) mProxy.onDisconnected(camera);
        }

        @Override
        public void onError(CameraDevice camera, int error) {
            if (VERBOSE) {
                Log.v(TAG, "onError: camera " + ((camera != null) ? camera.getId() : "null"));
            }

            if (error <= 0) {
                throw new AssertionError("Expected error to be a positive number");
            }

            synchronized (mLock) {
                // Don't assert initial state. Error can happen later.
                mNoReply = false;
                mError = error;
                mDevice = camera;
                mDeviceReady.open();

                if (mTimedOut && camera != null) {
                    camera.close();
                    return;
                }
            }

            if (mProxy != null) mProxy.onError(camera, error);
        }

        @Override
        public void onClosed(CameraDevice camera) {
            if (mProxy != null) mProxy.onClosed(camera);
        }

        CameraDevice blockUntilOpen() throws BlockingOpenException {
            /**
             * Block until onOpened, onError, or onDisconnected
             */
            if (!mDeviceReady.block(OPEN_TIME_OUT_MS)) {

                synchronized (mLock) {
                    if (mNoReply) { // Give the async camera a fighting chance (required)
                        mTimedOut = true; // Clean up camera if it ever arrives later
                        throw new TimeoutRuntimeException(String.format(
                                "Timed out after %d ms while trying to open camera device %s",
                                OPEN_TIME_OUT_MS, mCameraId));
                    }
                }
            }

            synchronized (mLock) {
                /**
                 * Determine which state we ended up in:
                 *
                 * - Throw exceptions for onError/onDisconnected
                 * - Return device for onOpened
                 */
                if (!mSuccess && mDevice != null) {
                    mDevice.close();
                }

                if (mSuccess) {
                    return mDevice;
                } else {
                    if (mDisconnected) {
                        throw new BlockingOpenException(
                                BlockingOpenException.ERROR_DISCONNECTED,
                                "Failed to open camera device: it is disconnected");
                    } else if (mError != ERROR_UNINITIALIZED) {
                        throw new BlockingOpenException(
                                mError,
                                "Failed to open camera device: error code " + mError);
                    } else {
                        throw new AssertionError("Failed to open camera device (impl bug)");
                    }
                }
            }
        }
    }
}
