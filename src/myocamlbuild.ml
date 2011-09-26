open Ocamlbuild_plugin;;

(*
 * Essential ocamlbuild setup for full project, including LLVM libs and cimg.c.
 * Cf. http://brion.inria.fr/gallium/index.php/Using_an_external_library
 *     http://brion.inria.fr/gallium/index.php/Ocamlbuild_example_with_C_stubs
 * for reference examples. 
 *)

(* require individual LLVM component libraries here *)
(* ~dir:... specifies search path for each lib. Need on *.ml as well as *.byte
 * to search for includes, not just libs to link. *)
ocaml_lib ~extern:true "unix";; (* Unix is (oddly) needed by llvm when building a toplevel. Must be first. *)
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_analysis";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitwriter";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_bitreader";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_target";;
ocaml_lib ~extern:true ~dir:"../../llvm/Debug+Asserts/lib/ocaml/" "llvm_executionengine";;

(* Define ocamlc link flag: -cc g++ *)
(* This is necessary to ensure linkage of libstdc++ for LLVM. *)
(* This actually gets set for a target with the `g++` tag in `_tags`. *)
(*flag ["link"; "ocaml"; "g++"] (S[A"-cc"; A"g++"]);;*) (* this version spews
tons of deprecation warning noise with g++ 4.4, just linking stdc++ below works
better *)
flag ["link"; "ocaml"; "g++"] (S[A"-cclib"; A"-lstdc++"]);;

(* Get config from libpng-config *)
let libpng_config arg =
  let cmd = Printf.sprintf "libpng-config --%s" arg in
  String.chomp (run_and_read cmd)
in

(* Link the cimg module.
 * Based on: 
 * http://stackoverflow.com/questions/2374136/ocamlbuild-building-toplevel/2377851#2377851 *)
ocaml_lib "img";
let libimg_stubs = "libimg_stubs." ^ !Options.ext_lib in
let link_png_path = libpng_config "L_opts" in
dep ["link"; "ocaml"; "use_img"] [libimg_stubs];
flag ["link"; "ocaml"; "use_img"] (S[A"-cclib"; A link_png_path; A"-cclib"; A"-lpng"; A"-cclib"; A libimg_stubs]);

(* C compiler arguments for building cimg.c -> cimg.o *)
let include_ocaml = "-I/usr/local/lib/ocaml" in
let use_c99 = "-std=c99" in
let include_png = libpng_config "cflags" in
flag ["c"; "compile"; "cimg_cflags"]
  (S[A"-ccopt"; A include_ocaml; A"-ccopt"; A use_c99; A"-ccopt"; A include_png]);;
