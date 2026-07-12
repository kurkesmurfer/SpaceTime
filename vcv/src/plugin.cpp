#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelProgram);
	p->addModel(modelStage4);
	p->addModel(modelHead);
	p->addModel(modelMidi);
	p->addModel(modelWidgetTest);
}
