open Ocamlbuild_plugin;;

(* require individual LLVM component libraries here *)
ocaml_lib ~extern:true "llvm";;
ocaml_lib ~extern:true "llvm_analysis";;
ocaml_lib ~extern:true "llvm_bitwriter";;

(* set ocamlc link flag: -cc g++ *)
(* presumably necessary to ensure linkage of libstdc++ for LLVM *)
flag ["link"; "ocaml"; "g++"] (S[A"-cc"; A"g++"]);;
