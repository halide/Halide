let () =
  for i=0 to (Array.length Sys.argv)-1 do
    Printf.printf "%d: %s\n" i Sys.argv.(i)
  done
