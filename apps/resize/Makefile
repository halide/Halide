include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

resize: ../../ resize.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) resize.cpp ../../$(LIB_HALIDE) -o resize -lpthread -ldl -lz $(PNGFLAGS) $(LDFLAGS)

out.png: resize
	./resize ../images/rgba.png out.png -f 2.0 -t cubic -s 3

clean:
	rm -f out.png resize
