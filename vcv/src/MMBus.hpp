#pragma once

// Shared-memory transport for the portable MM-prefixed authoring modules in
// VCV Rack. It intentionally uses the same target-neutral registry classes as
// MetaModule; only the global instances are host-specific.

#include "MetaModuleRemoteBus.hpp"
#include "MetaModuleTimingBus.hpp"

extern spacetime::MetaModuleTimingBusRegistry mmTimingBusRegistry;
extern spacetime::MetaModuleHeadRegistry mmHeadRegistry;
extern spacetime::MetaModuleStageTableRegistry mmStageTableRegistry;
extern spacetime::MetaModuleHeadMidiRegistry mmHeadMidiRegistry;
extern spacetime::MetaModuleMidiStatusRegistry mmMidiStatusRegistry;
extern spacetime::MetaModuleStageBankRegistry mmStageBankRegistry;

// The portable adapters intentionally keep the same implementation names as
// the MetaModule sources. Restrict these aliases to files that opt into this
// header so the native VCV expander family remains untouched.
#define timingBusRegistry mmTimingBusRegistry
#define headRegistry mmHeadRegistry
#define stageTableRegistry mmStageTableRegistry
#define headMidiRegistry mmHeadMidiRegistry
#define midiStatusRegistry mmMidiStatusRegistry
#define stageBankRegistry mmStageBankRegistry
