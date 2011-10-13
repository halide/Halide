(* This module is for testing the vectorize transform *)
open Vectorize
open Split
open Printf
open Ir
open Ir_printer
open Analysis

(* Some simple expressions *)
let () =
  let x = Var "x" and y = Var "y" in
  let vec s = vectorize_stmt "x_i" (split_stmt "x" "x" "x_i" 4 s) in
  let test s = printf "%s \n-> %s\n\n" (string_of_stmt s) (string_of_stmt (vec s)) in
  let load e = Load(i32, "input", e) in
  let store a b = Store(a, "output", b) in

  let trivial = Map ("x", IntImm(0), IntImm(4), store x x) in
  printf "\nTrivial:\n  %s\n\n" (string_of_stmt (vectorize_stmt "x" trivial));

  let trivial_product = Map ("x", IntImm(0), IntImm(4), store (x*~(IntImm 2)) x) in
  printf "\nTrivial product:\n  %s\n\n" (string_of_stmt (vectorize_stmt "x" trivial_product));

  let memcpy = Map ("x", IntImm(0), IntImm(100), store (load x) x) in
  printf "Split only:\n";
  printf "%s\n" (string_of_stmt (split_stmt "x" "x" "x_i" 4 memcpy));

  printf "Memcpy:\n";
  test memcpy;

  let entry s = ([Buffer "input"; Buffer "output"], s) in
  
  Cg_llvm.codegen_to_file "memcpy.bc" (entry (vec memcpy));

  let strided_memcpy = subs_stmt x ((IntImm 2) *~ x) memcpy in
  printf "Strided Memcpy:\n";
  test strided_memcpy;
  Cg_llvm.codegen_to_file "strided_memcpy.bc" (entry (vec strided_memcpy));
