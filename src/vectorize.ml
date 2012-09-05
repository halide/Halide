open Ir
open Util
open List
open Analysis

let expand e width = 
  if (is_scalar e) then Broadcast (e, width) else e

(* Substitute var for something that might be a vector in expr and propagate the vectorness upwards *)
let rec vector_subs_expr (env:expr StringMap.t) (expr:expr) =
  let (some_var, some_value) = StringMap.choose env in
  let width = vector_elements (val_type_of_expr some_value) in

  assert (StringMap.for_all (fun _ value -> vector_elements (val_type_of_expr value) = width) env);
 
  if width = 1 then
    (* If you pass a scalar, vector_subs_expr just acts as a bunch of subs_expr *)
    StringMap.fold (fun key value expr -> subs_expr (Var (i32, key)) value expr) env expr
  else

    let expand e = expand e width in
    let rec vec expr = 
      assert (is_scalar expr);

      (* Are any of the variables in the expression in the environment *)
      let rec contains_vars_in_env = function
        | Var (_, n) -> StringMap.mem n env
        | x -> fold_children_in_expr contains_vars_in_env (||) false x
      in

      match expr with
      | x when not (contains_vars_in_env x) -> x
          
      | Cast (t, expr) -> Cast (vector_of_val_type t width, vec expr)
          
      | Bop (op, a, b) -> 
          let va = vec a and vb = vec b in
          begin match (op, va, vb) with
            | (Add, Ramp (ba, sa, _), Ramp (bb, sb, _)) -> Ramp (ba +~ bb, sa +~ sb, width)
            | (Sub, Ramp (ba, sa, _), Ramp (bb, sb, _)) -> Ramp (ba -~ bb, sa -~ sb, width)
            | (Mul, Ramp (b, s, _), x) -> Ramp (b *~ x, s *~ x, width)
            | (Mul, x, Ramp (b, s, _)) -> Ramp (x *~ b, x *~ s, width)
            (* | (Div, Ramp (b, s, _), x) -> Ramp (b /~ x, s /~ x, width) *)
            | (Add, Ramp (b, s, _), x) -> Ramp (b +~ x, s, width)
            | (Add, x, Ramp (b, s, _)) -> Ramp (x +~ b, s, width)
            | (Sub, Ramp (b, s, _), x) -> Ramp (b -~ x, s, width)
            | (Sub, x, Ramp (b, s, _)) -> Ramp (x -~ b, (Cast (val_type_of_expr s, IntImm 0)) -~ s, width)
            | _ -> Bop (op, expand va, expand vb)
          end
            
      (* Cmp/And/Or/Not trivially expand:
       * vectorize both operands iff either is a vector *)
      | Cmp (op, a, b) -> Cmp (op, expand (vec a), expand (vec b))
      | And (a, b)     -> And (expand (vec a), expand (vec b))
      | Or (a, b)      -> Or (expand (vec a), expand (vec b))
      | Not (a)        -> Not (vec a)
          
      | Let (name, a, b) -> 
          assert (not (StringMap.mem name env));
          let a = vec a in
          let b = 
            if is_vector a then
              let env = StringMap.add name a env in
              vector_subs_expr env b
            else vec b
          in
          Let (name, a, b)
          
      | Select (c, a, b) ->
          let va = vec a and vb = vec b and vc = vec c in
          if is_scalar vc then
            (* Condition is scalar *)
            match (va, vb) with
              (* TODO: push this special case of ramp/ramp into constant fold *)
              (* Scalar selection between ramps of matching stride we can handle specially *)
              | (Ramp (ba, sa, _), Ramp (bb, sb, _)) when sa = sb -> 
                  Ramp (Select (c, ba, bb), sa, width)
              | _ -> Select (c, expand va, expand vb)
          else
            (* Condition is a vector *)
            Select (vc, expand va, expand vb)
              
      | Load (t, buf, idx) -> Load (vector_of_val_type t width, buf, vec idx)
      (* For extern calls, we blindly vectorize, then worry about calling a scalar version later during codegen *)
      | Call (ct, t, f, args) -> 
          Call (ct, vector_of_val_type t width, f, List.map (fun arg -> expand (vec arg)) args)

      | Var (t, name) -> assert (t = i32); StringMap.find name env

      | Debug (e, prefix, args) -> Debug (vec e, prefix, List.map vec args)
          
      | _ -> failwith "Can't vectorize vector code"
    in vec expr

let vectorize_expr (var:string) (min:expr) (width:int) (expr:expr) = 
  vector_subs_expr (StringMap.add var (Ramp (min, IntImm 1, width)) StringMap.empty) expr

let rec vectorize_stmt var stmt =
  let rec vectorize_stmt_inner (min:expr) (width:int) stmt =
    let vec = vectorize_stmt_inner min width 
    and vec_expr = vectorize_expr var min width in
    match stmt with
      | For (v, min, n, order, stmt) -> For (v, min, n, order, vec stmt)
      | Block l -> Block (map vec l)
      | Store (expr, buf, idx) -> 
          let vec_e = vec_expr expr in
          let vec_i = vec_expr idx in
          begin match (is_vector vec_e, is_vector vec_i) with
            (* If idx is a vector but expr is a scalar, insert a broadcast around expr *)
            | (false, true) -> 
                let vec_e = Broadcast (vec_e, vector_elements (val_type_of_expr vec_i)) in
                Store (vec_e, buf, vec_i)          
            (* If idx is a scalar but expr is a vector, uh... *)
            | (true, false) -> failwith (Printf.sprintf "Can't store a vector to a scalar address of %s" buf);
            (* If both are vectors or both are scalars, we're good *)
            | (_, _) -> Store (vec_e, buf, vec_i)
          end
      | Provide (expr, func, args) -> (* Provide (vec_expr expr, func, List.map vec_expr args) *)
          let vec_a = List.map vec_expr args in
          let vec_e = vec_expr expr in
          (* At most one of the args should be a vector *)
          let vector_args = List.filter is_vector vec_a in
          let num_vector_args = List.length vector_args in
          begin match (is_vector vec_e, num_vector_args) with
            (* If multiple args are vectors, we're in trouble *)
            | (_, n) when n > 1 -> failwith (Printf.sprintf "Can't vectorize across multiple axes of %s" func);
            (* If idx is a vector but expr is a scalar, insert a broadcast around expr *)
            | (false, 1) -> 
                let vec_e = Broadcast (vec_e, vector_elements (val_type_of_expr (List.hd vector_args))) in
                Provide (vec_e, func, vec_a)       
            (* If idx is a scalar but expr is a vector, we're in trouble *)
            | (true, 0) -> failwith (Printf.sprintf "Can't store a vector to a scalar index of %s\n" func);
            (* If both are vectors or both are scalars, we're good *)
            | (_, _) -> Provide (vec_e, func, vec_a)        
          end
      | Print (prefix, args) -> Print (prefix, List.map vec_expr args)
      | s -> failwith (Printf.sprintf "Can't vectorize: %s" (Ir_printer.string_of_stmt s))
  in
  match stmt with        
    | For (name, min, n, order, stmt) when name = var ->
      assert (not order); (* Doesn't make sense to vectorize ordered For *)
      begin match n with
        | IntImm size ->
          For (name, IntImm 0, IntImm 1, false, vectorize_stmt_inner min size stmt)
        | _ -> failwith "Can't vectorize map with non-constant size"
      end
    | For (name, min, n, order, stmt) -> For (name, min, n, order, vectorize_stmt var stmt)
    | Block l -> Block (map (vectorize_stmt var) l)
    | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline (name, ty, size,
                vectorize_stmt var produce,
                vectorize_stmt var consume)
    (* Anything that doesn't contain a sub-statement is unchanged *)
    | x -> x
