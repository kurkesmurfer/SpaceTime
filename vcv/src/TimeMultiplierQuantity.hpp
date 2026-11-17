#pragma once

#include "plugin.hpp"
#include "../../dsp/Chain.hpp"

// The stored parameter is normalized so CC and panel automation share the
// same logarithmic curve. The display shows the actual interval factor.
struct TimeMultiplierQuantity : rack::engine::ParamQuantity {
	float getDisplayValue() override {
		return spacetime::timeMultiplierFromControl(getValue());
	}

	void setDisplayValue(float value) override {
		setValue(spacetime::timeMultiplierControlFromFactor(value));
	}
};
