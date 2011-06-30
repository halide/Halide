(*open Bigarray*)
open Llvm
open Llvm_executionengine

let () =
  let mb = MemoryBuffer.of_file "bigarray.bc" in
  let c = create_context () in
  let m = Llvm_bitreader.parse_bitcode c mb in
  let ee = ExecutionEngine.create m in
  let f = match ExecutionEngine.find_function "ptr_test" ee with
    | Some f -> f
    | None -> raise Not_found
  in

  let arr = Bigarray.Array1.create Bigarray.int Bigarray.c_layout 10 in
    Bigarray.Array1.fill arr 0;
    arr.{0} <- 7;
    arr.{1} <- 6;

  (* run *)
  let res = ExecutionEngine.run_function f [| GenericValue.of_pointer arr |] ee in
    ignore (res);
    (*Printf.printf "Got: %d\n" (Nativeint.to_int (GenericValue.as_nativeint res));*)

  (* cleanup *)
    MemoryBuffer.dispose mb;
