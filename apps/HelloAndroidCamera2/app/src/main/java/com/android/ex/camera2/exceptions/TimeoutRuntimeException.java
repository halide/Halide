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
package com.android.ex.camera2.exceptions;

/**
 * Used instead of TimeoutException when something times out in a non-recoverable manner.
 *
 * <p>This typically happens due to a deadlock or bug in the camera service,
 * so please file a bug for this. This should never ever happen in normal operation, which is
 * why this exception is unchecked.</p>
 */
public class TimeoutRuntimeException extends RuntimeException {
    public TimeoutRuntimeException(String message) {
        super(message);
    }

    public TimeoutRuntimeException(String message, Throwable cause) {
        super(message, cause);
    }
}
