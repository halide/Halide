include ../support/Makefile.inc

ifeq ($(HL_TARGET),ptx)
  SCHEDULE=100
else
  SCHEDULE=0
endif

ifdef HEX_TOOLS
  # Precompute luminance LUT with FCam makeLUT to be able to measure per pixel
  # timings on small inputs that more closely match that seen with large
  CXXFLAGS+=-DFCAMLUT
  TGT_CXXFLAGS+=-DFCAMLUT
endif

all: process

camera_pipe: ../../ camera_pipe.cpp
	$(CXX) $(CXXFLAGS) camera_pipe.cpp -g $(LIB_HALIDE) -o camera_pipe $(LDFLAGS)

curved.o: camera_pipe
	${GDB} ./camera_pipe 8 ${SCHEDULE} # 8-bit output,

fcam/Demosaic.o: fcam/Demosaic.cpp fcam/Demosaic.h
	$(TGT_CXX) $(TGT_CXXFLAGS) -c -Wall -O3 $< -o $@

fcam/Demosaic_ARM.o: fcam/Demosaic_ARM.cpp fcam/Demosaic_ARM.h
	$(TGT_CXX) $(TGT_CXXFLAGS) -c -Wall -O3 $< -o $@

ifdef HEX_TOOLS
process: process.cpp curved.o fcam/Demosaic.o
	$(TGT_CXX) $(TGT_CXXFLAGS) -Wall -O3 $^ -o $@ -lhexagon
else
process: process.cpp curved.o fcam/Demosaic.o fcam/Demosaic_ARM.o
	$(CXX) $(CXXFLAGS) -Wall -O3 $^ -o $@ $(PNGFLAGS)
endif

out.png: process
	./process ../images/bayer_raw.png 3700 2.0 50 5 out.png

ifdef HEX_TOOLS
out.ppm: process
	${HEX_TOOLS}/bin/hexagon-sim ./process ${TIMING} -- ../images/bayer_raw.pgm 3700 2.0 50 1 out.ppm
	- cmp fcam_c.ppm ref_raw/fcam_c.ppm
	cmp out.ppm ref_raw/out.ppm

out_%.ppm: process
	${HEX_TOOLS}/bin/hexagon-sim ./process ${TIMING} -- ../images/bayer_$*.pgm 3700 2.0 50 1 out_$*.ppm
	- if [ -r ref_$*_LV$(LOG2VLEN)/fcam_c.ppm ]; then \
	    echo "cmp fcam_c.ppm ref_$*_LV$(LOG2VLEN)/fcam_c.ppm"; \
	    cmp fcam_c.ppm ref_$*_LV$(LOG2VLEN)/fcam_c.ppm; \
	else \
	    echo "cmp fcam_c.ppm ref_$*/fcam_c.ppm"; \
	    cmp fcam_c.ppm ref_$*/fcam_c.ppm; \
	fi
	if [ -r ref_$*_LV$(LOG2VLEN)/out_$*.ppm ]; then \
	    echo "cmp out_$*.ppm ref_$*_LV$(LOG2VLEN)/out_$*.ppm"; \
	    cmp out_$*.ppm ref_$*_LV$(LOG2VLEN)/out_$*.ppm; \
	else \
	    echo "cmp out_$*.ppm ref_$*/out_$*.ppm"; \
	    cmp out_$*.ppm ref_$*/out_$*.ppm; \
	fi
endif

../../bin/HalideTraceViz:
	$(MAKE) -C ../../ bin/HalideTraceViz

camera_pipe.avi: camera_pipe.cpp viz.sh $(HALIDE_TRACE_VIZ) ../../bin/HalideTraceViz
	bash viz.sh

clean:
	rm -f out.png out.ppm out_small.ppm fcam_c.ppm pmu_statsfile.txt pmu_stats.txt \
	      process curved.o curved.s curved.h camera_pipe fcam/*.o
