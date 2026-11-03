#pragma once

#include "StageTable.hpp"

namespace spacetime {

// Shared metadata for the MetaModule-compatible Program implementations.
// These arrays are deliberately namespace-scope `static const` objects:
// each translation unit gets internal linkage, so the C++11 VCV build does
// not need out-of-class definitions when an array element is passed by
// reference (as rack::string::f does). The catalog is small, and keeping it
// here makes the VCV compatibility module and native MetaModule stay in sync.
static const Field kMMProgramModifierFields[] = {
	Field::Quantize, Field::Slew, Field::Range,
	Field::VoltageSource, Field::Stop, Field::Sustain,
	Field::Enable, Field::First, Field::Last,
	Field::TimeSource, Field::Pulse1, Field::Pulse2,
};
enum {
	kMMProgramModifierCount =
		sizeof(kMMProgramModifierFields) / sizeof(kMMProgramModifierFields[0])
};

static const char* const kMMProgramModifierNames[] = {
	"Quantize", "Slew", "Range", "Voltage source", "Stop", "Sustain",
	"Enable", "First", "Last", "Time source", "Pulse 1", "Pulse 2",
};

static const char* const kMMProgramModifierDownLabels[] = {
	"Continuous", "Reduce slew", "Half (0-5 V)", "Internal",
	"Remove", "Remove", "Remove", "Remove", "Remove",
	"Internal (slider + range)", "Remove from stage", "Remove from stage",
};

static const char* const kMMProgramModifierUpLabels[] = {
	"Quantize", "Add slew", "Full (0-10 V)", "External (slider selects A-D)",
	"Add", "Add", "Add", "Set", "Set",
	"External (A-D CV)", "Add to stage", "Add to stage",
};

static_assert(sizeof(kMMProgramModifierNames) / sizeof(kMMProgramModifierNames[0]) ==
	kMMProgramModifierCount, "MM Program modifier name count drift");
static_assert(sizeof(kMMProgramModifierDownLabels) / sizeof(kMMProgramModifierDownLabels[0]) ==
	kMMProgramModifierCount, "MM Program modifier down-label count drift");
static_assert(sizeof(kMMProgramModifierUpLabels) / sizeof(kMMProgramModifierUpLabels[0]) ==
	kMMProgramModifierCount, "MM Program modifier up-label count drift");

} // namespace spacetime
