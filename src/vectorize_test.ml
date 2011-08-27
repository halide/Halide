(* This module is for testing the vectorize transform *)
open Vectorize
open Printf
open Ir
open Ir_printer

(* Some simple expressions *)
let () =
  let x = Var "x" and y = Var "y" in
  let vec e = vectorize_expr e "x" 4 in
  let vecs e = vectorize_stmt e "x" 4 in
  let test e = printf "%s \n-> %s\n\n" (string_of_expr e) (string_of_expr (vec e)) in
  let tests e = printf "%s \n-> %s\n\n" (string_of_stmt e) (string_of_stmt (vecs e)) in
  let load e = Load(u8, {buf=0; idx=e}) in
  let store a b = Store(a, {buf=1; idx=b}) in

  printf "Basic vectorized arithmetic:\n";
  test ((IntImm 2) +~ x);
  test ((IntImm 2) -~ x);
  test (x *~ (IntImm 2));

  printf "Vector load:\n";
  test (load x);
    
  printf "Vector load plus scalar load:\n";
  test ((load x) +~ (load y));

  printf "Gather:\n";
  test (load (load x));

  printf "Conditional load with matching strides:\n";
  test (load (Select(IntImm(3) =~ y, x +~ IntImm(1), x +~ IntImm(10))));

  printf "Memcpy:\n";
  tests (Map ("x", 0, 100, store (load x) x));

  printf "Histogram:\n";
  let v = load x in
  tests (Map ("x", 0, 100, store ((Load(u8, {buf=1; idx = v})) +~ IntImm(1)) v));
