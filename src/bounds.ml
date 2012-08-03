open Constant_fold
open Ir
open Util
open Analysis

type bounds_result = Range of (expr * expr) | Unbounded

let make_range (min, max) =
  let min = constant_fold_expr min in
  let max = constant_fold_expr max in
  (* dbg 2 "Making range %s %s\n%!" (Ir_printer.string_of_expr min) (Ir_printer.string_of_expr max);  *)
  assert (is_scalar min);
  assert (is_scalar max);
  Range (min, max)

let check_result expr result = 
  (* let result_string = match result with
    | Unbounded -> "Unbounded"
    | Range (min, max) -> 
        "(" ^ 
          Ir_printer.string_of_expr (constant_fold_expr min) ^ ", " ^ 
          Ir_printer.string_of_expr (constant_fold_expr max) ^ ")"
  in
  
  Printf.printf "Bounds of %s = %s\n%!" (Ir_printer.string_of_expr expr) result_string; 
  
  begin match result with 
    | Range (min, max) -> assert (is_scalar min && is_scalar max)
    | _ -> ()
  end *)
  ()


let is_monotonic = function
  | ".floor_f32"
  | ".ceil_f32"
  | ".sqrt_f32"
  | ".exp_f32"
  | ".log_f32" -> true
  | _ -> false

let bounds_of_type = function
  | UIntVector (8, _)
  | UInt 8  -> Range (Cast (UInt 8, IntImm 0), Cast (UInt 8, IntImm 255))
  | IntVector (8, _)
  | Int 8  -> Range (Cast (Int 8, IntImm (-128)), Cast (Int 8, IntImm 127))
  | UIntVector (16, _)
  | UInt 16  -> Range (Cast (UInt 16, IntImm 0), Cast (UInt 16, IntImm 65535))
  | IntVector (16, _)
  | Int 16  -> Range (Cast (Int 16, IntImm (-32768)), Cast (Int 16, IntImm 32767))
  | _ -> Unbounded

let bounds_of_expr_in_env env expr =
  let rec bounds_of_expr_in_env_inner env expr = 
    let recurse e = 
      let result = bounds_of_expr_in_env_inner env e in
      dbg 2 "Computed bounds of %s...\n%!" (Ir_printer.string_of_expr e);
      (* check_result e result; *)
      result
    in

    let type_bounds = bounds_of_type (val_type_of_expr expr) in

    let result = match expr with
      | Load (t, _, idx)   -> 
          (* if idx depends on anything in env then Unbounded else return this  *)
          let rec contains_var_in_env = function        
            | Var (t, n) -> StringMap.mem n env
            | expr -> fold_children_in_expr contains_var_in_env (fun x y -> x or y) false expr
          in 
          if contains_var_in_env idx then (bounds_of_type t) else make_range (expr, expr) 
      | Broadcast (e, _) -> recurse e
      | Cast (t, e)      -> begin
        let t = element_val_type t in
        match recurse e with
          | Range (min, max) -> make_range (Cast (t, min), Cast (t, max))
          | _ -> type_bounds
      end
      | Ramp (a, b, n)   -> begin    
        let maxn = Cast(val_type_of_expr b, IntImm (n-1)) in
        let zero = make_zero (val_type_of_expr a) in
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->               
              (* Compute the bounds of the product term *)
              let p1 = maxn *~ minb and
                  p2 = maxn *~ maxb in
              let (minc, maxc) = (Bop (Min, Bop (Min, p1, p2), zero),
                                  Bop (Max, Bop (Max, p1, p2), zero)) in              
              (* Add the base term *)
              make_range (mina +~ minc, maxa +~ maxc)
          | _ -> Unbounded
      end
      | ExtractElement (a, b) -> recurse a
      | Bop (Add, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> make_range (mina +~ minb, maxa +~ maxb)
          | _ -> Unbounded        
      end
      | Bop (Sub, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> make_range (mina -~ maxb, maxa -~ minb)
          | _ -> Unbounded
      end
      | Bop (Mul, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->              
              (* Each range can have one of three states
                 1) strictly positive
                 2) strictly negative
                 3) mixed
                 
                 We presume that it will be easier to constant fold
                 statments of the form x > 0 than mins and maxes of more
                 complicated expressions, so we break things up by
                 case.  *)
              
              let zero = make_zero (val_type_of_expr mina) in
              let b_positive = minb >=~ zero and
                  b_negative = maxb <=~ zero and
                  a_positive = mina >=~ zero and
                  a_negative = maxa <=~ zero 
              in
              
              let select3 cond1 (then1a, then1b) cond2 (then2a, then2b) (else_case_a, else_case_b) =
                (Select (cond1, then1a, Select(cond2, then2a, else_case_a)),
                 Select (cond1, then1b, Select(cond2, then2b, else_case_b)))
              in
              
              make_range (
                select3 a_positive (
                  select3 b_positive (
                    (mina *~ minb, maxa *~ maxb)
                  ) b_negative (
                    (maxa *~ minb, mina *~ maxb)
                  ) (* b_mixed *) (
                    (maxa *~ minb, maxa *~ maxb)
                  )
                ) a_negative (
                  select3 b_positive (
                    (mina *~ maxb, maxa *~ minb)
                  ) b_negative (
                    (maxa *~ maxb, mina *~ minb)
                  ) (* b_mixed *) (
                    (mina *~ maxb, mina *~ minb)
                  )
                ) (* a_mixed *) (
                  select3 b_positive (
                    (mina *~ maxb, maxa *~ maxb)
                  ) b_negative (
                    (maxa *~ minb, mina *~ minb)
                  ) (* b_mixed *) (
                    (Bop (Min, maxa *~ minb, mina *~ maxb),
                     Bop (Max, mina *~ minb, maxa *~ maxb))
                  )
                )
              )
          | _ -> Unbounded
      end
      | Bop (Div, a, b) -> begin
        (* If b could be zero then unbounded *)
        match (recurse b) with
          | Unbounded -> Unbounded
          | Range (minb, maxb) ->
              let zero = make_zero (val_type_of_expr minb) in
              let b_positive = 
                Constant_fold.constant_fold_expr (minb >=~ zero) in
              let b_negative =
                Constant_fold.constant_fold_expr (maxb <=~ zero) in
              
              if (b_positive = (bool_imm false) &&
                  b_negative = (bool_imm false)) then
                Unbounded
              else begin
                match (recurse a) with
                  | Range (mina, maxa) ->
                      (* a can have one of three states
                         1) strictly positive
                         2) strictly negative
                         3) mixed
                         
                         b can only be strictly positive or strictly negative,
                         because we know the range doesn't contain zero.
                         
                         We presume that it will be easier to constant fold
                         statments of the form x > 0 than mins and maxes of
                         more complicated expressions, so we again break
                         things up by case.  *)
                    
                      let a_positive = mina >=~ zero and
                        a_negative = maxa <=~ zero 
                      in
                      
                      let select3 cond1 (then1a, then1b) cond2 (then2a, then2b) (else_case_a, else_case_b) =
                        (Select (cond1, then1a, Select(cond2, then2a, else_case_a)),
                         Select (cond1, then1b, Select(cond2, then2b, else_case_b)))
                      in
                      
                      let select2 cond1 (then1a, then1b) (else_case_a, else_case_b) =
                        (Select (cond1, then1a, else_case_a),
                         Select (cond1, then1b, else_case_b))
                      in
                      
                      make_range (
                        select3 a_positive (
                          select2 b_positive (
                            (mina /~ maxb, maxa /~ minb)
                          ) (* b_negative *) (
                            (maxa /~ maxb, mina /~ minb)
                          )
                        ) a_negative (
                          select2 b_positive (
                            (mina /~ minb, maxa /~ maxb)
                          ) (* b_negative *) (
                            (maxa /~ minb, mina /~ maxb)
                          )
                        ) (* a_mixed *) (
                          select2 b_positive (
                            (mina /~ minb, maxa /~ minb)
                          ) (* b_negative *) (
                            (maxa /~ maxb, mina /~ maxb)
                          )
                        )
                      )
                        
                  | _ -> Unbounded
              end
      end
          
      | Bop (Mod, a, b) -> begin
        let zero e = make_zero (val_type_of_expr e) in
        let one e = make_one (val_type_of_expr e) in
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> 
              let cond = (mina >=~ (zero mina)) &&~ (maxa <~ minb) in
              make_range (Select (cond, mina, (zero mina)),
                     Select (cond, maxa, maxb -~ (one maxb)))
          | Unbounded, Range (minb, maxb) -> 
              make_range (zero maxb, maxb -~ (one maxb))
          | _ -> Unbounded
      end
          
      (* The Clamp pattern *)
      | Bop (Max, Bop (Min, a, b), c)
      | Bop (Max, c, Bop (Min, a, b))
      | Bop (Min, Bop (Max, a, c), b)
      | Bop (Min, b, Bop (Max, a, c)) when
          recurse a = Unbounded -> begin
            (* a is unbounded, b is the upper bound, c is the lower bound *)
            match (recurse b, recurse c) with
              | Range (minb, maxb), Range (minc, maxc) ->
                  make_range (minc, maxb)
              | _ -> Unbounded
          end
      | Bop (Max, Bop (Min, b, a), c)
      | Bop (Max, c, Bop (Min, b, a))
      | Bop (Min, Bop (Max, c, a), b)
      | Bop (Min, b, Bop (Max, c, a)) when
          recurse a = Unbounded -> begin
            (* a is unbounded, b is the upper bound, c is the lower bound *)
            match (recurse b, recurse c) with
              | Range (minb, maxb), Range (minc, maxc) ->
                make_range (minc, maxb)
              | _ -> Unbounded
          end
      | Bop (Max, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->
              make_range (Bop (Max, mina, minb), 
                     Bop (Max, maxa, maxb))
          | _ -> Unbounded
      end
      | Bop (Min, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->
              make_range (Bop (Min, mina, minb), 
                     Bop (Min, maxa, maxb))
          | _ -> Unbounded
      end
          
      | Not _
      | And (_, _)
      | Or (_, _)
      | Cmp (_, _, _) -> make_range (UIntImm 0, UIntImm 1)
          
      | Let (n, a, b) -> 
          (* Printf.printf "Computing bounds of %s...\n%!" (Ir_printer.string_of_expr expr); *)
          let result = bounds_of_expr_in_env_inner (StringMap.add n (recurse a) env) b in
          check_result expr result;
          result

      | Select (_, a, b) -> begin
        match (recurse a, recurse b) with 
          | Range (mina, maxa), Range (minb, maxb) ->
              make_range (Bop (Min, mina, minb),
                     Bop (Max, maxa, maxb))
          | _ -> Unbounded
      end
          
      | MakeVector l -> begin
        let rec build_range = function
          | (first::[]) -> recurse first
          | (first::rest) -> begin 
            match (recurse first, build_range rest) with
              | Range (mina, maxa), Range (minb, maxb) ->
                  make_range (Bop (Min, mina, minb),
                         Bop (Max, maxa, maxb))            
              | _ -> Unbounded
          end
          | [] -> failwith "Empty MakeVector encountered"
        in build_range l
      end
          
      (* Unary monotonic built-ins *)
      | Call (Extern, t, f, [arg]) when is_monotonic f ->
          begin match (recurse arg) with 
            | Range (min, max) ->
                make_range (Call (Extern, t, f, [min]),
                            Call (Extern, t, f, [max]))
            | _ -> Unbounded
          end
             
      (* Trig built-ins *)
      | Call (Extern, t, f, [arg]) when f = ".sin" || f = ".cos" -> 
          Range ((make_zero t) -~ (make_one t), make_one t)                  

      | Call (_, t, _, _) -> bounds_of_type t
      | Debug (e, _, _) -> recurse e
          
      | Var (t, n) -> begin
        try StringMap.find n env with Not_found -> 
          make_range (Var (t, n), Var (t, n))
      end
          
      | IntImm n -> make_range (IntImm n, IntImm n)
      | UIntImm n -> make_range (UIntImm n, UIntImm n)
      | FloatImm n -> make_range (FloatImm n, FloatImm n)
    in

    (*
    let result_string = match result with
      | Unbounded -> "Unbounded"
      | Range (min, max) -> 
          "(" ^ 
            Ir_printer.string_of_expr (constant_fold_expr min) ^ ", " ^ 
            Ir_printer.string_of_expr (constant_fold_expr max) ^ ")"
    in
    dbg 2 "Bounds of %s = %s\n" (Ir_printer.string_of_expr expr) result_string;
    *)

    result
  in
  let result =
  match bounds_of_expr_in_env_inner env expr with
    | Unbounded -> Unbounded
    | Range (min, max) -> make_range (constant_fold_expr min, constant_fold_expr max)
  in
  check_result expr result;
  result
          
          
          
let bounds_of_expr var (min, max) expr = 
  bounds_of_expr_in_env (StringMap.add var (make_range (min, max)) StringMap.empty) expr


let range_union a b = match (a, b) with
  | Range (mina, maxa), Range (minb, maxb) ->
      make_range (Bop (Min, mina, minb), Bop (Max, maxa, maxb))
  | _ -> Unbounded
     
let region_union a b = match (a, b) with
  | [], _ -> b
  | _, [] -> a
  | _ -> List.map2 range_union a b

(* What region of func is used by expr with variable ranges in env *)
let rec required_of_expr func env = function
  | Call (_, _, f, args) when f = func ->       
      let required_of_args = List.map (required_of_expr func env) args in
      let required_of_call = List.map (fun arg -> bounds_of_expr_in_env env arg) args in
      List.fold_left region_union required_of_call required_of_args
  | Let (n, a, b) ->
      let required_of_a = required_of_expr func env a in
      let bounds_of_a = bounds_of_expr_in_env env a in
      let required_of_b = required_of_expr func (StringMap.add n bounds_of_a env) b in
      region_union required_of_a required_of_b
  | expr -> fold_children_in_expr (required_of_expr func env) region_union [] expr  
  
(* What region of the func is used by the statement *)
let rec required_of_stmt func env = function
  | For (var, min, size, order, body) ->
      let env = StringMap.add var (make_range (min, min +~ size -~ (IntImm 1))) env in
      required_of_stmt func env body
  | LetStmt (name, expr, stmt) ->
      (* Brute force. Might get really slow for long chains of letstmts *)
      let subs = subs_expr_in_stmt (Var (val_type_of_expr expr, name)) expr in
      required_of_stmt func env (subs stmt)
      (* TODO: Why doesn't the below work? Does this mean let above is broken too?  
      let required_of_expr = required_of_expr func env expr in 
      let bounds_of_expr = bounds_of_expr_in_env env expr in
      let required_of_stmt = required_of_stmt func (StringMap.add name bounds_of_expr env) stmt in
      region_union required_of_expr required_of_stmt  *)
  | Provide (expr, name, args) ->
      (* A provide touches the same things as a similar call *)
      region_union 
        (required_of_expr func env expr) 
        (required_of_expr func env (Call (Func, val_type_of_expr expr, name, args)))
  | stmt -> fold_children_in_stmt (required_of_expr func env) (required_of_stmt func env) region_union stmt
  

        
