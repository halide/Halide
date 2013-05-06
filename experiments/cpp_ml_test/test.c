#include <stdio.h>
#include <caml/mlvalues.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <string.h>

int main(int eargc, char **argv) {

  caml_startup(argv);

  value *makeFoo1 = caml_named_value("makeFoo1");
  value *makeFoo2 = caml_named_value("makeFoo2");
  value *makeFoo3 = caml_named_value("makeFoo3");
  value *makeFoo4 = caml_named_value("makeFoo4");
  value *eatFoo = caml_named_value("eatFoo");

  printf("Got functions: %p %p %p %p %p\n", 
         makeFoo1, makeFoo2, makeFoo3, makeFoo4, eatFoo);

  value foo;

  // Prevent gc of foo
  register_global_root(&foo);

  foo = caml_callback(*makeFoo1, Val_unit);  
  caml_callback(*eatFoo, foo);

  foo = caml_callback(*makeFoo2, Val_int(17));  
  caml_callback(*eatFoo, foo);

  const char *msg = "Hi!";
  value caml_msg = caml_alloc_string(sizeof(msg)+100);
  strcpy(String_val(caml_msg), msg);
  foo = caml_callback(*makeFoo3, caml_msg);  
  caml_callback(*eatFoo, foo);

  foo = caml_callback2(*makeFoo4, Val_int(18), Val_int(19));  
  caml_callback(*eatFoo, foo);

  // Allow gc
  remove_global_root(&foo);

  return 0;
}
