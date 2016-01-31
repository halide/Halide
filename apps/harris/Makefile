include ../support/Makefile.inc

harris: harris.cpp
	$(CXX) $(CXXFLAGS) harris.cpp $(LIB_HALIDE) -o \
		harris -lpthread -ldl -lz $(LDFLAGS) $(LLVM_SHARED_LIBS) -ltinfo

harris.o: harris
	./harris

filter: harris.o filter.cpp
	$(CXX) $(CXXFLAGS) filter.cpp harris.o -lpthread -ldl -o filter $(PNGFLAGS) -lopencv_core -lopencv_imgproc -lopencv_highgui

clean: clean_bench clean_variants
	rm -f harris harris.o harris.h filter
