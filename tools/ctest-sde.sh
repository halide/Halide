#!/bin/bash

LOGFILE="$1"
shift

die () {
  echo "$@"
  exit 1
}

[[ $LOGFILE == --log-file=* ]] || die "Expected --log-file=* as first argument!"

LOGFILE=${LOGFILE#--log-file=}
touch "$LOGFILE"

[[ -z "${INTEL_SDE}" ]] && INTEL_SDE=$(dirname "$(realpath -s "$BASH_SOURCE")")/intel-sde/sde64
[[ -f "${INTEL_SDE}" ]] || die "$INTEL_SDE is not a valid path"

$INTEL_SDE $@

