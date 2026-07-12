// Shared "Bone" panel design system for the SpaceTime plugin family.
// Towering Inferno
// License: GPL-3.0-or-later
//
// Ported from the Collide plugin (vcv/paneltheme.hpp there), which applies
// the design_handoff_collide_panel_style palette/type system. Labels are
// drawn via NanoVG (PanelText, plugin.hpp) rather than SVG <text> because
// Rack's NanoSVG parser doesn't render <text> or <image> elements.
// Panel SVGs carry background art only.

#pragma once
#include <rack.hpp>
#include "plugin.hpp"

using namespace rack;

namespace spacetime {

// ---- Color tokens (Collide design_handoff README, "Colors" table) ----
inline NVGcolor colorTitle()        { return nvgRGB(0xd8, 0xcc, 0xb8); }
inline NVGcolor colorSubtitle()     { return nvgRGB(0x8a, 0x7e, 0x70); }
inline NVGcolor colorLabelPrimary() { return nvgRGB(0xb7, 0xac, 0x9d); }
inline NVGcolor colorLabelSection() { return nvgRGB(0x7b, 0x6f, 0x62); }
inline NVGcolor colorLabelCV()      { return nvgRGB(0x8a, 0x7e, 0x70); }
inline NVGcolor colorLabelIO()      { return nvgRGB(0xcf, 0xc2, 0xaf); }

// ---- Fonts (bundled; Window::loadFont caches by filename, cheap to call repeatedly) ----
inline std::shared_ptr<rack::window::Font> fontTitle() {
	static std::shared_ptr<rack::window::Font> f =
		APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Fraunces-SemiBold.ttf"));
	return f;
}
inline std::shared_ptr<rack::window::Font> fontLabelMedium() {
	static std::shared_ptr<rack::window::Font> f =
		APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/BarlowCondensed-Medium.ttf"));
	return f;
}
inline std::shared_ptr<rack::window::Font> fontLabelSemiBold() {
	static std::shared_ptr<rack::window::Font> f =
		APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/BarlowCondensed-SemiBold.ttf"));
	return f;
}
inline std::shared_ptr<rack::window::Font> fontLabelBold() {
	static std::shared_ptr<rack::window::Font> f =
		APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/BarlowCondensed-Bold.ttf"));
	return f;
}

#ifndef METAMODULE
// Convenience factory: positions a PanelText at an mm coordinate, centered.
inline PanelText* addLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text,
                           std::shared_ptr<rack::window::Font> font, float fontSize,
                           NVGcolor color, float letterSpacing = 0.f) {
	auto* t = new PanelText;
	t->text = text;
	t->font = font;
	t->fontSize = fontSize;
	t->color = color;
	t->letterSpacing = letterSpacing;
	t->box.pos = mm2px(Vec(xmm, ymm));
	t->box.size = Vec(0, 0);
	w->addChild(t);
	return t;
}

inline void addTitle(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontTitle(), 16.f, colorTitle(), 0.4f);
}
inline void addSubtitle(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelMedium(), 7.f, colorSubtitle(), 0.6f);
}
inline void addKnobLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.5f, colorLabelPrimary(), 0.5f);
}
inline void addSectionHeading(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.f, colorLabelSection(), 0.7f);
}
inline void addCvLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.f, colorLabelCV(), 0.5f);
}
inline void addIoLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelBold(), 7.5f, colorLabelIO(), 0.5f);
}
// SpaceTime extension: dense panels need a smaller tier than Collide's
// helpers (note names under the preset row, LED state letters).
inline void addMicroLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 5.5f, colorLabelCV(), 0.4f);
}
#endif

} // namespace spacetime
