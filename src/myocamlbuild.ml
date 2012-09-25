open Ocamlbuild_plugin
(* open Command *)
open Ocamlbuild_pack.My_unix
(* open Ocamlbuild_pack.Ocaml_compiler
open Ocamlbuild_pack.Ocaml_utils
open Ocamlbuild_pack.Tools *)
open Printf

let llvm_prefix = "../../llvm/Release+Asserts";;
let llvm_tool nm = A(llvm_prefix / "bin" / nm);;

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
ocaml_lib ~extern:true "str";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm_analysis";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm_bitwriter";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm_bitreader";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm_target";;
ocaml_lib ~extern:true ~dir:(llvm_prefix/"lib/ocaml/") "llvm_executionengine";;

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
  A"-cclib"; A ("-L" ^ llvm_prefix ^ "/lib");
  A"-cclib"; A "-lLLVMLinker";
  A"-cclib"; A "-lLLVMipo";
  A"-cclib"; A "-lLLVMVectorize";
  A"-cclib"; A "-lLLVMArchive";
  A"-cclib"; A "-lLLVMInterpreter";
  A"-cclib"; A "-lLLVMX86AsmParser";
  A"-cclib"; A "-lLLVMX86CodeGen";
  A"-cclib"; A "-lLLVMSelectionDAG";
  A"-cclib"; A "-lLLVMAsmPrinter";
  A"-cclib"; A "-lLLVMMCParser";
  A"-cclib"; A "-lLLVMX86Disassembler";
  A"-cclib"; A "-lLLVMX86Desc";
  A"-cclib"; A "-lLLVMX86AsmPrinter";
  A"-cclib"; A "-lLLVMX86Utils";
  A"-cclib"; A "-lLLVMX86Info";
  A"-cclib"; A "-lLLVMBitReader";
  A"-cclib"; A "-lLLVMBitWriter";
  A"-cclib"; A "-lLLVMJIT";
  A"-cclib"; A "-lLLVMRuntimeDyld";
  A"-cclib"; A "-lLLVMExecutionEngine";
  A"-cclib"; A "-lLLVMCodeGen";
  A"-cclib"; A "-lLLVMScalarOpts";
  A"-cclib"; A "-lLLVMInstCombine";
  A"-cclib"; A "-lLLVMTransformUtils";
  A"-cclib"; A "-lLLVMipa";
  A"-cclib"; A "-lLLVMAnalysis";
  A"-cclib"; A "-lLLVMTarget";
  A"-cclib"; A "-lLLVMMC";
  A"-cclib"; A "-lLLVMObject";
  A"-cclib"; A "-lLLVMCore";
  A"-cclib"; A "-lLLVMSupport"
] in
(* PTX target libraries *)
let ptx_llsupport_linkflags =
(*[
  A"-cclib"; A "-lLLVMPTXCodeGen";
  A"-cclib"; A "-lLLVMPTXAsmPrinter";
  A"-cclib"; A "-lLLVMPTXInfo";
  A"-cclib"; A "-lLLVMPTXDesc"
] in*)
List.flatten (
List.map
(fun fl -> [A"-cclib"; A fl])
["-lLLVMNVPTXCodeGen"; "-lLLVMSelectionDAG"; "-lLLVMAsmPrinter";
 "-lLLVMMCParser"; "-lLLVMCodeGen"; "-lLLVMScalarOpts";
 "-lLLVMInstCombine"; "-lLLVMTransformUtils"; "-lLLVMipa"; 
 "-lLLVMAnalysis"; "-lLLVMNVPTXDesc"; "-lLLVMNVPTXInfo"; "-lLLVMTarget";
 "-lLLVMNVPTXAsmPrinter"; "-lLLVMMC"; "-lLLVMObject"; "-lLLVMBitReader"; 
 "-lLLVMCore"; "-lLLVMSupport"]
) in
 let 
llsupport_linkflags =
  if arch <> Arm then
    llsupport_linkflags @ ptx_llsupport_linkflags
  else
    llsupport_linkflags
in

flag ["link"; "ocaml"; "use_llsupport"]
  (S llsupport_linkflags);;

let include_ocaml = "-I/usr/local/lib/ocaml" in
let include_llvm = "-I../../llvm/include" in (* TODO: base off of llvm_prefix? *)
flag ["c"; "compile"; "llsupport_cflags"]
  (S[A"-cc"; A"g++";
     A"-ccopt"; A "-fPIC"; (* Linux at least requires PIC *)
     A"-ccopt"; A include_ocaml;
     A"-ccopt"; A include_llvm;
     A"-ccopt"; A"-D__STDC_LIMIT_MACROS";
     A"-ccopt"; A"-D__STDC_CONSTANT_MACROS"]);;

rule "Generate initial module source strings for ML emission"
  ~prod: "architecture_posix_initmod.ml"
  ~deps: ["architecture.posix.stdlib.cpp"; "cpp2ml.py"]
  begin fun env build ->
    let c =
    Cmd(S([
      A"python"; A"cpp2ml.py"; P(env "architecture.posix.stdlib.cpp");
      Sh " > "; P(env "architecture_posix_initmod.ml")]))
    in
    (* failwith (Command.to_string c); *)
    c
  end;;
rule "Generate initial modules"
  ~prod: "architecture.%(arch).initmod.c"
  ~deps: ["architecture.%(arch).stdlib.cpp";
          "architecture.posix.stdlib.cpp";
          "architecture.x86.stdlib.cpp";
          "architecture.%(arch).stdlib.ll";
          "buffer.h";
          "bitcode2cpp.py"]
  begin fun env build ->
    let arch = env "%(arch)" in
    let ccflags =
      if arch = "x86" or arch = "ptx" then
        ["-march=corei7"]
      else if arch = "x86_avx" then
        ["-march=corei7-avx"]
      else if arch = "arm_android" || arch = "arm" then
        ["-m32"]
      else
        []
    in
    let llstub = env "architecture.%(arch).stdlib.ll" in
    let llstubs =
      if arch = "ptx" then
        let x86_ll = "architecture.x86.stdlib.ll" in
        let x86_cpp = "architecture.x86.stdlib.cpp" in
        ignore (build [[x86_ll; x86_cpp]]);
        [llstub; x86_ll]
      else if arch = "x86_avx" then
        let x86_ll = "architecture.x86.stdlib.ll" in
        let x86_cpp = "architecture.x86.stdlib.cpp" in
        ignore (build [[x86_ll; x86_cpp]]);
        [x86_ll; llstub]
      else [llstub]
    in
    let c =
    Cmd(S([
      A"clang"; A"-emit-llvm"; A"-O3"] @ (List.map (fun flag -> (A flag)) ccflags)
      @ [A"-S"; P(env "architecture.%(arch).stdlib.cpp"); A"-o"; A"-";
      Sh " | ";
      A"grep"; A"-v"; A"^target triple";
      Sh " | ";
      A"grep"; A"-v"; A"^target datalayout";
      Sh " | ";
      A"grep"; A"-v"; A"^; ModuleID";
      Sh " | "] @
     [A"cat"; A"-";] @ (List.map (fun s -> (P s)) llstubs) @
     [Sh " | ";
      llvm_tool "llvm-as"; A"-"; A"-o"; A"-";
      Sh " | ";
      A"python"; A"bitcode2cpp.py"; P(env "%(arch)");
      Sh " > "; P(env "architecture.%(arch).initmod.c")
    ]))
    in
    c
  end;;
