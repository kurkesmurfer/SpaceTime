// Created once by PanelGenerator. YOURS TO EDIT — it is never overwritten.
//
// Every generated widget derives from one of these, so anything you put in a
// base reaches all of that family and survives every regeneration. Theme
// switching, shadows, tooltips, hover behaviour: here, not in the generated
// file, which is rewritten on every emit.
//
// Delete this file to have it written again from scratch.

#pragma once
#include "plugin.hpp"

namespace headlayoutmockupui {

using namespace rack;

/// Rack sweeps a knob from minAngle to maxAngle itself; ±0.83·π is its own
/// default and what every ComponentLibrary knob uses.
struct KnobBase : app::SvgKnob {
    KnobBase() {
        minAngle = -0.83f * M_PI;
        maxAngle =  0.83f * M_PI;
    }
};

struct PortBase : app::SvgPort {};

/// Flat panel artwork reads better without Rack's circular drop shadow.
struct SwitchBase : app::SvgSwitch {
    SwitchBase() {
        shadow->opacity = 0.f;
    }
};

struct SliderBase : app::SvgSlider {};

struct PlainBase : widget::SvgWidget {};

} // namespace headlayoutmockupui
