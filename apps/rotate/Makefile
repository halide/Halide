include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

rotate: ../../ rotate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) rotate.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o rotate -lpthread -ldl -lz $(PNGFLAGS) $(LDFLAGS)

out.png: rotate
	./rotate ../images/rgb.png out.png -r 15 -s 3

clean:
	rm -f out.png rotate
