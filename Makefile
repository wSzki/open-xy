# Local browser preview and SDK checks; no install, firmware, or device targets.
ENGINE ?= rings
PORT ?= 8917
PYTHON ?= $(if $(wildcard local/preview-env/bin/python),local/preview-env/bin/python,python)
WASM_CXX ?= $(if $(wildcard /opt/homebrew/opt/llvm@15/bin/clang++),/opt/homebrew/opt/llvm@15/bin/clang++,clang++)
PREVIEW_DIR ?= local/$(ENGINE)-preview-$(shell date +%Y%m%d-%H%M%S)
PREVIEW ?= $(shell for dir in local/$(ENGINE)-preview-*; do test -f "$$dir/engine.wasm" && test -f "$$dir/manifest.json" && printf '%s\n' "$$dir"; done | sort | tail -n 1)

.DEFAULT_GOAL := run
.PHONY: run preview check help

run:
	@if test -z "$(PREVIEW)"; then \
		printf '%s\n' "No built browser preview for ENGINE=$(ENGINE). Run 'make preview ENGINE=$(ENGINE)' first." >&2; exit 1; \
	fi
	@if test ! -f "$(PREVIEW)/engine.wasm" || test ! -f "$(PREVIEW)/manifest.json"; then \
		printf '%s\n' "PREVIEW=$(PREVIEW) must contain engine.wasm and manifest.json; build it with 'make preview ENGINE=$(ENGINE)'." >&2; exit 1; \
	fi
	@if ! command -v "$(PYTHON)" >/dev/null 2>&1; then \
		printf '%s\n' "Python interpreter '$(PYTHON)' not found; set PYTHON=/path/to/python." >&2; exit 1; \
	fi
	@exec "$(PYTHON)" tools/serve_harness.py --preview "$(PREVIEW)" --port "$(PORT)"

preview:
	@if ! command -v "$(PYTHON)" >/dev/null 2>&1; then \
		printf '%s\n' "Python interpreter '$(PYTHON)' not found; set PYTHON=/path/to/python." >&2; exit 1; \
	fi
	@if ! command -v "$(WASM_CXX)" >/dev/null 2>&1; then \
		printf '%s\n' "WebAssembly compiler '$(WASM_CXX)' not found; set WASM_CXX to an existing WebAssembly-capable clang++." >&2; exit 1; \
	fi
	@if test -e "$(PREVIEW_DIR)"; then \
		printf '%s\n' "Preview output '$(PREVIEW_DIR)' already exists; choose a fresh PREVIEW_DIR." >&2; exit 1; \
	fi
	@exec "$(PYTHON)" sdk.py preview --engine "$(ENGINE)" --out "$(PREVIEW_DIR)" --cxx "$(WASM_CXX)"

check:
	@if ! command -v "$(PYTHON)" >/dev/null 2>&1; then \
		printf '%s\n' "Python interpreter '$(PYTHON)' not found; set PYTHON=/path/to/python." >&2; exit 1; \
	fi
	@exec "$(PYTHON)" sdk.py check

help:
	@printf '%s\n' 'make [run]       Serve the newest built local/$(ENGINE)-preview-* browser preview on 127.0.0.1:$(PORT)' \
		'make preview     Build a fresh WASM preview (ENGINE=$(ENGINE), output=$(PREVIEW_DIR))' \
		'make check       Run the SDK public checks' \
		'make help        Show these commands' \
		'Overrides: ENGINE, PREVIEW, PREVIEW_DIR, PORT, PYTHON, WASM_CXX'
