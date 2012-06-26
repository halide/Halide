open Ir
open Sexplib
open Batteries_uni

let _ =
  let entry =
    File.with_file_in Sys.argv.(1)
      (fun ins -> entrypoint_of_sexp (Sexp.of_string (String.strip (IO.read_all ins))))
  in

  let name,args,body = entry in

  Printf.printf "loaded %s:\n%s\n  ==>\n" name (Ir_printer.string_of_stmt body);

  Printf.printf "%s\n" (Pretty.to_string 80 (Ppcee.program (Cg_c.cg_entry entry)))
