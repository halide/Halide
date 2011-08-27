(* This module is for testing the vectorize transform *)
open Vectorize
open Printf
open Ir
open Ir_printer

(* Some simple expressions *)
let () =
  let x = Var "x" and y = Var "y" in
  let vec e = vectorize_expr e "x" 4 in
  let test e = printf "%s \n-> %s\n\n" (string_of_expr e) (string_of_expr (vec e)) in
  let load e = Load(u8, {buf=0; idx=e}) in

  (* basic vector arithmetic *)
  test ((IntImm 2) +~ x);
  test ((IntImm 2) -~ x);
  test (x *~ (IntImm 2));

  (* vector load *)
  test (load x);
    
  (* vector load plus scalar load *)
  test ((load x) +~ (load y));

  (* Gather *)
  test (load (load x));

  (* Vector load with base address that depends on y *)
  test (load (Select(IntImm(3) =~ y, x +~ IntImm(1), x +~ IntImm(10))));
