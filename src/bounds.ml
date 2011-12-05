open Constant_fold
open Ir
open Util
open Analysis

type bounds_result = Range of (expr * expr) | Unbounded

let bounds_of_expr_in_env env expr =
  let rec bounds_of_expr_in_env_inner env expr = 
    let recurse = bounds_of_expr_in_env_inner env in
    match expr with
      | Load (t, _, idx)   -> 
          (* if idx depends on anything in env then Unbounded else return this *)
          let rec contains_var_in_env = function        
            | Var (t, n) -> StringMap.mem n env
            | expr -> fold_children_in_expr contains_var_in_env (or) false expr
          in 
          if contains_var_in_env idx then Unbounded else Range (expr, expr)
      | Broadcast (e, _) -> recurse e
      | Cast (t, e)      -> begin
        match recurse e with
          | Range (min, max) -> Range (Cast (t, min), Cast (t, max))
          | _ -> Unbounded              
      end
      | Ramp (a, b, n)   -> begin    
        let n = Cast(val_type_of_expr b, IntImm n) in
        let zero = make_zero (val_type_of_expr a) in
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> 
              (* Compute the bounds of the product term *)
              let p1 = n *~ minb and
                  p2 = n *~ maxb in
              let (minc, maxc) = (Bop (Min, Bop (Min, p1, p2), zero),
                                  Bop (Max, Bop (Max, p1, p2), zero)) in              
              (* Add the base term *)
              Range (mina +~ minc, maxa +~ maxc)
        | _ -> Unbounded
      end
      | ExtractElement (a, b) -> recurse a
      | Bop (Add, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> Range (mina +~ minb, maxa +~ maxb)
          | _ -> Unbounded        
      end
      | Bop (Sub, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> Range (mina -~ maxb, maxa -~ minb)
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
              
              let zero = make_zero (val_type_of_expr a) in
              let b_positive = minb >=~ zero and
                  b_negative = maxb <=~ zero and
                  a_positive = mina >=~ zero and
                  a_negative = maxa <=~ zero 
              in
              
              let select3 cond1 (then1a, then1b) cond2 (then2a, then2b) (else_case_a, else_case_b) =
                (Select (cond1, then1a, Select(cond2, then2a, else_case_a)),
                 Select (cond1, then1b, Select(cond2, then2b, else_case_b)))
              in
              
              Range (
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
              let zero = make_zero (val_type_of_expr b) in
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
                      
                      Range (
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
        let zero = make_zero (val_type_of_expr b) in
        let one = Cast (val_type_of_expr b, IntImm 1) in
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) -> 
              let cond = (mina >=~ zero) &&~ (maxa <~ minb) in
              Range (Select (cond, mina, zero),
                     Select (cond, maxa, maxb -~ one))
          | Unbounded, Range (minb, maxb) -> 
              Range (zero, maxb -~ one)
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
                  Range (minc, maxb)
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
                Range (minc, maxb)
              | _ -> Unbounded
          end
      | Bop (Max, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->
              Range (Bop (Max, mina, minb), 
                     Bop (Max, maxa, maxb))
          | _ -> Unbounded
      end
      | Bop (Min, a, b) -> begin
        match (recurse a, recurse b) with
          | Range (mina, maxa), Range (minb, maxb) ->
              Range (Bop (Min, mina, minb), 
                     Bop (Min, maxa, maxb))
          | _ -> Unbounded
      end
          
      | Not _
      | And (_, _)
      | Or (_, _)
      | Cmp (_, _, _) -> Range (UIntImm 0, UIntImm 1)
          
      | Let (n, a, b) -> bounds_of_expr_in_env_inner (StringMap.add n (recurse a) env) b
          
      | Select (_, a, b) -> begin
        match (recurse a, recurse b) with 
          | Range (mina, maxa), Range (minb, maxb) ->
              Range (Bop (Min, mina, minb),
                     Bop (Max, maxa, maxb))
          | _ -> Unbounded
      end
          
      | MakeVector l -> begin
        let rec build_range = function
          | (first::[]) -> recurse first
          | (first::rest) -> begin 
            match (recurse first, build_range rest) with
              | Range (mina, maxa), Range (minb, maxb) ->
                  Range (Bop (Min, mina, minb),
                         Bop (Max, maxa, maxb))            
              | _ -> Unbounded
          end
          | [] -> raise (Wtf "Empty MakeVector encountered")
        in build_range l
      end
          
      | Call (_, _, _) -> Unbounded (* TODO: we could pull in the definition of pure functions here *)
      | Debug (e, _, _) -> recurse e
          
      | Var (t, n) -> begin
        try StringMap.find n env with Not_found -> Range (Var (t, n), Var (t, n))
      end
          
      | IntImm n -> Range (IntImm n, IntImm n)
      | UIntImm n -> Range (UIntImm n, UIntImm n)
      | FloatImm n -> Range (FloatImm n, FloatImm n)
  in
  match bounds_of_expr_in_env_inner env expr with
    | Unbounded -> Unbounded
    | Range (min, max) -> Range (constant_fold_expr min, constant_fold_expr max)
          
          
          
let bounds_of_expr var (min, max) expr = 
  bounds_of_expr_in_env (StringMap.add var (Range (min, max)) StringMap.empty) expr


let range_union a b = match (a, b) with
  | Range (mina, maxa), Range (minb, maxb) ->
      Range (Bop (Min, mina, minb), Bop (Max, maxa, maxb))
  | _ -> Unbounded
     
let region_union a b = match (a, b) with
  | [], _ -> b
  | _, [] -> a
  | _ -> List.map2 range_union a b

(* What region of func is used by expr with variable ranges in env *)
let rec required_of_expr func env = function
  | Call (_, f, args) when f = func -> 
      List.map (fun arg -> bounds_of_expr_in_env env arg) args
  | expr -> fold_children_in_expr (required_of_expr func env) region_union [] expr  
  
(* What region of the func is used by the statement *)
let rec required_of_stmt func env = function
  | For (var, min, size, order, body) ->
      let env = StringMap.add var (Range (min, min +~ size -~ (IntImm 1))) env in
      required_of_stmt func env body
  | stmt -> fold_children_in_stmt (required_of_expr func env) (required_of_stmt func env) region_union stmt
  
(* What region of the func is used by an entire function body *)
let required_of_body func env = function
  | Pure expr -> required_of_expr func env expr 
  | Impure (init, update_loc, update_val) -> 
      let r1 = required_of_expr func env init in
      let r2 = required_of_expr func env update_val in
      let rest = List.map (required_of_expr func env) update_loc in
      List.fold_left region_union (region_union r1 r2) rest
        
