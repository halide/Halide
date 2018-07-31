#### What is this ?
The goal of this scipt is to install benchmarking frameworks such as PENCIL, TVM, TC, ...
The script download all the necessary packages and installs them automatically.


#### Requirements
	- cmake.
	- wget.
	- Halide, if not installed, you can install it using
	  	Halide/util/auto_scheduler_benchmarks/scripts/install_halide.sh
	- LLVM, if not installed, you can install it using
	  	Halide/util/auto_scheduler_benchmarks/scripts/install_llvm.sh
	- TVM requirements: python python-dev python-setuptools gcc libtinfo-dev zlib1g-dev
		If not installed, you can install them using apt-get with the following script
 		Halide/util/auto_scheduler_benchmarks/scripts/install_tvm_required_packages.sh
	- PENCIL assumes that autoconf and libtool are installed. If not you can install them
		with "sudo apt-get install autoconf libtool".


#### Configuration
Edit the script Halide/util/auto_scheduler_benchmarks/install_frameworks.sh
to set the variable LLVM_PREFIX.  The prefix is where LLVM is installed
and it should contain bin/clan and bin/llvm-config. Example:

	LLVM_PREFIX=/data/scratch/baghdadi/llvm_60_prefix/

#### How to use the script ?
Assuming you are in the Halide root directory.

	cd util/auto_scheduler_benchmarks
	./install_frameworks.sh

This will install the frameworks in the directory "software".

#### Clean Installed Packages
Delete the "software" directory.

	rm -rf Halide/util/auto_scheduler_benchmarks/software/

