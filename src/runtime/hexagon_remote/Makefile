.PHONY: clean

COMMON_FLAGS = -I ${HEX_SDK_ROOT}inc/stddef
COMMON_CC_FLAGS = ${COMMON_FLAGS} \
	-I ${HEX_SDK_ROOT}lib/common/remote/ship/android_Release

QAIC_FLAGS = ${COMMON_FLAGS}
REMOTE_CC_FLAGS = ${COMMON_CC_FLAGS}
HOST_CC_FLAGS = ${COMMON_CC_FLAGS}

halide_hexagon_remote.h: halide_hexagon_remote.idl
	$(QAIC) $(QAIC_FLAGS) halide_hexagon_remote.idl

halide_hexagon_remote_skel.c: halide_hexagon_remote.idl
	$(QAIC) $(QAIC_FLAGS) halide_hexagon_remote.idl

halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	$(QAIC) $(QAIC_FLAGS) halide_hexagon_remote.idl

halide_hexagon_remote_skel.o: halide_hexagon_remote_skel.c halide_hexagon_remote.h
	$(HEX_CC) $(REMOTE_CC_FLAGS) -c halide_hexagon_remote_skel.c -o halide_hexagon_remote_skel.o

halide_hexagon_v60_remote.so: halide_hexagon_remote.cpp halide_hexagon_remote_skel.o halide_hexagon_remote.h
	$(HEX_CXX) $(REMOTE_CC_FLAGS) halide_hexagon_remote.cpp halide_hexagon_remote_skel.o -o halide_hexagon_v60_remote.so

halide_hexagon_v60_host.so: halide_hexagon_remote_stub.c halide_hexagon_remote.h
	$(CC) $(HOST_CC_FLAGS) halide_hexagon_remote_stub.c -o halide_hexagon_v60_host.so

clean:
	rm -rf halide_hexagon_v60_host.so halide_hexagon_v60_remote.so halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
