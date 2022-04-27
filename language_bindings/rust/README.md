# Halide Rust Bindings


## Instalation Steps:
These instructions were tested on a fresh install of Ubuntu 21.04.

First, update your system.
```
dev@ubuntu:~$ sudo apt update
dev@ubuntu:~$ sudo apt upgrade
```
Then, install Halide's necessary build tools and dependencies.
```
dev@ubuntu:~$ sudo apt install \
              build-essential git ninja-build \
              clang-tools lld llvm-dev libclang-dev \
              libpng-dev libjpeg-dev libgl-dev \
              python3-dev python3-numpy python3-scipy python3-imageio \
              libopenblas-dev libeigen3-dev libatlas-base-dev
```
Next, you will want the latest version of CMake (3.16+ supported)

```
dev@ubuntu:~$ wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null |\
              gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
dev@ubuntu:~$ sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
dev@ubuntu:~$ sudo apt install cmake kitware-archive-keyring
dev@ubuntu:~$ sudo rm /etc/apt/trusted.gpg.d/kitware.gpg
```

Alternatively, you can install CMake via the Snap store:

```
dev@ubuntu:~$ snap install cmake
```

Be sure to also install the GPU drivers for your system (either Nvidia or AMD).

### Get Halide & run the build

This is mostly copied from the README, so check there first if these instructions have gone stale.

```
dev@ubuntu:~$ git clone https://github.com/halide/Halide.git
dev@ubuntu:~$ Make #(Inside the cloned repo)
```

## Description of bindings

TODO...?

## link to cargo docs

TODO...

## Demo app Getting Started Guide

1.) Install Halide 
    https://github.com/halide/Halide
   
2.) Install Rust
    https://www.rust-lang.org/tools/install

1.) In the terminal cd into the Halide/language_bindings/rust/apps/iir_blur

2.) run the following:
```
  Cargo Build
  Cargo test
  Cargo run
```
3.) You should now have a slightly blurry picture of a cat in the images/ directory called.

## Building custom generator
Halide rust bindings allow a rust user to generate a user defined Halide generator and automatically create associated rust bindings.  
1.) Step one TODO when finished with automatic generating of generators


TOMOVE..?
  
