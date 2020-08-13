include ../support/Makefile.inc
include ../support/autoscheduler.inc

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune:
	cd $(CURDIR)/../onnx && $(MAKE) build && cd $(CURDIR) && \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(CURDIR)/../onnx/bin/host/onnx_converter.generator \
		onnx_model_generator \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY) \
		'model_file_path=$(CURDIR)/../onnx/models/mobilenetv2_3.onnx'

