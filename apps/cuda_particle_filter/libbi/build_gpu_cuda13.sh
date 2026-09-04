#!/bin/bash
# Build LibBi 1.4.5's GPU particle filter under CUDA 13 / Blackwell (sm_120).
# LibBi predates CUDA 12's removal of the texture-reference API, the legacy
# kernel-launch ABI, thrust::identity, and it bundles Thrust 1.8.2 (2015).
# This script builds against CUDA 13's own CCCL Thrust plus three small shims.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SHIM="$HERE/cuda13_shim"
CUDA=/usr/local/cuda
ARCH=sm_120

export PERL5LIB=$HOME/perl5/lib/perl5 PATH=$HOME/perl5/bin:$PATH
# Generate the model-specific tree and the bi:: framework copy (no --enable-cuda
# here: the stock automake build cannot finish under CUDA 13; we drive it below).
rm -rf "$HERE/.Gordon"
libbi filter --model-file "$HERE/Gordon.bi" --obs-file "$HERE/data.nc" \
    --init-file "$HERE/data.nc" --end-time 4 --nparticles 8 \
    --resampler metropolis --output-file /dev/null --dry-run 2>/dev/null || true
# libbi --dry-run may not exist in 1.4.5; fall back to a tiny real CPU run to
# force code generation of the build tree:
if [ ! -d "$HERE/.Gordon" ]; then
  libbi filter --model-file "$HERE/Gordon.bi" --obs-file "$HERE/data.nc" \
      --init-file "$HERE/data.nc" --end-time 4 --nparticles 8 \
      --resampler metropolis --output-file /dev/null >/dev/null 2>&1 || true
fi

B="$HERE/.Gordon/build_assert_openmp_cuda_${ARCH}"
# libbi names the CUDA build dir after CUDA_ARCH; create/enter it fresh by
# copying the generated src tree.
SRCTREE=$(find "$HERE/.Gordon" -maxdepth 2 -type d -name src | head -1)
mkdir -p "$B"; cp -r "$(dirname "$SRCTREE")"/* "$B"/ 2>/dev/null || true
cd "$B"

# The one framework source change (also back-ported to the installed master):
sed -i 's/cudaThreadSetCacheConfig(cudaFuncCachePreferL1)/cudaDeviceSetCacheConfig(cudaFuncCachePreferL1)/' src/bi/init.hpp

./configure --enable-assert --enable-openmp --enable-cuda --disable-cudafastmath \
  --disable-gpucache --disable-sse --disable-avx --disable-mpi --disable-vampir \
  --disable-single --disable-extradebug --disable-diagnostics --disable-gperftools \
  --disable-dependency-tracking \
  'CXXFLAGS=-O3 -g3 -funroll-loops' LINKFLAGS= CUDA_ARCH=$ARCH \
  "CPPFLAGS=-I$SHIM -I$CUDA/include/cccl -I/usr/include/x86_64-linux-gnu -DHAVE_CBLAS_H=1"

# Force-include the compat header (thrust::identity) into every compile.
sed -i 's#^AM_CPPFLAGS = #AM_CPPFLAGS = -include libbi_compat.hpp #' Makefile
# LibBi hand-adds unconditional `include .../*_gpu.Po`; make them optional.
sed -i -E 's|^include (src/\$\(DEPDIR\)/[A-Za-z_]+_gpu\.Po)|-include \1|' Makefile

make -j8 filter_gpu
echo "Built: $B/filter_gpu"
