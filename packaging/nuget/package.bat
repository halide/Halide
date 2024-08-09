@echo off
setlocal

set halide_source=%cd%\%~1
set halide_build_root=%cd%\%~2
set halide_arch=%~3
set script_dir=%~dp0

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
    echo Usage: %~0 "<source-dir>" "<build-dir>" [win-x86,win-x64]
    goto error
)

if "%halide_build_root%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [win-x86,win-x64]
    goto error
)

if "%halide_arch%" == "" (
    echo Usage: %~0 "<source-dir>" "<build-dir>" [win-x86,win-x64]
    goto error
)

REM Ninja Multi-Config in 3.18 has some sort of bug. Very disappointing.
cmake --preset=package-nuget-%halide_arch% -S "%halide_source%" -B "%halide_build_root%\%halide_arch%"
if ERRORLEVEL 1 goto error

REM We don't distribute Debug binaries because they aren't useful
REM cmake --build %halide_build_root% --config Debug
cmake --build "%halide_build_root%\%halide_arch%" --config Release
if ERRORLEVEL 1 goto error

pushd "%halide_build_root%"

cpack -G NuGet -C Release --config "%script_dir%Halide.runtime.%halide_arch%\config.cmake"
if ERRORLEVEL 1 (
    popd
    goto error
)

REM TODO: write a second script for this? It should always be safe to clobber.
cmake -DHalide_USE_PACKAGE=%halide_arch% -P "%script_dir%Halide\build.cmake"
if ERRORLEVEL 1 (
    popd
    goto error
)

popd

exit /b

:error
exit /b 1
