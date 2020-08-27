@echo off

pushd %~dp0\..

if not exist "%VCPKG_ROOT%\.vcpkg-root" (
    echo Must define VCPKG_ROOT to be the root of the VCPKG install
    goto return
)

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
      -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO ^
      -S . -B build/shared

if %errorlevel% neq 0 goto return

cmake --build build/shared --config Debug
cmake --build build/shared --config Release

pushd build\shared
cpack -C "Debug;Release"
popd

REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM

:static

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A x64 ^
      -DBUILD_SHARED_LIBS=NO -DHalide_BUNDLE_LLVM=YES ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
      -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO ^
      -S . -B build/static

if %errorlevel% neq 0 goto return

REM LLVM Debug + Halide exceeds the COFF 4GB limit!!
REM cmake --build build/static --config Debug
cmake --build build/static --config Release

pushd build\static
REM cpack -C "Debug;Release"
cpack -C "Release"
popd

:return
popd
exit /b
