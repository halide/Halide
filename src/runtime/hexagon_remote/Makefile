.PHONY: clean

halide_hexagon_remote.h: halide_hexagon_remote.idl
	$(QAIC) halide_hexagon_remote.idl

halide_hexagon_remote_skel.c: halide_hexagon_remote.idl
	$(QAIC) halide_hexagon_remote.idl

halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	$(QAIC) halide_hexagon_remote.idl

halide_hexagon_remote.so: halide_hexagon_remote.cpp halide_hexagon_remote_skel.c halide_hexagon_remote.h
	$(HEX_CLANG) halide_hexagon_remote.cpp halide_hexagon_remote_skel.c

clean:
	rm -rf halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
