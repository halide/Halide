#!/bin/bash

set -e -u
echo Running $1 $2 $TEST_TMPDIR
$1 $2 $TEST_TMPDIR

# Verify expected output files exist
[ -f "$TEST_TMPDIR/haar_x.png" ] || exit -1
[ -f "$TEST_TMPDIR/inverse_haar_x.png" ] || exit -1
[ -f "$TEST_TMPDIR/daubechies_x.png" ] || exit -1
[ -f "$TEST_TMPDIR/inverse_daubechies_x.png" ] || exit -1
