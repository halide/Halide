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
package com.android.ex.camera2.utils;

import android.util.Log;

/**
 * Writes trace events to the system trace buffer.  These trace events can be
 * collected and visualized using the Systrace tool.
 *
 * <p>
 * This tracing mechanism is independent of the method tracing mechanism
 * offered by {@link Debug#startMethodTracing}.  In particular, it enables
 * tracing of events that occur across multiple processes.
 * </p>
 *
 * <p>
 * All traces are written using the <pre>APP</pre> tag.
 * </p>
 */
public final class SysTrace {

    private static final String TAG = "SysTrace";
    private static final boolean VERBOSE = Log.isLoggable(TAG, Log.VERBOSE);

    private static int sNestingLevel = 0;

    /**
     * Writes trace message to indicate the value of a given counter.
     *
     * @param counterName The counter name to appear in the trace.
     * @param counterValue The counter value.
     *
     */
    public static void traceCounter(String counterName, int counterValue) {
        if (VERBOSE) {
            Log.v(TAG, "traceCounter " + counterName + " " + counterValue);
        }
    }

    /**
     * Writes a trace message to indicate that a given section of code has begun. This call must
     * be followed by a corresponding call to {@link #endSection()} on the same thread.
     *
     * <p class="note"> At this time the vertical bar character '|', newline character '\n', and
     * null character '\0' are used internally by the tracing mechanism.  If sectionName contains
     * these characters they will be replaced with a space character in the trace.
     *
     * @param sectionName The name of the code section to appear in the trace.  This may be at
     * most 127 Unicode code units long.
     */
    public static void beginSection(String sectionName) {
        if (VERBOSE) {
            Log.v(TAG, String.format("beginSection[%d] %s", sNestingLevel, sectionName));
            sNestingLevel++;
        }
    }

    /**
     * Writes a trace message to indicate that a given section of code has
     * ended.
     * <p>
     * This call must be preceded by a corresponding call to
     * {@link #beginSection(String)}. Calling this method will mark the end of
     * the most recently begun section of code, so care must be taken to ensure
     * that beginSection / endSection pairs are properly nested and called from
     * the same thread.
     * </p>
     */
    public static void endSection() {
        if (VERBOSE) {
            sNestingLevel--;
            Log.v(TAG, String.format("endSection[%d]", sNestingLevel));
        }
    }

    /**
     * Writes a trace message to indicate that a given section of code has
     * begun.
     *
     * <p>Must be followed by a call to {@link #endSectionAsync} using the same
     * tag. Unlike {@link #beginSection} and {@link #endSection},
     * asynchronous events do not need to be nested. The name and cookie used to
     * begin an event must be used to end it.</p>
     *
     * @param methodName The method name to appear in the trace.
     * @param cookie Unique identifier for distinguishing simultaneous events
     */
    public static void beginSectionAsync(String methodName, int cookie) {
        if (VERBOSE) {
            Log.v(TAG, "beginSectionAsync " + methodName + " " + cookie);
        }
    }

    /**
     * Writes a trace message to indicate that the current method has ended.
     * Must be called exactly once for each call to {@link #beginSectionAsync}
     * using the same tag, name and cookie.
     *
     * @param methodName The method name to appear in the trace.
     * @param cookie Unique identifier for distinguishing simultaneous events
     */
    public static void endSectionAsync(String methodName, int cookie) {
        if (VERBOSE) {
            Log.v(TAG, "endSectionAsync " + methodName + " " + cookie);
        }
    }
}
