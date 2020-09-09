@echo off

set halide_source="%~1"
set halide_build_root="%~2"
set halide_arch="%~3"

if not exist "%VCPKG_ROOT%\.vcpkg-root" (
    echo Must define VCPKG_ROOT to be the root of the VCPKG install
    goto return
)

if not exist "%LLVM_DIR%\LLVMConfig.cmake" (
    echo Must set specific LLVM_DIR for packaging
    goto return
)

if not exist "%Clang_DIR%\ClangConfig.cmake" (
    echo Must set specific Clang_DIR for packaging
    goto return
)

if "%halide_source%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto return
)

if "%halide_build_root%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto return
)

if "%halide_arch%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto return
)

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A "%halide_arch%" ^
      "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
      "-DLLVM_DIR=%LLVM_DIR%" ^
      "-DClang_DIR=%Clang_DIR%" ^
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
      -B "%halide_build_root%"

if %errorlevel% neq 0 goto return

REM We don't distribute Debug binaries because they aren't useful
REM cmake --build %halide_build_root% --config Debug
cmake --build "%halide_build_root%" --config Release

pushd "%halide_build_root%"
cpack -C "Release"
popd

:return
exit /b
