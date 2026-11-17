#pragma once

#include "plugin.hpp"
#include "../../dsp/Chain.hpp"

// Shows the normalized knob as the actual pulse time while preserving the
// piecewise-logarithmic control curve in the stored 0..1 parameter.
struct PulseTimeQuantity : rack::engine::ParamQuantity {
	float getDisplayValue() override {
		return spacetime::pulseTimeFromControl(getValue()) * 1000.f;
	}

	void setDisplayValue(float value) override {
		setValue(spacetime::pulseControlFromTime(value * 0.001f));
	}
};
