(* This module is for testing the vectorize transform *)
open Vectorize
open Split
open Printf
open Ir
open Ir_printer

(* Some simple expressions *)
let () =
  let x = Var "x" and y = Var "y" in
  let vec s = vectorize_stmt "x_i" (split_stmt "x" "x" "x_i" 4 s) in
  let test s = printf "%s \n-> %s\n\n" (string_of_stmt s) (string_of_stmt (vec s)) in
  let load e = Load(u8, {buf="input"; idx=e}) in
  let store a b = Store(a, {buf="output"; idx=b}) in

  let trivial = Map ("x", IntImm(0), IntImm(4), store x x) in
  printf "\nTrivial:\n  %s\n\n" (string_of_stmt (vectorize_stmt "x" trivial));

  let trivial_product = Map ("x", IntImm(0), IntImm(4), store (x*~(IntImm 2)) x) in
  printf "\nTrivial product:\n  %s\n\n" (string_of_stmt (vectorize_stmt "x" trivial_product));

  let memcpy = Map ("x", IntImm(0), IntImm(100), store (load x) x) in
  printf "Split only:\n";
  printf "%s\n" (string_of_stmt (split_stmt "x" "x" "x_i" 4 memcpy));

  printf "Memcpy:\n";
  test memcpy;

  printf "Histogram:\n";
  let v = load x in
  test (Map ("x", IntImm(0), IntImm(100), store ((Load(u8, {buf="output"; idx = v})) +~ IntImm(1)) v));
