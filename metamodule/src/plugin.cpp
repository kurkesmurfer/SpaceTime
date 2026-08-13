#include <rack.hpp>

using namespace rack;

void initProbeCore();
void initProbeRemote();
extern Model* modelSpaceTimeProgram;
extern Model* modelSpaceTimeTimingMonitor;
extern Model* modelSpaceTimeHead;
extern Model* modelSpaceTimeStage4;
extern Model* modelSpaceTimeMidi;

// METAMODULE_BUILTIN is set only by the simulator's ext-plugins.cmake, never
// by the real hardware build (metamodule/CMakeLists.txt's own create_plugin()
// call) -- this whole block is inert outside the simulator. The local
// ~/Development/metamodule checkout predates the simulator's automatic
// init()-rename/symbol-localization pass (see its ext-plugins.cmake header
// comment, "Don't forget to change init() => init_BrandSlug(), and add
// `extern` to the pluginInstance!"), so this follows the same manual pattern
// already established for Schlappi-vcv/src/plugin.cpp rather than assuming
// the newer upstream behavior that isn't present in this checkout yet.
#ifdef METAMODULE_BUILTIN
extern Plugin* pluginInstance;
#else
Plugin* pluginInstance;
#endif

#ifdef METAMODULE_BUILTIN
void init_SpaceTime(Plugin* plugin) {
#else
extern "C" void init(Plugin* plugin) {
#endif
	pluginInstance = plugin;
	plugin->addModel(modelSpaceTimeProgram);
	plugin->addModel(modelSpaceTimeTimingMonitor);
	plugin->addModel(modelSpaceTimeHead);
	plugin->addModel(modelSpaceTimeStage4);
	plugin->addModel(modelSpaceTimeMidi);
	initProbeCore();
	initProbeRemote();
}
