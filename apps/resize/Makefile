include ../support/Makefile.inc

CXXFLAGS += -g -Wall -Werror -Wno-unused-function 

.PHONY: clean

resize: ../../ resize.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) resize.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o resize -lpthread -ldl -lz $(PNGFLAGS) $(LDFLAGS)

out.png: resize
	./resize ../images/rgba.png out.png -f 2.0 -t cubic -s 3

clean:
	rm -f out.png resize
