SHELL := /usr/bin/env bash

SAMPLE_URL ?= https://github.com/freekof2/AD/releases/download/1.0.0.1/AdsPower.Global.exe
SAMPLE_SHA256 ?= c9ac99bb20ef2121a4e1c4331af249c4db4edd81b7482a2e3a04c7ee59d309cb
SAMPLE ?= samples/1.0.0.1/AdsPower.Global.exe
ANALYSIS_DIR ?= analysis/1.0.0.1
PYTHON ?= python3

.PHONY: fetch analyze decompile clean

fetch:
	$(PYTHON) scripts/static_decompile.py fetch \
		--url "$(SAMPLE_URL)" \
		--sha256 "$(SAMPLE_SHA256)" \
		--output "$(SAMPLE)"

analyze: $(SAMPLE)
	$(PYTHON) scripts/static_decompile.py local \
		--sample "$(SAMPLE)" \
		--analysis-dir "$(ANALYSIS_DIR)" \
		--sha256 "$(SAMPLE_SHA256)" \
		--unpack

# This target only invokes a decompiler when the PE has a CLR directory and the
# local environment explicitly provides the optional tool. The sample is never run.
decompile: $(SAMPLE)
	$(PYTHON) scripts/static_decompile.py local \
		--sample "$(SAMPLE)" \
		--analysis-dir "$(ANALYSIS_DIR)" \
		--sha256 "$(SAMPLE_SHA256)" \
		--unpack \
		--decompile

$(SAMPLE):
	@mkdir -p "$(@D)"
	$(PYTHON) scripts/static_decompile.py fetch \
		--url "$(SAMPLE_URL)" \
		--sha256 "$(SAMPLE_SHA256)" \
		--output "$@"

clean:
	rm -rf analysis samples
