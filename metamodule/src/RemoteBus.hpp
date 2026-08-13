#pragma once

// EB8 (METAMODULE_EXPANDER_BUS_PLAN.md): the shared-plugin-memory globals
// for head relocation, same pattern as TimingBus.hpp/TimingBus.cpp -- one
// real definition (RemoteBus.cpp), extern-declared here so every module
// .cpp in this plugin binary that includes this header shares the same
// instances. This is what actually makes the bus a bus: two module
// instances of the same plugin only see each other's data because the
// firmware loader initializes one shared copy of the plugin's globals
// (METAMODULE_IMPLEMENTATION_PLAN.md's "Feasibility result").

#include "MetaModuleRemoteBus.hpp"

extern spacetime::MetaModuleHeadRegistry headRegistry;
extern spacetime::MetaModuleStageTableRegistry stageTableRegistry;
extern spacetime::MetaModuleHeadMidiRegistry headMidiRegistry;
extern spacetime::MetaModuleMidiStatusRegistry midiStatusRegistry;
// EB3, wired to a real module 2026-08-09: Stage4 (MetaModule) publishes its
// own bank's voltage/time here; Program reads it and arbitrates against its
// own table via the same feed-forward shadow-compare HeadRemote already
// uses for MIDI-vs-knob contention, just running on Program's side this
// time since Program (not Stage4) is the authoritative table owner.
extern spacetime::MetaModuleStageBankRegistry stageBankRegistry;
