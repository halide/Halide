# Using Docker Images
This document describes how to use different docker images to create environments for
learning, testing and benchmarking

---
## NVIDIA (x86_64)
This environment uses [NVIDIA TensorRT](https://docs.nvidia.com/deeplearning/tensorrt/container-release-notes/running.html#running) base images providing:
- Ubuntu >= `18.04`
- Python >= `3.6.5`
- CUDA >= `10.2`
- TensorRT >= `7.0`

And adds:
- cmake >= `3.16.5`
- llvm >= `12.0.0`
- Halide `latest release`

#### Requirements
In order to use provided docker images please install [nvidia-container-toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)

#### Build
Build image for specific cuda version by setting `TENSORRT_BASE` version
- CUDA `10.2` => TENSORRT_BASE=`20.01-py3`
- CUDA `11.0` => TENSORRT_BASE=`20.08-py3`
- CUDA `11.2` => TENSORRT_BASE=`21.02-py3`
- CUDA `11.3` => TENSORRT_BASE=`21.03-py3`       

Controll versions and number of paraller build jobs by specifying:
- CMAKE_VERSION=`>=3.16.5`
- LLVM_VERSION=`>=12.0.0`
- JOBS=`NUM`

```
% cd Halide
% docker build \
  --build-arg TENSORRT_BASE=20.08 \
  --build-arg CMAKE_VERSION=3.16.5 \
  --build-arg LLVM_VERSION=12.0.0 \
  --build-arg JOBS=8 \
  -t halide_nvidia_x86_64 -f docker/Dockerfile.nvidia.x86_64 .
```

#### Run
Run container enabling GPU and display/camera passthrough, this will allow you to use camera and open images inside
of running environment. You can mount your Halide project and accelerate development by specifying `-v /path/to/my/project:/home/user/app`
this will allow you to develop on your host using editor of your choice and all changes made in your project will be reflected inside of the environment.
```
% cd My-Halide-Project
% docker run --name halide_nvidia_x86_64 --rm -it \
    --gpus=all \
    --ipc=host \
    -v $(pwd):/home/user/app \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    --device /dev/video0 \
    -e DISPLAY=$DISPLAY \
    halide_nvidia_x86_64
```
---
## NVIDIA Jetson (aarch64)
TODO