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
  let z = Var (i32, "z") in
  let f x y = Call (Int 32, "f", [x; y]) in

  let env = StringMap.empty in
  let env = StringMap.add "x" (Range (IntImm 0, IntImm 100)) env in
  let env = StringMap.add "y" (Range (IntImm 10, IntImm 20)) env in

  let test = test_in_env env in

  let string_of_region region =
    let string_of_range = function
      | Range (min, max) -> "[" ^ (string_of_expr (constant_fold_expr min)) ^ ", " ^
          (string_of_expr (constant_fold_expr max)) ^ "]"
      | _ -> "Unbounded"
    in
    let region = (List.map string_of_range region) in
    String.concat ", " region
  in

  let test_roe e = 
    let region = required_of_expr "f" env e in    
    let str = string_of_region region in
    Printf.printf "%s uses f over %s\n" (string_of_expr e) str
  in

  let test_ros s =
    let region = required_of_stmt "f" (StringMap.empty) s in
    let str = string_of_region region in
    Printf.printf "This statement:\n%suses f over %s\n" (string_of_stmt s) str
  in

  let one = IntImm 1 in  
  let two = IntImm 2 in

  test x;
  test y;

  test (x +~ one);
  test (x *~ two);
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
  test (Let ("z", x +~ one, z +~ two));

  test (Bop (Max, Bop (Min, f x y, IntImm 100), IntImm 0));

  test_roe (f x y);
  test_roe (f y y);
  test_roe ((f x y) *~ (f (x +~ one) (y -~ one)));
  test_roe (   (f (x *~ two) (y *~ two)) +~
               (f (x *~ two +~ one) (y *~ two)) +~ 
               (f (x *~ two +~ one) (y *~ two +~ one)) +~ 
               (f (x *~ two) (y *~ two +~ one)));
               

  test_ros (For ("x", IntImm 50, IntImm 50, true, 
                 For ("y", IntImm 60, IntImm 60, true, 
                      Store (f (x*~two) y, "buffer", y *~ (IntImm 60) +~ (f x x)))));


  test_ros (For ("x", IntImm 50, IntImm 50, true, 
                 For ("y", IntImm 60, IntImm 60, true, 
                      Store (f (f (x*~two) y) y, "buffer", y *~ (IntImm 60) +~ (f x x)))));
