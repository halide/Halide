open Llvm
external init_module_ptx     : llmodule -> unit = "init_module_ptx"
external init_module_ptx_dev : llmodule -> unit = "init_module_ptx_dev"
external init_module_x86     : llmodule -> unit = "init_module_x86"
external init_module_x86_avx : llmodule -> unit = "init_module_x86_avx"
external init_module_arm     : llmodule -> unit = "init_module_arm"
external init_module_arm_android : llmodule -> unit = "init_module_arm_android"
