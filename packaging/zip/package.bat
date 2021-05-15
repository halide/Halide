@echo off

set halide_source=%~1
set halide_build_root=%~2
set halide_arch=%~3

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

if not exist "%LLD_DIR%\LLDConfig.cmake" (
    echo Must set specific LLD_DIR for packaging
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

cmake --preset=package-windows -A "%halide_arch%" -S "%halide_source%" -B "%halide_build_root%"
if ERRORLEVEL 1 goto error

REM We don't distribute Debug binaries because they aren't useful
cmake --build "%halide_build_root%" --config Release
if ERRORLEVEL 1 goto error

pushd "%halide_build_root%"
cpack -G ZIP -C "Release"
if ERRORLEVEL 1 (
    popd
    goto error
)
popd

exit /b

:error
exit /b 1
