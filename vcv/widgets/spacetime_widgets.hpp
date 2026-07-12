// SpaceTime — reusable widget library (WP3).
// Towering Inferno — Bone design system (see src/paneltheme.hpp).
//
// Contents:
//   kHeadColors[8]       — family head colour set (single source of truth)
//   SpringSwitch3        — three-position spring-return momentary
//   addStageLedCluster() — edit-select LED + 8 colour-coded head dots
//   addLimitedBank()     — five-button limited-range octave bank
//   addPresetRow()       — 12 slot buttons + Load/Save/Key/Scale mode row
//                          (modal behaviour: dsp/PresetRow.hpp)
//
// Pure UI: all state is driven via params and lights only.

#pragma once
#include <rack.hpp>
#include "plugin.hpp"

using namespace rack;

namespace spacetime {

// ---- Head colours (keep as the ONLY definition; Stage4/Head/WidgetTest use it)
static const float kHeadColors[8][3] = {
	{1.00f, 0.20f, 0.20f},  // 1 red
	{0.20f, 0.80f, 0.20f},  // 2 green
	{0.20f, 0.40f, 1.00f},  // 3 blue
	{1.00f, 0.80f, 0.00f},  // 4 yellow
	{0.80f, 0.20f, 0.80f},  // 5 magenta
	{0.20f, 0.80f, 0.80f},  // 6 cyan
	{1.00f, 0.53f, 0.07f},  // 7 orange
	{0.93f, 0.93f, 0.93f},  // 8 white
};

// Set one RGB head dot (3 consecutive light ids) to head colour h, scaled.
inline void setHeadDot(engine::Module* m, int lightBaseId, int h, float brightness, float dt) {
	for (int c = 0; c < 3; c++)
		m->lights[lightBaseId + c].setBrightnessSmooth(brightness * kHeadColors[h][c], dt);
}

// ---- SpringSwitch3 ---------------------------------------------------------
// Three-position momentary with spring return to center, matching the 248t
// programming levers. Param is configSwitch(0, 2, 1): press on the upper half
// drives the value to 2 (up/add), lower half to 0 (down/remove); release
// returns to 1 (center). Modules react to edges.
//
// History: momentary — pushes NO history events (never one per frame; the
// program-state change it triggers is the module's responsibility).
struct SpringSwitch3 : app::SvgSwitch {
	SpringSwitch3() {
		momentary = false;  // spring behaviour implemented below
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_1.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/spring3_2.svg")));
	}

	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);  // drag target, tooltip, context menu
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			engine::ParamQuantity* pq = getParamQuantity();
			if (pq) {
				bool up = e.pos.y < box.size.y / 2.f;
				pq->setValue(up ? 2.f : 0.f);
			}
		}
	}

	// Bypass Switch's cycling/momentary logic entirely.
	void onDragStart(const DragStartEvent& e) override {}
	void onDragEnd(const DragEndEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq)
			pq->setValue(1.f);  // spring return to center
	}
};

// Param config helper for SpringSwitch3.
inline void configSpring3(engine::Module* m, int paramId, const std::string& name,
                          const std::string& downLabel, const std::string& upLabel) {
	m->configSwitch(paramId, 0.f, 2.f, 1.f, name, {downLabel, "—", upLabel});
}

// ---- LatchSpringSwitch3 -----------------------------------------------------
// Hardware Cont/Strobe switch: LATCHING up and center, MOMENTARY down.
// Clicking the upper half toggles between up (2) and center (1); the lower
// half drives 0 while held and springs back to the latched position.
struct LatchSpringSwitch3 : SpringSwitch3 {
	float latched = 1.f;

	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			engine::ParamQuantity* pq = getParamQuantity();
			if (pq) {
				bool up = e.pos.y < box.size.y / 2.f;
				if (up) {
					latched = (pq->getValue() > 1.5f) ? 1.f : 2.f;  // toggle up/center
					pq->setValue(latched);
				}
				else {
					if (pq->getValue() > 0.5f)
						latched = (pq->getValue() > 1.5f) ? 2.f : 1.f;  // remember
					pq->setValue(0.f);  // momentary down (strobe)
				}
			}
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && pq->getValue() < 0.5f)
			pq->setValue(latched);  // spring back from down only
	}
};

// ---- Stage LED cluster -----------------------------------------------------
// Geometry matches STAGE4 (WP1b): edit-select LED 4 mm above the first dot
// row; 2 rows x 4 RGB dots at 2.2 mm x-pitch, 3 mm row pitch.
// headLightBaseId must be the first of 8 consecutive RGB (3-id) lights.
inline void addStageLedCluster(ModuleWidget* w, engine::Module* m,
                               float xmm, float ymm,  // ymm = first dot row
                               int editLightId, int headLightBaseId) {
	w->addChild(createLightCentered<MediumLight<RedLight>>(
		mm2px(Vec(xmm, ymm - 4.f)), m, editLightId));
	for (int h = 0; h < 8; h++) {
		float dx = (h % 4 - 1.5f) * 2.2f;
		float dy = (h < 4) ? 0.f : 3.f;
		w->addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
			mm2px(Vec(xmm + dx, ymm + dy)), m, headLightBaseId + h * 3));
	}
}

// ---- Limited-range octave bank ----------------------------------------------
// Five LEDButton+light pairs (octaves -2..+2). Radio behaviour (exactly one
// selected) is module logic; buttons are momentary.
inline void addLimitedBank(ModuleWidget* w, engine::Module* m,
                           float x0mm, float ymm, float pitchMm,
                           int paramBaseId, int lightBaseId) {
	for (int i = 0; i < 5; i++) {
		float x = x0mm + pitchMm * i;
		w->addParam(createParamCentered<LEDButton>(mm2px(Vec(x, ymm)), m, paramBaseId + i));
		w->addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(x, ymm)), m, lightBaseId + i));
	}
}

// ---- Preset row --------------------------------------------------------------
// 12 slot buttons in one row plus the Load/Save/Key/Scale mode row.
// Modal press behaviour lives in dsp/PresetRow.hpp (PresetRowLogic).
inline void addPresetRow(ModuleWidget* w, engine::Module* m,
                         float x0mm, float ymm, float pitchMm,
                         int slotParamBaseId, int slotLightBaseId) {
	for (int i = 0; i < 12; i++) {
		float x = x0mm + pitchMm * i;
		w->addParam(createParamCentered<LEDButton>(mm2px(Vec(x, ymm)), m, slotParamBaseId + i));
		w->addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(x, ymm)), m, slotLightBaseId + i));
	}
}

inline void addPresetModeButton(ModuleWidget* w, engine::Module* m,
                                float xmm, float ymm, int paramId, int lightId) {
	w->addParam(createParamCentered<LEDButton>(mm2px(Vec(xmm, ymm)), m, paramId));
	w->addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(xmm, ymm)), m, lightId));
}

} // namespace spacetime
