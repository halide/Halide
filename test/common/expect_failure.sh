#!/bin/bash
#
# Simple bash script that will execute another process, which is *expected* to fail;
# this is useful mainly for running test/error and other expected-to-fail tests.
#

echo Running "$1"

if $1; then
  echo "Expected Failure from '$1', but got Success"
  exit 255
fi

echo "Success"
exit 0