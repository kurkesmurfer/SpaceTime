#include <rack.hpp>

using namespace rack;

void initProbeCore();
void initProbeRemote();
extern Model* modelSpaceTimeCore;

Plugin* pluginInstance;

extern "C" void init(Plugin* plugin) {
	pluginInstance = plugin;
	plugin->addModel(modelSpaceTimeCore);
	initProbeCore();
	initProbeRemote();
}
