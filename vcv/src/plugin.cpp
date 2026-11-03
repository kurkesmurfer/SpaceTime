#include "plugin.hpp"

#include <settings.hpp>

Plugin* pluginInstance;
extern Model* modelStageTime;

namespace spacetime {

int panelThemePreference = PANEL_THEME_FOLLOW_RACK;

bool useDarkPanels() {
	if (panelThemePreference == PANEL_THEME_LIGHT)
		return false;
	if (panelThemePreference == PANEL_THEME_DARK)
		return true;
	return settings::preferDarkPanels;
}

void setPanelThemePreference(int preference) {
	panelThemePreference = clamp(preference,
		(int)PANEL_THEME_FOLLOW_RACK, (int)PANEL_THEME_DARK);
}

} // namespace spacetime

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelProgram);
	p->addModel(modelStage4);
	p->addModel(modelHead);
	p->addModel(modelHeadAll);
	p->addModel(modelMidi);
	p->addModel(modelStageTime);
	p->addModel(modelGlueLeft);
	p->addModel(modelGlueRight);
	p->addModel(modelWidgetTest);
	// The former MM* authoring twins remain compiled temporarily as migration
	// reference code, but are no longer registered. Original Program, Stage4
	// and Head modules now carry the portable MetaModule contract directly.
}

json_t* settingsToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "panelTheme",
		json_integer(spacetime::panelThemePreference));
	return root;
}

void settingsFromJson(json_t* root) {
	json_t* theme = json_object_get(root, "panelTheme");
	if (theme)
		spacetime::setPanelThemePreference((int)json_integer_value(theme));
}
