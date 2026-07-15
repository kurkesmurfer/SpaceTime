VCV_DIR := vcv
TOOLCHAIN_DIR ?= $(HOME)/Development/rack-plugin-toolchain
JOBS ?= 4

.PHONY: all clean cleandep dep dist install toolchain

all:
	$(MAKE) -C $(VCV_DIR)

clean:
	$(MAKE) -C $(VCV_DIR) clean

cleandep:
	$(MAKE) -C $(VCV_DIR) cleandep

dep:
	$(MAKE) -C $(VCV_DIR) dep

dist:
	$(MAKE) -C $(VCV_DIR) dist
	rm -rf dist
	mkdir -p dist
	cp $(VCV_DIR)/dist/*.vcvplugin dist/

install:
	$(MAKE) -C $(VCV_DIR) install

toolchain:
	@test -d "$(TOOLCHAIN_DIR)" || { echo "VCV toolchain not found: $(TOOLCHAIN_DIR)" >&2; exit 1; }
	@command -v docker >/dev/null || { echo "Docker is required by the VCV toolchain on macOS" >&2; exit 1; }
	cd "$(TOOLCHAIN_DIR)" && $(MAKE) -j$(JOBS) docker-plugin-build PLUGIN_DIR="$(CURDIR)" MAKE=make
