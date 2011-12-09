open Ocamlbuild_plugin;;

(*
 * Essential ocamlbuild setup for full project, including LLVM libs 
 * Cf. http://brion.inria.fr/gallium/index.php/Using_an_external_library
 *     http://brion.inria.fr/gallium/index.php/Ocamlbuild_example_with_C_stubs
 * for reference examples. 
 *)

(* require individual LLVM component libraries here *)
(* ~dir:... specifies search path for each lib. Need on *.ml as well as *.byte
 * to search for includes, not just libs to link. *)
ocaml_lib ~extern:true "unix";; (* Unix is (oddly) needed by llvm when building a toplevel. Must be first. *)
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_analysis";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitwriter";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitreader";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_target";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_executionengine";;

(* Define ocamlc link flag: -cc g++ *)
(* This is necessary to ensure linkage of libstdc++ for LLVM. *)
(* This actually gets set for a target with the `g++` tag in `_tags`. *)
(*flag ["link"; "ocaml"; "g++"] (S[A"-cc"; A"g++"]);;*) (* this version spews
tons of deprecation warning noise with g++ 4.4, just linking stdc++ below works
better *)
flag ["link"; "ocaml"; "g++"] (S[A"-cclib"; A"-lstdc++"]);;

(* Compiler support C++ lib *)
(* ocaml_lib "llsupport"; *)
let libllsupport_impl = "libllsupport_impl." ^ !Options.ext_lib in
dep ["link"; "ocaml"; "use_llsupport"] [libllsupport_impl];
flag ["link"; "ocaml"; "use_llsupport"]
  (S[A"-cclib"; A libllsupport_impl;
     A"-cclib"; A "-L../../llvm/Debug+Asserts/lib";
     A"-cclib"; A "-lLLVMLinker";
     (* PTX target libraries *)
     A"-cclib"; A "-lLLVMPTXCodeGen";
     A"-cclib"; A "-lLLVMPTXAsmPrinter";
     A"-cclib"; A "-lLLVMPTXInfo";
     A"-cclib"; A "-lLLVMPTXDesc";
  ]);;

let include_ocaml = "-I/usr/local/lib/ocaml" in
let include_llvm = "-I../../llvm/include" in
flag ["c"; "compile"; "llsupport_cflags"]
  (S[A"-cc"; A"g++";
     A"-ccopt"; A include_ocaml;
     A"-ccopt"; A include_llvm;
     A"-ccopt"; A"-D__STDC_LIMIT_MACROS";
     A"-ccopt"; A"-D__STDC_CONSTANT_MACROS"]);;
