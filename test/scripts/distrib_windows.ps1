# Set the root dir of the Halide checkout
$ROOT = "C:\Code\Halide"
cd $ROOT

$ErrorActionPreference = "Continue"

# Requires:
#  subversion for windows
#  cmake for windows
#  7-Zip
#  git for windows
#  Visual Studio express 2013
#  .Net framework 4.5.1
#  Microsoft Build Tools 2013

# Add the relevant tools to the path
$env:PATH += ";C:\Program Files (x86)\Subversion\bin"
$env:PATH += ";C:\Program Files (x86)\CMake\bin"
$env:PATH += ";C:\Program Files (x86)\CMake 2.8\bin"
$env:PATH += ";C:\Program Files (x86)\Git\bin"
$env:PATH += ";C:\Program Files (x86)\7-Zip"
$env:PATH += ";C:\Program Files (x86)\MSBuild\12.0\bin"

# Get llvm
#svn co http://llvm.org/svn/llvm-project/llvm/trunk $ROOT\llvm
#svn co http://llvm.org/svn/llvm-project/cfe/trunk $ROOT\llvm\tools\clang

#git clone http://git.chromium.org/native_client/pnacl-llvm.git $ROOT\pnacl-llvm
#git checkout


# Update llvm source
svn up $ROOT\llvm\tools\clang
svn up $ROOT\llvm

#cd $ROOT\pnacl-llvm
# This version of pnacl llvm doesn't really compile on windows.
# - Comment out '#error unknown architecture' in ResolvePNaClIntrinsics.cpp
# - Clang compiled with msvc won't work, so you need to manually get the nacl sdk, get pepper_35, and
#   copy the contents of the folder that contains clang.exe into pnacl-llvm/nacl-sdk-bin, and also copy
#   the dlls from one of the folders that contains cygwin1.dll

#git fetch
#git checkout 650319f0929eea0cb49581e2ecffa3641f11ec02
#cd $ROOT

$COMMIT = git show HEAD | head -n1 | cut -b8-
$DATE = date +%Y_%m_%d

# Build latest llvm
cd $ROOT\llvm
if (! (Test-Path build-64)) {
  mkdir build-64
}
cd build-64
cmake -D LLVM_ENABLE_TERMINFO=OFF -D LLVM_TARGETS_TO_BUILD='X86;ARM;NVPTX;AArch64' -D LLVM_ENABLE_ASSERTIONS=ON -D CMAKE_BUILD_TYPE=Release -G "Visual Studio 12 Win64" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\ALL_BUILD.vcxproj

cd $ROOT\llvm
if (! (Test-Path build-32)) { 
  mkdir build-32
}
cd build-32
cmake -D LLVM_ENABLE_TERMINFO=OFF -D LLVM_TARGETS_TO_BUILD='X86;ARM;NVPTX;AArch64' -D LLVM_ENABLE_ASSERTIONS=ON -D CMAKE_BUILD_TYPE=Release -D LLVM_BUILD_32_BITS=ON -G "Visual Studio 12" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\ALL_BUILD.vcxproj

#cd $ROOT\pnacl-llvm
#if (! (Test-Path build-64)) {
#  mkdir build-64
#}
#cd build-64
#cmake -D LLVM_ENABLE_TERMINFO=OFF -D LLVM_TARGETS_TO_BUILD='X86;ARM;NVPTX;AArch64' -D LLVM_ENABLE_ASSERTIONS=ON -D CMAKE_BUILD_TYPE=Release -G "Visual Studio 12 Win64" ..
#MSBuild.exe /t:Build /p:Configuration="Release" .\ALL_BUILD.vcxproj

#cd $ROOT\pnacl-llvm
#if (! (Test-Path build-32)) {
#  mkdir build-32
#}
#cd build-32
#cmake -D LLVM_ENABLE_TERMINFO=OFF -D LLVM_TARGETS_TO_BUILD='X86;ARM;NVPTX;AArch64' -D LLVM_ENABLE_ASSERTIONS=ON -D CMAKE_BUILD_TYPE=Release -D LLVM_BUILD_32_BITS=ON -G "Visual Studio 12" ..
#MSBuild.exe /t:Build /p:Configuration="Release" .\ALL_BUILD.vcxproj

# Build Halide
cd $ROOT
if (! (Test-Path build_64_trunk)) {
  mkdir build_64_trunk
}
cd build_64_trunk
cmake -D LLVM_BIN=$ROOT\llvm\build-64\Release\bin -D LLVM_INCLUDE="$ROOT\llvm\include;$ROOT\llvm\build-64\include" -D LLVM_LIB=$ROOT\llvm\build-64\Release\lib -D LLVM_VERSION=36 -D TARGET_ARM=ON -D TARGET_NATIVE_CLIENT=OFF -D TARGET_OPENCL=ON -D TARGET_PTX=ON -D TARGET_SPIR=ON -D TARGET_X86=ON -D WITH_TEST_CORRECTNESS=ON -D WITH_TEST_ERROR=ON -D WITH_TEST_WARNING=ON -D WITH_TEST_PERFORMANCE=ON -D WITH_TEST_STATIC=ON -D WITH_TEST_GENERATORS=ON -D HALIDE_SHARED_LIBRARY=ON -G "Visual Studio 12 Win64" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\All_BUILD.vcxproj
if ($LastExitCode) {
  echo "Build failed!"
  exit $LastExitCode
}


cd $ROOT
if (! (Test-Path build_32_trunk)) {
  mkdir build_32_trunk
}
cd build_32_trunk
cmake -D LLVM_BIN=$ROOT\llvm\build-32\Release\bin -D LLVM_INCLUDE="$ROOT\llvm\include;$ROOT\llvm\build-32\include" -D LLVM_LIB=$ROOT\llvm\build-32\Release\lib -D LLVM_VERSION=36 -D TARGET_ARM=ON -D TARGET_NATIVE_CLIENT=OFF -D TARGET_OPENCL=ON -D TARGET_PTX=ON -D TARGET_SPIR=ON -D TARGET_X86=ON -D WITH_TEST_CORRECTNESS=ON -D WITH_TEST_ERROR=ON -D WITH_TEST_WARNING=ON -D WITH_TEST_PERFORMANCE=ON -D WITH_TEST_STATIC=ON -D WITH_TEST_GENERATORS=ON -D HALIDE_SHARED_LIBRARY=ON -G "Visual Studio 12" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\All_BUILD.vcxproj
if ($LastExitCode) {
  echo "Build failed!"
  exit $LastExitCode
}


# Build Halide against pnacl
cd $ROOT
if (! (Test-Path build_64_pnacl)) {
  mkdir build_64_pnacl
}
cd build_64_pnacl
# nacl-sdk-bin contains clang.exe, llvm-as.exe, and the required dlls scavenged from the nacl sdk version pepper_35
cmake -D LLVM_BIN=$ROOT\pnacl-llvm\nacl-sdk-bin -D LLVM_INCLUDE="$ROOT\pnacl-llvm\include;$ROOT\pnacl-llvm\build-64\include" -D LLVM_LIB=$ROOT\pnacl-llvm\build-64\lib\Release -D LLVM_VERSION=34 -D TARGET_ARM=ON -D TARGET_NATIVE_CLIENT=ON -D TARGET_OPENCL=ON -D TARGET_PTX=ON -D TARGET_X86=ON -D WITH_TEST_CORRECTNESS=ON -D WITH_TEST_ERROR=ON -D WITH_TEST_WARNING=ON -D WITH_TEST_PERFORMANCE=ON -D WITH_TEST_STATIC=OFF -D WITH_TEST_GENERATORS=OFF -D HALIDE_SHARED_LIBRARY=ON -G "Visual Studio 12 Win64" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\All_BUILD.vcxproj
if ($LastExitCode) {
  echo "Build failed!"
  exit $LastExitCode
}

cd $ROOT
if (! (Test-Path build_32_pnacl)) { 
  mkdir build_32_pnacl
}
cd build_32_pnacl
# nacl-sdk-bin contains clang.exe, llvm-as.exe, and the required dlls scavenged from the nacl sdk version pepper_35
cmake -D LLVM_BIN=$ROOT\pnacl-llvm\nacl-sdk-bin -D LLVM_INCLUDE="$ROOT\pnacl-llvm\include;$ROOT\pnacl-llvm\build-32\include" -D LLVM_LIB=$ROOT\pnacl-llvm\build-32\lib\Release -D LLVM_VERSION=34 -D TARGET_ARM=ON -D TARGET_NATIVE_CLIENT=ON -D TARGET_OPENCL=ON -D TARGET_PTX=ON -D TARGET_X86=ON -D WITH_TEST_CORRECTNESS=ON -D WITH_TEST_ERROR=ON -D WITH_TEST_WARNING=ON -D WITH_TEST_PERFORMANCE=ON -D WITH_TEST_STATIC=OFF -D WITH_TEST_GENERATORS=OFF -D HALIDE_SHARED_LIBRARY=ON -G "Visual Studio 12" ..
MSBuild.exe /m /t:Build /p:Configuration="Release" .\All_BUILD.vcxproj
if ($LastExitCode) {
  echo "Build failed!"
  exit $LastExitCode
}

# Run the tests
foreach ($d in "32_trunk","64_trunk","64_pnacl","32_pnacl") {
  $env:HL_JIT_TARGET = "host"

  cd ${ROOT}\build_${d}\bin\Release

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
#      exit $LastExitCode
    }
  }

  # GPU and static tests
  if ($d.EndsWith("trunk")) {
    Get-ChildItem . -filter static*.exe | ForEach {
      echo ""
      echo $_.Fullname
      &$_.Fullname
      if ($LastExitCode) {
        echo "Test failed!"
        exit $LastExitCode
      }
    }

    Get-ChildItem . -filter exec_test_*.exe | ForEach {
      echo ""
      echo $_.Fullname
      &$_.Fullname
      if ($LastExitCode) {
        echo "Test failed!"
        exit $LastExitCode
      }
    }


    Get-ChildItem . -filter correctness*.exe | ForEach {
      echo ""
      echo $_.Fullname
      $env:HL_JIT_TARGET = "cuda"
      &$_.Fullname
      if ($LastExitCode) {
        echo "Test failed with cuda!"
        exit $LastExitCode
      }
      $env:HL_JIT_TARGET = "opencl"
      &$_.Fullname
      if ($LastExitCode) {
        echo "Test failed with opencl!"
        exit $LastExitCode
      }
    }
  }

  cd $ROOT
  if (! (Test-Path distrib)) {
    mkdir distrib
  }
  cd distrib

  rm Halide.h
  rm Halide.lib
  rm Halide.dll
  cp ..\build_${d}\include\Halide.h .
  cp ..\build_${d}\lib\Release\Halide.lib .
  cp ..\build_${d}\bin\Release\Halide.dll .
  &7z a Halide_Windows_${d}_${COMMIT}_${DATE}.zip Halide.h Halide.lib Halide.dll
}