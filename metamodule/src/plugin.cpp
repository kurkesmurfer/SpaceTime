#include <rack.hpp>

using namespace rack;

void initProbeCore();
void initProbeRemote();
extern Model* modelSpaceTimeCore;
extern Model* modelSpaceTimeTimingMonitor;

Plugin* pluginInstance;

extern "C" void init(Plugin* plugin) {
	pluginInstance = plugin;
	plugin->addModel(modelSpaceTimeCore);
	plugin->addModel(modelSpaceTimeTimingMonitor);
	initProbeCore();
	initProbeRemote();
}
