; dummy function for now, to be sure this actually gets loaded

define weak_odr i32 @someshit(i32 %x) nounwind readnone {
  %1 = mul nsw i32 %x, %x
  ret i32 %1
}
