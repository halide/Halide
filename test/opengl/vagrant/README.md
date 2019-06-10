# Testing OpenGL on Ubuntu 14 & 16 using vagrant & VirtualBox

## Overview

This subdirectory (`Halide/test/opengl/vagrant`) provides the setup to build Halide and run the OpenGL tests headlessly on Ubuntu 14.04 and/or 16.04, running virtually under [vagrant](http://vagrantup.com) and [VirtualBox](https://www.virtualbox.org).

This is intended in particular for use by those who develop Halide's OpenGL back-end on OS X and need to test on Linux.

The `Vagrantfile` provisions with the necessary capabilities to build Halide and build & run Halide's OpenGL test suite.  In particular it installs llvm-3.8 and OpenGL with software rendering to a dummy X server.

## Quick instructions

Presuming that you have [vagrant](http://vagrantup.com) and [VirtualBox](https://www.virtualbox.org) installed,

```
$ cd Halide/test/opengl/vagrant
$ vagrant up [u14|u16]
[...]
$ vagrant ssh [u14|u16] -c "sh /vagrant/build_tests.sh"
[...]
```

The `[u14|u16]` argument is optional, the default is `u16` to use the Ubuntu 16.04 virtual machine.  Specify `u14` to use the Ubuntu 14.04 macihne.

After a bit of time and a lot of verbiage, you should eventually see the `make` output for building and running the OpenGL tests

## Detailed instructions

### Starting and provisioning the virtual machine(s)

As per above, you can start the machines using

```
$ cd Halide/test/opengl/vagrant
$ vagrant up [u14|u16]
[...]
```

The first time you run it for a given machine, it will download the necessary base box, then boot and provision the machine.  This will take several minutes.

You may notice some errors or warnings in the output of `vagrant up`'s provisioning; these can be safely ignored.  (In particular for `u16` the output ends with `ttyname failed: Inappropriate ioctl for device` which looks omnious but is harmless.)

As usual, you can stop or power down the machine using `vagrant suspend [u14|u16]` or `vagrant halt [u14|u16]`; subsequently starting it up again using `vagrant up [u14|u16]` should be reasonably quick.  For more info, see the `vagrant help` or the [vagrant](http://vagrantup.com) docs.

See the `Vagrantfile` for the specific details of what gets provisioned.

### Building Halide and running the tests

The virtual machine has these directories live-shared with the host:

* `/Halide` - The root of your Halide source tree
* `/vagrant` - The vagrant work directory.  I.e. effectively a hard link to `/Halide/test/opengl/vagrant`

Because these are live shared, you can edit Halide source files on your host machine but build them on the virtual machine.

The script `build_tests.sh`, run on the virtual machine, is just a quick shorthand to minimize the amount of typing, letting you build and run everything at once from the host via

```
$ vagrant ssh [u14|u16] -c "sh /vagrant/build_tests.sh"
```

But of course for more focused development & debugging you might want to do things one step at a time:

```
$ vagrant ssh [u14|u16]
[...Ubuntu motd...]
vagrant@vagrant:~$
```

These are the steps taken by `build_tests.sh`:

#### 1. Create an out-of-tree build directory

```
vagrant@vagrant:~$ mkdir ~/halide_build
vagrant@vagrant:~$ cd ~/halide_build
vagrant@vagrant:~/halide_build$ ln -s /Halide/Makefile .
```

It's important to build out-of-tree, because `/Halide` tree is live shared and we don't want the virtual machine's object files to clobber the host object files!

#### 2. Build Halide

Nothing special here, just build normally, e.g.:

```
vagrant@vagrant:~/halide_build$ make -j 3
```

The machine is provisioned with environment variables `LLVM_CONFIG` globally set appropriately.

#### 3. Build & run the OpenGL tests

Again nothing special here, just build the opengl tests normally, e.g.:

```
vagrant@vagrant:~/halide_build$ make -k test_opengl
```

Or of course you can build and run just one test, e.g.:

```
vagrant@vagrant:~/halide_build$ make opengl_float_texture
```

The machine is provisioned with environment variables `HL_TARGET` and `HL_JIT_TARGET` set to `host-opengl`.  You can of course override in your shell, e.g. if you want to use `host-opengl-debug`.

The machine is provisioned with `lldb` installed in case you need to do some debugging.  Aside from that it's bare-bones; if you need anything else for your debugging or development you will need to `apt-get install` it.