open Llvm
open Printf

let _ =
  let ctx = create_context () in
  let m = create_module ctx "test_module" in
  
  let print_val prefix v = printf "%s%s" prefix (value_name v) in
  
  let print_func f =
    print_val "fun " f;
    (* printf "("; *)
    (* iter_params (print_val " ") f; *)
    (* printf " )"; *)
    printf " -> %s\n" (string_of_lltype (type_of f))
  in
  
  printf "Created empty module:\n";
  iter_functions print_func m;
  printf "---\n";

  Stdlib.init_module_ptx m;
  printf "Initialized with PTX stdlib:\n";
  iter_functions print_func m;
