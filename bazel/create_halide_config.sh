#!/bin/bash
set -eu

# We need to capture the system libraries that we'll need to link 
# against, so that downstream consumers of our build rules don't
# have to guess what's necessary on their system; call
# llvm-config and capture the result in a Skylark macro that
# we include in our distribution.
LLVM_SYSTEM_LIBS=`${LLVM_CONFIG} --system-libs | sed -e 's/[\/&]/\\&/g'`

if [[ $1 == "bzl" ]]; then

cat <<multiline_literal_EOF
# Machine-Generated: Do Not Edit
def halide_system_libs():
  return "${LLVM_SYSTEM_LIBS}"
multiline_literal_EOF

else

echo "create_halide_config does not understand \"$1\"" > /dev/stderr
exit 1

fi
