@echo off

pushd %~dp0\..

if not exist "%VCPKG_ROOT%\.vcpkg-root" (
    echo Must define VCPKG_ROOT to be the root of the VCPKG install
    goto return
)

cmake -G "Ninja Multi-Config" -DBUILD_SHARED_LIBS=YES ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
      -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO ^
      -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO ^
      -S . -B build/shared

if %errorlevel% neq 0 goto return

cmake --build build/shared --config Debug
cmake --build build/shared --config Release

cd build\shared
cpack -B .. -C "Debug;Release"

:return
popd
exit /b
