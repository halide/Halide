@echo off

set halide_source=%~1
set halide_build_root=%~2
set halide_arch=%~3

REM TODO: this temporary, until release branches for Halide are created.
REM Remove as soon as that is done.
if "%Halide_VERSION%" == "" (
    echo Must set specific Halide_VERSION for packaging
    goto error
)

if not exist "%VCPKG_ROOT%\.vcpkg-root" (
    echo Must define VCPKG_ROOT to be the root of the VCPKG install
    goto error
)

if not exist "%LLVM_DIR%\LLVMConfig.cmake" (
    echo Must set specific LLVM_DIR for packaging
    goto error
)

if not exist "%Clang_DIR%\ClangConfig.cmake" (
    echo Must set specific Clang_DIR for packaging
    goto error
)

if "%halide_source%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto error
)

if "%halide_build_root%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto error
)

if "%halide_arch%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [Win32,x64,ARM,ARM64]
    goto error
)

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake -G "Visual Studio 16 2019" -Thost=x64 -A "%halide_arch%" ^
      "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
      "-DLLVM_DIR=%LLVM_DIR%" ^
      "-DClang_DIR=%Clang_DIR%" ^
      "-DHalide_VERSION=%Halide_VERSION%" ^
      -DBUILD_SHARED_LIBS=YES ^
      -DWITH_TESTS=NO ^
      -DWITH_APPS=NO ^
      -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES ^
      -DWITH_UTILS=NO ^
      -DWITH_PYTHON_BINDINGS=NO ^
      "-DCMAKE_INSTALL_BINDIR=bin/$<CONFIG>" ^
      "-DCMAKE_INSTALL_LIBDIR=lib/$<CONFIG>" ^
      "-DHALIDE_INSTALL_CMAKEDIR=lib/cmake/Halide" ^
      "-DHALIDE_INSTALL_DATADIR=share/Halide" ^
      -S "%halide_source%" ^
      -B "%halide_build_root%"
if ERRORLEVEL 1 goto error

REM We don't distribute Debug binaries because they aren't useful
REM cmake --build %halide_build_root% --config Debug
cmake --build "%halide_build_root%" --config Release
if ERRORLEVEL 1 goto error

pushd "%halide_build_root%"
cpack -C "Release"
if ERRORLEVEL 1 goto error
popd

exit /b

:error
exit /b 1
