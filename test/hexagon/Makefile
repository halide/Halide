HALIDE_TOP = $(shell pwd)/../../
CLANG = hexagon-clang++

SUBDIRS = codegen apps
.PHONY: all check check-codegen clean bmark-report

all: check-codegen perf

check-codegen:
	cd codegen && $(MAKE) all

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done



check: check-codegen
perf:
	$(MAKE) -C apps all

bmark-report:
	$(MAKE) -C apps bmark-report
