#pragma once
#include <rack.hpp>

using namespace rack;

// Panel text label — drawn via NanoVG since NanoSVG ignores <text> elements
// in panel SVGs. Same solution as the Collide plugin (paneltheme there).
// Not compiled for MetaModule: labels should be baked into the panel PNG
// there instead (same convention as Collide/Siren).
#ifndef METAMODULE
struct PanelText : Widget {
	std::string text;
	float fontSize = 11.0f;
	float letterSpacing = 0.f;
	NVGcolor color = nvgRGB(0xb0, 0xb0, 0xcc);
	int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
	std::shared_ptr<rack::window::Font> font;

	void draw(const DrawArgs& args) override {
		auto f = font ? font : APP->window->uiFont;
		if (!f || !f->handle) return;
		nvgFontFaceId(args.vg, f->handle);
		nvgFontSize(args.vg, fontSize);
		nvgTextLetterSpacing(args.vg, letterSpacing);
		nvgTextAlign(args.vg, align);
		nvgFillColor(args.vg, color);
		nvgText(args.vg, 0, 0, text.c_str(), nullptr);
	}
};
#endif

extern Plugin* pluginInstance;

extern Model* modelProgram;
extern Model* modelStage4;
extern Model* modelHead;
extern Model* modelMidi;
extern Model* modelWidgetTest;  // WP3 dev-only; hide at release (WP8)
