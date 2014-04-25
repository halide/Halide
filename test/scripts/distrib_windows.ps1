# Set the root dir of the Halide checkout
$ROOT = "C:\Code\Halide"

$ErrorActionPreference = "Continue"

# Requires:
#  subversion for windows
#  cmake for windows
#  7-Zip
#  git for windows
#  Visual Studio express 2013
#  .Net framework 4.5.1
#  Microsoft Build Tools 2013
#  llvm trunk checkout via svn in ROOT\llvm
#  clang trunk checkout via svn in ROOT\llvm\tools\clang

# Add the relevant tools to the path
$env:PATH += ";C:\Program Files (x86)\Subversion\bin"
$env:PATH += ";C:\Program Files (x86)\CMake 2.8\bin"
$env:PATH += ";C:\Program Files (x86)\Git\bin"
$env:PATH += ";C:\Program Files (x86)\7-Zip"
$env:PATH += ";C:\Program Files (x86)\MSBuild\12.0\bin"

# Update source
svn up $ROOT\llvm\tools\clang
svn up $ROOT\llvm
git pull

# Build latest llvm
cd $ROOT\llvm
if (! (Test-Path build-64)) {
  mkdir build-64
}
cd build-64
cmake -D LLVM_ENABLE_TERMINFO=OFF -D LLVM_TARGETS_TO_BUILD='X86;ARM;NVPTX;AArch64' -D LLVM_ENABLE_ASSERTIONS=ON -D CMAKE_BUILD_TYPE=Release -G "Visual Studio 12 Win64" ..
MSBuild.exe /t:Build /p:Configuration="Release" .\ALL_BUILD.vcxproj

# Build Halide
cd $ROOT
if (! (Test-Path build)) {
  mkdir build
}
cd build
cmake -D LLVM_BIN=$ROOT\llvm\build-64\Release\bin -D LLVM_INCLUDE="$ROOT\llvm\include;$ROOT\llvm\build-64\include" -D LLVM_LIB=$ROOT\llvm\build-64\Release\lib -D LLVM_VERSION=35 -D TARGET_ARM=ON -D TARGET_NATIVE_CLIENT=OFF -D TARGET_OPENCL=ON -D TARGET_PTX=ON -D TARGET_SPIR=ON -D TARGET_X86=ON -D WITH_TEST_CORRECTNESS=ON -D WITH_TEST_ERROR=ON -D WITH_TEST_WARNING=ON -D WITH_TEST_PERFORMANCE=ON -D HALIDE_SHARED_LIBRARY=ON -G "Visual Studio 12 Win64" ..
MSBuild.exe /t:Build /p:Configuration="Release" .\All_BUILD.vcxproj
if ($LastExitCode) {
  echo "Build failed!"
  exit $LastExitCode
}


# Run the tests
$env:HL_JIT_TARGET = "host"

cd $ROOT\build\bin\Release

Get-ChildItem . -filter correctness*.exe | ForEach { 
  echo ""
  echo $_.Fullname
  &$_.Fullname
  if ($LastExitCode) {
    echo "Test failed!"
    exit $LastExitCode
  }
}

Get-ChildItem . -filter performance*.exe | ForEach { 
  echo ""
  echo $_.Fullname
  &$_.Fullname
  if ($LastExitCode) {
    echo "Test failed!"
    exit $LastExitCode
  }
}


cd $ROOT

$COMMIT = git show HEAD | head -n1 | cut -b8-
$DATE = date +%Y_%m_%d

if (! (Test-Path distrib)) {
  mkdir distrib
}
cd distrib
cp ..\build\include\Halide.h .
cp ..\build\lib\Release\Halide.lib .
cp ..\build\bin\Release\Halide.dll .
&7z a Halide_Windows_64_trunk_${COMMIT}_${DATE}.zip Halide.h Halide.lib Halide.dll
