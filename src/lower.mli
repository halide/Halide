(* Generate the realization of some function over the region specified
   in the schedule tree. *)
val lower_function : string -> Ir.environment -> Schedule.schedule_tree -> bool -> Ir.stmt
