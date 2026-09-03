// Shared "Bone" panel design system for the SpaceTime plugin family.
// Kurkesmurfer
// License: GPL-3.0-or-later
//
// Ported from the Collide plugin (vcv/paneltheme.hpp there), which applies
// the design_handoff_collide_panel_style palette/type system. Labels are
// drawn via NanoVG (PanelText, plugin.hpp) rather than SVG <text> because
// Rack's NanoSVG parser doesn't render <text> or <image> elements.
// Panel SVGs carry background art only.

#pragma once
#include <rack.hpp>
#include <functional>
#include <sstream>
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

inline NVGcolor colorTitleLight()        { return nvgRGB(0x2b, 0x25, 0x20); }
inline NVGcolor colorSubtitleLight()     { return nvgRGB(0x5f, 0x56, 0x4d); }
inline NVGcolor colorLabelPrimaryLight() { return nvgRGB(0x33, 0x2d, 0x27); }
inline NVGcolor colorLabelSectionLight() { return nvgRGB(0x67, 0x5d, 0x53); }
inline NVGcolor colorLabelCVLight()      { return nvgRGB(0x5d, 0x55, 0x4d); }
inline NVGcolor colorLabelIOLight()      { return nvgRGB(0x20, 0x1c, 0x18); }

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
struct ThemedPanel : app::SvgPanel {
	std::shared_ptr<window::Svg> lightSvg;
	std::shared_ptr<window::Svg> darkSvg;
	bool lastDark = false;
	bool initialized = false;

	void setBackgrounds(std::shared_ptr<window::Svg> light,
	                    std::shared_ptr<window::Svg> dark) {
		lightSvg = light;
		darkSvg = dark;
		updateBackground();
	}

	void updateBackground() {
		bool dark = useDarkPanels();
		if (initialized && dark == lastDark)
			return;
		lastDark = dark;
		initialized = true;
		app::SvgPanel::setBackground(dark ? darkSvg : lightSvg);
	}

	void step() override {
		updateBackground();
		app::SvgPanel::step();
	}
};

inline ThemedPanel* createThemedPanel(const std::string& lightPath,
                                     const std::string& darkPath) {
	ThemedPanel* panel = new ThemedPanel;
	panel->setBackgrounds(window::Svg::load(lightPath), window::Svg::load(darkPath));
	return panel;
}

// Rack counterpart of MetaModule's VCVTextDisplay for the portable modules.
struct MMStatusReadout : Widget {
	std::function<std::string()> textProvider;
	std::string previewText;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::shared_ptr<window::Font> font = APP->window->loadFont(
				asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				std::string text = textProvider ? textProvider() : std::string();
				if (text.empty())
					text = previewText;
				nvgSave(args.vg);
				nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 11.f);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(args.vg, nvgRGB(0xc6, 0xe1, 0xcf));
				std::istringstream lines(text);
				std::string line;
				float y = 5.f;
				while (std::getline(lines, line) && y < box.size.y - 3.f) {
					nvgText(args.vg, 6.f, y, line.c_str(), nullptr);
					y += 14.f;
				}
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}
};

inline MMStatusReadout* addMMStatusReadout(ModuleWidget* w, Vec posMm,
		Vec sizeMm, const std::string& preview,
		std::function<std::string()> textProvider) {
	auto* display = new MMStatusReadout;
	display->box.pos = mm2px(posMm);
	display->box.size = mm2px(sizeMm);
	display->previewText = preview;
	display->textProvider = textProvider;
	w->addChild(display);
	return display;
}

inline void appendPanelThemeMenu(Menu* menu) {
	static const std::vector<std::string> labels = {"Follow Rack", "Light", "Dark"};
	menu->addChild(createIndexSubmenuItem("Panel theme", labels,
		[]() { return panelThemePreference + 1; },
		[](int value) { setPanelThemePreference(value - 1); }));
}

// Convenience factory: positions a PanelText at an mm coordinate, centered.
inline PanelText* addLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text,
                           std::shared_ptr<rack::window::Font> font, float fontSize,
                           NVGcolor color, float letterSpacing = 0.f,
                           NVGcolor lightColor = nvgRGBA(0, 0, 0, 0)) {
	auto* t = new PanelText;
	t->text = text;
	t->font = font;
	t->fontSize = fontSize;
	t->color = color;
	t->lightColor = lightColor.a > 0.f ? lightColor : color;
	t->themeAware = true;
	t->letterSpacing = letterSpacing;
	t->box.pos = mm2px(Vec(xmm, ymm));
	t->box.size = Vec(0, 0);
	w->addChild(t);
	return t;
}

inline void addTitle(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontTitle(), 16.f, colorTitle(), 0.4f, colorTitleLight());
}
inline void addSubtitle(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelMedium(), 7.f, colorSubtitle(), 0.6f, colorSubtitleLight());
}
inline void addKnobLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.5f, colorLabelPrimary(), 0.5f, colorLabelPrimaryLight());
}
inline void addSectionHeading(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.f, colorLabelSection(), 0.7f, colorLabelSectionLight());
}
inline void addCvLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 7.f, colorLabelCV(), 0.5f, colorLabelCVLight());
}
inline void addIoLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelBold(), 7.5f, colorLabelIO(), 0.5f, colorLabelIOLight());
}
// SpaceTime extension: dense panels need a smaller tier than Collide's
// helpers (note names under the preset row, LED state letters).
inline void addMicroLabel(ModuleWidget* w, float xmm, float ymm, const std::string& text) {
	addLabel(w, xmm, ymm, text, fontLabelSemiBold(), 5.5f, colorLabelCV(), 0.4f, colorLabelCVLight());
}

inline void addHeaderMark(ModuleWidget* w, float panelWidthMm, float titleWidthMm) {
	constexpr float markHeightMm = 5.2f;
	constexpr float markWidthMm = markHeightMm * (5.41f / 10.94f);
	constexpr float gapMm = 1.2f;
	float leftMm = (panelWidthMm - markWidthMm - gapMm - titleWidthMm) * 0.5f;
	auto* mark = new SvgWidget;
	mark->box.pos = mm2px(Vec(leftMm, 3.2f));
	mark->box.size = mm2px(Vec(markWidthMm, markHeightMm));
	mark->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,
		"res/kurkesmurfer-header-logo.svg")));
	w->addChild(mark);
}

inline void addHeaderLockup(ModuleWidget* w, float panelWidthMm,
		const std::string& title, float titleWidthMm) {
	constexpr float markHeightMm = 5.2f;
	constexpr float markWidthMm = markHeightMm * (5.41f / 10.94f);
	constexpr float gapMm = 1.2f;
	float leftMm = (panelWidthMm - markWidthMm - gapMm - titleWidthMm) * 0.5f;
	addTitle(w, leftMm + markWidthMm + gapMm + titleWidthMm * 0.5f, 5.8f, title);
}

struct RotatedPanelText : PanelText {
	float rotation = 0.f;
	void draw(const DrawArgs& args) override {
		nvgSave(args.vg);
		nvgRotate(args.vg, rotation);
		PanelText::draw(args);
		nvgRestore(args.vg);
	}
};

inline void addVerticalHeaderLockup(ModuleWidget* w, float panelWidthMm,
		const std::string& title, const std::string& subtitle) {
	auto addRotated = [=](float y, const std::string& text,
			std::shared_ptr<rack::window::Font> font, float size,
			NVGcolor dark, float spacing, NVGcolor light) {
		auto* t = new RotatedPanelText;
		t->text = text;
		t->font = font;
		t->fontSize = size;
		t->color = dark;
		t->lightColor = light;
		t->themeAware = true;
		t->letterSpacing = spacing;
		t->rotation = 0.5f * M_PI;
		t->box.pos = mm2px(Vec(panelWidthMm * 0.5f, y));
		t->box.size = Vec(0, 0);
		w->addChild(t);
	};
	addRotated(18.2f, title, fontTitle(), 16.f, colorTitle(), 0.4f, colorTitleLight());
	addRotated(33.5f, subtitle, fontLabelMedium(), 7.f,
		colorSubtitle(), 0.6f, colorSubtitleLight());
}

struct ManufacturerWordmark : PanelText {
	float panelWidthPx = 0.f;
	void draw(const DrawArgs& args) override {
		auto f = font ? font : APP->window->uiFont;
		if (!f || !f->handle) return;
		nvgFontFaceId(args.vg, f->handle);
		nvgFontSize(args.vg, fontSize);
		nvgTextLetterSpacing(args.vg, letterSpacing);
		float wordWidth = nvgTextBounds(args.vg, 0.f, 0.f, text.c_str(), NULL, NULL);
		float characterSpace = nvgTextBounds(args.vg, 0.f, 0.f, " ", NULL, NULL);
		float screwInnerEdgePx = mm2px(Vec(4.5f, 0.f)).x;
		if (wordWidth + 2.f * characterSpace > panelWidthPx - 2.f * screwInnerEdgePx)
			return;
		PanelText::draw(args);
	}
};

inline void addManufacturerWordmark(ModuleWidget* w, float panelWidthMm) {
	auto* t = new ManufacturerWordmark;
	t->text = "kurkesmurfer";
	t->font = fontLabelSemiBold();
	t->fontSize = 15.2f;
	t->color = colorLabelPrimary();
	t->lightColor = colorLabelPrimaryLight();
	t->themeAware = true;
	t->letterSpacing = 0.75f;
	t->panelWidthPx = mm2px(Vec(panelWidthMm, 0.f)).x;
	t->box.pos = mm2px(Vec(panelWidthMm * 0.5f, 125.f));
	t->box.size = Vec(0, 0);
	w->addChild(t);
}

// Kurkesmurfer's bent-corkscrew mark is deliberately a single continuous
// spiral with a K-like handle. The panel SVG is the subdued Rack treatment:
// 65% spiral opacity and 75% handle/joint opacity. Keep the full-strength
// canonical logo for manuals and other supporting material.
//
// Position from the painted right and bottom edges. Stage4 uses the helper's
// 3.21 mm right margin; the 2 HP Glue panels override it with 1.04 mm. Both
// use a 4 mm bottom margin at their call sites. The 0.70 call-site scale
// resolves to the approved 5.41 x 10.94 mm painted mark.
struct CornerMark : SvgWidget {
	CornerMark(float panelWidthMm, float panelHeightMm, float scale = 1.f,
	           float rightMarginMm = 3.21f, float bottomMarginMm = 9.36f) {
		float w = 7.7286f * scale;
		float h = 15.6286f * scale;
		box.pos = mm2px(Vec(panelWidthMm - rightMarginMm - w,
		                       panelHeightMm - bottomMarginMm - h));
		box.size = mm2px(Vec(w, h));
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,
			"res/kurkesmurfer-panel-logo.svg")));
	}
};
#endif

} // namespace spacetime
