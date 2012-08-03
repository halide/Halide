open Ocamlbuild_pack
open Ocamlbuild_plugin

(*
 * Essential ocamlbuild setup for full project, including LLVM libs 
 * Cf. http://brion.inria.fr/gallium/index.php/Using_an_external_library
 *     http://brion.inria.fr/gallium/index.php/Ocamlbuild_example_with_C_stubs
 * for reference examples. 
 *)

let flag_tags prefix name targets =
  let target_tag t =
    (Tags.of_list prefix) ++ t in
  let target_tags = List.map (fun t -> (T(target_tag t))) targets in
  Printf.printf "Target tags: %s\n" (Command.string_of_command_spec (Command.reduce (S(target_tags))));
  flag (prefix @ [name]) (S(target_tags))

let () = dispatch begin function
  | After_rules ->

      (* require individual LLVM component libraries here *)
      (* ~dir:... specifies search path for each lib. Need on *.ml as well as *.byte
       * to search for includes, not just libs to link. *)
      ocaml_lib ~extern:true "unix"; (* Unix is (oddly) needed by llvm when building a toplevel. Must be first. *)
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm";
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_analysis";
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitwriter";
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitreader";
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_target";
      ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_executionengine";
      ocaml_lib ~extern:true ~dir:"../../src/_build/" "fimage";

      (* Ensure linkage of libstdc++ for LLVM with the `g++` tag *)
      flag ["link"; "ocaml"; "g++"] (S[A"-cclib"; A"-lstdc++"]);

      (*Printf.printf "Cmd: %s\n" (Command.string_of_command_spec (S[A"Atom"; T(Tags.of_list ["ocaml"; "compile"; "use_llvm"])]));*)
      (*Printf.printf "Cmd: %s\n" (Command.string_of_command_spec (S[A"Atom"; T(Tags.of_list ["ocaml"; "link"; "byte"; "use_llvm"])]));*)

      (*flag ["use_fimage"] (T(Tags.of_list ["g++"; "use_llvm"; "use_llvm_target"; "use_llvm_analysis"; "use_llvm_bitwriter"]));*)
      (*flag_tags ["ocaml"; "compile"]        "use_fimage" ["g++"; "use_llvm"; "use_llvm_target"; "use_llvm_analysis"];*)
      (*flag_tags ["ocaml"; "link"; "byte"]   "use_fimage" ["g++"; "use_llvm"; "use_llvm_target"; "use_llvm_analysis"];*)
      (*flag_tags ["ocaml"; "link"; "native"] "use_fimage" ["g++"; "use_llvm"; "use_llvm_target"; "use_llvm_analysis"];*)
      ()
  | _ -> ()
end

