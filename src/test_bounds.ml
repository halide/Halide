open Ir
open Ir_printer
open Bounds
open Constant_fold
open Util

let test_in_env env expr =
  match (bounds_of_expr_in_env env expr) with
    | Unbounded -> 
        Printf.printf "%s is unbounded\n" (string_of_expr expr)
    | Range (min, max) ->
        let min = constant_fold_expr min in
        let max = constant_fold_expr max in
        Printf.printf "%s is bounded by %s, %s\n" (string_of_expr expr) (string_of_expr min) (string_of_expr max)  

let test expr =
  test_in_env (StringMap.add "x" (Range (IntImm 0, IntImm 100)) StringMap.empty) expr
          
let _ =
  let x = Var (i32, "x") in
  let y = Var (i32, "y") in

  let env = StringMap.empty in
  let env = StringMap.add "x" (Range (IntImm 0, IntImm 100)) env in
  let env = StringMap.add "y" (Range (IntImm 10, IntImm 20)) env in

  let test = test_in_env env in

  test x;
  test y;

  test (x +~ (IntImm 1));
  test (x *~ (IntImm 2));
  test (x -~ (IntImm 1));
  test (x /~ (IntImm 7));
  test (Bop (Max, IntImm 18, x));
  test (Bop (Min, IntImm 18, x));
  test ((IntImm 100) /~ (x +~ (IntImm 5)));
  test ((IntImm 100) /~ (x -~ (IntImm 5)));
  test (Bop (Mul, x, (IntImm 100) -~ x));
  
  test (x +~ y);
  test (x *~ y);
  test (x /~ y);
  test (x -~ y);
  test (Bop (Max, x, y));
  test (Select (x >~ y, x, y));
  test (Bop (Min, x, y));
  test (Select (x >~ y, IntImm 10, IntImm 20));
