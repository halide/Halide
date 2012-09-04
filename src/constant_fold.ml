open Ir
open Analysis
open Util

let is_const_zero = function
  | IntImm 0 
  | FloatImm 0.0
  | Broadcast (IntImm 0, _)
  | Broadcast (FloatImm 0.0, _) -> true
  | _ -> false
and is_const_one = function
  | IntImm 1
  | FloatImm 1.0
  | Broadcast (IntImm 1, _)
  | Broadcast (FloatImm 1.0, _) -> true
  | _ -> false
and is_const = function
  | IntImm _ 
  | FloatImm _
  | Broadcast (IntImm _, _)
  | Broadcast (FloatImm _, _) -> true
  | _ -> false

(* Is an expression sufficiently simple that it should just be substituted in when it occurs in a let *)
let rec is_trivial = function
  | Var (_, _) -> true
  | Broadcast (Var (_, _), _) -> true
  | x -> is_const x

let rec is_simple x =
  (* One child simple, other children const *)
  let check = function
    | x when is_const x -> 0
    | x when is_simple x -> 1
    | _ -> 9999
  in
  let complexity = fold_children_in_expr check (+) 0 x in
  complexity <= 1

let rec constant_fold_expr expr = 
  let recurse = constant_fold_expr in

  dbg 3 "Constant folding: %s\n" (Ir_printer.string_of_expr expr);
  
  match expr with
    (* Ignoring most const-casts for now, because we can't represent immediates of arbitrary types *)
    | Cast (t, e) when val_type_of_expr e = t -> recurse e
    | Cast (t, e) -> 
        begin match (t, recurse e) with
          | (Int 32, IntImm x)    -> IntImm x
          | (Int 32, FloatImm x)  -> IntImm (int_of_float x)
          | (Float 32, IntImm x)  -> FloatImm (float_of_int x)
          | (Float 32, FloatImm x) -> FloatImm x
          | (t, e)                -> Cast(t, recurse e)
        end

    (* basic binary ops *)
    | Bop (op, a, b) ->
      begin match (op, recurse a, recurse b) with
        | (_, IntImm   x, IntImm   y) -> IntImm   (caml_iop_of_bop op x y)
        | (_, FloatImm x, FloatImm y) -> FloatImm (caml_fop_of_bop op x y)

        (* Identity operations. These are not strictly constant
           folding, but they tend to come up at the same time *)
        | (Add, x, y) when is_const_zero x -> y
        | (Add, x, y) when is_const_zero y -> x
        | (Sub, x, y) when is_const_zero y -> x
        | (Sub, x, y) when x = y -> make_zero (val_type_of_expr x)
        | (Mul, x, y) when is_const_one x -> y
        | (Mul, x, y) when is_const_one y -> x
        | (Mul, x, y) when is_const_zero x -> x
        | (Mul, x, y) when is_const_zero y -> y
        | (Div, x, y) when is_const_one y -> x
        | (Div, x, y) when x = y -> make_one (val_type_of_expr x)

        (* Commonly occuring reducible integer div/mod operations:
           when m % n = 0
           x*m % n -> 0
           (x*m + y) % n -> (y % n)
           x*m / n -> x*(m/n)
           (x*m + y) / n -> x*(m/n) + y/n

           e.g.: (4*x + 1) % 2 -> 1
        *)
        | (Mod, Bop (Mul, IntImm m, x), IntImm n) 
        | (Mod, Bop (Mul, x, IntImm m), IntImm n) when (m mod n = 0) -> 
            IntImm 0
        | (Mod, Bop (Add, y, Bop (Mul, x, IntImm m)), IntImm n) 
        | (Mod, Bop (Add, y, Bop (Mul, IntImm m, x)), IntImm n) 
        | (Mod, Bop (Add, Bop (Mul, x, IntImm m), y), IntImm n) 
        | (Mod, Bop (Add, Bop (Mul, IntImm m, x), y), IntImm n) when (m mod n = 0) -> 
            recurse (y %~ (IntImm n))
        | (Mod, x, IntImm n) -> begin
          match reduce_expr_modulo x n with
            | Some k -> IntImm k
            | None -> Bop (Mod, x, IntImm n)
        end
        | (Div, Bop (Mul, IntImm m, x), IntImm n) 
        | (Div, Bop (Mul, x, IntImm m), IntImm n) when (m mod n = 0) -> 
            recurse (x *~ (IntImm (m/n)))
        | (Div, Bop (Add, y, Bop (Mul, x, IntImm m)), IntImm n) 
        | (Div, Bop (Add, y, Bop (Mul, IntImm m, x)), IntImm n) 
        | (Div, Bop (Add, Bop (Mul, x, IntImm m), y), IntImm n) 
        | (Div, Bop (Add, Bop (Mul, IntImm m, x), y), IntImm n) when (m mod n = 0) -> 
            recurse ((x *~ (IntImm (m/n))) +~ (y /~ (IntImm n)))


        (* op (Ramp, Broadcast) should be folded into the ramp *)
        | (Add, Broadcast (e, _), Ramp (b, s, n)) 
        | (Add, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b +~ e), s, n)
        | (Sub, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b -~ e), s, n)
        | (Mul, Broadcast (e, _), Ramp (b, s, n)) 
        | (Mul, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b *~ e), recurse (s *~ e), n)
        (* | (Div, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b /~ e), recurse (s /~ e), n) *)

        (* op (Broadcast, Broadcast) should be folded into the broadcast *)
        | (Add, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (a +~ b, n))
        | (Sub, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (a -~ b, n))
        | (Mul, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (a *~ b, n))
        | (Div, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (a /~ b, n))
        | (Mod, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (a %~ b, n))
        | (Max, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (Bop (Max, a, b), n))
        | (Min, Broadcast (a, n), Broadcast(b, _)) -> recurse (Broadcast (Bop (Min, a, b), n))

        (* Converting subtraction to addition *)
        | (Sub, x, IntImm y) -> recurse (x +~ (IntImm (-y)))
        | (Sub, x, FloatImm y) -> recurse (x +~ (FloatImm (-.y)))

        (* Convert const + varying to varying + const (reduces the number of cases to check later) *)
        | (Add, x, y) when is_const x -> recurse (y +~ x)
        | (Mul, x, y) when is_const x -> recurse (y *~ x)

        (* Convert divide by float constants to multiplication *)
        | (Div, x, FloatImm y) -> Bop (Mul, x, FloatImm (1.0 /. y))

        (* Ternary expressions that can be reassociated. Previous passes have cut down on the number we need to check. *)
        (* (X + y) + z -> X + (y + z) *)
        | (Add, Bop (Add, x, y), z) when is_const y && is_const z -> recurse (x +~ (y +~ z))
        (* (x - Y) + z -> (x + z) - Y *)
        | (Add, Bop (Sub, x, y), z) when is_const x && is_const z -> recurse ((x +~ z) -~ y)            

        (* In ternary expressions with one constant, pull the constant outside *)
        | (Add, Bop (Add, x, y), z) when is_const y -> recurse ((x +~ z) +~ y)
        | (Sub, Bop (Add, x, y), z) when is_const y -> recurse ((x -~ z) +~ y)
        | (Add, z, Bop (Add, x, y)) when is_const y -> recurse ((x +~ z) +~ y)
        | (Sub, z, Bop (Add, x, y)) when is_const y -> recurse ((z -~ x) -~ y)

        (* Additions or subtractions that cancel an inner term *)
        | (Sub, Bop (Add, x, y), z) 
        | (Add, z, Bop (Sub, x, y))
        | (Add, Bop (Sub, x, y), z) when y = z -> x
        | (Sub, Bop (Add, x, y), z) when x = z -> y

        | (Sub, Bop (Add, x, Bop (Add, y, z)), w) when w = z -> 
            recurse (x +~ y)
        | (Sub, Bop (Add, x, Bop (Add, z, y)), w) when w = z -> 
            recurse (x +~ y) 

        (* Ternary expressions that should be distributed *)
        | (Mul, Bop (Add, x, y), z) when is_const y && is_const z -> recurse ((x *~ z) +~ (y *~ z))     

        (* These particular patterns are commonly generated by lowering, so we should catch and simplify them *)
        | (Max, Bop (Add, x, IntImm a), Bop (Add, y, IntImm b)) when x = y ->
            if a > b then x +~ IntImm a else x +~ IntImm b
        | (Max, x, Bop (Add, y, IntImm a)) 
        | (Max, Bop (Add, x, IntImm a), y) when x = y ->
            if a > 0 then x +~ IntImm a else x
        | (Min, Bop (Add, x, IntImm a), Bop (Add, y, IntImm b)) when x = y ->
            if a < b then x +~ IntImm a else x +~ IntImm b
        | (Min, x, Bop (Add, y, IntImm a)) 
        | (Min, Bop (Add, x, IntImm a), y) when x = y ->
            if a < 0 then x +~ IntImm a else x

        | (Min, x, y)
        | (Max, x, y) when x = y -> x

        | (op, x, y) -> Bop (op, x, y)
      end

    (* comparison *)
    | Cmp (op, a, b) ->
      begin match (recurse a, recurse b) with
        | (Broadcast (x, w), Broadcast(y, _)) -> Broadcast (recurse (Cmp (op, x, y)), w)
        | (IntImm  x, IntImm  y)   -> bool_imm (caml_op_of_cmp op x y) 
        | (FloatImm x, FloatImm y) -> bool_imm (caml_op_of_cmp op x y) 
        | (x, y) -> Cmp (op, x, y)
      end

    (* logical *)
    | And (a, b) ->
      begin match (recurse a, recurse b) with
        | (Broadcast (x, w), Broadcast(y, _)) -> Broadcast (recurse (And (x, y)), w)
        | (Cast (UInt 1, IntImm 0), _)
        | (_, Cast(UInt 1, IntImm 0)) -> Cast(UInt 1, IntImm 0)
        | (Cast(UInt 1, IntImm 1), x)
        | (x, Cast(UInt 1, IntImm 1)) -> x
        | (x, y) -> And (x, y)
      end
    | Or (a, b) ->
      begin match (recurse a, recurse b) with
        | (Broadcast (x, w), Broadcast(y, _)) -> Broadcast (recurse (Or (x, y)), w)
        | (Cast(UInt 1, IntImm 1), _)
        | (_, Cast(UInt 1, IntImm 1)) -> Cast(UInt 1, IntImm 1)
        | (Cast(UInt 1, IntImm 0), x)
        | (x, Cast(UInt 1, IntImm 0)) -> x
        | (x, y) -> Or (x, y)
      end
    | Not a ->
      begin match recurse a with
        | Broadcast (x, w) -> Broadcast (recurse (Not x), w)
        | Cast(UInt 1, IntImm 0) -> Cast(UInt 1, IntImm 1)
        | Cast(UInt 1, IntImm 1) -> Cast(UInt 1, IntImm 0)
        | x -> Not x
      end
    | Select (c, a, b) ->
      begin match (recurse c, recurse a, recurse b) with
        | (_, x, y) when x = y -> x
        | (Broadcast (Cast(UInt 1, IntImm 0), _), _, x) 
        | (Cast(UInt 1, IntImm 0), _, x) -> x
        | (Broadcast (Cast(UInt 1, IntImm 1), _), x, _) 
        | (Cast(UInt 1, IntImm 1), x, _) -> x
        | (c, x, y) -> Select (c, x, y)
      end
    | Load (t, buf, idx) -> Load (t, buf, recurse idx)
    | MakeVector l -> MakeVector (List.map recurse l)
    | Broadcast (e, n) -> Broadcast (recurse e, n)
    | Ramp (b, s, n) -> Ramp (recurse b, recurse s, n)
    | ExtractElement (MakeVector l, IntImm elem) ->
        recurse (List.nth l elem)
    | ExtractElement (Ramp (base, stride, _), elem) ->
        recurse (base +~ (Cast (val_type_of_expr stride, elem)) *~ stride)
    | ExtractElement (a, b) -> ExtractElement (recurse a, recurse b)
    | Debug (e, n, args) -> Debug (recurse e, n, args)

    | Let (n, a, b) -> 
        let a = recurse a and b = recurse b in
        let old = Var (val_type_of_expr a, n) in
        begin match a with
          (* It's really important to inline ramps and broadcasts to get good loads and stores *)
          | Broadcast (x, w) -> 
              let newvar = Var (val_type_of_expr x, n) in
              recurse (Let (n, x, subs_expr old (Broadcast (newvar, w)) b))
          | Ramp (base, stride, w) -> 
              let newvar = Var (val_type_of_expr base, n) in
              recurse (Let (n, base, subs_expr old (Ramp (newvar, stride, w)) b))
          | x when is_simple x -> recurse (subs_expr old a b)
          (* if a is a multiple of something, that should go to the inside to help out any modulo analysis *)
          | _  when (is_scalar a && is_integral a) -> begin            
            match Analysis.compute_remainder_modulus a with
              | (0, 1) -> Let (n, a, b)
              | (_, 0) -> failwith ("lhs of let is apparently a constant but was not folded in by is_simple rule: " ^ (Ir_printer.string_of_expr (Let (n, a, b))))
              | (r, m) -> 
                  let r = Cast (val_type_of_expr a, IntImm r) in
                  let m = Cast (val_type_of_expr a, IntImm m) in
                  let oldvar = Var (val_type_of_expr a, n) in
                  let newvar = (old *~ m) +~ r in
                  let a = recurse ((a -~ r) /~ m) in
                  Let (n, a, recurse (subs_expr oldvar newvar b))                    
          end
          | _ -> Let (n, a, b)
        end
    | Call (ct, rt, n, args) -> Call (ct, rt, n, List.map recurse args)

    (* Immediates are unchanged *)
    | x -> x

let constant_fold_stmt stmt =
  let rec inner env = function
    | For (var, min, size, order, stmt) ->
        (* Remove trivial for loops *)
        let min = constant_fold_expr min in 
        let size = constant_fold_expr size in
        if size = IntImm 1 then
          inner env (LetStmt (var, min, stmt))
        else
          (* Consider rewriting the loop from 0 to size - in many cases it will simplify the interior *)
          let body = inner env stmt in
          let old_var = Var (Int 32, var) in
          let new_var = (old_var +~ min) in
          let alternative = inner env (subs_expr_in_stmt old_var new_var body) in
          let complexity stmt = fold_children_in_stmt (fun x -> 1) (fun x -> 1) (+) stmt in
          if (complexity alternative) <= (complexity body) then
            For (var, IntImm 0, size, order, alternative)
          else 
            For (var, min, size, order, body)
    | Block l ->
        Block (List.map (inner env) l)
    | Store (e, buf, idx) ->
        Store (constant_fold_expr e, buf, constant_fold_expr idx)
    | Provide (e, func, args) ->
        Provide (constant_fold_expr e, func, List.map constant_fold_expr args)
    | LetStmt (name, value, stmt) ->        
        let value = constant_fold_expr value in
        let t = val_type_of_expr value in
        let var = Var (t, name) in
        let rec scoped_subs_stmt value stmt = match stmt with
          | LetStmt (n, _, _) when n = name -> stmt
          | _ -> mutate_children_in_stmt (subs_expr var value) (scoped_subs_stmt value) stmt
        in
        if (is_simple value) then begin
          inner env (scoped_subs_stmt value stmt)
        end else begin
          try begin
            (* Check if this value already had a name *)
            let (n, v) = List.find (fun (n, v) -> v = value) env in
            inner env (scoped_subs_stmt (Var (t, n)) stmt)
          end with Not_found -> begin
            let env = (name, value)::env in
            LetStmt (name, value, inner env stmt)
          end
        end
    | Pipeline (n, ty, size, produce, consume) -> 
        Pipeline (n, ty, constant_fold_expr size,
                  inner env produce,
                  inner env consume)
    | Print (p, l) -> 
        Print (p, List.map constant_fold_expr l)
    | Assert(e, str) ->
        Assert (constant_fold_expr e, str)
  in
  inner [] stmt
