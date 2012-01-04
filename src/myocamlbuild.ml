open Ocamlbuild_plugin
(* open Command *)
open Ocamlbuild_pack.My_unix
(* open Ocamlbuild_pack.Ocaml_compiler
open Ocamlbuild_pack.Ocaml_utils
open Ocamlbuild_pack.Tools *)
open Printf

let trim str =   if str = "" then "" else   let search_pos init p next =
    let rec search i =
      if p i then raise(Failure "empty") else
      match str.[i] with
      | ' ' | '\n' | '\r' | '\t' -> search (next i)
      | _ -> i
    in
    search init   in   let len = String.length str in   try
    let left = search_pos 0 (fun i -> i >= len) (succ)
    and right = search_pos (len - 1) (fun i -> i < 0) (pred)
    in
    String.sub str left (right - left + 1)   with   | Failure "empty" -> "" ;;

(* Host OS detection, from:
 * https://github.com/avsm/mirage/blob/master/scripts/myocamlbuild.ml *)

type unix = Linux | Darwin
type arch = X86_32 | X86_64 | Arm

let host =
  match trim (String.lowercase (run_and_read "uname -s")) with
    | "linux"  -> eprintf "host = linux\n"; Linux
    | "darwin" -> eprintf "host = darwin\n"; Darwin
    | os -> eprintf "`%s` is not a supported host OS\n" os; exit (-1);;

let arch =
  let a =
    begin match trim (String.lowercase (run_and_read "uname -m")) with
      | "x86_32" | "i686"  -> X86_32
      | "i386" -> (match host with Linux -> X86_32 | Darwin -> X86_64) 
      | "x86_64" | "amd64" -> X86_64
      | "armv7l" -> Arm
      | arch -> eprintf "`%s` is not a supported arch\n" arch; exit (-1) end
  in
  ignore (match a with
    | X86_32 -> eprintf "arch = x86_32\n"
    | X86_64 -> eprintf "arch = x86_64\n"
    | Arm -> eprintf "arch = ARM\n");
  a;;

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
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm";;
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm_analysis";;
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm_bitwriter";;
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm_bitreader";;
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm_target";;
ocaml_lib ~extern:true ~dir:"../../llvm/Release+Asserts/lib/ocaml/" "llvm_executionengine";;

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
let llsupport_linkflags = [
  A"-cclib"; A libllsupport_impl;
  A"-cclib"; A "-L../../llvm/Release+Asserts/lib";
  A"-cclib"; A "-lLLVMLinker"
] in
(* PTX target libraries *)
let ptx_llsupport_linkflags = [
  A"-cclib"; A "-lLLVMPTXCodeGen";
  A"-cclib"; A "-lLLVMPTXAsmPrinter";
  A"-cclib"; A "-lLLVMPTXInfo";
  A"-cclib"; A "-lLLVMPTXDesc"
] in
let llsupport_linkflags =
  if arch <> Arm then
    llsupport_linkflags @ ptx_llsupport_linkflags
  else
    llsupport_linkflags
in

flag ["link"; "ocaml"; "use_llsupport"]
  (S llsupport_linkflags);;

let include_ocaml = "-I/usr/local/lib/ocaml" in
let include_llvm = "-I../../llvm/include" in
flag ["c"; "compile"; "llsupport_cflags"]
  (S[A"-cc"; A"g++";
     A"-ccopt"; A "-fPIC"; (* Linux at least requires PIC *)
     A"-ccopt"; A include_ocaml;
     A"-ccopt"; A include_llvm;
     A"-ccopt"; A"-D__STDC_LIMIT_MACROS";
     A"-ccopt"; A"-D__STDC_CONSTANT_MACROS"]);;
