.PHONY: clean

halide_remote.h: halide_remote.idl
	$(QAIC) halide_remote.idl

halide_remote_skel.c: halide_remote.idl
	$(QAIC) halide_remote.idl

halide_remote_stub.c: halide_remote.idl
	$(QAIC) halide_remote.idl

halide_remote.so: halide_remote.cpp halide_remote_skel.c halide_remote.h
	$(HEX_CLANG) halide_remote.cpp halide_remote_skel.c

clean:
	rm -rf halide_remote.h halide_remote_skel.c halide_remote_stub.c
