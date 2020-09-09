@echo off

set halide_source="%~1"
set halide_build_root="%~2"

if "%halide_source%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>"
    goto return
)

if "%halide_build_root%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>"
    goto return
)

if not exist "%VCPKG_ROOT%\.vcpkg-root" (
    echo Must define VCPKG_ROOT to be the root of the VCPKG install
    goto return
)

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A x64 ^
      "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
      -DBUILD_SHARED_LIBS=YES ^
      -DWITH_TESTS=NO ^
      -DWITH_APPS=NO ^
      -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES ^
      -DWITH_UTILS=NO ^
      -DWITH_PYTHON_BINDINGS=NO ^
      "-DCMAKE_INSTALL_BINDIR=bin/$<CONFIG>" ^
      "-DCMAKE_INSTALL_LIBDIR=lib/$<CONFIG>" ^
      "-DHALIDE_INSTALL_CMAKEDIR=lib" ^
      -S "%halide_source%" ^
      -B "%halide_build_root%/shared"

if %errorlevel% neq 0 goto return

REM We don't distribute Debug binaries because they aren't useful
REM cmake --build %halide_build_root%/shared --config Debug
cmake --build "%halide_build_root%/shared" --config Release

pushd "%halide_build_root%\shared"
cpack -C "Release"
popd

REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM REM

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A x64 ^
      "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
      -DBUILD_SHARED_LIBS=NO ^
      -DHalide_BUNDLE_LLVM=YES ^
      -DWITH_TESTS=NO ^
      -DWITH_APPS=NO ^
      -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES ^
      -DWITH_UTILS=NO ^
      -DWITH_PYTHON_BINDINGS=NO ^
      "-DCMAKE_INSTALL_BINDIR=bin/$<CONFIG>" ^
      "-DCMAKE_INSTALL_LIBDIR=lib/$<CONFIG>" ^
      "-DHALIDE_INSTALL_CMAKEDIR=lib" ^
      -S "%halide_source%" ^
      -B "%halide_build_root%/static"

if %errorlevel% neq 0 goto return

REM LLVM Debug + Halide exceeds the COFF 4GB limit!!
REM cmake --build build/static --config Debug
cmake --build build/static --config Release

pushd "%halide_build_root%\static"
cpack -C "Release"
popd

:return
popd
exit /b
