# SpaceTime MetaModule Implementation Plan

**Created:** 2026-07-13  
**Target SDK:** local MetaModule Plugin SDK 2.2 (`97ee128`)  
**Feasibility:** **GO, as one fused module; NO-GO as the VCV expander chain**

## Feasibility result

MetaModule SDK 2.2 explicitly does not support Rack expander communication.
PROGRAM, STAGE4, MIDI and HEAD therefore cannot be ported as independent modules
while retaining their current behavior. A single module owning the program,
stages, heads and MIDI state has no equivalent blocker.

The other relevant SDK capabilities are present:

- Rack-style modules, params, jacks, lights and JSON state are supported.
- `midi::InputQueue` and `midi::Output` are available; MetaModule's own MIDI-CV
  module uses the same input queue for CC, notes, clock and transport.
- VCV-port context menus are supported on the GUI thread.
- Dynamic text displays are supported through `MetaModule::VCVTextDisplay`.
- Panels and component artwork must be PNG; SVG and NanoVG text are not rendered.

This makes a Rack-adaptor fused module the lowest-risk first implementation.
A native `CoreProcessor` rewrite would add a second UI/persistence abstraction
without solving a problem that the adaptor leaves open.

## Product shape

The first release target is a fixed **16-stage, two-head SpaceTime**. This matches
the hardware-scale instrument and the fallback already named in WP9. The shared
engine remains dimensioned for 32 stages and eight heads, so larger variants do
not require a DSP rewrite.

The recommended panel is a compact, banked monolith rather than an 82 HP literal
concatenation of PROGRAM + four STAGE4 + MIDI + two HEAD panels:

- PROGRAM controls remain directly visible.
- STAGES shows one four-stage bank at a time, selected by a 1-4 bank control.
- HEAD shows one of two head control sets at a time, selected by a 1-2 control.
- All head inputs and outputs remain dedicated, stable jacks; banking changes
  editing only, never patch routing.
- A small dynamic display shows stage bank, selected stage/head, key/scale and
  MIDI activity. MIDI can still address all 16 stages and both heads directly.

The alternative is a faithful 82 HP panel that requires horizontal panning even
when zoomed out. It remains a valid fallback if banked editing proves confusing.
Panel layout and the permanent module slug are review gates before registration.

## Architecture

Add a platform-neutral `dsp/SpaceTimeEngine.hpp` that owns the state which is
currently distributed through expanders:

- `StageTable` with 16 active stages and slider-takeover state;
- `ProgramLogic`, preset row, globals and key/scale;
- two `HeadDSP` instances and their configurations/status;
- `MidiCore`, including incoming edits/transport and outgoing lanes;
- selected stage, stage bank, selected head and display-feedback state.

The engine API accepts fixed-size input/config snapshots and returns fixed-size
head outputs/status. It performs no allocation in `process()` and contains no
Rack or MetaModule headers. `Chain.hpp` remains the VCV transport only; the fused
engine calls the same underlying logic directly and does not simulate expanders.

Add a MetaModule Rack adapter under `metamodule/src/` that:

- maps params and virtual jacks into the engine;
- drains raw MIDI into `MidiCore` and sends its output through `midi::Output`;
- runs heads at audio rate and scans controls/lights at the established divided
  control rate;
- saves program words, slider values/takeover, presets, global settings, MIDI
  setup and bank/head selection in patch state;
- provides MetaModule text displays and context-menu configuration where useful.

The MetaModule target gets its own registration source. It must not compile the
four expander models merely to reuse VCV's `plugin.cpp`.

## Work packages

### MM0 - Build skeleton and API probes

Create `metamodule/CMakeLists.txt`, `plugin-mm.json`, target-local registration,
an inert fused Rack-style module and a placeholder 240-pixel-high PNG panel.
Compile and package against SDK 2.2. Prove MIDI input/output construction, JSON
state round-trip and a dynamic text display in the smallest possible module.

**Exit:** loadable `.mmplugin`; MIDI CC/clock reaches a counter; one output MIDI
message can be observed; state and display survive patch reload.

### MM1 - Fused engine

Implement `SpaceTimeEngine` and host tests. Start with four stages/one head in
tests, then exercise the release dimensions of 16/two. Reuse `StageTable`,
`ProgramLogic`, `HeadDSP`, `PresetRowLogic` and `MidiCore`; do not duplicate their
algorithms.

**Exit:** golden VCV-vs-fused traces match for stage CV, timing, pulses, clock
sources, transport, key/scale, preset recall and MIDI edits.

### MM2 - Panel mockup

Produce the compact panel with four-stage and two-head banking, fixed I/O and
baked labels. Convert at 240 px high and inspect both 240 px and 180 px display
scales. Freeze the module slug only after this review.

**Exit:** human layout gate; every control and jack has a stable ID and label.

### MM3 - Control and stage integration

Wire PROGRAM, stage bank, selected-stage feedback, slider takeover, presets,
key/scale and pulse-retrigger behavior. Keep MIDI updates visually reflected in
the currently visible bank, just as CC 89-91 now update VCV feedback.

**Exit:** all 16 stages can be programmed from panel and MIDI; save/reload is
lossless; changing banks does not alter values.

### MM4 - Heads and I/O

Wire two heads, all four clock modes, dedicated CV inputs/outputs, status lights
and head selection. Verify that banking edits only the selected head while both
heads continue processing continuously.

**Exit:** two-head manual patch passes internal, external CV, MIDI-clock and
virtual-clock tests, including tick-for-tick virtual-clock verification.

### MM5 - MIDI completion

Port the accepted implementation chart unchanged: channel 16 default for
PROGRAM/stages, head channels 1-2, CC 0-91, realtime clock/transport and outgoing
note/CC lanes. Expose settings that cannot sensibly be inherited from the patch
through the module action menu or compact panel controls.

**Exit:** DROID remote controls the fused module; outgoing note on/off and CC are
verified on hardware. Fourteen-bit CV output remains explicitly later.

### MM6 - Performance and hardware validation

Measure release and stress configurations on actual MetaModule. Record average
and peak CPU, MIDI-clock stability, event loss under dense CC traffic and patch
load/save behavior. Run at supported sample rates and with both heads active.

**Exit:** no audio-thread allocation, no dropped transport/clock events in the
test patch, and documented CPU headroom. VCV's M3 figures are useful context but
are not a substitute for this measurement.

### MM7 - Packaging and manual

Finalize PNG assets, metadata, versioning, install instructions, limitations and
the MetaModule-specific control map. Build a clean `.mmplugin` without modifying
`vcv/plugin.json` as a side effect of configuration.

## Testing matrix

| Layer | Verification |
|---|---|
| Shared engine | Existing host suite plus fused-engine unit/golden tests |
| Adapter | SDK build with warnings reviewed; package contents inspected |
| Persistence | Save/reload/reset tests for stages, presets, banks, heads and MIDI |
| MIDI in | CC map, PC 1-12, F8/FA/FB/FC, channel filtering, dense DROID stream |
| MIDI out | Note-on/off for P1/P2/ALL and throttled 7-bit CC on both heads |
| Timing | External, MIDI and virtual clock edge counts compared to source logs |
| UI | 240/180 px screenshots; bank/head state and remote feedback remain legible |
| Performance | Average/peak CPU and event-loss test on MetaModule hardware |

## Decision gates

1. Approve compact banked panel versus faithful 82 HP panel.
2. Freeze the new fused module slug after the panel mockup.
3. Confirm the initial release at 16 stages/two heads before MM1 is dimensioned.
4. Decide whether a later 32-stage/eight-head variant is one larger module or a
   separate fixed-size model after MM6 performance data exists.

## Known risks

- Direct USB MIDI is supported by the adaptor API and used by a core port, but
  third-party input/output routing still needs a hardware proof in MM0.
- Banked params improve screen fit but make hardware mappings contextual. The
  DROID's direct MIDI map remains non-banked and is the primary remote surface.
- The current MIDI clock interpretation advances on each F8 edge. Its intended
  musical relationship to 24 PPQN must be settled by the existing DROID test;
  the MetaModule port must preserve, not silently redefine, that behavior.
- Context-menu-only settings are less immediate on hardware. Performance-critical
  choices belong on the panel; setup choices may remain in the action menu.
