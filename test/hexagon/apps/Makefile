HALIDE_TOP = $(shell pwd)/../../
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++

SUBDIRS = \
  median \
  gaussian3x3_update_functions \
  sobel \
  integrate \
  dilate3x3 \
  gaussian5x5 \
  histogram \
  conv3x3a16 \
  conv3x3a32

NOT_READY4PRIMTIME = \
  nv12torgb888 \
  sigma3x3 \
  bilateral \
  epsilon \
  fast9 \
  invsqrt

# Tests currently known to fail (failures ignored)
KNOWNFAIL = \
  integrate \
  invsqrt \
  sigma3x3

# Keep going on unknown failures (if set to non-zero)
KEEPGOING=0

.PHONY: all run perf bmark-report

all: perf not-ready

run:
	for dir in $(SUBDIRS); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir clean run; \
		if [ $$? -ne 0 ];  then \
		  IGNORE=$(KEEPGOING); \
		  for chk in $(KNOWNFAIL); do \
		    if [ "$$dir" = "$$chk" ]; then \
		      echo "KNOWNFAIL"; \
		      echo "Warning: non-zero exit status, but ignoring knownfail"; \
		      IGNORE=1; \
		    fi; \
		  done; \
		  if [ "$$IGNORE" -eq 0 ];  then \
		    echo "FAIL"; \
		    echo "Error: non-zero exit status"; \
		    exit 1; \
		  fi; \
		fi \
	done

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

bmark-report:
	for dir in $(SUBDIRS); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir bmark-report; \
	done

perf: run bmark-report

not-ready:
	for dir in $(NOT_READY4PRIMTIME); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir TIMING=  EXTRA_FLAGS=-DNOVECTOR clean run; \
		if [ $$? -ne 0 ];  then \
		  IGNORE=$(KEEPGOING); \
		  for chk in $(KNOWNFAIL); do \
		    if [ "$$dir" = "$$chk" ]; then \
		      echo "KNOWNFAIL"; \
		      echo "Warning: non-zero exit status, but ignoring knownfail"; \
		      IGNORE=1; \
		    fi; \
		  done; \
		  if [ "$$IGNORE" -eq 0 ];  then \
		    echo "FAIL"; \
		    echo "Error: non-zero exit status"; \
		    exit 1; \
		  fi; \
		fi \
	done

