# Building V8 from upstream sources for Halide

Steps minimized from:

https://v8.dev/docs/build
https://v8.dev/docs/source-code
https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up

## Step 1: Install depot_tools

```
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
$ echo "export PATH=\"$PWD/depot_tools:\$PATH\"" >> ~/.bashrc
$ source ~/.bashrc
$ gclient
```

## Step 2: Checkout sources

```
$ fetch v8
... wait a long time ...
$ cd v8
```

## Step 3: Update sources and install dependencies

```
$ git pull && gclient sync
$ ./build/install-build-deps.sh  # Linux-only
```

You might need to patch this script to un-indent the Python code string in `build_apt_package_list`.
Also, to use the script on newer Ubuntu systems, pass `--unsupported` to the script on the command
line.

Repeat this step any time to update the checkout.

## Step 4: Build!

```
$ tools/dev/gm.py x64.release
```

Quickly cancel the build and then run:

```
$ gn args out/x64.release
```

Make sure that `is_component_build = true` appears in the args file. Then run

```
$ ninja -C out/x64.release
```

# Linking Halide to V8

Build Halide as normal, passing the following options:

```
-DWITH_WABT=NO
-DWITH_V8=YES
-DV8_ROOT=/path/to/v8
```

If it detects things wrong, you can set:

```
-DV8_LIBRARY=/path/to/libv8.so
-DV8_INCLUDE_PATH=/path/to/include
```

Where `/path/to/include` should contain `v8.h` and `libplatform/libplatform.h`.