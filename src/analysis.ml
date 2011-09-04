open Ir
open Ir_printer
open Util

exception ModulusOfNonInteger 
exception ModulusOfMakeVector
exception ModulusOfBroadcast

let rec gcd x y = match (x, y) with
  | (0, n) | (n, 0) -> n
  | (a, b) -> gcd b (a mod b)

let rec binop_remainder_modulus x y op = 
    let (xr, xm) = compute_remainder_modulus x in
    let (yr, ym) = compute_remainder_modulus y in
    let g = gcd xm ym in
    (((op xr yr) + g) mod g, g)

and compute_remainder_modulus = function
  | IntImm(x) | UIntImm(x) -> (x, 0)
  | Cast(t, x) -> compute_remainder_modulus x 
  | Bop(Add, x, y) -> binop_remainder_modulus x y ( + )
  | Bop(Sub, x, y) -> binop_remainder_modulus x y ( - )
  | Bop(Mul, x, y) -> 
    let (xr, xm) = compute_remainder_modulus x in
    let (yr, ym) = compute_remainder_modulus y in
    if xm = 0 then
      (xr * yr, xr * ym)
    else if ym = 0 then
      (yr * xr, yr * xm)
    else 
      binop_remainder_modulus x y ( * )
  | Bop _ | Load _ | Var _ | ExtractElement _ -> (0, 1)
  | MakeVector _ -> raise ModulusOfMakeVector
  | Broadcast _ -> raise ModulusOfBroadcast
  | e -> raise ModulusOfNonInteger

(* Reduces an expression modulo n *)
(* returns an integer in [0, m-1], or m if unknown *)
let reduce_expr_modulo expr n = 
  let (r, m) = compute_remainder_modulus expr in
  Printf.printf "Remainder is %d modulo %d (querying for %d)\n%!" r m n; 
  if (m mod n = 0) then Some (r mod n) else None;

