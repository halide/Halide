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

package com.android.ex.camera2.utils;

import android.os.SystemClock;
import android.util.Log;

import com.android.ex.camera2.exceptions.TimeoutRuntimeException;

import java.util.Arrays;
import java.util.Collection;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Block until a specific state change occurs.
 *
 * <p>Provides wait calls that block until the next unobserved state of the
 * requested type arrives. Unobserved states are states that have occurred since
 * the last wait, or that will be received from the camera device in the
 * future.</p>
 *
 * <p>Thread interruptions are not supported; interrupting a thread that is either
 * waiting with {@link #waitForState} / {@link #waitForAnyOfStates} or is currently in
 * {@link StateChangeListener#onStateChanged} (provided by {@link #getListener}) will result in an
 * {@link UnsupportedOperationException} being raised on that thread.</p>
 */
public final class StateWaiter {

    private static final String TAG = "StateWaiter";
    private static final boolean VERBOSE = Log.isLoggable(TAG, Log.VERBOSE);

    private final String[] mStateNames;
    private final int mStateCount;
    private final StateChangeListener mListener;

    /** Guard waitForState, waitForAnyState to only have one waiter */
    private final AtomicBoolean mWaiting = new AtomicBoolean(false);

    private final LinkedBlockingQueue<Integer> mQueuedStates = new LinkedBlockingQueue<>();

    /**
     * Create a new state waiter.
     *
     * <p>All {@code state}/{@code states} arguments used in other methods must be
     * in the range of {@code [0, stateNames.length - 1]}.</p>
     *
     * @param stateNames an array of string names, used to mark the range of the valid states
     */
    public StateWaiter(String[] stateNames) {
        mStateCount = stateNames.length;
        mStateNames = new String[mStateCount];
        System.arraycopy(stateNames, /*srcPos*/0, mStateNames, /*dstPos*/0, mStateCount);

        mListener = new StateChangeListener() {
            @Override
            public void onStateChanged(int state) {
                queueStateTransition(checkStateInRange(state));
            }
        };
    }

    public StateChangeListener getListener() {
        return mListener;
    }

    /**
     * Wait until the desired state is observed, checking all state
     * transitions since the last time a state was waited on.
     *
     * <p>Any intermediate state transitions that is not {@code state} are ignored.</p>
     *
     * <p>Note: Only one waiter allowed at a time!</p>
     *
     * @param state state to observe a transition to
     * @param timeoutMs how long to wait in milliseconds
     *
     * @throws IllegalArgumentException if {@code state} was out of range
     * @throws TimeoutRuntimeException if the desired state is not observed before timeout.
     * @throws IllegalStateException if another thread is already waiting for a state transition
     */
    public void waitForState(int state, long timeoutMs) {
        Integer[] stateArray = { checkStateInRange(state) };

        waitForAnyOfStates(Arrays.asList(stateArray), timeoutMs);
    }

    /**
     * Wait until the one of the desired {@code states} is observed, checking all
     * state transitions since the last time a state was waited on.
     *
     * <p>Any intermediate state transitions that are not in {@code states} are ignored.</p>
     *
     * <p>Note: Only one waiter allowed at a time!</p>
     *
     * @param states Set of desired states to observe a transition to.
     * @param timeoutMs how long to wait in milliseconds
     *
     * @return the state reached
     *
     * @throws IllegalArgumentException if {@code state} was out of range
     * @throws TimeoutRuntimeException if none of the states is observed before timeout.
     * @throws IllegalStateException if another thread is already waiting for a state transition
     */
    public int waitForAnyOfStates(Collection<Integer> states, final long timeoutMs) {
        checkStateCollectionInRange(states);

        // Acquire exclusive waiting privileges
        if (mWaiting.getAndSet(true)) {
            throw new IllegalStateException("Only one waiter allowed at a time");
        }

        Integer nextState = null;
        try {
            if (VERBOSE) {
                StringBuilder s = new StringBuilder("Waiting for state(s) ");
                appendStateNames(s, states);
                Log.v(TAG, s.toString());
            }

            long timeoutLeft = timeoutMs;
            long startMs = SystemClock.elapsedRealtime();
            while ((nextState = mQueuedStates.poll(timeoutLeft, TimeUnit.MILLISECONDS)) != null) {
                if (VERBOSE) {
                    Log.v(TAG, "  Saw transition to " + getStateName(nextState));
                }

                if (states.contains(nextState)) {
                    break;
                }

                long endMs = SystemClock.elapsedRealtime();
                timeoutLeft -= (endMs - startMs);
                startMs = endMs;
            }
        } catch (InterruptedException e) {
            throw new UnsupportedOperationException("Does not support interrupts on waits", e);
        } finally {
            // Release exclusive waiting privileges
            mWaiting.set(false);
        }

        if (!states.contains(nextState)) {
            StringBuilder s = new StringBuilder("Timed out after ");
            s.append(timeoutMs);
            s.append(" ms waiting for state(s) ");
            appendStateNames(s, states);

            throw new TimeoutRuntimeException(s.toString());
        }

        return nextState;
    }

    /**
     * Convert state integer to a String
     */
    public String getStateName(int state) {
        return mStateNames[checkStateInRange(state)];
    }

    /**
     * Append all states to string
     */
    public void appendStateNames(StringBuilder s, Collection<Integer> states) {
        checkStateCollectionInRange(states);

        boolean start = true;
        for (Integer state : states) {
            if (!start) {
                s.append(" ");
            }

            s.append(getStateName(state));
            start = false;
        }
    }

    private void queueStateTransition(int state) {
        if (VERBOSE) Log.v(TAG, "setCurrentState - state now " + getStateName(state));

        try {
            mQueuedStates.put(state);
        } catch (InterruptedException e) {
            throw new UnsupportedOperationException("Unable to set current state", e);
        }
    }

    private int checkStateInRange(int state) {
        if (state < 0 || state >= mStateCount) {
            throw new IllegalArgumentException("State out of range " + state);
        }

        return state;
    }

    private Collection<Integer> checkStateCollectionInRange(Collection<Integer> states) {
        for (int state : states) {
            checkStateInRange(state);
        }

        return states;
    }

}
