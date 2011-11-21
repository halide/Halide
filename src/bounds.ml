open Constant_fold
open Ir
open Util

type bounds_result = Range of (expr * expr) | Unbounded

let rec bounds_of_expr_in_env env expr =
  let recurse = bounds_of_expr_in_env env in
  match expr with
    | Load (t, _, _)   -> Unbounded
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
            Range (mina +~ minc, mina +~ maxc)
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

    | Let (n, a, b) -> bounds_of_expr_in_env (StringMap.add n (recurse a) env) b
        
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
      try StringMap.find n env with Not_found -> Unbounded
    end

    | IntImm n -> Range (IntImm n, IntImm n)
    | UIntImm n -> Range (UIntImm n, UIntImm n)
    | FloatImm n -> Range (FloatImm n, FloatImm n)




let bounds_of_expr var (min, max) expr = 
  bounds_of_expr_in_env (StringMap.add var (Range (min, max)) StringMap.empty) expr

