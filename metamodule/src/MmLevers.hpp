// SpaceTime — shared modifier-lever widget for the portable MMProgram family.
// Kurkesmurfer
// License: GPL-3.0-or-later
//
// MmLeverControl is a three-position control (min 0 / mid 1 / max 2) that
// renders as a small vertical lever. One header is shared by both authoring
// targets so the VCV twin (metamodule/src mirrored in vcv/src/MMProgram.cpp)
// and the MetaModule module present an identical param/lever contract:
//
//   - VCV Rack: behaves as the 248t momentary spring (press up, release,
//     springs back to centre). The param value only *transients* to 0/2 and
//     returns to 1, so the module's position-compare logic re-arms per press.
//   - MetaModule: the SDK's element layer maps a 3-frame SvgSwitch to a
//     latched three-way flip switch, so the lever *holds* its detent. A single
//     throw still moves one stage / applies one modifier, and position 1 is a
//     no-op on the module side. Same param range (0..2) either way.
//
// The module (SpaceTimeProgram::process) is position-based on this param, so
// identical C++ covers both hosts.
#pragma once

#include <rack.hpp>
using namespace rack;

// pluginInstance is defined in the respective plugin.cpp of each authoring
// target; forward-declare it here so shared widget code can resolve it.
extern Plugin* pluginInstance;

struct MmLeverControl : app::SvgSwitch {
	MmLeverControl() {
		momentary = false;
		// asset::plugin rewrites .svg -> .png on the MetaModule core (I ship
		// spring3_0/1/2.png in assets/components) and loads the SVG natively in
		// Rack; the same three frame paths therefore work for both hosts.
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_1.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_2.svg")));
	}

#ifdef METAMODULE
	// The MetaModule core engine routes its own touches for a latched
	// flip-switch element; no custom C++ interaction is needed.
#else
	// VCV: three-position momentary spring-return, matching spacetime_widgets
	// SpringSwitch3's gesture (upper half -> 2, lower half -> 0, release -> 1).
	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			engine::ParamQuantity* pq = getParamQuantity();
			if (pq) {
				bool up = e.pos.y < box.size.y / 2.f;
				pq->setValue(up ? 2.f : 0.f);
			}
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq)
			pq->setValue(1.f);  // spring return to centre
	}
#endif
};
