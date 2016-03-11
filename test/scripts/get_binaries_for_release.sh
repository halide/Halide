# Grabs all the binaries needed to make a release from the public buildbot

VERSION=$1

echo "Getting binaries for a release of Halide commit " $VERSION

curl http://buildbot.halide-lang.org/  | grep $VERSION | sed "s/.*halide-/halide-/" | sed "s/zip.*/zip/" | sed "s/tgz.*/tgz/" | egrep "trunk|pnacl" | while read F; do echo $F; curl http://buildbot.halide-lang.org/${F} > ${F} ; done
