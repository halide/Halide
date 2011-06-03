#!/bin/sh
# From http://nicolaspouillard.fr/ocamlbuild/ocamlbuild-user-guide.html
set -e

TARGET=convolution
# TODO: change flag paths to infer absolute version of relative path to fimage/llvm/Debug+Asserts/…
FLAGS="-cflags -I,/usr/local/lib/ocaml/ -lflags -I,/usr/local/lib/ocaml/"
OCAMLBUILD=ocamlbuild

ocb()
{
  $OCAMLBUILD $FLAGS $*
}

rule() {
  case $1 in
    clean)  ocb -clean;;
    native) ocb $TARGET.native;;
    byte)   ocb $TARGET.byte;;
    all)    ocb $TARGET.native $TARGET.byte;;
    depend) echo "Not needed.";;
    *)      echo "Unknown action $1";;
  esac;
}

if [ $# -eq 0 ]; then
  rule all
else
  while [ $# -gt 0 ]; do
    rule $1;
    shift
  done
fi
