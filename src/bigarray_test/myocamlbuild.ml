open Ocamlbuild_plugin;;

(* require individual LLVM component libraries here *)
ocaml_lib ~extern:true "llvm";;
ocaml_lib ~extern:true "llvm_analysis";;
ocaml_lib ~extern:true "llvm_bitwriter";;
ocaml_lib ~extern:true "llvm_bitreader";;
ocaml_lib ~extern:true "llvm_executionengine";;

ocaml_lib ~extern:true "bigarray";;

(* define ocamlc link flag: -cc g++ *)
(* presumably necessary to ensure linkage of libstdc++ for LLVM *)
(* this actually gets set for a target with the `g++` tag in _tags *)
(*flag ["link"; "ocaml"; "g++"] (S[A"-cc"; A"g++"]);;*) (* this version spews
tons of deprecation warning noise with g++ 4.4, just linking stdc++ below works
better *)
flag ["link"; "ocaml"; "g++"] (S[A"-cclib"; A"-lstdc++"]);;
