# SpaceTime — VCV Rack 2 plugin
# Build: make (requires Rack SDK)
RACK_DIR ?= $(HOME)/Development/Rack-SDK

# Generic DSP headers (Rack-free) live in ../dsp
FLAGS += -I../dsp
# Reusable widget library (WP3)
FLAGS += -Iwidgets

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
