#!/usr/bin/env bash
#
# Smoke-tests an Android camera app against a running emulator/device (adb
# must already see exactly the target device). Two phases:
#
# 1. Fresh install + launch, with the target permission NOT pre-granted:
#    confirms the OS shows a runtime permission prompt. Apps that forget to
#    request a dangerous permission at runtime (relying on the manifest
#    declaration alone, which stopped being sufficient in Android 6.0) pass a
#    build but fail this immediately.
#
# 2. Grant the permission, relaunch, and confirm the app stays alive with no
#    fatal crash and none of a set of known-bad error signatures in logcat.
#    This catches native-side bugs (bad buffer setup, out-of-bounds crops,
#    etc.) that only manifest once real frames start flowing through the
#    pipeline -- invisible to a build-only check. Deliberately does *not*
#    require seeing a "pipeline completed" log line: whether the camera
#    preview surface actually negotiates a size the pipeline runs on is
#    sensitive to the emulator's screen/camera configuration, which varies
#    across environments and isn't itself what regressed here.
set -euo pipefail

if [ "$#" -lt 4 ]; then
    echo "Usage: $0 <apk> <package> <activity> <permission> [known-bad-pattern ...]" >&2
    exit 1
fi

apk=$1
package=$2
activity=$3
permission=$4
shift 4
known_bad_patterns=("FATAL EXCEPTION" "Fatal signal" "$@")

# Only one process can hold the (emulated) camera device at a time; make sure
# this app releases it on exit so a subsequent test for a different app isn't
# starved of a camera to open.
trap 'adb shell am force-stop "$package" >/dev/null 2>&1 || true' EXIT

adb uninstall "$package" >/dev/null 2>&1 || true
adb install "$apk"

echo "== Phase 1: confirm $package requests $permission at runtime =="
adb shell am start -W -n "$package/$activity"
sleep 5
top_activity=$(adb shell dumpsys activity activities | grep -m1 "topResumedActivity=")
if ! echo "$top_activity" | grep -q "permissioncontroller"; then
    echo "FAIL: $package did not prompt for $permission on first launch"
    echo "$top_activity"
    exit 1
fi
echo "OK: permission prompt shown"

echo "== Phase 2: grant permission and confirm it runs with no known-bad symptoms =="
adb shell pm grant "$package" "$permission"
adb logcat -c
adb shell am force-stop "$package"
adb shell am start -W -n "$package/$activity"
sleep 10

if ! adb shell pidof "$package" >/dev/null; then
    echo "FAIL: $package is not running after launch (crashed?)"
    adb logcat -d | tail -200
    exit 1
fi

logs=$(adb logcat -d)
pattern=$(
    IFS='|'
    echo "${known_bad_patterns[*]}"
)
if echo "$logs" | grep -qE "$pattern"; then
    echo "FAIL: found a known-bad symptom in logcat for $package"
    echo "$logs" | grep -B5 -A30 -E "$pattern"
    exit 1
fi

echo "OK: $package ran with no known-bad symptoms"
